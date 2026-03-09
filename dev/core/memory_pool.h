/**
 * @file    memory_pool.h
 * @brief   Fixed-size block memory pool — mmap-backed, seccomp-safe.
 *
 * @details
 * The memory pool allocates a contiguous, mmap'd region of equally-sized
 * blocks.  Allocation and deallocation are O(1) using a bit-map scan with
 * __builtin_ctz().  The pool never calls malloc()/free(); it relies solely
 * on mmap(MAP_PRIVATE|MAP_ANONYMOUS) which is seccomp-safe in all service
 * profiles.
 *
 * Security features:
 *   - madvise(MADV_DONTDUMP) — pool pages are excluded from core dumps.
 *   - explicit_bzero on destroy — the data region is wiped before unmap.
 *   - Optional double-free detection logged to syslog.
 *   - Leak detection on destroy: warns if used_blocks > 0.
 *
 * Thread safety:
 *   - Set config.thread_safe = 1 to enable an internal pthread_mutex_t.
 *   - With thread_safe = 0 the pool is lock-free (no syscall overhead).
 *
 * Typical usage:
 * @code
 *   memory_pool_config_t cfg = {
 *       .block_size  = 4096,
 *       .block_count = 8,
 *       .thread_safe = 0,
 *       .name        = "audio_frame_pool",
 *   };
 *   memory_pool_t *pool = memory_pool_create(&cfg);
 *
 *   void *frame = memory_pool_alloc(pool);
 *   // ... use frame ...
 *   memory_pool_free(pool, frame);
 *   memory_pool_destroy(pool);
 * @endcode
 *
 * @author  Service Layer project
 * @version 1.0.0
 */

#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stddef.h>
#include <stdint.h>

/* ── sensor helper types ────────────────────────────────────────────────── */

/**
 * @brief Typed union covering all sensor reading variants.
 *
 * Used solely for pool-sizing the sensor service's reading_pool.
 * @note block_size = sizeof(sensor_reading_t) = 32 bytes (8-byte aligned).
 */
typedef struct {
    float    x, y, z;       /**< 3-axis values (accel / gyro / mag). */
    float    value;         /**< Scalar value (temp / pressure / etc.).  */
    uint64_t timestamp;     /**< Monotonic timestamp in nanoseconds.     */
    int      type;          /**< 1 = 3-axis, 0 = scalar.                */
} sensor_reading_t;

/* ── error codes ────────────────────────────────────────────────────────── */

/** @defgroup MPOOL_ERR Memory pool error codes
 * @{ */
#define MPOOL_OK            0   /**< Success.                           */
#define MPOOL_ERR_NULL     -1   /**< NULL pointer argument.             */
#define MPOOL_ERR_CONFIG   -2   /**< Invalid configuration value.       */
#define MPOOL_ERR_NOMEM    -3   /**< mmap() failed — out of memory.     */
#define MPOOL_ERR_FULL     -4   /**< All blocks are in use.             */
#define MPOOL_ERR_INVALID  -5   /**< Pointer not from this pool.        */
/** @} */

/* ── configuration ──────────────────────────────────────────────────────── */

/**
 * @brief Pool creation parameters.
 *
 * All fields are copied into the pool on creation; the caller need not
 * keep @p config alive after memory_pool_create() returns.
 */
typedef struct {
    size_t      block_size;   /**< Bytes per block (>= 8, rounded to 8).    */
    size_t      block_count;  /**< Number of blocks in the pool (>= 1).     */
    int         thread_safe;  /**< Non-zero → enable internal pthread mutex.*/
    const char *name;         /**< Human-readable label (for logging only). */
} memory_pool_config_t;

/* ── statistics ─────────────────────────────────────────────────────────── */

/**
 * @brief Snapshot of pool usage counters.
 *
 * Returned by value from memory_pool_get_stats(); fields are consistent
 * only if the caller holds an external lock or thread_safe is enabled.
 */
typedef struct {
    size_t total_blocks; /**< Total blocks configured at creation.       */
    size_t free_blocks;  /**< Blocks currently available.                */
    size_t used_blocks;  /**< Blocks currently allocated.                */
    size_t peak_used;    /**< Highest concurrent usage watermark.        */
    size_t alloc_count;  /**< Cumulative successful allocations.         */
    size_t free_count;   /**< Cumulative frees.                          */
    size_t fail_count;   /**< Cumulative allocation failures (pool full).*/
} memory_pool_stats_t;

/* ── opaque pool handle ─────────────────────────────────────────────────── */

/** @brief Opaque handle returned by memory_pool_create(). Never dereference directly. */
typedef struct memory_pool memory_pool_t;

/* ── public API ─────────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new memory pool.
 *
 * Allocates two mmap regions: one for the data blocks and one for the
 * ownership bitmap.  Both are marked MADV_DONTDUMP.  A pthread_mutex is
 * initialised only if @p config->thread_safe is non-zero.
 *
 * @param config  Pool parameters (must not be NULL).
 * @return        Pointer to new pool, or NULL on failure.
 */
memory_pool_t *memory_pool_create(const memory_pool_config_t *config);

/**
 * @brief Destroy a pool and release all resources.
 *
 * Calls explicit_bzero on the data region before munmap().  Logs a syslog
 * warning if any blocks are still allocated (leak detection).
 *
 * @param pool  Pool to destroy.  Passing NULL is a no-op.
 */
void memory_pool_destroy(memory_pool_t *pool);

/**
 * @brief Allocate one block from the pool.
 *
 * Scans the ownership bitmap with __builtin_ctz() starting from the hint
 * position, wrapping around once if required.  The block is zeroed via
 * memset before being returned.
 *
 * @param pool  Pool to allocate from (must not be NULL).
 * @return      Pointer to the allocated block, or NULL if pool is full.
 */
void *memory_pool_alloc(memory_pool_t *pool);

/**
 * @brief Release a block back to the pool.
 *
 * Validates that @p ptr was obtained from @p pool.  Logs a syslog warning
 * and returns without modifying the pool if @p ptr is invalid or already
 * free (double-free detection).
 *
 * @param pool  Pool that owns @p ptr.
 * @param ptr   Pointer previously returned by memory_pool_alloc().
 */
void memory_pool_free(memory_pool_t *pool, void *ptr);

/**
 * @brief Read current pool statistics.
 *
 * @param pool  Pool to query (must not be NULL).
 * @param out   Destination stats struct (must not be NULL).
 * @return      MPOOL_OK, or MPOOL_ERR_NULL if either argument is NULL.
 */
int memory_pool_get_stats(const memory_pool_t *pool, memory_pool_stats_t *out);

/**
 * @brief Reset the pool to its initial empty state.
 *
 * Marks all blocks as free and clears the bitmap.  Any pointers previously
 * returned by memory_pool_alloc() are silently invalidated.  Counters
 * (alloc_count, free_count, fail_count, peak_used) are also reset.
 *
 * @param pool  Pool to reset.
 * @return      MPOOL_OK, or MPOOL_ERR_NULL if @p pool is NULL.
 */
int memory_pool_reset(memory_pool_t *pool);

/**
 * @brief Log pool statistics and usage summary via syslog (LOG_INFO).
 *
 * Useful for debugging and operational visibility.  Output includes the
 * pool name, block dimensions, and all counters from memory_pool_stats_t.
 *
 * @param pool  Pool to dump (must not be NULL).
 */
void memory_pool_dump(const memory_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* MEMORY_POOL_H */
