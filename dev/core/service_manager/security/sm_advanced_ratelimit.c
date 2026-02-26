#define _POSIX_C_SOURCE 200809L

/*
 * sm_advanced_ratelimit.c - Advanced rate limiting
 */

#include "../security/sm_advanced_ratelimit.h"
#include "../security/sm_rate_limit.h"
#include "../observability/sm_logging.h"
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

int sm_ratelimit_init(void)
{
    memset(&g_service_limits, 0, sizeof(g_service_limits));
    pthread_mutex_init(&g_service_limits.mutex, NULL);
    sm_log(SM_LOG_INFO, "ratelimit: advanced rate limiting initialized");
    return 0;
}

void sm_ratelimit_set_service_limit(const char* service_name, pid_t pid, double capacity)
{
    if (!service_name) return;
    
    pthread_mutex_lock(&g_service_limits.mutex);
    
    /* Check if service already exists */
    for (int i = 0; i < g_service_limits.count; i++) {
        if (!strcmp(g_service_limits.buckets[i].service_name, service_name) &&
            g_service_limits.buckets[i].pid == pid) {
            g_service_limits.buckets[i].tokens = capacity;
            pthread_mutex_unlock(&g_service_limits.mutex);
            return;
        }
    }
    
    /* Add new service bucket if space available */
    if (g_service_limits.count < MAX_SERVICE_BUCKETS) {
        strncpy(g_service_limits.buckets[g_service_limits.count].service_name, 
                service_name, sizeof(g_service_limits.buckets[g_service_limits.count].service_name) - 1);
        g_service_limits.buckets[g_service_limits.count].pid = pid;
        g_service_limits.buckets[g_service_limits.count].tokens = capacity;
        g_service_limits.buckets[g_service_limits.count].last_refill = 0;
        g_service_limits.count++;
    }
    
    pthread_mutex_unlock(&g_service_limits.mutex);
}

int sm_ratelimit_check_extended(pid_t pid, const char* service_name, uint16_t operation)
{
    (void)operation;  /* parameter not used in current implementation */
    
    /* First check global and per-PID limits (existing system) */
    if (sm_rate_limit_check(pid) != 0) {
        return -1;  /* global limit hit */
    }
    
    if (!service_name) {
        return 0;  /* no service limits, allow */
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
