/**
 * @file    camera_proxy.cpp
 * @brief   CameraProxy implementation.
 */

#include "camera_proxy.h"
#include <algorithm>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>
#include <sstream>

namespace middleware {

// ── protocol wire helpers ────────────────────────────────────────────────────

#pragma pack(push, 1)
struct CameraSetResReq   { uint32_t width; uint32_t height; };
struct CameraSetFpsReq   { uint32_t fps; };
struct CameraSetFmtReq   { uint8_t  format; };
#pragma pack(pop)

// ── ctor / dtor ──────────────────────────────────────────────────────────────

CameraProxy::CameraProxy(ProxyConfig cfg)
    : ServiceProxy("camera_service", std::move(cfg)) {}

CameraProxy::~CameraProxy() {
    disconnect();
}

// ── public API ───────────────────────────────────────────────────────────────

Result<void> CameraProxy::startStream() {
    auto r = sendRequestWaitReply(CAMERA_MSG_START_STREAM, nullptr, 0);
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    streaming_.store(true, std::memory_order_release);
    return Result<void>::ok();
}

Result<void> CameraProxy::stopStream() {
    streaming_.store(false, std::memory_order_release);
    auto r = sendRequestWaitReply(CAMERA_MSG_STOP_STREAM, nullptr, 0);
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

bool CameraProxy::isStreaming() const noexcept {
    return streaming_.load(std::memory_order_acquire);
}

Result<void> CameraProxy::setResolution(uint32_t width, uint32_t height) {
    CameraSetResReq req{width, height};
    auto r = sendRequestWaitReply(CAMERA_MSG_SET_RESOLUTION, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<void> CameraProxy::setFps(uint32_t fps) {
    CameraSetFpsReq req{fps};
    auto r = sendRequestWaitReply(CAMERA_MSG_SET_FPS, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<void> CameraProxy::setFormat(CameraFrame::Format fmt) {
    CameraSetFmtReq req{static_cast<uint8_t>(fmt)};
    auto r = sendRequestWaitReply(CAMERA_MSG_SET_FORMAT, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<CameraFrame> CameraProxy::captureStillFrame(
    std::vector<uint8_t>& pixel_buf)
{
    // Ask daemon to store one frame into SHM and signal us.
    auto r = sendRequestWaitReply(CAMERA_MSG_CAPTURE_STILL, nullptr, 0);
    if (r.isErr()) return Result<CameraFrame>::err(r.error());

    CameraFrame frame{};
    bool got = false;
    auto read_r = shm_reader_.readFrame([&](const CameraFrame& f) {
        frame = f;
        // Deep copy the pixel data so the caller owns it after SHM is recycled.
        pixel_buf.assign(static_cast<const uint8_t*>(f.data),
                         static_cast<const uint8_t*>(f.data) + f.data_bytes);
        frame.data = pixel_buf.data();
        got = true;
    });
    if (!got)
        return Result<CameraFrame>::err(ProxyError::Timeout,
                                        "SHM frame not available after capture");
    return Result<CameraFrame>::ok(frame);
}

Result<CameraDeviceInfo> CameraProxy::getDeviceInfo() {
    auto r = sendRequestWaitReply(CAMERA_MSG_GET_DEVICE_INFO, nullptr, 0);
    if (r.isErr()) return Result<CameraDeviceInfo>::err(r.error());

    auto& msg = r.value();
    CameraDeviceInfo info{};
    const size_t copy_sz = std::min(static_cast<size_t>(msg.payload_len),
                                    sizeof(CameraDeviceInfo));
    std::memcpy(&info, msg.payload, copy_sz);
    releaseBuffer(msg);
    return Result<CameraDeviceInfo>::ok(info);
}

void CameraProxy::onFrameReady(CameraFrameCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    frame_cb_ = std::move(cb);
}

// ── private: ServiceProxy virtuals ──────────────────────────────────────────

Result<void> CameraProxy::onConnected() {
    if (!config_.use_shared_memory) return Result<void>::ok();

    std::string shm_name = config_.shm_name_prefix
                         + "camera_"
                         + std::to_string(getpid());
    auto r = shm_reader_.open(shm_name);
    (void)r;
    return Result<void>::ok();
}

void CameraProxy::onDisconnected() {
    streaming_.store(false, std::memory_order_release);
    shm_reader_.close();
}

void CameraProxy::ioLoop() {
    // Single-iteration SHM poll — outer while(!stopping_) in ServiceProxy
    // drives the loop.  Use sleep when not streaming or ring is empty.
    if (!streaming_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return;
    }
    if (!shm_reader_.isOpen() || !shm_reader_.pollReady()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }

    shm_reader_.readFrame([this](const CameraFrame& raw) {
        std::vector<uint8_t> pixel_copy(
            static_cast<const uint8_t*>(raw.data),
            static_cast<const uint8_t*>(raw.data) + raw.data_bytes);

        CameraFrame safe = raw;
        threadPool().post([this, safe = std::move(safe),
                           pixels = std::move(pixel_copy)]() mutable {
            safe.data = pixels.data();
            std::lock_guard<std::mutex> lk(cb_mutex_);
            if (frame_cb_) frame_cb_(safe);
        });
    });
}

void CameraProxy::dispatchFrame(const ProxyMessage& msg) {
    (void)msg; // Reserved for socket-based frame fallback.
}

} // namespace middleware
