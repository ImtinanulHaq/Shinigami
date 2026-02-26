/*
 * sm_monitoring.h - Resource monitoring for service manager
 *
 * Monitors CPU, memory, and file descriptor usage.
 * Detects memory leaks, resource exhaustion, and performance issues.
 * 
 * USAGE:
 *   sm_monitoring_init(service_name);
 *   // Periodically call:
 *   sm_monitoring_sample();
 *   
 *   // Get stats:
 *   sm_monitoring_stats_t stats = sm_monitoring_get_stats();
 * 
 * MEMORY LEAK DETECTION:
 *   Tracks memory trend (rate of change).
 *   If memory consistently increases without plateau, flags as potential leak.
 */

#ifndef SM_MONITORING_H
#define SM_MONITORING_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resource alert types */
typedef enum {
    SM_ALERT_NONE = 0,
    SM_ALERT_HIGH_MEMORY,       /* > 80% of threshold */
    SM_ALERT_HIGH_CPU,          /* > 80% single-core usage */
    SM_ALERT_HIGH_FD_COUNT,     /* > 80% of max FDs */
    SM_ALERT_MEMORY_LEAK,       /* Consistent memory growth detected */
    SM_ALERT_FD_LEAK,           /* FDs not being closed */
} sm_alert_type_t;

/* Monitoring alert notification */
typedef struct {
    sm_alert_type_t alert_type;
    time_t timestamp;
    double value;              /* Current value that triggered alert */
    double threshold;          /* Alert threshold */
    const char* description;
} sm_monitoring_alert_t;

/* Resource sample snapshot */
typedef struct {
    time_t timestamp;
    double cpu_percent;        /* 0-100, single-core equivalent */
    uint64_t rss_bytes;        /* Resident set size in bytes */
    uint64_t vms_bytes;        /* Virtual memory size in bytes */
    int open_fds;              /* Number of open file descriptors */
    int max_fds;               /* Max allowed FDs for process */
} sm_monitoring_sample_t;

/* Resource monitoring statistics */
typedef struct {
    int sample_count;
    
    /* Memory stats */
    uint64_t peak_rss_bytes;
    uint64_t current_rss_bytes;
    double avg_rss_bytes;
    double memory_growth_rate;  /* Bytes per second */
    
    /* CPU stats */
    double peak_cpu_percent;
    double avg_cpu_percent;
    
    /* FD stats */
    int peak_open_fds;
    int current_open_fds;
    
    /* State */
    int is_initialized;
    time_t monitoring_started_time;
    
    /* Alerts */
    uint64_t total_alerts;
    sm_alert_type_t last_alert;
} sm_monitoring_stats_t;

/*
 * sm_monitoring_init(service_name)
 * 
 * Initialize resource monitoring for a service.
 * Must be called once before sampling.
 * 
 * PARAMETERS:
 *   service_name - name of service being monitored (for logging)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_monitoring_init(const char* service_name);

/*
 * sm_monitoring_sample()
 * 
 * Collect current resource snapshot.
 * Call periodically (e.g., every 5-10 seconds from main event loop).
 * 
 * PARAMETERS: none
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 * 
 * NOTES:
 *   - Reads /proc/self/stat and /proc/self/fd for current process
 *   - Detects anomalies (memory leak, FD leak)
 *   - Generates alerts if thresholds exceeded
 */
int sm_monitoring_sample(void);

/*
 * sm_monitoring_set_memory_threshold(bytes)
 * 
 * Set memory usage alert threshold (default 512MB).
 * 
 * PARAMETERS:
 *   bytes - alert threshold in bytes (0 to disable)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on invalid input
 */
int sm_monitoring_set_memory_threshold(uint64_t bytes);

/*
 * sm_monitoring_set_fd_threshold(count)
 * 
 * Set file descriptor count alert threshold (default 256).
 * Alert if > 80% of this threshold.
 * 
 * PARAMETERS:
 *   count - alert threshold FD count (0 to disable)
 * 
 * RETURNS:
 *   0 on success
 *   -1 on invalid input
 */
int sm_monitoring_set_fd_threshold(int count);

/*
 * sm_monitoring_get_stats()
 * 
 * Get current monitoring statistics.
 * 
 * RETURNS:
 *   Statistics structure
 * 
 * NOTES:
 *   - Memory growth rate calculated from last 5 samples
 *   - Peak values are max observed since init
 *   - Averages include all collected samples
 */
sm_monitoring_stats_t sm_monitoring_get_stats(void);

/*
 * sm_monitoring_get_last_sample()
 * 
 * Get most recent resource sample.
 * 
 * RETURNS:
 *   Sample structure (or zeroed if no samples collected)
 */
sm_monitoring_sample_t sm_monitoring_get_last_sample(void);

/*
 * sm_monitoring_get_last_alert()
 * 
 * Get most recent alert (if any).
 * 
 * RETURNS:
 *   Alert structure
 */
sm_monitoring_alert_t sm_monitoring_get_last_alert(void);

/*
 * sm_monitoring_cleanup()
 * 
 * Shutdown resource monitoring.
 * 
 * PARAMETERS: none
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_monitoring_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_MONITORING_H */
