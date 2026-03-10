/**
 * @file    camera_shm_reader.cpp
 * @brief   CameraShmReader implementation.
 */

#include "camera_shm_reader.h"
#include <cstring>

namespace middleware {

CameraShmReader::CameraShmReader() = default;
CameraShmReader::~CameraShmReader() { close(); }

Result<void> CameraShmReader::open(const std::string& shm_name) {
    auto r = shm_.open(shm_name, sizeof(CameraShmHeader));
    if (r.isErr()) return r;

    auto* h = static_cast<CameraShmHeader*>(shm_.data());
    if (h->magic != CAMERA_SHM_MAGIC) {
        shm_.close();
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "bad camera SHM magic");
    }
    if (h->version != CAMERA_SHM_VERSION) {
        shm_.close();
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "camera SHM version mismatch");
    }

    const size_t required = sizeof(CameraShmHeader)
                          + static_cast<size_t>(h->slot_count) * h->slot_size_bytes;
    if (shm_.size() < required) {
        shm_.close();
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "camera SHM too small");
    }

    hdr_       = h;
    cached_ri_ = h->read_index;
    return Result<void>::ok();
}

void CameraShmReader::close() noexcept {
    hdr_ = nullptr;
    shm_.close(false);
}

bool CameraShmReader::isOpen()    const noexcept { return hdr_ != nullptr; }
const CameraShmHeader* CameraShmReader::header() const noexcept { return hdr_; }

bool CameraShmReader::pollReady() const noexcept {
    if (!hdr_) return false;
    const uint32_t wi = __atomic_load_n(&hdr_->write_index, __ATOMIC_ACQUIRE);
    return wi != cached_ri_;
}

Result<void> CameraShmReader::readFrame(
    const std::function<void(const CameraFrame&)>& cb)
{
    if (!hdr_) return Result<void>::err(ProxyError::NotConnected);

    const uint32_t wi = __atomic_load_n(&hdr_->write_index, __ATOMIC_ACQUIRE);
    if (wi == cached_ri_) return Result<void>::err(ProxyError::Timeout);

    const uint32_t slot_idx = cached_ri_ % hdr_->slot_count;
    const uint8_t* base = static_cast<const uint8_t*>(shm_.data())
                        + sizeof(CameraShmHeader)
                        + slot_idx * hdr_->slot_size_bytes;

    CameraFrame frame{};
    frame.sequence   = wi;
    frame.width      = hdr_->width;
    frame.height     = hdr_->height;
    frame.fps        = hdr_->fps;
    frame.format     = static_cast<CameraFrame::Format>(hdr_->format);
    frame.data       = base;

    switch (frame.format) {
    case CameraFrame::Format::YUYV:
        frame.data_bytes = static_cast<size_t>(hdr_->width) * hdr_->height * 2;
        break;
    case CameraFrame::Format::RGB24:
        frame.data_bytes = static_cast<size_t>(hdr_->width) * hdr_->height * 3;
        break;
    default:
        // MJPEG — actual size stored in first 4 bytes of slot.
        uint32_t mjlen;
        std::memcpy(&mjlen, base, sizeof(mjlen));
        frame.data       = base + sizeof(uint32_t);
        frame.data_bytes = static_cast<size_t>(mjlen);
        break;
    }

    if (cb) cb(frame);

    ++cached_ri_;
    __atomic_store_n(&hdr_->read_index, cached_ri_, __ATOMIC_RELEASE);
    return Result<void>::ok();
}

} // namespace middleware
