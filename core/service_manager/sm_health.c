#define _POSIX_C_SOURCE 200809L

/*
 * sm_health.c - Periodic health monitor: heartbeat timeouts and restart logic.
 *
 * Fixes applied:
 *   - Services that have exceeded SM_HEALTH_MAX_RESTARTS are transitioned to
 *     SERVICE_DEAD.  The health check no longer logs for DEAD services, so
 *     there is no infinite log spam after giving up.
 *   - sm_registry_free_copy() is called after use of the snapshot copy.
 */

#include "sm_health.h"
#include "sm_logging.h"

#include <time.h>
#include <signal.h>
#include <unistd.h>

void sm_health_check(void)
{
    service_entry_t* services = NULL;
    int              count    = 0;
    int              i;
    time_t           now      = time(NULL);

    if (sm_registry_get_all(&services, &count) < 0 || count == 0) {
        sm_registry_free_copy(services);
        return;
    }

    for (i = 0; i < count; i++) {
        /* Work from a local copy of the name; update status via registry API */
        const char* name = services[i].name;

        /* Skip services that have already been given up on */
        if (services[i].status == SERVICE_DEAD) continue;

        /* Detect heartbeat timeout for running services */
        if (services[i].status == SERVICE_RUNNING) {
            time_t age = now - services[i].last_heartbeat;
            if (age > SM_HEARTBEAT_TIMEOUT) {
                sm_log(SM_LOG_WARN,
                       "health: '%s' heartbeat timeout (%lds) pid=%d - marking crashed",
                       name, (long)age, (int)services[i].pid);
                sm_registry_update_status(name, SERVICE_CRASHED);
                /* Re-read status for the restart logic below */
                services[i].status = SERVICE_CRASHED;
            }
        }

        /* Handle crashed services with exponential backoff restart */
        if (services[i].status == SERVICE_CRASHED) {
            int    restart_count = services[i].restart_count;
            time_t since_crash   = now - services[i].last_crash_time;

            /* Exponential backoff: 2, 4, 8 ... seconds, capped at 120 */
            int backoff = SM_HEALTH_RESTART_DELAY;
            if (restart_count > 1) {
                int shift = restart_count - 1;
                if (shift > 6) shift = 6;   /* cap shift to avoid overflow */
                backoff = SM_HEALTH_RESTART_DELAY * (1 << shift);
                if (backoff > 120) backoff = 120;
            }

            if (restart_count >= SM_HEALTH_MAX_RESTARTS) {
                /*
                 * Transition to DEAD so the health check does not log this
                 * message on every future iteration.
                 */
                sm_log(SM_LOG_ERROR,
                       "health: '%s' exceeded max restarts (%d) - marking dead",
                       name, SM_HEALTH_MAX_RESTARTS);
                sm_registry_update_status(name, SERVICE_DEAD);

            } else if (since_crash >= (time_t)backoff) {
                sm_log(SM_LOG_INFO,
                       "health: restarting '%s' attempt %d/%d (crashed %lds ago, backoff %ds)",
                       name, restart_count + 1, SM_HEALTH_MAX_RESTARTS,
                       (long)since_crash, backoff);

                /* Send SIGTERM first for graceful shutdown, then SIGKILL if needed */
                if (services[i].pid > 1) {
                    sm_log(SM_LOG_INFO, "health: sending SIGTERM to '%s' (pid=%d)",
                           name, (int)services[i].pid);
                    kill(services[i].pid, SIGTERM);
                    
                    /* Wait briefly for graceful termination (100ms) using POSIX nanosleep */
                    struct timespec ts = {0, 100000000};  /* 100ms in nanoseconds */
                    nanosleep(&ts, NULL);
                    
                    /* Check if process still exists, kill if necessary */
                    if (kill(services[i].pid, 0) == 0) {
                        sm_log(SM_LOG_WARN, "health: sending SIGKILL to '%s' (pid=%d)",
                               name, (int)services[i].pid);
                        kill(services[i].pid, SIGKILL);
                    }
                }

                /*
                 * In a full production system this is where a systemd
                 * restart command or a process supervisor notification
                 * would be sent.  The service will re-register itself
                 * when it comes back up.
                 */
            }
        }
    }

    sm_registry_free_copy(services);
}