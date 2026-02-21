#define _POSIX_C_SOURCE 200809L

/*
 * sm_advanced_ratelimit.c - Advanced rate limiting
 */

#include "sm_advanced_ratelimit.h"
#include "sm_rate_limit.h"
#include "sm_logging.h"
#include <string.h>
#include <pthread.h>

typedef struct {
    char service_name[64];
    pid_t pid;
    double tokens;
    double last_refill;
} service_bucket_t;

#define MAX_SERVICE_BUCKETS 256

static struct {
    service_bucket_t buckets[MAX_SERVICE_BUCKETS];
    int count;
    pthread_mutex_t mutex;
} g_service_limits;

int sm_ratelimit_check_extended(pid_t pid, const char* service_name, uint16_t operation)
{
    (void)operation;  /* parameter not used in current implementation */
    
    /* First check global and per-PID limits (existing system) */
    if (sm_rate_limit_check(pid) != 0) {
        return -1;  /* global limit hit */
    }
    
    /* Then check per-service limits */
    pthread_mutex_lock(&g_service_limits.mutex);
    
    for (int i = 0; i < g_service_limits.count; i++) {
        if (!strcmp(g_service_limits.buckets[i].service_name, service_name) &&
            g_service_limits.buckets[i].pid == pid) {
            if (g_service_limits.buckets[i].tokens < 1.0) {
                pthread_mutex_unlock(&g_service_limits.mutex);
                return -1;  /* service limit hit */
            }
            g_service_limits.buckets[i].tokens -= 1.0;
            pthread_mutex_unlock(&g_service_limits.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_service_limits.mutex);
    return 0;  /* not tracked, allow */
}

void sm_ratelimit_reset_service(const char* service_name)
{
    if (!service_name) return;
    
    pthread_mutex_lock(&g_service_limits.mutex);
    
    for (int i = 0; i < g_service_limits.count; i++) {
        if (!strcmp(g_service_limits.buckets[i].service_name, service_name)) {
            g_service_limits.buckets[i].tokens = 10.0;  /* reset to capacity */
        }
    }
    
    pthread_mutex_unlock(&g_service_limits.mutex);
    
    sm_log(SM_LOG_DEBUG, "ratelimit: reset limits for '%s'", service_name);
}

int sm_ratelimit_get_tokens(pid_t pid, const char* service_name)
{
    int tokens = 0;
    
    pthread_mutex_lock(&g_service_limits.mutex);
    
    for (int i = 0; i < g_service_limits.count; i++) {
        if (!strcmp(g_service_limits.buckets[i].service_name, service_name) &&
            g_service_limits.buckets[i].pid == pid) {
            tokens = (int)g_service_limits.buckets[i].tokens;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_service_limits.mutex);
    return tokens;
}
