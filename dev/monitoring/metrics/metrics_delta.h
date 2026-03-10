/**
 * @file    metrics_delta.h
 * @brief   Delta / rate-of-change computation for counters and gauges.
 *
 * Delta mode in the TUI sends the same snapshot but the rendering of each
 * metric switches from absolute value to per-second rate-of-change.
 *
 * All functions are pure / stateless helpers operating on two consecutive
 * snapshots.  They do not modify state.
 *
 * @thread_safety  Stateless — safe to call from any context.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief  Compute per-second rate given two counter values and elapsed time.
 * @param  curr       Current counter value.
 * @param  prev       Previous counter value.
 * @param  elapsed_ms Milliseconds between the two samples.
 * @return Samples per second (>= 0.0).  Returns 0 if elapsed_ms == 0 or
 *         if the counter wrapped (curr < prev) indicating a reset.
 */
static inline double delta_rate(uint64_t curr, uint64_t prev, uint64_t elapsed_ms)
{
    if (elapsed_ms == 0) return 0.0;
    if (curr < prev) return 0.0;   /* Reset detected — ignore. */
    double delta = (double)(curr - prev);
    return delta / ((double)elapsed_ms / 1000.0);
}

/**
 * @brief  Compute per-second rate for a floating-point gauge.
 * @param  curr       Current gauge value.
 * @param  prev       Previous gauge value.
 * @param  elapsed_ms Milliseconds between samples.
 * @return Rate of change per second (may be negative for decreasing gauges).
 */
static inline double delta_rate_f(double curr, double prev, uint64_t elapsed_ms)
{
    if (elapsed_ms == 0) return 0.0;
    return (curr - prev) / ((double)elapsed_ms / 1000.0);
}

/**
 * @brief  Format a delta value for display (e.g. "+2.3 KB/s", "-1/s").
 * @param  buf      Output buffer.
 * @param  buf_len  Size of buf.
 * @param  rate     Rate of change per second.
 * @param  unit     Unit suffix (e.g. "KB/s", "/s", "ms/s").
 */
void delta_format(char *buf, size_t buf_len, double rate, const char *unit);

/**
 * @brief  Determine if a counter is monotonically increasing by checking the
 *         last N measured rates are all > 0.
 * @param  rates     Array of N rate samples (per second).
 * @param  n         Number of samples.
 * @param  threshold Minimum rate to be considered "increasing".
 * @return 1 if all rates >= threshold, 0 otherwise.
 */
static inline int delta_is_monotonic(const double *rates, uint32_t n, double threshold)
{
    if (!rates || n == 0) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (rates[i] < threshold) return 0;
    }
    return 1;
}

/**
 * @brief  Detect a FD leak: FD count growing by >= 1/s for at least
 *         @p window_s consecutive seconds.
 * @param  fd_history  Array of recent FD counts (oldest first, 1/s).
 * @param  n           Number of samples.
 * @param  window_s    Minimum consecutive seconds of growth required.
 * @return 1 if leak detected, 0 otherwise.
 */
static inline int delta_detect_fd_leak(const uint32_t *fd_history, uint32_t n,
                                        uint32_t window_s)
{
    if (!fd_history || n < 2 || window_s < 2) return 0;
    uint32_t streak = 0;
    for (uint32_t i = 1; i < n; i++) {
        if (fd_history[i] > fd_history[i-1]) {
            streak++;
            if (streak >= window_s) return 1;
        } else {
            streak = 0;
        }
    }
    return 0;
}

/**
 * @brief  Detect a VSZ memory leak: VSZ growing by >= 1MB per minute for
 *         at least @p window_mins consecutive minutes.
 * @param  vsz_history  Array of VSZ bytes (oldest first, sampled at ~60s).
 * @param  n            Number of samples.
 * @param  window_mins  Minimum consecutive minutes of growth required.
 * @param  min_rate_bps Minimum bytes/second to be considered a leak (e.g. 16384=1MB/min).
 * @return 1 if leak detected, 0 otherwise.
 */
static inline int delta_detect_vsz_leak(const uint64_t *vsz_history, uint32_t n,
                                          uint32_t window_mins, uint64_t min_rate_bps)
{
    if (!vsz_history || n < 2) return 0;
    uint32_t streak = 0;
    for (uint32_t i = 1; i < n; i++) {
        if (vsz_history[i] > vsz_history[i-1]) {
            uint64_t diff_per_s = (vsz_history[i] - vsz_history[i-1]) / 60u;
            if (diff_per_s >= min_rate_bps) {
                streak++;
                if (streak >= window_mins) return 1;
            } else {
                streak = 0;
            }
        } else {
            streak = 0;
        }
    }
    return 0;
}
