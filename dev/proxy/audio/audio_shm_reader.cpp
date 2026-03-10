/**
 * @file    audio_shm_reader.cpp
 * @brief   AudioShmReader implementation.
 */

#include "audio_shm_reader.h"
#include <cstring>
#include <cstdint>
#include <atomic>

namespace middleware {

AudioShmReader::AudioShmReader() = default;
AudioShmReader::~AudioShmReader() { close(); }

// ── open ──────────────────────────────────────────────────────────────────

Result<void> AudioShmReader::open(const std::string& shm_name) {
    auto r = shm_.open(shm_name, sizeof(AudioShmHeader));
    if (r.isErr()) return r;

    auto* h = static_cast<AudioShmHeader*>(shm_.data());

    // Validate magic and version.
    if (h->magic != AUDIO_SHM_MAGIC) {
        shm_.close();
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "bad audio SHM magic");
    }
    if (h->version != AUDIO_SHM_VERSION) {
        shm_.close();
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "audio SHM version mismatch");
    }

    // Check that the mapped region is large enough for all declared slots.
    const size_t required = sizeof(AudioShmHeader)
                          + static_cast<size_t>(h->slot_count)
                            * h->slot_size_bytes;
    if (shm_.size() < required) {
        shm_.close();
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "audio SHM too small");
    }

    hdr_       = h;
    cached_ri_ = h->read_index;
    return Result<void>::ok();
}

// ── close ─────────────────────────────────────────────────────────────────

void AudioShmReader::close() noexcept {
    hdr_ = nullptr;
    shm_.close(false);  // Proxy never unlinks — daemon is owner.
}

bool AudioShmReader::isOpen()    const noexcept { return hdr_ != nullptr; }
const AudioShmHeader* AudioShmReader::header() const noexcept { return hdr_; }

// ── pollReady ─────────────────────────────────────────────────────────────

bool AudioShmReader::pollReady() const noexcept {
    if (!hdr_) return false;
    // Compare daemon's write_index with our local cached_ri_.
    // Use acquire load to see writes the daemon made before updating wi.
    const uint32_t wi = __atomic_load_n(&hdr_->write_index,
                                        __ATOMIC_ACQUIRE);
    return wi != cached_ri_;
}

// ── readFrame ─────────────────────────────────────────────────────────────

Result<void> AudioShmReader::readFrame(
    const std::function<void(const AudioFrame&)>& cb)
{
    if (!hdr_) {
        return Result<void>::err(ProxyError::NotConnected,
                                 "SHM reader not open");
    }

    const uint32_t wi = __atomic_load_n(&hdr_->write_index,
                                        __ATOMIC_ACQUIRE);
    if (wi == cached_ri_) {
        return Result<void>::err(ProxyError::Timeout, "no frame ready");
    }

    const uint32_t slot_idx = cached_ri_ % hdr_->slot_count;

    // Pointer to the raw slot data.
    const uint8_t* base = static_cast<const uint8_t*>(shm_.data())
                          + sizeof(AudioShmHeader)
                          + slot_idx * hdr_->slot_size_bytes;

    AudioFrame frame{};
    frame.sequence    = wi;   // Use write_index as monotonic counter.
    frame.sample_rate = hdr_->sample_rate;
    frame.channels    = hdr_->channels;
    frame.bit_depth   = hdr_->bit_depth;
    frame.frame_count = hdr_->frames_per_slot;
    frame.data        = reinterpret_cast<const int16_t*>(base);
    frame.data_bytes  = static_cast<size_t>(hdr_->frames_per_slot)
                        * hdr_->channels
                        * (hdr_->bit_depth / 8);

    // Invoke callback — data pointer valid only here.
    if (cb) cb(frame);

    // Advance read pointer.
    ++cached_ri_;
    __atomic_store_n(&hdr_->read_index, cached_ri_, __ATOMIC_RELEASE);

    return Result<void>::ok();
}

} // namespace middleware
