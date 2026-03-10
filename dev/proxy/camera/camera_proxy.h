/**
 * @file    camera_proxy.h
 * @brief   CameraProxy — high-level camera access API.
 *
 * Messages: 0x0200–0x02FF
 */
#pragma once

#include "../base/service_proxy.h"
#include "camera_frame.h"
#include "camera_shm_reader.h"
#include <functional>
#include <atomic>
#include <mutex>

namespace middleware {

/** @brief Message type constants for the camera protocol. */
enum CameraMessageType : uint16_t {
    CAMERA_MSG_START_STREAM   = 0x0201,
    CAMERA_MSG_STOP_STREAM    = 0x0202,
    CAMERA_MSG_SET_RESOLUTION = 0x0203,
    CAMERA_MSG_SET_FPS        = 0x0204,
    CAMERA_MSG_SET_FORMAT     = 0x0205,
    CAMERA_MSG_CAPTURE_STILL  = 0x0206,
    CAMERA_MSG_GET_DEVICE_INFO= 0x0207,
    CAMERA_MSG_FRAME_READY    = 0x020C,
};

/** @brief Callback types. */
using CameraFrameCallback = std::function<void(CameraFrame)>;

/**
 * @class  CameraProxy
 * @brief  Connects to the camera HAL daemon and exposes stream/capture APIs.
 *
 * Thread-safe: all public methods may be called from any thread.
 */
class PROXY_API CameraProxy final : public ServiceProxy {
public:
    explicit CameraProxy(ProxyConfig cfg = {});
    ~CameraProxy() override;

    // ---- Stream control ----
    Result<void> startStream();
    Result<void> stopStream();
    [[nodiscard]] bool isStreaming() const noexcept;

    // ---- Configuration ----
    Result<void> setResolution(uint32_t width, uint32_t height);
    Result<void> setFps(uint32_t fps);
    Result<void> setFormat(CameraFrame::Format fmt);

    // ---- Synchronous single-frame capture ----
    Result<CameraFrame> captureStillFrame(
        std::vector<uint8_t>& pixel_buf);

    // ---- Device info ----
    Result<CameraDeviceInfo> getDeviceInfo();

    // ---- Async callback ----
    /** @brief Register callback invoked on the callback thread for each frame.
     *  Frame pixel data is COPIED into @p pixel_buf owned by CameraFrame. */
    void onFrameReady(CameraFrameCallback cb);

private:
    Result<void> onConnected()    override;
    void         onDisconnected() override;
    void         ioLoop()         override;

    void dispatchFrame(const ProxyMessage& msg);

    CameraShmReader        shm_reader_;
    CameraFrameCallback    frame_cb_;
    mutable std::mutex     cb_mutex_;
    std::atomic<bool>      streaming_{false};
    CameraDeviceInfo       device_info_{};

    // Per-frame pixel copy buffer
    std::vector<uint8_t>   frame_pixel_buf_;
    std::mutex             pixel_buf_mutex_;
};

} // namespace middleware
