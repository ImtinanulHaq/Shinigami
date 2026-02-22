#define _POSIX_C_SOURCE 200809L

/*
 * sm_graceful_shutdown.c - Graceful shutdown
 */

#include "../lifecycle/sm_graceful_shutdown.h"
#include "../observability/sm_logging.h"
#include "../infrastructure/sm_registry.h"
#include "../lifecycle/sm_persistence.h"
#include "../lifecycle/sm_dependencies.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_SHUTDOWN_HOOKS 10

static void (*g_shutdown_hooks[MAX_SHUTDOWN_HOOKS])(void);
static int g_hook_count = 0;
static pthread_mutex_t hooks_mutex = PTHREAD_MUTEX_INITIALIZER;

void sm_register_shutdown_hook(void (*hook)(void))
{
    if (!hook) return;
    
    pthread_mutex_lock(&hooks_mutex);
    if (g_hook_count < MAX_SHUTDOWN_HOOKS) {
        g_shutdown_hooks[g_hook_count++] = hook;
    }
    pthread_mutex_unlock(&hooks_mutex);
}

void sm_graceful_shutdown(int timeout_sec)
{
    service_entry_t* services = NULL;
    int count = 0;
    
    sm_log(SM_LOG_INFO, "shutdown: graceful shutdown initiated (timeout=%ds)", timeout_sec);
    
    /* Call all registered shutdown hooks */
    pthread_mutex_lock(&hooks_mutex);
    for (int i = 0; i < g_hook_count; i++) {
        if (g_shutdown_hooks[i]) {
            g_shutdown_hooks[i]();
        }
    }
    pthread_mutex_unlock(&hooks_mutex);
    
    /* Get all services */
    if (sm_registry_get_all(&services, &count) < 0) {
        sm_log(SM_LOG_WARN, "shutdown: failed to get services list");
        return;
    }
    
    /* Get startup order from dependency graph (for reverse shutdown order) */
    char startup_order[32][64] = {0};
    int ordered_count = count;
    
    if (sm_deps_get_startup_order((char (*)[64])startup_order, &ordered_count) == 0 && ordered_count > 0) {
        sm_log(SM_LOG_INFO, "shutdown: using dependency-aware shutdown order (%d services)", ordered_count);
        
        /* Send SIGTERM to services in REVERSE dependency order
         * (reverse of startup order means shutdown dependencies-last) */
        for (int i = ordered_count - 1; i >= 0; i--) {
            for (int j = 0; j < count; j++) {
                if (strcmp(services[j].name, startup_order[i]) == 0) {
                    if (services[j].status != SERVICE_STOPPED && services[j].pid > 1) {
                        sm_log(SM_LOG_INFO, "shutdown: sending SIGTERM to '%s' (pid=%d, depends on %d services)",
                               services[j].name, (int)services[j].pid, ordered_count - i - 1);
                        kill(services[j].pid, SIGTERM);
                    }
                    break;
                }
            }
        }
    } else {
        /* Fallback: reverse registration order if dependency info not available */
        sm_log(SM_LOG_WARN, "shutdown: dependency order unavailable, using reverse registration order");
        for (int i = count - 1; i >= 0; i--) {
            if (services[i].status != SERVICE_STOPPED && services[i].pid > 1) {
                sm_log(SM_LOG_INFO, "shutdown: sending SIGTERM to '%s' (pid=%d)",
                       services[i].name, (int)services[i].pid);
                kill(services[i].pid, SIGTERM);
            }
        }
    }
    
    sm_registry_free_copy(services);
    
    /* Wait for graceful shutdown with polling (no blocking sleep) */
    for (int i = 0; i < timeout_sec * 10; i++) {
        /* Use POSIX-compliant nanosleep instead of usleep */
        struct timespec ts = {0, 100000000};  /* 100ms in nanoseconds */
        nanosleep(&ts, NULL);
        
        int running = 0;
        if (sm_registry_get_all(&services, &count) == 0) {
            for (int j = 0; j < count; j++) {
                if (services[j].status != SERVICE_STOPPED) {
                    running++;
                }
            }
            sm_registry_free_copy(services);
        }
        if (running == 0) break;
    }
    
    /* Kill any remaining services */
    if (sm_registry_get_all(&services, &count) == 0) {
        for (int i = 0; i < count; i++) {
            if (services[i].status != SERVICE_STOPPED && services[i].pid > 1) {
                sm_log(SM_LOG_WARN, "shutdown: killing '%s' (pid=%d)",
                       services[i].name, (int)services[i].pid);
                kill(services[i].pid, SIGKILL);
            }
        }
        sm_registry_free_copy(services);
    }
    
    /* Save registry state */
    sm_persistence_save(NULL);
    
    sm_log(SM_LOG_INFO, "shutdown: graceful shutdown complete");
}

void sm_force_shutdown(void)
{
    sm_log(SM_LOG_CRIT, "shutdown: force shutdown");
    sm_graceful_shutdown(1);  /* minimal timeout */
}
