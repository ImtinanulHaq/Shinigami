#define _POSIX_C_SOURCE 200809L

/*
 * sm_logging.c - Thread-safe, rotating file and syslog logger.
 *
 * FIXES:
 *   1. sanitize_log_buffer() — control chars replaced to prevent log injection.
 *   2. Per-thread re-entrancy guard (in_log) — prevents deadlock if a signal
 *      handler calls sm_log() while the mutex is already held.
 *   3. log_rotate_locked() — called with mutex held, never calls sm_log().
 *   4. sm_logging_close() — writes shutdown line directly, not via sm_log().
 *   5. sm_log() — message formatted BEFORE taking the lock (shorter lock window).
 *   6. sm_log_timestamp() — thread-local buffer, safe for concurrent calls.
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

/* ── State ───────────────────────────────────────────────────────────────────── */

static int             log_fd      = -1;
static sm_log_level_t  log_level   = SM_LOG_INFO;
static pthread_mutex_t log_mutex   = PTHREAD_MUTEX_INITIALIZER;
static int             syslog_open = 0;

static const char* const level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "CRIT"
};

/* Per-thread guard: 1 while sm_log() is running in this thread. */
static __thread int in_log = 0;

/* ── Internal helpers ────────────────────────────────────────────────────────── */

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
 * Replace control characters with spaces.
 * Prevents newline injection attacks in log files.
 * Must be called with mutex held.
 */
static void sanitize_log_buffer(char* buf)
{
    for (char* p = buf; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) *p = ' ';
    }
}

/*
 * Rotate log file when it exceeds SM_LOG_MAX_SIZE.
 * Must only be called with log_mutex held.
 * Never calls sm_log() — avoids recursive locking.
 */
static void log_rotate_locked(void)
{
    struct stat st;
    char        old_path[600];
    char        new_path[600];

    if (fstat(log_fd, &st) < 0) return;
    if (st.st_size < SM_LOG_MAX_SIZE) return;

    /* Shift backups: .3 -> .4, .2 -> .3, .1 -> .2, .0 -> .1 */
    for (int i = SM_LOG_BACKUP_COUNT - 2; i >= 0; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", SM_LOG_FILE, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", SM_LOG_FILE, i + 1);
        rename(old_path, new_path);
    }

    /* Current file -> .0 */
    snprintf(new_path, sizeof(new_path), "%s.0", SM_LOG_FILE);
    rename(SM_LOG_FILE, new_path);

    /* Open a fresh log file */
    close(log_fd);
    log_fd = open(SM_LOG_FILE,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
    /* If open fails, log_fd = -1; further writes are silently skipped. */
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

int sm_logging_init(void)
{
    pthread_mutex_lock(&log_mutex);

    openlog("servicemanager", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog_open = 1;

    log_fd = open(SM_LOG_FILE,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
    if (log_fd < 0) {
        syslog(LOG_ERR, "cannot open log file %s: %s",
               SM_LOG_FILE, strerror(errno));
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    pthread_mutex_unlock(&log_mutex);

    sm_log(SM_LOG_INFO, "logging initialized (file=%s)", SM_LOG_FILE);
    return 0;
}

/*
 * Returns a formatted timestamp string.
 * Thread-local buffer — safe for concurrent calls without locking.
 */
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
    char buf[SM_LOG_BUFFER_SIZE];

    if (level < log_level) return;

    /* FIX 2: Skip if already logging in this thread — prevents deadlock. */
    if (in_log) return;
    in_log = 1;

    /* FIX 5: Format message BEFORE taking the lock — shorter lock window. */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* FIX 1: Strip control characters before writing. */
    sanitize_log_buffer(buf);

    pid_t       pid = getpid();
    const char* ts  = sm_log_timestamp();
    const char* lv  = level_name(level);

    pthread_mutex_lock(&log_mutex);

    if (syslog_open) {
        syslog(syslog_priority(level), "[%s] [%d] %s", lv, (int)pid, buf);
    }

    if (log_fd >= 0) {
        log_rotate_locked();
        if (log_fd >= 0) {
            dprintf(log_fd, "%s [%s] [%d] %s\n", ts, lv, (int)pid, buf);
        }
    }

    pthread_mutex_unlock(&log_mutex);

    /* Critical messages also go to stderr, outside the lock. */
    if (level >= SM_LOG_CRIT) {
        fprintf(stderr, "%s [%s] [%d] %s\n", ts, lv, (int)pid, buf);
        fflush(stderr);
    }

    in_log = 0;
}

/*
 * FIX 4: Write shutdown line directly with dprintf — do NOT call sm_log()
 * here because the mutex is already held.
 */
void sm_logging_close(void)
{
    pthread_mutex_lock(&log_mutex);

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