#include "sm_health.h"
#include "sm_logging.h"
#include <time.h>
#include <signal.h>
#include <unistd.h>

// ── HEALTH CHECK ───────────────────────────────────────────────────────────────

void sm_health_check(void)
{
    time_t now = time(NULL);
    service_entry_t* services;
    int count;

    if (sm_registry_get_all(&services, &count) < 0)
        return;

    for (int i = 0; i < count; i++) {
        service_entry_t* s = &services[i];

        // Check heartbeat timeout
        if (s->status == SERVICE_RUNNING &&
            (now - s->last_heartbeat) > SM_HEARTBEAT_TIMEOUT) {
            sm_log(SM_LOG_WARN, "service '%s' heartbeat timeout (pid=%d)",
                   s->name, s->pid);
            sm_registry_update_status(s->name, SERVICE_CRASHED);
        }

        // Handle crashed services with exponential backoff
        if (s->status == SERVICE_CRASHED) {
            // Calculate backoff delay
            int backoff_delay = SM_HEALTH_RESTART_DELAY;
            if (s->restart_count > 1) {
                backoff_delay = SM_HEALTH_RESTART_DELAY * (1 << (s->restart_count - 1));
                if (backoff_delay > 120) backoff_delay = 120;  // cap at 2 minutes
            }

            time_t time_since_crash = now - s->last_crash_time;

            if (s->restart_count >= SM_HEALTH_MAX_RESTARTS) {
                sm_log(SM_LOG_ERROR,
                       "service '%s' exceeded max restarts (%d), giving up",
                       s->name, SM_HEALTH_MAX_RESTARTS);
            } else if (time_since_crash >= backoff_delay) {
                sm_log(SM_LOG_INFO,
                       "restarting service '%s' (restart #%d, was crashed %lds ago)",
                       s->name, s->restart_count + 1, time_since_crash);

                // Try to kill old process
                if (s->pid > 0) {
                    kill(s->pid, SIGKILL);
                }

                // Mark as waiting for re-registration
                // In production, this would trigger a systemd restart or similar
            }
        }
    }
}
