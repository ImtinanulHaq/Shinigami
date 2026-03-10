/**
 * @file    metrics_types.h
 * @brief   Fundamental metric type definitions: gauge, counter, histogram, sparkline.
 *
 * These types are intentionally simple value-containers.  The metrics_history
 * module provides time-series storage on top of them.
 *
 * @thread_safety  Values are plain scalars; callers must synchronise.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ── Gauge — any floating-point value ─────────────────────────────────────── */

/**
 * @brief  A gauge represents a value that can go up or down.
 *         Examples: CPU%, RAM bytes, queue depth, fill percentage.
 */
typedef struct {
    double   value;
    double   min_lifetime;
    double   max_lifetime;
    uint64_t last_update_ms;
} gauge_t;

static inline void gauge_set(gauge_t *g, double v, uint64_t ts_ms)
{
    g->value = v;
    if (v < g->min_lifetime) g->min_lifetime = v;
    if (v > g->max_lifetime) g->max_lifetime = v;
    g->last_update_ms = ts_ms;
}

static inline void gauge_init(gauge_t *g)
{
    g->value = 0.0;
    g->min_lifetime = 1e18;
    g->max_lifetime = -1e18;
    g->last_update_ms = 0;
}

/* ── Counter — monotonically increasing integer ───────────────────────────── */

/**
 * @brief  A counter may only increase.  Resets are tracked separately.
 *         Rate-of-change is computed by metrics_delta.h.
 */
typedef struct {
    uint64_t value;
    uint64_t prev_value;     /**< Previous snapshot — for rate calculation. */
    uint64_t last_update_ms;
    uint64_t reset_count;
} counter_t;

static inline void counter_add(counter_t *c, uint64_t delta, uint64_t ts_ms)
{
    c->prev_value = c->value;
    c->value += delta;
    c->last_update_ms = ts_ms;
}

static inline void counter_set(counter_t *c, uint64_t new_val, uint64_t ts_ms)
{
    if (new_val < c->value) {
        c->reset_count++;
    }
    c->prev_value = c->value;
    c->value = new_val;
    c->last_update_ms = ts_ms;
}

static inline void counter_init(counter_t *c)
{
    c->value = 0;
    c->prev_value = 0;
    c->last_update_ms = 0;
    c->reset_count = 0;
}

/* ── Histogram — distribution over configurable buckets ──────────────────── */

#define METRIC_HISTOGRAM_MAX_BUCKETS  32

/**
 * @brief  A histogram stores the distribution of observed values.
 *         Compatible with Prometheus histogram exposition format.
 */
typedef struct {
    double   upper_bounds[METRIC_HISTOGRAM_MAX_BUCKETS]; /**< Bucket le= values. */
    uint64_t counts[METRIC_HISTOGRAM_MAX_BUCKETS];       /**< Cumulative counts. */
    uint32_t num_buckets;
    double   sum;
    uint64_t count;
    uint64_t last_update_ms;
} histogram_t;

/**
 * @brief  Observe a value into the histogram.
 * @param  h       Target histogram.
 * @param  value   Observed value (same units as upper_bounds).
 * @param  ts_ms   Timestamp (for last_update_ms).
 */
static inline void histogram_observe(histogram_t *h, double value, uint64_t ts_ms)
{
    h->sum += value;
    h->count++;
    for (uint32_t i = 0; i < h->num_buckets; i++) {
        if (value <= h->upper_bounds[i]) {
            h->counts[i]++;
        }
    }
    h->last_update_ms = ts_ms;
}

/**
 * @brief  Initialise a histogram with the given bucket upper bounds.
 * @param  h              Target histogram.
 * @param  upper_bounds   Array of upper bounds (must be sorted ascending).
 * @param  num_buckets    Number of buckets.
 */
static inline void histogram_init(histogram_t *h, const double *upper_bounds,
                                   uint32_t num_buckets)
{
    if (num_buckets > METRIC_HISTOGRAM_MAX_BUCKETS)
        num_buckets = METRIC_HISTOGRAM_MAX_BUCKETS;
    for (uint32_t i = 0; i < num_buckets; i++) {
        h->upper_bounds[i] = upper_bounds[i];
        h->counts[i] = 0;
    }
    h->num_buckets = num_buckets;
    h->sum = 0.0;
    h->count = 0;
    h->last_update_ms = 0;
}

/**
 * @brief  Estimate a percentile from the histogram by linear interpolation.
 * @param  h   The histogram.
 * @param  p   Percentile in range [0, 1] (e.g. 0.99 for P99).
 * @return Estimated value, or 0 if histogram is empty.
 */
static inline double histogram_percentile(const histogram_t *h, double p)
{
    if (h->count == 0) return 0.0;
    uint64_t target = (uint64_t)(p * (double)h->count);
    for (uint32_t i = 0; i < h->num_buckets; i++) {
        if (h->counts[i] >= target) {
            return h->upper_bounds[i];
        }
    }
    return h->upper_bounds[h->num_buckets - 1];
}

/* ── Sparkline — compact float history for terminal Unicode bars ──────────── */

#define SPARKLINE_CAPACITY  64

/**
 * @brief  A circular buffer of float values for rendering sparklines.
 *         Values are normalised to [0,1] by the renderer.
 */
typedef struct {
    float    values[SPARKLINE_CAPACITY];
    uint64_t timestamps_ms[SPARKLINE_CAPACITY];
    uint32_t head;
    uint32_t count;
    float    min_seen;
    float    max_seen;
} sparkline_t;

static inline void sparkline_push(sparkline_t *s, float v, uint64_t ts_ms)
{
    s->values[s->head] = v;
    s->timestamps_ms[s->head] = ts_ms;
    s->head = (s->head + 1) % SPARKLINE_CAPACITY;
    if (s->count < SPARKLINE_CAPACITY) s->count++;
    if (v < s->min_seen) s->min_seen = v;
    if (v > s->max_seen) s->max_seen = v;
}

static inline void sparkline_init(sparkline_t *s)
{
    for (uint32_t i = 0; i < SPARKLINE_CAPACITY; i++) {
        s->values[i] = 0.0f;
        s->timestamps_ms[i] = 0;
    }
    s->head = 0;
    s->count = 0;
    s->min_seen = 1e18f;
    s->max_seen = -1e18f;
}

/**
 * @brief  Render sparkline as Unicode block characters into buf.
 * @param  s     The sparkline.
 * @param  buf   Output buffer (must hold at least points*4 bytes for UTF-8).
 * @param  points  Number of most-recent points to render.
 * @param  buf_len  Size of buf.
 */
void sparkline_render(const sparkline_t *s, char *buf, uint32_t points, size_t buf_len);

/* ── Utility: millisecond timestamp ──────────────────────────────────────── */

static inline uint64_t metric_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static inline uint64_t metric_wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}
