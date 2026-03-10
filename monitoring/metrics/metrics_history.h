/**
 * @file    metrics_history.h
 * @brief   Per-metric time-series storage with variable resolution.
 *
 * Each history instance owns a heap-allocated circular buffer of (value, ts)
 * pairs.  Resolution and duration are configured at init time:
 *
 *  - CPU/RAM/Load:               1000 ms resolution, 300 000 ms window  (300 pts)
 *  - Ring buf / io_uring / Pool: 100 ms resolution,  60 000 ms window   (600 pts)
 *  - Security / Alerts / Health: 1000 ms resolution, 3 600 000 ms window (3600 pts)
 *
 * @thread_safety  Not thread-safe.  Callers must hold the appropriate
 *                 per-subsystem rwlock when calling push/query.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char        name[64];
    uint32_t    resolution_ms;   /**< How often a point is stored. */
    uint32_t    duration_ms;     /**< Total history window. */
    uint32_t    capacity;        /**< = duration_ms / resolution_ms */
    uint32_t    head;            /**< Next write position. */
    uint32_t    count;           /**< How many valid entries (≤ capacity). */
    double     *values;          /**< Circular buffer [capacity]. */
    uint64_t   *timestamps_ms;   /**< Timestamps [capacity]. */
    uint64_t    last_push_ms;    /**< Monotonic time of last push. */
} metrics_history_t;

/**
 * @brief  Allocate and initialise a metrics_history_t.
 * @param  name           Metric name (copied, max 63 chars).
 * @param  resolution_ms  Minimum ms between stored points.
 * @param  duration_ms    Total history window duration.
 * @return Pointer on success, NULL on allocation failure.
 */
static inline metrics_history_t *
metrics_history_create(const char *name, uint32_t resolution_ms, uint32_t duration_ms)
{
    if (resolution_ms == 0) resolution_ms = 1;
    metrics_history_t *h = (metrics_history_t *)malloc(sizeof(*h));
    if (!h) return NULL;
    memset(h, 0, sizeof(*h));
    snprintf(h->name, sizeof(h->name), "%s", name ? name : "");
    h->resolution_ms = resolution_ms;
    h->duration_ms   = duration_ms;
    h->capacity      = duration_ms / resolution_ms;
    if (h->capacity == 0) h->capacity = 1;
    h->values        = (double   *)calloc(h->capacity, sizeof(double));
    h->timestamps_ms = (uint64_t *)calloc(h->capacity, sizeof(uint64_t));
    if (!h->values || !h->timestamps_ms) {
        free(h->values);
        free(h->timestamps_ms);
        free(h);
        return NULL;
    }
    return h;
}

/**
 * @brief  Free a metrics_history_t and its data buffers.
 * @param  h  May be NULL (no-op).
 */
static inline void metrics_history_destroy(metrics_history_t *h)
{
    if (!h) return;
    free(h->values);
    free(h->timestamps_ms);
    free(h);
}

/**
 * @brief  Push a new value.  If less than resolution_ms has elapsed since the
 *         last push, the call is ignored (rate limiting for storage).
 * @param  h      History instance.
 * @param  value  New sample.
 * @param  ts_ms  Monotonic timestamp (from metric_now_ms()).
 */
static inline void metrics_history_push(metrics_history_t *h,
                                         double value, uint64_t ts_ms)
{
    if (!h) return;
    if (h->count > 0 && (ts_ms - h->last_push_ms) < h->resolution_ms) return;
    h->values[h->head]        = value;
    h->timestamps_ms[h->head] = ts_ms;
    h->head = (h->head + 1) % h->capacity;
    if (h->count < h->capacity) h->count++;
    h->last_push_ms = ts_ms;
}

/**
 * @brief  Copy the N most-recent values into out_buf (oldest → newest order).
 * @param  h        History instance.
 * @param  out_buf  Caller-provided float array of length n.
 * @param  n        Number of points to copy.
 * @param  ts_buf   Optional: caller array of uint64_t for timestamps; may be NULL.
 * @return Actual number of points copied (≤ n, ≤ h->count).
 */
static inline uint32_t metrics_history_get_last(const metrics_history_t *h,
                                                  float *out_buf, uint32_t n,
                                                  uint64_t *ts_buf)
{
    if (!h || !out_buf || n == 0) return 0;
    uint32_t avail = h->count < n ? h->count : n;
    /* Oldest entry is at (head - count + capacity) % capacity */
    uint32_t start = (h->head + h->capacity - h->count) % h->capacity;
    /* We want only the last `avail` entries */
    start = (h->head + h->capacity - avail) % h->capacity;
    for (uint32_t i = 0; i < avail; i++) {
        uint32_t idx = (start + i) % h->capacity;
        out_buf[i] = (float)h->values[idx];
        if (ts_buf) ts_buf[i] = h->timestamps_ms[idx];
    }
    return avail;
}

/**
 * @brief  Return the most-recent value, or 0.0 if history is empty.
 */
static inline double metrics_history_last(const metrics_history_t *h)
{
    if (!h || h->count == 0) return 0.0;
    uint32_t idx = (h->head + h->capacity - 1) % h->capacity;
    return h->values[idx];
}

/**
 * @brief  Compute the average of the last @p n samples.
 * @return Average, or 0.0 if not enough data.
 */
static inline double metrics_history_avg(const metrics_history_t *h, uint32_t n)
{
    if (!h || h->count == 0) return 0.0;
    uint32_t avail = h->count < n ? h->count : n;
    double sum = 0.0;
    uint32_t start = (h->head + h->capacity - avail) % h->capacity;
    for (uint32_t i = 0; i < avail; i++) {
        sum += h->values[(start + i) % h->capacity];
    }
    return sum / avail;
}
