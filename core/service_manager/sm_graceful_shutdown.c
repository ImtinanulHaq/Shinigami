#define _POSIX_C_SOURCE 200809L

/*
 * sm_graceful_shutdown.c - Graceful shutdown
 */

#include "sm_graceful_shutdown.h"
#include "sm_logging.h"
#include "sm_registry.h"
#include "sm_persistence.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SHUTDOWN_HOOKS 10

static void (*g_shutdown_hooks[MAX_SHUTDOWN_HOOKS])(void);
static int g_hook_count = 0;

void sm_register_shutdown_hook(void (*hook)(void))
{
    if (hook && g_hook_count < MAX_SHUTDOWN_HOOKS) {
        g_shutdown_hooks[g_hook_count++] = hook;
    }
}

void sm_graceful_shutdown(int timeout_sec)
{
    service_entry_t* services = NULL;
    int count = 0;
    
    sm_log(SM_LOG_INFO, "shutdown: graceful shutdown initiated (timeout=%ds)", timeout_sec);
    
    /* Call all registered shutdown hooks */
    for (int i = 0; i < g_hook_count; i++) {
        if (g_shutdown_hooks[i]) {
            g_shutdown_hooks[i]();
        }
    }
    
    /* Get all services */
    if (sm_registry_get_all(&services, &count) < 0) {
        sm_log(SM_LOG_WARN, "shutdown: failed to get services list");
        return;
    }
    
    /* Send SIGTERM to all services in reverse registration order */
    for (int i = count - 1; i >= 0; i--) {
        if (services[i].status != SERVICE_STOPPED && services[i].pid > 1) {
            sm_log(SM_LOG_INFO, "shutdown: sending SIGTERM to '%s' (pid=%d)",
                   services[i].name, (int)services[i].pid);
            kill(services[i].pid, SIGTERM);
        }
    }
    
    sm_registry_free_copy(services);
    
    /* Wait for graceful shutdown */
    for (int i = 0; i < timeout_sec; i++) {
        sleep(1);
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
