/**
 * @file    audio_frame.h
 * @brief   AudioFrame data structure and shared memory layout.
 *
 * AudioFrame holds a pointer directly into the shared memory ring buffer.
 * This pointer is valid ONLY during the onFrameReady callback.  After the
 * callback returns, the daemon may overwrite that SHM slot.
 *
 * ⚠️  IMPORTANT — OWNERSHIP:
 *   AudioFrame::data points into shared memory.  It must NOT be stored
 *   or accessed after the onFrameReady callback returns.  If you need to
 *   keep the data, copy it immediately:
 * @code
 *   audio.onFrameReady([](const AudioFrame& f) {
 *       std::vector<int16_t> copy(f.data, f.data + f.frame_count * f.channels);
 *       // ... use copy ...
 *   });
 * @endcode
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace middleware {

/**
 * @brief A captured audio frame delivered to the application.
 *
 * All metadata fields are copies from the SHM header and remain valid after
 * the callback.  Only @p data and @p data_bytes are invalidated on callback
 * return.
 */
struct AudioFrame {
    uint32_t        sequence;       ///< Monotonic frame counter (starts at 1)
    uint64_t        timestamp_us;   ///< Microseconds since daemon start
    uint32_t        sample_rate;    ///< e.g. 44100, 48000
    uint16_t        channels;       ///< 1 (mono) or 2 (stereo)
    uint16_t        bit_depth;      ///< 16 or 32 bits per sample
    uint32_t        frame_count;    ///< PCM frames (samples per channel)

    /**
     * @brief Pointer into SHM — valid ONLY during onFrameReady callback.
     * @warning  DO NOT store this pointer.  DO NOT use it after return.
     */
    const int16_t*  data;

    /**
     * @brief Total bytes: frame_count * channels * (bit_depth / 8).
     */
    size_t          data_bytes;
};

// ── Shared memory layout (C-compatible — used by both daemon and proxy) ────

/**
 * @brief Header at the start of the audio SHM region.
 *
 * The daemon writes this at creation time.  The proxy validates magic and
 * version before using the region.  Frame slots immediately follow the header.
 *
 * @note Must remain a POD / standard-layout type (included by C daemons too).
 */
struct AudioShmHeader {
    uint32_t  magic;           ///< Must equal AUDIO_SHM_MAGIC
    uint32_t  version;         ///< Layout version (AUDIO_SHM_VERSION)
    uint32_t  slot_count;      ///< Number of frame slots in the ring
    uint32_t  slot_size_bytes; ///< Bytes per slot (must fit one full frame)
    uint32_t  sample_rate;     ///< As configured by the daemon
    uint16_t  channels;
    uint16_t  bit_depth;
    uint32_t  frames_per_slot; ///< PCM frames per slot

    // Written by daemon (producer), read by proxy (consumer).
    // Both sides use volatile / atomic access patterns.
    volatile uint32_t write_index; ///< Next slot daemon will write
    volatile uint32_t read_index;  ///< Next slot proxy will read

    uint8_t   _pad[40];  ///< Pad header to 80 bytes (cache-line friendly)
    // Frame data follows: slot_count * slot_size_bytes bytes
};

static constexpr uint32_t AUDIO_SHM_MAGIC   = 0xA0D10B0Fu;
static constexpr uint32_t AUDIO_SHM_VERSION = 1u;

/**
 * @brief Device information returned by AudioProxy::getDeviceInfo().
 */
struct AudioDeviceInfo {
    char     device_name[64];    ///< e.g. "hw:0,0"
    uint32_t min_sample_rate;
    uint32_t max_sample_rate;
    uint16_t max_channels;
    uint16_t supported_bit_depths; ///< bitmask: bit 0 = 8, bit 1 = 16, bit 2 = 32
};

} // namespace middleware
