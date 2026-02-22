/*
 * sm_watchdog.h - Watchdog timer for system self-healing
 *
 * Interfaces with Linux /dev/watchdog to provide system-level automatic recovery.
 * If the service manager hangs or crashes, the watchdog ensures system restart.
 * 
 * USAGE:
 *   sm_watchdog_init();  // Start watchdog thread
 *   // Watchdog thread keeps /dev/watchdog alive
 *   sm_watchdog_cleanup();  // Shutdown (or system restarts if not called)
 * 
 * ERROR CODES:
 *   0 = success
 *   -1 = failure (specific error logged to syslog)
 */

#ifndef SM_WATCHDOG_H
#define SM_WATCHDOG_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog statistics */
typedef struct {
    uint64_t total_keepalives;
    uint64_t total_failures;
    time_t last_keepalive_time;
    int watchdog_fd;
    int is_running;
} sm_watchdog_stats_t;

/*
 * sm_watchdog_init()
 * 
 * Initialize watchdog timer and start background thread.
 * The watchdog thread will continuously write to /dev/watchdog to keep system alive.
 * If the thread stops writing (due to hang/crash), kernel auto-restarts system.
 * 
 * PARAMETERS: none
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (watchdog unavailable, permission denied, malloc failed, etc)
 * 
 * NOTES:
 *   - Requires /dev/watchdog readable and writable
 *   - Creates background thread with SCHED_FIFO priority if possible
 *   - Non-fatal if watchdog unavailable (logs warning, continues normally)
 */
int sm_watchdog_init(void);

/*
 * sm_watchdog_set_timeout(timeout_seconds)
 * 
 * Set watchdog timeout. Keepalive must occur before timeout expires.
 * 
 * PARAMETERS:
 *   timeout_seconds - watchdog timeout in seconds (typically 10-60)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_watchdog_set_timeout(int timeout_seconds);

/*
 * sm_watchdog_keepalive()
 * 
 * Manually feed the watchdog (typically called from main event loop).
 * Periodically calling this prevents system restart.
 * 
 * PARAMETERS: none
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 * 
 * NOTES:
 *   - Background thread also calls this automatically
 *   - Can be called from signal handlers
 */
int sm_watchdog_keepalive(void);

/*
 * sm_watchdog_get_stats()
 * 
 * Get watchdog runtime statistics.
 * 
 * RETURNS:
 *   Statistics struct with counters
 */
sm_watchdog_stats_t sm_watchdog_get_stats(void);

/*
 * sm_watchdog_cleanup()
 * 
 * Shutdown watchdog and close device.
 * Must be called before exit or system will restart when timeout expires.
 * 
 * PARAMETERS: none
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_watchdog_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_WATCHDOG_H */
