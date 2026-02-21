#ifndef SM_METRICS_H
#define SM_METRICS_H

/*
 * sm_metrics.h - Performance and usage metrics collection
 *
 * Tracks request throughput, latency, errors for monitoring and debugging.
 */

#include <stdint.h>
#include <time.h>

typedef struct {
    uint64_t total_requests;
    uint64_t total_register;
    uint64_t total_lookup;
    uint64_t total_heartbeat;
    uint64_t total_unregister;
    uint64_t total_errors;
    uint64_t total_auth_failures;
    uint64_t total_ratelimit_hits;
    uint64_t peak_qps;
    uint64_t avg_latency_us;  /* microseconds */
} sm_metrics_t;

/* Initialize metrics */
int sm_metrics_init(void);

/* Record a request */
void sm_metrics_request(uint16_t msg_type, int32_t latency_us, int success);

/* Get current metrics snapshot */
sm_metrics_t sm_metrics_get(void);

/* Reset metrics */
void sm_metrics_reset(void);

/* Cleanup */
void sm_metrics_cleanup(void);

#endif /* SM_METRICS_H */
