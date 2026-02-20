#ifndef SM_LOGGING_H
#define SM_LOGGING_H

#include <stdint.h>
#include <time.h>

// ── LOG LEVELS ─────────────────────────────────────────────────────────────────

typedef enum {
    SM_LOG_DEBUG   = 0,
    SM_LOG_INFO    = 1,
    SM_LOG_WARN    = 2,
    SM_LOG_ERROR   = 3,
    SM_LOG_CRIT    = 4,
} sm_log_level_t;

// ── CONFIGURATION ─────────────────────────────────────────────────────────────

#define SM_LOG_FILE         "/var/log/servicemanager.log"
#define SM_LOG_MAX_SIZE     (10 * 1024 * 1024)  // 10MB
#define SM_LOG_BACKUP_COUNT 5                    // keep 5 rotated logs
#define SM_LOG_BUFFER_SIZE  4096

// ── FUNCTIONS ──────────────────────────────────────────────────────────────────

// Initialize logging (must be called once at startup)
int  sm_logging_init(void);

// Log a message (printf-style)
void sm_log(sm_log_level_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Close logging (flush and cleanup)
void sm_logging_close(void);

// Get human-readable timestamp
const char* sm_log_timestamp(void);

// Set log level threshold
void sm_set_log_level(sm_log_level_t level);

#endif // SM_LOGGING_H
