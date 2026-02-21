#define _POSIX_C_SOURCE 200809L

/*
 * sm_metrics.c - Metrics collection
 */

#include "sm_metrics.h"
#include "sm_logging.h"
#include "sm_protocol.h"
#include <pthread.h>
#include <string.h>

static struct {
    uint64_t total_requests;
    uint64_t by_type[5];  /* register, lookup, heartbeat, unregister, unknown */
    uint64_t total_errors;
    uint64_t total_auth_failures;
    uint64_t total_ratelimit_hits;
    uint64_t total_latency_us;
    uint64_t peak_qps;
    pthread_mutex_t mutex;
} g_metrics;

int sm_metrics_init(void)
{
    memset(&g_metrics, 0, sizeof(g_metrics));
    pthread_mutex_init(&g_metrics.mutex, NULL);
    sm_log(SM_LOG_INFO, "metrics: initialized");
    return 0;
}

void sm_metrics_request(uint16_t msg_type, int32_t latency_us, int success)
{
    int idx = 4;  /* unknown */
    
    switch (msg_type) {
        case SM_MSG_REGISTER:   idx = 0; break;
        case SM_MSG_LOOKUP:     idx = 1; break;
        case SM_MSG_HEARTBEAT:  idx = 2; break;
        case SM_MSG_UNREGISTER: idx = 3; break;
    }
    
    pthread_mutex_lock(&g_metrics.mutex);
    g_metrics.total_requests++;
    g_metrics.by_type[idx]++;
    g_metrics.total_latency_us += latency_us;
    if (!success) g_metrics.total_errors++;
    pthread_mutex_unlock(&g_metrics.mutex);
}

void sm_metrics_auth_failure(void)
{
    pthread_mutex_lock(&g_metrics.mutex);
    g_metrics.total_auth_failures++;
    pthread_mutex_unlock(&g_metrics.mutex);
}

void sm_metrics_ratelimit_hit(void)
{
    pthread_mutex_lock(&g_metrics.mutex);
    g_metrics.total_ratelimit_hits++;
    pthread_mutex_unlock(&g_metrics.mutex);
}

sm_metrics_t sm_metrics_get(void)
{
    sm_metrics_t m;
    
    pthread_mutex_lock(&g_metrics.mutex);
    m.total_requests = g_metrics.total_requests;
    m.total_register = g_metrics.by_type[0];
    m.total_lookup = g_metrics.by_type[1];
    m.total_heartbeat = g_metrics.by_type[2];
    m.total_unregister = g_metrics.by_type[3];
    m.total_errors = g_metrics.total_errors;
    m.total_auth_failures = g_metrics.total_auth_failures;
    m.total_ratelimit_hits = g_metrics.total_ratelimit_hits;
    
    if (g_metrics.total_requests > 0) {
        m.avg_latency_us = g_metrics.total_latency_us / g_metrics.total_requests;
    }
    pthread_mutex_unlock(&g_metrics.mutex);
    
    return m;
}

void sm_metrics_reset(void)
{
    pthread_mutex_lock(&g_metrics.mutex);
    memset(&g_metrics, 0, sizeof(g_metrics));
    pthread_mutex_unlock(&g_metrics.mutex);
}

void sm_metrics_cleanup(void)
{
    pthread_mutex_destroy(&g_metrics.mutex);
}
