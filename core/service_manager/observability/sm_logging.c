#define _POSIX_C_SOURCE 200809L

/*
 * sm_logging.c - Thread-safe, rotating file and syslog logger.
 *
 * Fixes applied:
 *   - Buffer sanitized before writing: newlines and carriage-returns in
 *     formatted messages are replaced with spaces to prevent log injection.
 *   - Re-entrant safety: a per-thread flag prevents sm_log() from being called
 *     recursively (e.g. from a signal handler while the mutex is held).
 *   - log_rotate() is called with the mutex already held; it does NOT call
 *     sm_log() to avoid recursive locking.
 *   - sm_logging_close() flushes inline rather than calling sm_log() while
 *     holding the mutex.
 */

#include "../observability/sm_logging.h"

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
#include <errno.h>

/* ── STATE ──────────────────────────────────────────────────────────────────── */

static int             log_fd      = -1;
static sm_log_level_t  log_level   = SM_LOG_INFO;
static pthread_mutex_t log_mutex   = PTHREAD_MUTEX_INITIALIZER;
static int             syslog_open = 0;

static const char* const level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "CRIT"
};

/*
 * Per-thread re-entrancy guard.
 * Set to 1 while sm_log() is executing in this thread.
 * Prevents recursive calls (e.g. from signal handlers) from deadlocking.
 */
static __thread int in_log = 0;

/* ── HELPERS ────────────────────────────────────────────────────────────────── */

static const char* level_name(sm_log_level_t level)
{
    if (level >= 0 && level < (int)(sizeof(level_names) / sizeof(level_names[0])))
        return level_names[level];
    return "UNKNOWN";
}

static int syslog_priority(sm_log_level_t level)
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

/*
 * Replace control characters that could corrupt the log file or enable
 * log-injection attacks.  Replaces \n \r \t and all other non-printable
 * characters below 0x20 with a space.
 * Called while the mutex is already held.
 */
static void sanitize_log_buffer(char* buf)
{
    for (char* p = buf; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) *p = ' ';
    }
}

/*
 * Rotate the log file if it exceeds SM_LOG_MAX_SIZE.
 * Must only be called while log_mutex is held and log_fd is valid.
 * Does NOT call sm_log() to avoid re-entrancy.
 */
static void log_rotate_locked(void)
{
    struct stat st;
    char        old_path[600], new_path[600];
    int         i;

    if (fstat(log_fd, &st) < 0) return;
    if (st.st_size < SM_LOG_MAX_SIZE) return;

    /* Shift rotated files: N-2 -> N-1, ..., 0 -> 1 */
    for (i = SM_LOG_BACKUP_COUNT - 2; i >= 0; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", SM_LOG_FILE, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", SM_LOG_FILE, i + 1);
        rename(old_path, new_path);
    }

    /* Move current to .0 */
    snprintf(new_path, sizeof(new_path), "%s.0", SM_LOG_FILE);
    rename(SM_LOG_FILE, new_path);

    /* Close old fd and open new file */
    close(log_fd);
    log_fd = open(SM_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
    /* If re-open fails, log_fd will be -1; further writes are silently skipped */
}

/* ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────── */

int sm_logging_init(void)
{
    pthread_mutex_lock(&log_mutex);

    openlog("servicemanager", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog_open = 1;

    log_fd = open(SM_LOG_FILE,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                  0640);
    if (log_fd < 0) {
        syslog(LOG_ERR, "cannot open log file %s: %s", SM_LOG_FILE, strerror(errno));
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    pthread_mutex_unlock(&log_mutex);

    sm_log(SM_LOG_INFO, "logging initialized (file=%s)", SM_LOG_FILE);
    return 0;
}

const char* sm_log_timestamp(void)
{
    static __thread char buf[32];
    time_t    now = time(NULL);
    struct tm tm;

    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void sm_log(sm_log_level_t level, const char* fmt, ...)
{
    char        buf[SM_LOG_BUFFER_SIZE];
    va_list     ap;
    pid_t       pid;
    const char* ts;
    const char* lv;

    if (level < log_level) return;

    /* Re-entrancy guard: skip if already logging in this thread */
    if (in_log) return;
    in_log = 1;

    /* Format the message before taking the lock (keeps lock duration short) */
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Sanitize: remove any control chars that could corrupt log files */
    sanitize_log_buffer(buf);

    pid = getpid();
    ts  = sm_log_timestamp();
    lv  = level_name(level);

    pthread_mutex_lock(&log_mutex);

    if (syslog_open) {
        syslog(syslog_priority(level), "[%s] [%d] %s", lv, (int)pid, buf);
    }

    if (log_fd >= 0) {
        log_rotate_locked();
        if (log_fd >= 0) {   /* rotation may have failed */
            dprintf(log_fd, "%s [%s] [%d] %s\n", ts, lv, (int)pid, buf);
        }
    }

    pthread_mutex_unlock(&log_mutex);

    /* Critical messages also go to stderr (outside the lock) */
    if (level >= SM_LOG_CRIT) {
        fprintf(stderr, "%s [%s] [%d] %s\n", ts, lv, (int)pid, buf);
        fflush(stderr);
    }

    in_log = 0;
}

void sm_logging_close(void)
{
    pthread_mutex_lock(&log_mutex);

    /* Write shutdown message directly without calling sm_log() (mutex is held) */
    if (log_fd >= 0) {
        dprintf(log_fd, "%s [INFO] [%d] logging shutdown\n",
                sm_log_timestamp(), (int)getpid());
        close(log_fd);
        log_fd = -1;
    }

    if (syslog_open) {
        syslog(LOG_INFO, "logging shutdown");
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