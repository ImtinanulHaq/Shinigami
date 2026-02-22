/*
 * sm_rolling_restart.h - Zero-downtime rolling restart for configuration changes
 *
 * Restarts services one at a time instead of all at once.
 * Maintains service availability during configuration updates.
 * 
 * USAGE:
 *   sm_rolling_restart_init(max_restarts);
 *   sm_rolling_restart_start(service_list, count, delay_seconds);
 *   // Monitor progress:
 *   sm_rolling_restart_progress_t progress = sm_rolling_restart_get_progress();
 *   sm_rolling_restart_cleanup();
 */

#ifndef SM_ROLLING_RESTART_H
#define SM_ROLLING_RESTART_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Restart state for a service */
typedef enum {
    RESTART_PENDING = 0,
    RESTART_IN_PROGRESS = 1,
    RESTART_SUCCESS = 2,
    RESTART_FAILED = 3,
    RESTART_SKIPPED = 4,
} sm_restart_state_t;

/* Individual service restart info */
typedef struct {
    char service_name[64];
    sm_restart_state_t state;
    time_t start_time;
    time_t completion_time;
    int retry_count;
    int max_retries;
    char error_message[256];
} sm_service_restart_t;

/* Rolling restart progress */
typedef struct {
    int total_services;
    int completed_count;
    int success_count;
    int failed_count;
    int in_progress_count;
    int current_service_idx;
    float completion_percent;
    time_t start_time;
    time_t estimated_completion_time;
    int is_active;
} sm_rolling_restart_progress_t;

/*
 * sm_rolling_restart_init(max_concurrent_restarts)
 * 
 * Initialize rolling restart system.
 * 
 * PARAMETERS:
 *   max_concurrent_restarts - max services restarting simultaneiously (default 1)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_rolling_restart_init(int max_concurrent_restarts);

/*
 * sm_rolling_restart_start(service_names, count, delay_between_restarts_sec)
 * 
 * Begin rolling restart of multiple services.
 * 
 * PARAMETERS:
 *   service_names - array of service names to restart
 *   count - number of services
 *   delay_between_restarts_sec - wait time between each restart (e.g., 10 seconds)
 * 
 * RETURNS:
 *   0 on success (restart queued)
 *   -1 on error
 * 
 * NOTES:
 *   - Non-blocking, restarts happen in background thread
 *   - Poll with sm_rolling_restart_get_progress() for status
 */
int sm_rolling_restart_start(const char** service_names, int count, int delay_between_restarts_sec);

/*
 * sm_rolling_restart_get_progress()
 * 
 * Get current rolling restart progress.
 * 
 * RETURNS:
 *   Progress structure with current status
 */
sm_rolling_restart_progress_t sm_rolling_restart_get_progress(void);

/*
 * sm_rolling_restart_get_service_status(service_name)
 * 
 * Get restart status for specific service.
 * 
 * PARAMETERS:
 *   service_name - service to query
 * 
 * RETURNS:
 *   Service restart info (or zero-filled if not found)
 */
sm_service_restart_t sm_rolling_restart_get_service_status(const char* service_name);

/*
 * sm_rolling_restart_cancel()
 * 
 * Cancel in-progress rolling restart.
 * Allows already-started restarts to complete, but cancels queued ones.
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_rolling_restart_cancel(void);

/*
 * sm_rolling_restart_is_active()
 * 
 * Check if restart is currently running.
 * 
 * RETURNS:
 *   1 if active, 0 if not
 */
int sm_rolling_restart_is_active(void);

/*
 * sm_rolling_restart_cleanup()
 * 
 * Shutdown rolling restart system.
 * Cancels any in-progress restart.
 * 
 * RETURNS:
 *   0 on success
 */
int sm_rolling_restart_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_ROLLING_RESTART_H */
