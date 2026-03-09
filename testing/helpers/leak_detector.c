/**
 * @file leak_detector.c
 * @brief malloc/free tracking wrapper implementation.
 */
#include "leak_detector.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define LD_MAX_ALLOCS 4096

typedef struct {
    void   *ptr;
    size_t  size;
} ld_entry_t;

static ld_entry_t  g_table[LD_MAX_ALLOCS];
static int         g_count;
static ld_stats_t  g_stats;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void ld_reset(void)
{
    pthread_mutex_lock(&g_lock);
    memset(g_table, 0, sizeof(g_table));
    g_count = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    pthread_mutex_unlock(&g_lock);
}

ld_stats_t ld_get_stats(void)
{
    pthread_mutex_lock(&g_lock);
    ld_stats_t s = g_stats;
    pthread_mutex_unlock(&g_lock);
    return s;
}

void ld_dump(void)
{
    ld_stats_t s = ld_get_stats();
    fprintf(stderr,
            "[LeakDetector] allocs=%zu frees=%zu live=%zu live_bytes=%zu "
            "peak=%zu double_frees=%zu\n",
            s.total_allocs, s.total_frees,
            s.live_allocs,  s.live_bytes,
            s.peak_bytes,   s.double_frees);
}

/* Internal: record an allocation */
static void record_alloc(void *ptr, size_t size)
{
    if (!ptr) return;
    pthread_mutex_lock(&g_lock);
    g_stats.total_allocs++;
    g_stats.live_allocs++;
    g_stats.live_bytes += size;
    if (g_stats.live_bytes > g_stats.peak_bytes)
        g_stats.peak_bytes = g_stats.live_bytes;
    for (int i = 0; i < LD_MAX_ALLOCS; i++) {
        if (!g_table[i].ptr) {
            g_table[i].ptr  = ptr;
            g_table[i].size = size;
            if (i >= g_count) g_count = i + 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* Internal: remove a recorded allocation */
static void record_free(void *ptr)
{
    if (!ptr) return;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_count; i++) {
        if (g_table[i].ptr == ptr) {
            g_stats.total_frees++;
            g_stats.live_allocs--;
            g_stats.live_bytes -= g_table[i].size;
            g_table[i].ptr  = NULL;
            g_table[i].size = 0;
            pthread_mutex_unlock(&g_lock);
            return;
        }
    }
    /* Not found — double-free or free of unknown pointer */
    g_stats.double_frees++;
    pthread_mutex_unlock(&g_lock);
}

/* ── Override wrappers (call real libc via dlsym in production;
       here we use __wrap_ prefix with --wrap linker option) ─────── */

void *__wrap_malloc(size_t size)
{
    extern void *__real_malloc(size_t);
    void *p = __real_malloc(size);
    record_alloc(p, size);
    return p;
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
    extern void *__real_calloc(size_t, size_t);
    void *p = __real_calloc(nmemb, size);
    record_alloc(p, nmemb * size);
    return p;
}

void *__wrap_realloc(void *ptr, size_t size)
{
    extern void *__real_realloc(void *, size_t);
    record_free(ptr);
    void *p = __real_realloc(ptr, size);
    record_alloc(p, size);
    return p;
}

void __wrap_free(void *ptr)
{
    extern void __real_free(void *);
    record_free(ptr);
    __real_free(ptr);
}
