#define _POSIX_C_SOURCE 200809L

/*
 * sm_metrics.c - Lock-free performance metrics collection
 *
 * Concurrency design (two-level):
 *
 *   Level 1 - Lock-free hot path (called on every request):
 *     All counters are _Atomic uint64_t.  atomic_fetch_add() compiles to a
 *     single hardware instruction (LOCK XADD on x86-64) - no OS scheduler
 *     involvement, no thread queue, no context switch overhead.
 *     At 1 M req/s the difference vs. a mutex is ~300 ns per request
 *     (mutex acquire ~30-100 cycles; atomic add ~5 cycles).
 *
 *   Level 2 - Coarse QPS window mutex (called at most once per second):
 *     The 60-second rolling history buffer is a non-atomic array because
 *     updating it requires multiple dependent writes.  A dedicated qps_mutex
 *     guards only this flush; it is grabbed << 1 % of the time even at
 *     1 M req/s, so it never becomes a bottleneck.
 *
 *   sm_metrics_get() - zero locks:
 *     Reads all counters with atomic_load(RELAXED).  The snapshot may be
 *     off by at most one in-flight request - acceptable for monitoring.
 */

#include "../observability/sm_metrics.h"
#include "../observability/sm_logging.h"
#include "../infrastructure/sm_protocol.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/* Rolling average window: track QPS for the last 60 seconds */
#define QPS_HISTORY_SIZE 60

/* -- Internal state ------------------------------------------------------- */

static struct {
    /* --- Hot-path counters (lock-free) ---------------------------------- */
    _Atomic uint64_t total_requests;          /* incremented on every request  */
    _Atomic uint64_t by_type[5];              /* [0]=reg [1]=lkp [2]=hb [3]=un [4]=unk */
    _Atomic uint64_t total_errors;
    _Atomic uint64_t total_auth_failures;
    _Atomic uint64_t total_ratelimit_hits;
    _Atomic uint64_t total_latency_us;        /* sum of all latencies           */

    /* Per-second bucket: hot-path increments atomically; window thread
     * swaps it out under qps_mutex once per wall-clock second.            */
    _Atomic uint64_t request_count_current_sec;

    /* --- QPS window (protected by qps_mutex, updated <=1 Hz) ----------- */
    _Atomic uint64_t peak_qps;
    _Atomic uint64_t rolling_avg_qps;
    _Atomic time_t   last_qps_reset;          /* atomic so hot-path can read   */

    uint64_t         qps_history[QPS_HISTORY_SIZE];
    int              history_idx;
    pthread_mutex_t  qps_mutex;               /* guards history[] and idx only  */
} g_metrics;

/* -- Init / cleanup ------------------------------------------------------- */

int sm_metrics_init(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    atomic_init(&g_metrics.total_requests,           0);
    atomic_init(&g_metrics.total_errors,              0);
    atomic_init(&g_metrics.total_auth_failures,       0);
    atomic_init(&g_metrics.total_ratelimit_hits,      0);
    atomic_init(&g_metrics.total_latency_us,          0);
    atomic_init(&g_metrics.request_count_current_sec, 0);
    atomic_init(&g_metrics.peak_qps,                  0);
    atomic_init(&g_metrics.rolling_avg_qps,           0);
    atomic_init(&g_metrics.last_qps_reset, (time_t)time(NULL));
    for (int i = 0; i < 5; i++) atomic_init(&g_metrics.by_type[i], 0);

    pthread_mutex_init(&g_metrics.qps_mutex, NULL);
    sm_log(SM_LOG_INFO, "metrics: initialised (lock-free counters, qps mutex)");
    return 0;
}

void sm_metrics_cleanup(void)
{
    pthread_mutex_destroy(&g_metrics.qps_mutex);
}

/* -- Hot path (lock-free) ------------------------------------------------- */

void sm_metrics_request(uint16_t msg_type, int32_t latency_us, int success)
{
    /* --- 1. Classify message type --------------------------------------- */
    int idx = 4;  /* unknown */
    switch (msg_type) {
        case SM_MSG_REGISTER:   idx = 0; break;
        case SM_MSG_LOOKUP:     idx = 1; break;
        case SM_MSG_HEARTBEAT:  idx = 2; break;
        case SM_MSG_UNREGISTER: idx = 3; break;
    }

    uint64_t latency_u = (uint64_t)(latency_us < 0 ? 0 : latency_us);

    /* --- 2. Lock-free counter updates (single hardware instruction each) */
    atomic_fetch_add_explicit(&g_metrics.total_requests,            1,         memory_order_relaxed);
    atomic_fetch_add_explicit(&g_metrics.by_type[idx],              1,         memory_order_relaxed);
    atomic_fetch_add_explicit(&g_metrics.total_latency_us,          latency_u, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_metrics.request_count_current_sec, 1,         memory_order_relaxed);
    if (!success)
        atomic_fetch_add_explicit(&g_metrics.total_errors, 1, memory_order_relaxed);

    /* --- 3. QPS window flush (at most once per second) ------------------ */
    time_t now  = time(NULL);
    time_t last = atomic_load_explicit(&g_metrics.last_qps_reset, memory_order_relaxed);
    if (now != last) {
        /* Use trylock: only one thread flushes; all others skip cheaply.  */
        if (pthread_mutex_trylock(&g_metrics.qps_mutex) == 0) {
            /* Re-check under lock to prevent double-flush. */
            last = atomic_load_explicit(&g_metrics.last_qps_reset, memory_order_relaxed);
            if (now != last) {
                /* Atomically swap the bucket to zero so concurrent
                 * increments arriving at the boundary are not lost.       */
                uint64_t bucket = atomic_exchange_explicit(
                    &g_metrics.request_count_current_sec, 0,
                    memory_order_acq_rel);

                /* Update peak. */
                uint64_t old_peak = atomic_load_explicit(&g_metrics.peak_qps,
                                                         memory_order_relaxed);
                if (bucket > old_peak)
                    atomic_store_explicit(&g_metrics.peak_qps, bucket,
                                         memory_order_relaxed);

                /* Append to rolling history and recompute 60-s average.  */
                g_metrics.qps_history[g_metrics.history_idx] = bucket;
                g_metrics.history_idx = (g_metrics.history_idx + 1) % QPS_HISTORY_SIZE;

                uint64_t sum = 0;
                for (int i = 0; i < QPS_HISTORY_SIZE; i++)
                    sum += g_metrics.qps_history[i];
                atomic_store_explicit(&g_metrics.rolling_avg_qps,
                                      sum / QPS_HISTORY_SIZE,
                                      memory_order_relaxed);

                atomic_store_explicit(&g_metrics.last_qps_reset, now,
                                      memory_order_relaxed);
            }
            pthread_mutex_unlock(&g_metrics.qps_mutex);
        }
        /* If trylock failed: another thread is already flushing - skip.  */
    }
}

void sm_metrics_auth_failure(void)
{
    atomic_fetch_add_explicit(&g_metrics.total_auth_failures, 1,
                              memory_order_relaxed);
}

void sm_metrics_ratelimit_hit(void)
{
    atomic_fetch_add_explicit(&g_metrics.total_ratelimit_hits, 1,
                              memory_order_relaxed);
}

/* -- Read path (lock-free snapshot) --------------------------------------- */

sm_metrics_t sm_metrics_get(void)
{
    sm_metrics_t m;
    memset(&m, 0, sizeof(m));

    /* RELAXED loads: monitoring-quality snapshot, no lock needed. */
    m.total_requests       = atomic_load_explicit(&g_metrics.total_requests,       memory_order_relaxed);
    m.total_register       = atomic_load_explicit(&g_metrics.by_type[0],           memory_order_relaxed);
    m.total_lookup         = atomic_load_explicit(&g_metrics.by_type[1],           memory_order_relaxed);
    m.total_heartbeat      = atomic_load_explicit(&g_metrics.by_type[2],           memory_order_relaxed);
    m.total_unregister     = atomic_load_explicit(&g_metrics.by_type[3],           memory_order_relaxed);
    m.total_errors         = atomic_load_explicit(&g_metrics.total_errors,         memory_order_relaxed);
    m.total_auth_failures  = atomic_load_explicit(&g_metrics.total_auth_failures,  memory_order_relaxed);
    m.total_ratelimit_hits = atomic_load_explicit(&g_metrics.total_ratelimit_hits, memory_order_relaxed);
    m.peak_qps             = atomic_load_explicit(&g_metrics.peak_qps,             memory_order_relaxed);
    m.rolling_avg_qps      = atomic_load_explicit(&g_metrics.rolling_avg_qps,      memory_order_relaxed);

    uint64_t total_lat = atomic_load_explicit(&g_metrics.total_latency_us,
                                              memory_order_relaxed);
    if (m.total_requests > 0)
        m.avg_latency_us = total_lat / m.total_requests;

    return m;
}

/* -- Reset (testing / admin only - not on hot path) ----------------------- */

void sm_metrics_reset(void)
{
    pthread_mutex_lock(&g_metrics.qps_mutex);

    atomic_store(&g_metrics.total_requests,            0);
    atomic_store(&g_metrics.total_errors,               0);
    atomic_store(&g_metrics.total_auth_failures,        0);
    atomic_store(&g_metrics.total_ratelimit_hits,       0);
    atomic_store(&g_metrics.total_latency_us,           0);
    atomic_store(&g_metrics.request_count_current_sec,  0);
    atomic_store(&g_metrics.peak_qps,                   0);
    atomic_store(&g_metrics.rolling_avg_qps,            0);
    atomic_store(&g_metrics.last_qps_reset, (time_t)time(NULL));
    for (int i = 0; i < 5; i++) atomic_store(&g_metrics.by_type[i], 0);

    memset(g_metrics.qps_history, 0, sizeof(g_metrics.qps_history));
    g_metrics.history_idx = 0;

    pthread_mutex_unlock(&g_metrics.qps_mutex);
}
