#ifndef SM_METRICS_H
#define SM_METRICS_H

/*
 * sm_metrics.h - Performance and usage metrics collection
 *
 * Tracks request throughput, latency, errors for monitoring and debugging.
 *
 * Concurrency model (two-level):
 *   Hot path  — fully lock-free via C11 stdatomic.h (_Atomic uint64_t).
 *               atomic_fetch_add() compiles to a single LOCK XADD on x86-64,
 *               eliminating all thread-queue overhead on the critical path.
 *   QPS window — one coarse mutex grabbed at most once per second to flush
 *               the rolling 60-second history buffer.  Zero impact at scale.
 */

#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

/* Public snapshot — plain (non-atomic) struct returned by sm_metrics_get().
 * Values are point-in-time reads; monitoring-quality consistency is sufficient. */
typedef struct {
    uint64_t total_requests;
    uint64_t total_register;
    uint64_t total_lookup;
    uint64_t total_heartbeat;
    uint64_t total_unregister;
    uint64_t total_errors;
    uint64_t total_auth_failures;
    uint64_t total_ratelimit_hits;
    uint64_t peak_qps;          /* highest single-second QPS ever seen     */
    uint64_t rolling_avg_qps;   /* average over last 60 seconds            */
    uint64_t avg_latency_us;    /* mean request latency in microseconds    */
} sm_metrics_t;

/* Initialise — must be called once before any other function. */
int  sm_metrics_init(void);

/* Record one completed request (hot path — lock-free). */
void sm_metrics_request(uint16_t msg_type, int32_t latency_us, int success);

/* Record an HMAC / auth failure (lock-free). */
void sm_metrics_auth_failure(void);

/* Record a rate-limit hit (lock-free). */
void sm_metrics_ratelimit_hit(void);

/* Return a consistent snapshot for display / export (lock-free reads). */
sm_metrics_t sm_metrics_get(void);

/* Zero all counters (acquires qps_mutex; use only for testing / reset). */
void sm_metrics_reset(void);

/* Tear down — destroy internal mutex. */
void sm_metrics_cleanup(void);

#endif /* SM_METRICS_H */
