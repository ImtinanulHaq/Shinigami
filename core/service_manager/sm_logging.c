#define _POSIX_C_SOURCE 200809L

#include "sm_logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

// ── STATE ──────────────────────────────────────────────────────────────────────

static int                    log_fd      = -1;
static sm_log_level_t         log_level   = SM_LOG_INFO;
static const char*            level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "CRIT"
};
static pthread_mutex_t        log_mutex   = PTHREAD_MUTEX_INITIALIZER;
static int                    syslog_open = 0;

// ── HELPERS ────────────────────────────────────────────────────────────────────

static const char* get_level_name(sm_log_level_t level)
{
    if (level >= 0 && level < (int)(sizeof(level_names) / sizeof(level_names[0])))
        return level_names[level];
    return "UNKNOWN";
}

static int get_syslog_priority(sm_log_level_t level)
{
    switch (level) {
        case SM_LOG_DEBUG: return LOG_DEBUG;
        case SM_LOG_INFO:  return LOG_INFO;
        case SM_LOG_WARN:  return LOG_WARNING;
        case SM_LOG_ERROR: return LOG_ERR;
        case SM_LOG_CRIT:  return LOG_CRIT;
        default:           return LOG_INFO;
    }
}

static void log_rotate(void)
{
    // Check if file needs rotation
    struct stat st;
    if (stat(SM_LOG_FILE, &st) < 0)
        return;  // file doesn't exist yet

    if (st.st_size < SM_LOG_MAX_SIZE)
        return;  // not yet at limit

    // Rotate: .4 -> delete, .3 -> .4, .2 -> .3, .1 -> .2, .0 -> .1, current -> .0
    char old_path[512], new_path[512];

    for (int i = SM_LOG_BACKUP_COUNT - 2; i >= 0; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", SM_LOG_FILE, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", SM_LOG_FILE, i + 1);
        rename(old_path, new_path);
    }

    // Rename current to .0
    snprintf(new_path, sizeof(new_path), "%s.0", SM_LOG_FILE);
    rename(SM_LOG_FILE, new_path);

    // Close and reopen
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}

// ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────────

int sm_logging_init(void)
{
    pthread_mutex_lock(&log_mutex);

    // Open syslog
    openlog("servicemanager", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog_open = 1;

    // Open log file
    log_fd = open(SM_LOG_FILE,
                  O_WRONLY | O_CREAT | O_APPEND,
                  0640);
    if (log_fd < 0) {
        syslog(LOG_ERR, "cannot open log file %s", SM_LOG_FILE);
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    sm_log(SM_LOG_INFO, "logging initialized");
    pthread_mutex_unlock(&log_mutex);
    return 0;
}

const char* sm_log_timestamp(void)
{
    static __thread char buf[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void sm_log(sm_log_level_t level, const char* fmt, ...)
{
    if (level < log_level)
        return;  // skip if below threshold

    pthread_mutex_lock(&log_mutex);

    char buffer[SM_LOG_BUFFER_SIZE];
    va_list ap;

    // Format the message
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    pid_t pid = getpid();
    const char* ts = sm_log_timestamp();
    const char* lv = get_level_name(level);

    // Write to syslog
    if (syslog_open) {
        syslog(get_syslog_priority(level),
               "[%s] [%d] %s\n",
               lv, pid, buffer);
    }

    // Write to file (with rotation)
    if (log_fd >= 0) {
        log_rotate();

        dprintf(log_fd, "%s [%s] [%d] %s\n",
                ts, lv, pid, buffer);
    }

    // For critical errors, also stderr
    if (level >= SM_LOG_CRIT) {
        fprintf(stderr, "%s [%s] [%d] %s\n",
                ts, lv, pid, buffer);
    }

    pthread_mutex_unlock(&log_mutex);
}

void sm_logging_close(void)
{
    pthread_mutex_lock(&log_mutex);

    if (log_fd >= 0) {
        sm_log(SM_LOG_INFO, "logging shutdown");
        close(log_fd);
        log_fd = -1;
    }

    if (syslog_open) {
        closelog();
        syslog_open = 0;
    }

    pthread_mutex_unlock(&log_mutex);
}

void sm_set_log_level(sm_log_level_t level)
{
    pthread_mutex_lock(&log_mutex);
    log_level = level;
    pthread_mutex_unlock(&log_mutex);
}
