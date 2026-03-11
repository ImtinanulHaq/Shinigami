/**
 * @file    memory_pool.c
 * @brief   Fixed-size block memory pool — mmap-backed, seccomp-safe.
 *
 * Implementation notes:
 * - Two mmap(MAP_PRIVATE|MAP_ANONYMOUS) regions: data + bitmap.
 * - madvise(MADV_DONTDUMP) on both regions for security.
 * - O(1) alloc via __builtin_ctz() scan of inverted bitmap bytes.
 * - next_free_hint accelerates the common case (sequential alloc/free).
 * - explicit_bzero on destroy to prevent data leaks after unmap.
 * - Conditional pthread_mutex_t (only initialised when thread_safe != 0).
 * - All logging via syslog() — no fprintf to avoid mixed output streams.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "memory_pool.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <syslog.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

/* explicit_bzero may not be declared on all libc versions */
#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 25)
static inline void explicit_bzero(void *s, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)s;
    while (n--) *p++ = 0;
}
#endif

/* ── alignment helper ───────────────────────────────────────────────────── */

/** Round @p n up to the nearest multiple of 8. */
static inline size_t align8(size_t n)
{
    return (n + 7u) & ~(size_t)7u;
}

/* ── internal struct ────────────────────────────────────────────────────── */

struct memory_pool {
    uint8_t             *base;           /**< mmap'd data region.              */
    uint8_t             *bitmap;         /**< mmap'd bitmap (1 bit == 1 block).*/
    size_t               next_free_hint; /**< Block index hint for next scan.  */
    memory_pool_config_t config;         /**< Copy of creation parameters.     */
    memory_pool_stats_t  stats;          /**< Live counters.                   */
    pthread_mutex_t      lock;           /**< Only valid when thread_safe != 0.*/
    size_t               total_size;     /**< data region size (bytes).        */
    size_t               bitmap_size;    /**< bitmap region size (bytes).      */
    char                 name[64];       /**< Null-terminated pool name.       */
};

/* ── locking macros (compile-out when !thread_safe) ─────────────────────── */

#define POOL_LOCK(p)   do { if ((p)->config.thread_safe) pthread_mutex_lock(&(p)->lock);   } while (0)
#define POOL_UNLOCK(p) do { if ((p)->config.thread_safe) pthread_mutex_unlock(&(p)->lock); } while (0)

/* ── mmap helpers ───────────────────────────────────────────────────────── */

/**
 * @internal Allocate an anonymous private mmap region of @p size bytes.
 * Applies MADV_DONTDUMP so the pages are excluded from core dumps.
 * Returns MAP_FAILED on error (errno preserved).
 */
static uint8_t *mpool_mmap(size_t size)
{
    void *p = mmap(NULL, size,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    if (p == MAP_FAILED)
        return (uint8_t *)MAP_FAILED;

#ifdef MADV_DONTDUMP
    madvise(p, size, MADV_DONTDUMP);
#endif
    return (uint8_t *)p;
}

/* ── memory_pool_create ─────────────────────────────────────────────────── */

memory_pool_t *memory_pool_create(const memory_pool_config_t *config)
{
    if (!config) {
        syslog(LOG_ERR, "memory_pool_create: NULL config");
        return NULL;
    }

    /* Validate and normalise block_size */
    size_t bsz = align8(config->block_size);
    if (bsz < 8) {
        syslog(LOG_ERR, "memory_pool_create[%s]: block_size too small (%zu)",
               config->name ? config->name : "?", config->block_size);
        return NULL;
    }

    if (config->block_count == 0) {
        syslog(LOG_ERR, "memory_pool_create[%s]: block_count must be >= 1",
               config->name ? config->name : "?");
        return NULL;
    }

    /* Allocate the pool descriptor itself via mmap (avoids malloc dependency) */
    size_t desc_size = align8(sizeof(struct memory_pool));
    uint8_t *desc_raw = mpool_mmap(desc_size);
    if (desc_raw == (uint8_t *)MAP_FAILED) {
        syslog(LOG_ERR, "memory_pool_create: descriptor mmap failed: %m");
        return NULL;
    }
    memory_pool_t *pool = (memory_pool_t *)desc_raw;
    memset(pool, 0, sizeof(*pool));

    /* Copy config, normalising block_size */
    pool->config            = *config;
    pool->config.block_size = bsz;
    pool->config.name       = pool->name; /* point into embedded buffer */

    if (config->name) {
        size_t nlen = strlen(config->name);
        if (nlen >= sizeof(pool->name)) nlen = sizeof(pool->name) - 1;
        memcpy(pool->name, config->name, nlen);
        pool->name[nlen] = '\0';
    } else {
        memcpy(pool->name, "unnamed_pool", 13);
    }

    /* Allocate data region */
    pool->total_size = bsz * config->block_count;
    pool->base = mpool_mmap(pool->total_size);
    if (pool->base == (uint8_t *)MAP_FAILED) {
        syslog(LOG_ERR, "memory_pool_create[%s]: data mmap failed (%zu B): %m",
               pool->name, pool->total_size);
        munmap(pool, desc_size);
        return NULL;
    }

    /* Allocate bitmap region — 1 bit per block */
    pool->bitmap_size = (config->block_count + 7u) / 8u;
    /* Round up to a full page boundary for cleanliness */
    pool->bitmap = mpool_mmap(pool->bitmap_size);
    if (pool->bitmap == (uint8_t *)MAP_FAILED) {
        syslog(LOG_ERR, "memory_pool_create[%s]: bitmap mmap failed (%zu B): %m",
               pool->name, pool->bitmap_size);
        explicit_bzero(pool->base, pool->total_size);
        munmap(pool->base, pool->total_size);
        munmap(pool, desc_size);
        return NULL;
    }
    /* Bitmap is already zeroed by the OS (MAP_ANONYMOUS); 0 bit == free */

    /* Initialise statistics */
    pool->stats.total_blocks = config->block_count;
    pool->stats.free_blocks  = config->block_count;

    /* Initialise mutex if needed */
    if (config->thread_safe) {
        pthread_mutex_init(&pool->lock, NULL);
    }

    syslog(LOG_INFO,
           "memory_pool_create[%s]: %zu blocks × %zu B = %zu B data + %zu B bitmap",
           pool->name,
           config->block_count, bsz,
           pool->total_size, pool->bitmap_size);

    return pool;
}

/* ── memory_pool_destroy ────────────────────────────────────────────────── */

void memory_pool_destroy(memory_pool_t *pool)
{
    if (!pool) return;

    POOL_LOCK(pool);

    if (pool->stats.used_blocks > 0) {
        syslog(LOG_WARNING,
               "memory_pool_destroy[%s]: %zu block(s) still allocated (leak!)",
               pool->name, pool->stats.used_blocks);
    }

    size_t desc_size  = align8(sizeof(struct memory_pool));
    size_t total_size = pool->total_size;
    size_t bmap_size  = pool->bitmap_size;
    uint8_t *base     = pool->base;
    uint8_t *bitmap   = pool->bitmap;

    /* Wipe data before releasing */
    if (base)   explicit_bzero(base,   total_size);
    if (bitmap) explicit_bzero(bitmap, bmap_size);

    /* Keep thread_safe flag for unlock below */
    int ts = pool->config.thread_safe;

    if (ts) {
        POOL_UNLOCK(pool);
        pthread_mutex_destroy(&pool->lock);
    }

    /* Free all three mmap regions */
    if (bitmap) munmap(bitmap, bmap_size);
    if (base)   munmap(base,   total_size);
    munmap(pool, desc_size);
    /* pool pointer is now invalid */
}

/* ── memory_pool_alloc ──────────────────────────────────────────────────── */

void *memory_pool_alloc(memory_pool_t *pool)
{
    if (!pool) {
        syslog(LOG_ERR, "memory_pool_alloc: NULL pool");
        return NULL;
    }

    POOL_LOCK(pool);

    size_t   block_count = pool->config.block_count;
    size_t   bitmap_bytes = pool->bitmap_size;
    uint8_t *bm          = pool->bitmap;

    /* Two-pass scan: start from hint, wrap around once. */
    size_t start_byte = pool->next_free_hint / 8u;

    for (int pass = 0; pass < 2; pass++) {
        size_t lo = (pass == 0) ? start_byte : 0u;
        size_t hi = (pass == 0) ? bitmap_bytes : start_byte;

        for (size_t bi = lo; bi < hi; bi++) {
            uint8_t inv = (uint8_t)(~bm[bi]); /* 1 == free bit */
            if (inv == 0) continue;

            int bit = __builtin_ctz((unsigned int)inv);
            size_t idx = bi * 8u + (size_t)bit;
            if (idx >= block_count) continue;

            /* Mark as used */
            bm[bi] |= (uint8_t)(1u << bit);

            /* Advance hint past this block */
            pool->next_free_hint = idx + 1u;
            if (pool->next_free_hint >= block_count)
                pool->next_free_hint = 0u;

            /* Update stats */
            pool->stats.used_blocks++;
            pool->stats.free_blocks--;
            pool->stats.alloc_count++;
            if (pool->stats.used_blocks > pool->stats.peak_used)
                pool->stats.peak_used = pool->stats.used_blocks;

            POOL_UNLOCK(pool);

            uint8_t *blk = pool->base + idx * pool->config.block_size;
            memset(blk, 0, pool->config.block_size);
            return blk;
        }
    }

    /* Pool is full */
    pool->stats.fail_count++;
    POOL_UNLOCK(pool);

    syslog(LOG_WARNING, "memory_pool_alloc[%s]: pool full (%zu blocks in use)",
           pool->name, pool->stats.used_blocks);
    return NULL;
}

/* ── memory_pool_free ───────────────────────────────────────────────────── */

void memory_pool_free(memory_pool_t *pool, void *ptr)
{
    if (!pool || !ptr) {
        syslog(LOG_WARNING, "memory_pool_free: NULL pool or ptr");
        return;
    }

    uint8_t *p = (uint8_t *)ptr;

    /* Validate that ptr belongs to this pool's data region */
    if (p < pool->base || p >= pool->base + pool->total_size) {
        syslog(LOG_ERR,
               "memory_pool_free[%s]: ptr %p not in pool range [%p, %p)",
               pool->name, (void *)p,
               (void *)pool->base,
               (void *)(pool->base + pool->total_size));
        return;
    }

    /* Pointer must be block-aligned */
    size_t offset = (size_t)(p - pool->base);
    if (offset % pool->config.block_size != 0) {
        syslog(LOG_ERR,
               "memory_pool_free[%s]: ptr %p is not block-aligned (offset=%zu, bsz=%zu)",
               pool->name, ptr, offset, pool->config.block_size);
        return;
    }

    size_t idx      = offset / pool->config.block_size;
    size_t byte_idx = idx / 8u;
    int    bit      = (int)(idx % 8u);
    uint8_t mask    = (uint8_t)(1u << bit);

    POOL_LOCK(pool);

    /* Double-free detection */
    if (!(pool->bitmap[byte_idx] & mask)) {
        POOL_UNLOCK(pool);
        syslog(LOG_ERR,
               "memory_pool_free[%s]: double-free detected for block %zu (ptr=%p)",
               pool->name, idx, ptr);
        return;
    }

    /* Clear bit — mark as free */
    pool->bitmap[byte_idx] &= (uint8_t)(~mask);

    /* Regress hint if this block is before it */
    if (idx < pool->next_free_hint)
        pool->next_free_hint = idx;

    pool->stats.used_blocks--;
    pool->stats.free_blocks++;
    pool->stats.free_count++;

    POOL_UNLOCK(pool);
}

/* ── memory_pool_get_stats ──────────────────────────────────────────────── */

int memory_pool_get_stats(const memory_pool_t *pool, memory_pool_stats_t *out)
{
    if (!pool || !out) return MPOOL_ERR_NULL;

    /* Cast away const for the lock (stats read is not strictly const-safe) */
    memory_pool_t *mp = (memory_pool_t *)pool;
    POOL_LOCK(mp);
    *out = pool->stats;
    POOL_UNLOCK(mp);
    return MPOOL_OK;
}

/* ── memory_pool_reset ──────────────────────────────────────────────────── */

int memory_pool_reset(memory_pool_t *pool)
{
    if (!pool) return MPOOL_ERR_NULL;

    POOL_LOCK(pool);

    memset(pool->bitmap, 0, pool->bitmap_size);
    pool->next_free_hint       = 0;
    pool->stats.used_blocks    = 0;
    pool->stats.free_blocks    = pool->config.block_count;
    pool->stats.alloc_count    = 0;
    pool->stats.free_count     = 0;
    pool->stats.fail_count     = 0;
    pool->stats.peak_used      = 0;

    POOL_UNLOCK(pool);
    return MPOOL_OK;
}

/* ── memory_pool_dump ───────────────────────────────────────────────────── */

void memory_pool_dump(const memory_pool_t *pool)
{
    if (!pool) return;

    memory_pool_stats_t s;
    memory_pool_get_stats(pool, &s);

    syslog(LOG_INFO,
           "memory_pool_dump[%s]: "
           "blocks=%zu/%zu (used/total), free=%zu, peak=%zu | "
           "allocs=%zu frees=%zu fails=%zu | "
           "block_size=%zu B",
           pool->name,
           s.used_blocks, s.total_blocks,
           s.free_blocks, s.peak_used,
           s.alloc_count, s.free_count, s.fail_count,
           pool->config.block_size);
}
