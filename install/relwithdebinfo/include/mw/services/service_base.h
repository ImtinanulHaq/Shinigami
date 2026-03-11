/**
 * @file service_base.h
 * @brief Daemon lifecycle, logging, and signal infrastructure for all services.
 *
 * Global flags g_running and g_reload are set by dedicated sigaction handlers
 * (never signal()).  All log macros write simultaneously to syslog(LOG_DAEMON)
 * and g_log_file.  Function names use the service_base_ prefix.
 */

#ifndef SERVICE_BASE_H
#define SERVICE_BASE_H

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <syslog.h>

/* ── version ──────────────────────────────────────────────────────────── */

#define SERVICE_LAYER_VERSION_STR   "2.0.0"

/* ── limits ───────────────────────────────────────────────────────────── */

#define SERVICE_MAX_NAME    64
#define SERVICE_MAX_PATH    256
#define SERVICE_MAX_VERSION 32

/* Default PID directory — overridden at runtime via SVC_PID_DIR env var */
#ifndef SERVICE_PID_DIR
#define SERVICE_PID_DIR "/run"
#endif

/* ── global control flags (set by sigaction handlers) ────────────────── */

/**
 * SIGTERM/SIGINT handler clears this to 0.
 * Event loops must check !g_running to exit.
 */
extern volatile sig_atomic_t g_running;

/**
 * SIGHUP handler sets this to 1.
 * Event loops must re-read config when they see g_reload == 1, then reset it.
 */
extern volatile sig_atomic_t g_reload;

/**
 * Global log file opened by service_base_open_log().
 * May be NULL when running without a file sink.
 */
extern FILE *g_log_file;

/* ── error codes ──────────────────────────────────────────────────────── */

typedef enum {
    SVC_OK              =  0,
    SVC_ERR_GENERIC     = -1,
    SVC_ERR_INVALID     = -2,
    SVC_ERR_FORK        = -3,
    SVC_ERR_SETSID      = -4,
    SVC_ERR_CHDIR       = -5,
    SVC_ERR_PIDFILE     = -6,
    SVC_ERR_SIGNAL      = -7,
    SVC_ERR_ALREADY     = -8,    /* duplicate instance detected */
    SVC_ERR_HAL         = -9,
    SVC_ERR_SECURITY    = -10,
    SVC_ERR_IPC         = -11,
    SVC_ERR_CONFIG      = -12,
    SVC_ERR_NOMEM       = -13,
    SVC_ERR_TIMEOUT     = -14,
} svc_error_t;

/* ── daemon state ─────────────────────────────────────────────────────── */

typedef enum {
    SVC_STATE_INIT       = 0,
    SVC_STATE_STARTING   = 1,
    SVC_STATE_RUNNING    = 2,
    SVC_STATE_STOPPING   = 3,
    SVC_STATE_STOPPED    = 4,
    SVC_STATE_ERROR      = 5,
} svc_state_t;

/* ── daemon context ───────────────────────────────────────────────────── */

typedef struct {
    char        name[SERVICE_MAX_NAME];
    char        version[SERVICE_MAX_VERSION];
    char        exe_path[SERVICE_MAX_PATH];     /**< realpath of this binary   */
    char        pid_path[SERVICE_MAX_PATH];     /**< /run/<name>.pid           */
    char        config_path[SERVICE_MAX_PATH];  /**< -c argument               */
    char        log_path[SERVICE_MAX_PATH];     /**< from [server] log_file    */
    pid_t       pid;
    svc_state_t state;
    int         foreground; /**< 1 = -f flag, skip daemonize                   */
    int         verbose;    /**< 1 = -v flag, debug-level logging              */
    time_t      start_time;
    uint64_t    error_count;
} svc_context_t;

/* ── dual-sink log macros (syslog + file) ─────────────────────────────── */

/* Use numeric syslog priority constants directly to avoid shadowing
 * issues after we redefine LOG_ERR / LOG_INFO / LOG_DEBUG below. */
#define _PRIO_ERR   3   /* == LOG_ERR   from syslog.h */
#define _PRIO_INFO  6   /* == LOG_INFO  from syslog.h */
#define _PRIO_DEBUG 7   /* == LOG_DEBUG from syslog.h */

/* Shadow the syslog names with service-layer function-like macros */
#undef LOG_ERR
#undef LOG_INFO
#undef LOG_DEBUG

#define _SVC_LOG(pri, tag, fmt, ...) do {                                  \
    syslog(LOG_DAEMON | (pri), "[" tag "] [%s] " fmt,                     \
           __func__, ##__VA_ARGS__);                                       \
    if (g_log_file) {                                                      \
        fprintf(g_log_file, "[" tag "] [%s] " fmt "\n",                   \
                __func__, ##__VA_ARGS__);                                  \
        fflush(g_log_file);                                                \
    }                                                                      \
} while (0)

#define LOG_ERR(fmt, ...)   _SVC_LOG(_PRIO_ERR,   "ERR ", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  _SVC_LOG(LOG_WARNING,  "WARN", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  _SVC_LOG(_PRIO_INFO,   "INFO", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) _SVC_LOG(_PRIO_DEBUG,  "DBG ", fmt, ##__VA_ARGS__)

/* Short-form aliases used inside *_service_hal.c */
#define SVC_ERR   LOG_ERR
#define SVC_WARN  LOG_WARN
#define SVC_INFO  LOG_INFO
#define SVC_DBG   LOG_DEBUG

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Populate svc_context_t with defaults.
 */
int service_base_init(svc_context_t *ctx, const char *name);

/**
 * @brief Double-fork POSIX daemonize.
 *
 * fork() → setsid() → fork().  After the second fork:
 *   chdir("/"), umask(0), redirect stdin/stdout/stderr to /dev/null.
 * Each non-surviving process calls _exit(0).
 *
 * @return SVC_OK in the surviving grandchild, SVC_ERR_* on failure.
 */
int service_base_daemonize(svc_context_t *ctx);

/**
 * @brief Open syslog with LOG_DAEMON facility and optionally a log file.
 *
 * @param name     Identity string for openlog().
 * @param log_path File path for the file sink, or NULL for syslog only.
 */
void service_base_open_log(const char *name, const char *log_path);

/** @brief Flush/close the log file and call closelog(). */
void service_base_close_log(void);

/**
 * @brief Write the daemon's PID to /run/<svc_name>.pid.
 *
 * If the file already exists and kill(stored_pid, 0) succeeds the function
 * logs "already running with PID %d" and returns SVC_ERR_ALREADY so the
 * caller can exit, preventing duplicate daemon instances.
 *
 * @return SVC_OK, SVC_ERR_ALREADY, or SVC_ERR_PIDFILE.
 */
int service_base_write_pid(const char *svc_name);

/**
 * @brief Delete /run/<svc_name>.pid.
 */
void service_base_remove_pid(const char *svc_name);

/**
 * @brief Install sigaction handlers for SIGTERM, SIGINT, SIGHUP, SIGPIPE.
 *
 * SIGTERM / SIGINT → set g_running = 0.
 * SIGHUP           → set g_reload  = 1.
 * SIGPIPE          → SIG_IGN.
 *
 * @return SVC_OK or SVC_ERR_SIGNAL.
 */
int service_base_install_signals(void);

/** @brief Return a human-readable string for a svc_error_t value. */
const char *svc_error_string(int err);

#endif /* SERVICE_BASE_H */
