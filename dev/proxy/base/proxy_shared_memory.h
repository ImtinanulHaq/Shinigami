/**
 * @file    proxy_shared_memory.h
 * @brief   POSIX shared memory manager — shm_open / mmap / munmap wrapper.
 *
 * ProxyShmRegion owns a single named POSIX shared memory segment.
 * It can be created (producer side — e.g. daemon calling the helper
 * "create" factory) or opened (consumer side — proxy calling "open").
 *
 * The mapped region is accessible via data() immediately after a
 * successful open() or create() call.
 *
 * @thread_safety
 *   createOrOpen() / close() — NOT thread-safe.  Call from setup code only.
 *   data() / size()          — safe from any thread once opened.
 */

#pragma once

#include "proxy_result.h"
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace middleware {

/**
 * @brief RAII wrapper around a POSIX shared memory segment.
 *
 * Usage (proxy / consumer side):
 * @code
 *   ProxyShmRegion shm;
 *   auto r = shm.open("/middleware_proxy_audio_12345", required_size);
 *   if (r.isErr()) { ... }
 *   auto* hdr = static_cast<AudioShmHeader*>(shm.data());
 * @endcode
 *
 * Usage (daemon / producer side in tests):
 * @code
 *   ProxyShmRegion shm;
 *   auto r = shm.create("/middleware_proxy_audio_12345", total_size);
 * @endcode
 */
class ProxyShmRegion {
public:
    ProxyShmRegion();
    ~ProxyShmRegion();

    // Non-copyable; movable.
    ProxyShmRegion(const ProxyShmRegion&)            = delete;
    ProxyShmRegion& operator=(const ProxyShmRegion&) = delete;
    ProxyShmRegion(ProxyShmRegion&&) noexcept;
    ProxyShmRegion& operator=(ProxyShmRegion&&) noexcept;

    /**
     * @brief Create a new POSIX SHM segment (producer/test side).
     *
     * Creates the segment, sets its size with ftruncate, and maps it
     * PROT_READ | PROT_WRITE.  Will fail if a segment with @p name already
     * exists — call close() (with unlink=true) first if recreating.
     *
     * @param name       POSIX SHM name (must start with '/').
     * @param size_bytes Total size to allocate.
     * @return           Result<void>::ok() on success.
     */
    Result<void> create(std::string_view name, size_t size_bytes);

    /**
     * @brief Open an existing POSIX SHM segment (consumer/proxy side).
     *
     * Maps the segment PROT_READ | PROT_WRITE.  The segment must already
     * exist (created by the daemon) and have at least @p min_size bytes.
     *
     * @param name      POSIX SHM name (must start with '/').
     * @param min_size  Expected minimum size; mismatch → ShmSizeMismatch.
     * @return          Result<void>::ok() on success.
     */
    Result<void> open(std::string_view name, size_t min_size);

    /**
     * @brief Unmap and optionally unlink the SHM segment.
     *
     * @param unlink  If true, calls shm_unlink to remove the segment from
     *                the system (only the creator should unlink).
     */
    void close(bool unlink = false) noexcept;

    /** @return Pointer to the mapped region, or nullptr if not open. */
    [[nodiscard]] void*  data()    const noexcept { return ptr_;  }
    /** @return Size of the mapped region in bytes, or 0 if not open. */
    [[nodiscard]] size_t size()    const noexcept { return size_; }
    /** @return true if the segment is currently mapped. */
    [[nodiscard]] bool   isOpen()  const noexcept { return ptr_ != nullptr; }

private:
    void*       ptr_;
    size_t      size_;
    int         shm_fd_;
    std::string name_;
    bool        owner_;   ///< true if we created (and should unlink on close)
};

} // namespace middleware
