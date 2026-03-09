/**
 * @file service_base.c
 * @brief Daemonization, PID file management, signal handlers, and logging.
 */

#define _GNU_SOURCE
#include "service_base.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ── globals ──────────────────────────────────────────────────────────── */

volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_reload  = 0;
FILE                 *g_log_file = NULL;

/* ── private signal handlers ──────────────────────────────────────────── */

static void _sig_term(int sig)
{
    (void)sig;
    g_running = 0;
}

static void _sig_hup(int sig)
{
    (void)sig;
    g_reload = 1;
}

/* ── public API ───────────────────────────────────────────────────────── */

int service_base_init(svc_context_t *ctx, const char *name)
{
    if (!ctx || !name || name[0] == '\0')
        return SVC_ERR_INVALID;

    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->name, name, SERVICE_MAX_NAME - 1);
    strncpy(ctx->version, SERVICE_LAYER_VERSION_STR, SERVICE_MAX_VERSION - 1);
    snprintf(ctx->pid_path, sizeof(ctx->pid_path),
             SERVICE_PID_DIR "/%s.pid", name);
    ctx->state      = SVC_STATE_INIT;
    ctx->start_time = time(NULL);

    /* Capture the real path of this binary for SM registration */
    ssize_t n = readlink("/proc/self/exe", ctx->exe_path,
                         sizeof(ctx->exe_path) - 1);
    if (n > 0)
        ctx->exe_path[n] = '\0';

    return SVC_OK;
}

int service_base_daemonize(svc_context_t *ctx)
{
    (void)ctx;

    /* ── First fork ─────────────────────────── */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "daemonize: first fork failed: %s\n", strerror(errno));
        return SVC_ERR_FORK;
    }
    if (pid > 0)
        _exit(0); /* parent exits */

    /* Become session leader */
    if (setsid() < 0) {
        fprintf(stderr, "daemonize: setsid failed: %s\n", strerror(errno));
        return SVC_ERR_SETSID;
    }

    /* ── Second fork ────────────────────────── */
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "daemonize: second fork failed: %s\n", strerror(errno));
        return SVC_ERR_FORK;
    }
    if (pid > 0)
        _exit(0); /* session leader exits */

    /* Surviving grandchild continues */
    if (chdir("/") < 0) {
        fprintf(stderr, "daemonize: chdir('/') failed: %s\n", strerror(errno));
        return SVC_ERR_CHDIR;
    }
    umask(0); /* let explicit mode bits control permissions */

    /* Redirect stdin / stdout / stderr to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
            close(devnull);
    }

    return SVC_OK;
}

void service_base_open_log(const char *name, const char *log_path)
{
    /* openlog: LOG_PID | LOG_NDELAY, LOG_DAEMON facility */
    openlog(name, LOG_PID | LOG_NDELAY, LOG_DAEMON);

    if (log_path && log_path[0] != '\0') {
        g_log_file = fopen(log_path, "a");
        if (!g_log_file)
            syslog(LOG_DAEMON | LOG_WARNING,
                   "service_base_open_log: cannot open %s: %s",
                   log_path, strerror(errno));
    }
}

void service_base_close_log(void)
{
    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    closelog();
}

int service_base_write_pid(const char *svc_name)
{
    if (!svc_name || svc_name[0] == '\0')
        return SVC_ERR_INVALID;

    char path[SERVICE_MAX_PATH];
    snprintf(path, sizeof(path), SERVICE_PID_DIR "/%s.pid", svc_name);

    /* ── Duplicate-instance check ─────────────── */
    FILE *fp = fopen(path, "r");
    if (fp) {
        int stored = 0;
        if (fscanf(fp, "%d", &stored) == 1 && stored > 1) {
            /* kill(pid, 0) returns 0 if the process is alive */
            if (kill((pid_t)stored, 0) == 0) {
                LOG_ERR("service %s already running with PID %d — aborting",
                        svc_name, stored);
                fclose(fp);
                return SVC_ERR_ALREADY;
            }
        }
        fclose(fp);
        /* Stale PID file — unlink silently */
        unlink(path);
    }

    /* ── Write our PID ────────────────────────── */
    fp = fopen(path, "w");
    if (!fp) {
        LOG_ERR("cannot create PID file %s: %s", path, strerror(errno));
        return SVC_ERR_PIDFILE;
    }
    fprintf(fp, "%d\n", (int)getpid());
    fclose(fp);
    return SVC_OK;
}

void service_base_remove_pid(const char *svc_name)
{
    if (!svc_name) return;
    char path[SERVICE_MAX_PATH];
    snprintf(path, sizeof(path), SERVICE_PID_DIR "/%s.pid", svc_name);
    unlink(path);
}

int service_base_install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    /* SIGTERM → g_running = 0 */
    sa.sa_handler = _sig_term;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return SVC_ERR_SIGNAL;

    /* SIGINT → g_running = 0 */
    if (sigaction(SIGINT,  &sa, NULL) < 0) return SVC_ERR_SIGNAL;

    /* SIGHUP → g_reload = 1 */
    sa.sa_handler = _sig_hup;
    if (sigaction(SIGHUP,  &sa, NULL) < 0) return SVC_ERR_SIGNAL;

    /* SIGPIPE → SIG_IGN (prevents crash on broken SM socket) */
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) < 0) return SVC_ERR_SIGNAL;

    return SVC_OK;
}

const char *svc_error_string(int err)
{
    switch ((svc_error_t)err) {
    case SVC_OK:              return "success";
    case SVC_ERR_GENERIC:     return "generic error";
    case SVC_ERR_INVALID:     return "invalid argument";
    case SVC_ERR_FORK:        return "fork failed";
    case SVC_ERR_SETSID:      return "setsid failed";
    case SVC_ERR_CHDIR:       return "chdir failed";
    case SVC_ERR_PIDFILE:     return "PID file error";
    case SVC_ERR_SIGNAL:      return "sigaction failed";
    case SVC_ERR_ALREADY:     return "duplicate instance";
    case SVC_ERR_HAL:         return "HAL error";
    case SVC_ERR_SECURITY:    return "security module error";
    case SVC_ERR_IPC:         return "IPC error";
    case SVC_ERR_CONFIG:      return "config error";
    case SVC_ERR_NOMEM:       return "out of memory";
    case SVC_ERR_TIMEOUT:     return "timeout";
    default:                  return "unknown error";
    }
}
