#define _POSIX_C_SOURCE 200809L

/*
 * sm_rolling_restart.c - Rolling restart implementation
 *
 * Zero-downtime deployment: restart services one at a time
 */

#include "../enterprise/sm_rolling_restart.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>

#define MAX_ROLLING_SERVICES 256

typedef struct {
    sm_service_restart_t services[MAX_ROLLING_SERVICES];
    int service_count;
    
    int max_concurrent;
    int delay_between_restarts;
    
    pthread_t restart_thread;
    int thread_running;
    int is_active;
    
    int current_idx;
    
    pthread_mutex_t lock;
    pthread_cond_t cond;
} rolling_restart_ctx_t;

static rolling_restart_ctx_t g_rolling_restart = {0};
static int g_rolling_restart_initialized = 0;

static int restart_single_service(const char* service_name)
{
    sm_log(SM_LOG_INFO, "rolling_restart: restarting service '%s'", service_name);
    
    /* Stub: In production, call actual service restart
     * This would call the main service manager's restart API
     * For now, we just simulate a restart delay */
    
    sleep(2);  /* Simulate restart time */
    
    sm_log(SM_LOG_INFO, "rolling_restart: service '%s' restarted successfully", service_name);
    return 0;
}

static void* rolling_restart_thread_main(void* arg)
{
    (void)arg;
    
    sm_log(SM_LOG_INFO, "rolling_restart: background thread started");
    
    while (g_rolling_restart.thread_running) {
        pthread_mutex_lock(&g_rolling_restart.lock);
        
        if (!g_rolling_restart.is_active) {
            pthread_mutex_unlock(&g_rolling_restart.lock);
            sleep(1);
            continue;
        }
        
        /* Find next pending service */
        int next_idx = -1;
        for (int i = g_rolling_restart.current_idx; i < g_rolling_restart.service_count; i++) {
            if (g_rolling_restart.services[i].state == RESTART_PENDING) {
                next_idx = i;
                break;
            }
        }
        
        if (next_idx == -1) {
            /* All done */
            g_rolling_restart.is_active = 0;
            sm_log(SM_LOG_INFO, "rolling_restart: all services restarted");
            pthread_mutex_unlock(&g_rolling_restart.lock);
            break;
        }
        
        sm_service_restart_t* svc = &g_rolling_restart.services[next_idx];
        svc->state = RESTART_IN_PROGRESS;
        svc->start_time = time(NULL);
        
        pthread_mutex_unlock(&g_rolling_restart.lock);
        
        /* Perform restart outside lock */
        int ret = restart_single_service(svc->service_name);
        
        pthread_mutex_lock(&g_rolling_restart.lock);
        svc->completion_time = time(NULL);
        
        if (ret == 0) {
            svc->state = RESTART_SUCCESS;
            g_rolling_restart.current_idx = next_idx + 1;
        } else {
            svc->state = RESTART_FAILED;
            svc->retry_count++;
            if (svc->retry_count >= svc->max_retries) {
                g_rolling_restart.current_idx = next_idx + 1;
            }
            snprintf(svc->error_message, sizeof(svc->error_message),
                    "Restart failed (attempt %d/%d)", svc->retry_count, svc->max_retries);
        }
        
        pthread_mutex_unlock(&g_rolling_restart.lock);
        
        /* Wait between restarts */
        if (g_rolling_restart.delay_between_restarts > 0) {
            sleep(g_rolling_restart.delay_between_restarts);
        }
    }
    
    sm_log(SM_LOG_INFO, "rolling_restart: background thread exiting");
    return NULL;
}

int sm_rolling_restart_init(int max_concurrent_restarts)
{
    if (g_rolling_restart_initialized) {
        sm_log(SM_LOG_WARN, "rolling_restart: already initialized");
        return 0;
    }
    
    if (max_concurrent_restarts <= 0 || max_concurrent_restarts > 16) {
        sm_log(SM_LOG_ERROR, "rolling_restart: invalid max_concurrent %d", max_concurrent_restarts);
        return -1;
    }
    
    memset(&g_rolling_restart, 0, sizeof(g_rolling_restart));
    
    if (pthread_mutex_init(&g_rolling_restart.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "rolling_restart: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }
    
    if (pthread_cond_init(&g_rolling_restart.cond, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "rolling_restart: pthread_cond_init failed: %s", strerror(errno));
        pthread_mutex_destroy(&g_rolling_restart.lock);
        return -1;
    }
    
    g_rolling_restart.max_concurrent = max_concurrent_restarts;
    g_rolling_restart.thread_running = 1;
    
    if (pthread_create(&g_rolling_restart.restart_thread, NULL, rolling_restart_thread_main, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "rolling_restart: pthread_create failed: %s", strerror(errno));
        pthread_cond_destroy(&g_rolling_restart.cond);
        pthread_mutex_destroy(&g_rolling_restart.lock);
        return -1;
    }
    
    g_rolling_restart_initialized = 1;
    sm_log(SM_LOG_INFO, "rolling_restart: initialized with max %d concurrent", max_concurrent_restarts);
    return 0;
}

int sm_rolling_restart_start(const char** service_names, int count,
                            int delay_between_restarts_sec)
{
    if (!g_rolling_restart_initialized) {
        sm_log(SM_LOG_ERROR, "rolling_restart: not initialized");
        return -1;
    }
    
    if (!service_names || count <= 0 || count > MAX_ROLLING_SERVICES) {
        sm_log(SM_LOG_ERROR, "rolling_restart: invalid service count %d", count);
        return -1;
    }
    
    if (delay_between_restarts_sec < 0 || delay_between_restarts_sec > 3600) {
        sm_log(SM_LOG_ERROR, "rolling_restart: invalid delay %d", delay_between_restarts_sec);
        return -1;
    }
    
    pthread_mutex_lock(&g_rolling_restart.lock);
    
    if (g_rolling_restart.is_active) {
        pthread_mutex_unlock(&g_rolling_restart.lock);
        sm_log(SM_LOG_WARN, "rolling_restart: already active");
        return -1;
    }
    
    /* Clear previous data */
    memset(g_rolling_restart.services, 0, sizeof(g_rolling_restart.services));
    g_rolling_restart.service_count = count;
    g_rolling_restart.current_idx = 0;
    g_rolling_restart.delay_between_restarts = delay_between_restarts_sec;
    
    /* Initialize service list */
    for (int i = 0; i < count; i++) {
        strncpy(g_rolling_restart.services[i].service_name, service_names[i], 
               sizeof(g_rolling_restart.services[i].service_name) - 1);
        g_rolling_restart.services[i].state = RESTART_PENDING;
        g_rolling_restart.services[i].max_retries = 3;
    }
    
    g_rolling_restart.is_active = 1;
    
    pthread_cond_signal(&g_rolling_restart.cond);
    pthread_mutex_unlock(&g_rolling_restart.lock);
    
    sm_log(SM_LOG_INFO, "rolling_restart: started for %d services (delay=%d sec)",
           count, delay_between_restarts_sec);
    return 0;
}

sm_rolling_restart_progress_t sm_rolling_restart_get_progress(void)
{
    sm_rolling_restart_progress_t progress = {0};
    
    if (!g_rolling_restart_initialized) {
        return progress;
    }
    
    pthread_mutex_lock(&g_rolling_restart.lock);
    
    progress.total_services = g_rolling_restart.service_count;
    progress.is_active = g_rolling_restart.is_active;
    progress.current_service_idx = g_rolling_restart.current_idx;
    
    for (int i = 0; i < g_rolling_restart.service_count; i++) {
        if (g_rolling_restart.services[i].state == RESTART_SUCCESS) {
            progress.success_count++;
        } else if (g_rolling_restart.services[i].state == RESTART_FAILED) {
            progress.failed_count++;
        } else if (g_rolling_restart.services[i].state == RESTART_IN_PROGRESS) {
            progress.in_progress_count++;
        }
    }
    
    progress.completed_count = progress.success_count + progress.failed_count;
    
    if (progress.total_services > 0) {
        progress.completion_percent = (progress.completed_count * 100.0) / progress.total_services;
    }
    
    pthread_mutex_unlock(&g_rolling_restart.lock);
    
    return progress;
}

sm_service_restart_t sm_rolling_restart_get_service_status(const char* service_name)
{
    sm_service_restart_t status = {0};
    
    if (!g_rolling_restart_initialized || !service_name) {
        return status;
    }
    
    pthread_mutex_lock(&g_rolling_restart.lock);
    
    for (int i = 0; i < g_rolling_restart.service_count; i++) {
        if (strcmp(g_rolling_restart.services[i].service_name, service_name) == 0) {
            status = g_rolling_restart.services[i];
            break;
        }
    }
    
    pthread_mutex_unlock(&g_rolling_restart.lock);
    
    return status;
}

int sm_rolling_restart_cancel(void)
{
    if (!g_rolling_restart_initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_rolling_restart.lock);
    
    /* Mark remaining services as skipped */
    for (int i = 0; i < g_rolling_restart.service_count; i++) {
        if (g_rolling_restart.services[i].state == RESTART_PENDING) {
            g_rolling_restart.services[i].state = RESTART_SKIPPED;
        }
    }
    
    g_rolling_restart.is_active = 0;
    
    pthread_mutex_unlock(&g_rolling_restart.lock);
    
    sm_log(SM_LOG_INFO, "rolling_restart: cancelled");
    return 0;
}

int sm_rolling_restart_is_active(void)
{
    if (!g_rolling_restart_initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_rolling_restart.lock);
    int active = g_rolling_restart.is_active;
    pthread_mutex_unlock(&g_rolling_restart.lock);
    
    return active;
}

int sm_rolling_restart_cleanup(void)
{
    if (!g_rolling_restart_initialized) {
        return 0;
    }
    
    /* Cancel any active restart */
    sm_rolling_restart_cancel();
    
    /* Stop thread */
    g_rolling_restart.thread_running = 0;
    
    if (pthread_join(g_rolling_restart.restart_thread, NULL) != 0) {
        sm_log(SM_LOG_WARN, "rolling_restart: pthread_join failed");
    }
    
    pthread_cond_destroy(&g_rolling_restart.cond);
    pthread_mutex_destroy(&g_rolling_restart.lock);
    
    memset(&g_rolling_restart, 0, sizeof(g_rolling_restart));
    g_rolling_restart_initialized = 0;
    
    sm_log(SM_LOG_INFO, "rolling_restart: cleanup complete");
    return 0;
}
