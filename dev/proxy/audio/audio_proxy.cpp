/**
 * @file    audio_proxy.cpp
 * @brief   AudioProxy implementation.
 */

#include "audio_proxy.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <cstring>
#include <sstream>

namespace middleware {

// ── Construction ─────────────────────────────────────────────────────────

AudioProxy::AudioProxy(ProxyConfig config)
    : ServiceProxy("audio_service", std::move(config))
    , shm_reader_(std::make_unique<AudioShmReader>())
{}

AudioProxy::~AudioProxy() {
    // onDisconnected called by base destructor via disconnect().
}

// ── Lifecycle interface ────────────────────────────────────────────────────

Result<void> AudioProxy::onConnected() {
    // The daemon is expected to place its SHM name in serviceInfo.ring_name.
    // If we have SM-discovered ring_name, use it; otherwise derive from pid.
    if (!config_.use_shared_memory) {
        return Result<void>::ok();
    }

    // Derive SHM name if not set.
    if (shm_name_.empty()) {
        std::ostringstream oss;
        oss << config_.shm_name_prefix << "audio_" << ::getpid();
        shm_name_ = oss.str();
    }

    // Open SHM (daemon must have created it before registering with SM).
    auto r = shm_reader_->open(shm_name_);
    if (r.isErr()) {
        // Non-fatal — fallback to socket-based delivery.
        shm_reader_.reset(new AudioShmReader());
        return Result<void>::ok();
    }

    return Result<void>::ok();
}

void AudioProxy::onDisconnected() {
    capturing_.store(false, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
    if (shm_reader_) shm_reader_->close();
    if (shm_eventfd_ >= 0) { ::close(shm_eventfd_); shm_eventfd_ = -1; }
}

// ── Frame callback ────────────────────────────────────────────────────────

void AudioProxy::onFrameReady(std::function<void(const AudioFrame&)> cb) {
    frame_cb_ = std::move(cb);
}

void AudioProxy::onShmFrameReady() {
    if (!shm_reader_->isOpen()) return;
    while (shm_reader_->pollReady()) {
        auto r = shm_reader_->readFrame([this](const AudioFrame& f) {
            // Post to callback thread — frame_cb_ runs outside io thread.
            // We must copy the data because the SHM slot may be overwritten
            // by the time the callback thread runs.
            if (frame_cb_) {
                // Shallow copy of the struct — note data pointer is still
                // the SHM pointer, but it's valid during the callback thread
                // dispatch ONLY if the ring is large enough. For safety,
                // copy data into a buffer owned by the lambda.
                std::vector<int16_t> buf(
                    f.data,
                    f.data + f.frame_count * f.channels);
                AudioFrame copy = f;
                copy.data = buf.data();
                postCallback([cb = frame_cb_,
                              copy = std::move(copy),
                              buf  = std::move(buf)]() mutable {
                    copy.data = buf.data();
                    cb(copy);
                });
            }
        });
        if (r.isErr()) break;
    }
}

// ── Capture ───────────────────────────────────────────────────────────────

Result<void> AudioProxy::startCapture() {
    if (capturing_.load()) return Result<void>::ok();
    auto r = sendMessage(AUDIO_MSG_START_CAPTURE, nullptr, 0);
    if (r.isOk()) capturing_.store(true, std::memory_order_release);
    return r;
}

Result<void> AudioProxy::stopCapture() {
    if (!capturing_.load()) return Result<void>::ok();
    auto r = sendMessage(AUDIO_MSG_STOP_CAPTURE, nullptr, 0);
    if (r.isOk()) capturing_.store(false, std::memory_order_release);
    return r;
}

bool AudioProxy::isCapturing() const noexcept {
    return capturing_.load(std::memory_order_acquire);
}

// ── Playback ──────────────────────────────────────────────────────────────

Result<void> AudioProxy::startPlayback() {
    auto r = sendMessage(AUDIO_MSG_START_PLAYBACK, nullptr, 0);
    if (r.isOk()) playing_.store(true, std::memory_order_release);
    return r;
}

Result<void> AudioProxy::stopPlayback() {
    auto r = sendMessage(AUDIO_MSG_STOP_PLAYBACK, nullptr, 0);
    if (r.isOk()) playing_.store(false, std::memory_order_release);
    return r;
}

Result<void> AudioProxy::writePlaybackFrame(const int16_t* data,
                                             size_t frame_count) {
    if (!data || frame_count == 0) {
        return Result<void>::err(ProxyError::InvalidArgument,
                                 "null data or zero frame_count");
    }

    const auto* hdr = shm_reader_->isOpen() ? shm_reader_->header() : nullptr;
    const uint16_t channels   = hdr ? hdr->channels   : 2;
    const uint16_t bit_depth  = hdr ? hdr->bit_depth  : 16;
    const size_t   bytes = frame_count * channels * (bit_depth / 8);
    return sendMessage(AUDIO_MSG_WRITE_FRAME,
                       data, static_cast<uint32_t>(bytes));
}

bool AudioProxy::isPlaying() const noexcept {
    return playing_.load(std::memory_order_acquire);
}

// ── Configuration ─────────────────────────────────────────────────────────

Result<void> AudioProxy::setSampleRate(uint32_t hz) {
    if (hz < 8000 || hz > 192000) {
        return Result<void>::err(ProxyError::InvalidArgument,
                                 "sample rate out of range");
    }
    AudioCmdSampleRate cmd{ hz };
    auto r = sendRequestWaitReply(AUDIO_MSG_SET_SAMPLE_RATE,
                                  &cmd, sizeof(cmd));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    auto& msg = r.value();
    const auto* rsp = reinterpret_cast<const AudioRspStatus*>(msg.payload);
    int st = (rsp && msg.payload_len >= sizeof(*rsp)) ? rsp->status : 0;
    releaseBuffer(msg);
    if (st != 0)
        return Result<void>::err(ProxyError::InvalidResponse, "daemon rejected");
    return Result<void>::ok();
}

Result<void> AudioProxy::setChannels(uint16_t count) {
    if (count == 0 || count > 2) {
        return Result<void>::err(ProxyError::InvalidArgument);
    }
    AudioCmdChannels cmd{ count };
    auto r = sendRequestWaitReply(AUDIO_MSG_SET_CHANNELS, &cmd, sizeof(cmd));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<void> AudioProxy::setBitDepth(uint16_t bits) {
    if (bits != 16 && bits != 32) {
        return Result<void>::err(ProxyError::InvalidArgument,
                                 "bit_depth must be 16 or 32");
    }
    AudioCmdBitDepth cmd{ bits };
    auto r = sendRequestWaitReply(AUDIO_MSG_SET_BIT_DEPTH, &cmd, sizeof(cmd));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<AudioDeviceInfo> AudioProxy::getDeviceInfo() {
    auto r = sendRequestWaitReply(AUDIO_MSG_GET_DEVICE_INFO, nullptr, 0);
    if (r.isErr()) {
        return Result<AudioDeviceInfo>::err(r.error(), r.detail());
    }
    auto& msg = r.value();
    AudioDeviceInfo info{};
    if (msg.payload_len >= sizeof(AudioRspDeviceInfo)) {
        const auto* rsp = reinterpret_cast<const AudioRspDeviceInfo*>(
                                msg.payload);
        info = rsp->info;
    }
    releaseBuffer(msg);
    return Result<AudioDeviceInfo>::ok(info);
}

// ── Volume ────────────────────────────────────────────────────────────────

Result<void> AudioProxy::setVolume(float level) {
    if (level < 0.0f || level > 1.0f) {
        return Result<void>::err(ProxyError::InvalidArgument,
                                 "volume must be 0.0 – 1.0");
    }
    AudioCmdVolume cmd{ level };
    auto r = sendRequestWaitReply(AUDIO_MSG_SET_VOLUME, &cmd, sizeof(cmd));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<float> AudioProxy::getVolume() {
    auto r = sendRequestWaitReply(AUDIO_MSG_GET_VOLUME, nullptr, 0);
    if (r.isErr()) return Result<float>::err(r.error(), r.detail());
    auto& msg = r.value();
    float lvl = 0.0f;
    if (msg.payload_len >= sizeof(AudioRspVolume)) {
        const auto* rsp = reinterpret_cast<const AudioRspVolume*>(msg.payload);
        lvl = rsp->level;
    }
    releaseBuffer(msg);
    return Result<float>::ok(lvl);
}

} // namespace middleware
