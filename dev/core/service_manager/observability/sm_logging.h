#ifndef SM_LOGGING_H
#define SM_LOGGING_H

#include <stdint.h>
#include <time.h>

/* Log level thresholds */
typedef enum {
    SM_LOG_DEBUG   = 0,
    SM_LOG_INFO    = 1,
    SM_LOG_WARN    = 2,
    SM_LOG_ERROR   = 3,
    SM_LOG_CRIT    = 4,
} sm_log_level_t;

/* Log file configuration */
#define SM_LOG_FILE         "/var/log/middleware/servicemanager.log"
#define SM_LOG_MAX_SIZE     (10 * 1024 * 1024)  /* 10 MB per file */
#define SM_LOG_BACKUP_COUNT 5                    /* keep 5 rotated files */
#define SM_LOG_BUFFER_SIZE  4096                 /* max formatted message size */

/* Initialize logging - must be called once at startup before any sm_log() calls */
int  sm_logging_init(void);

/* Log a message at the given level (printf-style format) */
void sm_log(sm_log_level_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Flush pending writes and close all log destinations */
void sm_logging_close(void);

/* Return a static per-thread timestamp string (YYYY-MM-DD HH:MM:SS) */
const char* sm_log_timestamp(void);

/* Change the minimum level that sm_log() will write */
void sm_set_log_level(sm_log_level_t level);

#endif /* SM_LOGGING_H */