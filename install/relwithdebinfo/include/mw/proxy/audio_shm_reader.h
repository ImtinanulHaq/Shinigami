/**
 * @file    audio_shm_reader.h
 * @brief   Reads PCM audio frames from the shared memory ring buffer.
 *
 * AudioShmReader attaches to a POSIX SHM segment created by the audio
 * service daemon.  It monitors the write_index field in AudioShmHeader and
 * calls user callbacks whenever new frames are available.
 *
 * @thread_safety
 *   open() / close()    — NOT thread-safe; call from setup/teardown only.
 *   pollReady()         — safe from the io thread only.
 *   readFrame()         — safe from the io thread only.
 */

#pragma once

#include "audio_frame.h"
#include "../base/proxy_result.h"
#include "../base/proxy_shared_memory.h"
#include <functional>
#include <string>

namespace middleware {

/**
 * @brief Shared memory ring buffer reader for PCM audio frames.
 */
class AudioShmReader {
public:
    AudioShmReader();
    ~AudioShmReader();

    /**
     * @brief Attach to an existing audio SHM segment.
     *
     * @param shm_name  POSIX SHM name (e.g. "/middleware_proxy_audio_1234").
     * @return          ok() on success, ShmSizeMismatch if magic/version wrong.
     */
    Result<void> open(const std::string& shm_name);

    /**
     * @brief Detach from the SHM segment.
     *
     * Does NOT unlink (the daemon is the owner and will unlink on shutdown).
     */
    void close() noexcept;

    /**
     * @brief Check whether at least one unread frame is available.
     *
     * @return true if write_index != read_index.
     */
    [[nodiscard]] bool pollReady() const noexcept;

    /**
     * @brief Read the next frame from the ring and invoke the callback.
     *
     * Fills an AudioFrame whose @p data points directly into the SHM slot,
     * calls @p cb, then advances read_index.  After cb returns the SHM slot
     * may be overwritten by the daemon.
     *
     * @param cb  Callback invoked with the frame (synchronously, this call).
     * @return    ok() on success, RecvFailed if the ring appears corrupt.
     */
    Result<void> readFrame(
        const std::function<void(const AudioFrame&)>& cb);

    /** @return true if open() succeeded and the SHM is mapped. */
    [[nodiscard]] bool isOpen() const noexcept;

    /** @return Pointer to the SHM header (null if not open). */
    [[nodiscard]] const AudioShmHeader* header() const noexcept;

private:
    ProxyShmRegion     shm_;
    AudioShmHeader*    hdr_{nullptr};   ///< Pointer into shm_.data()
    uint32_t           cached_ri_{0};   ///< Local mirror of read_index
};

} // namespace middleware
