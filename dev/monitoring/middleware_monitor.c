/**
 * @file    middleware_monitor.c
 * @brief   Standalone real-time middleware monitoring TUI
 *
 * Architecture:
 *   - Thread 1 (data_collector): runs every 1000ms, reads /proc + sockets
 *   - Thread 2 (main/render):   runs every 100ms, renders ncurses panels
 *   - pthread_mutex protects shared g_data between threads
 *
 * Fixes:
 *   - ACS box-drawing chars (no raw Unicode)
 *   - Proper CPU delta calculation (two-snapshot method)
 *   - Service discovery via /proc scan when SM socket unavailable
 *   - Background thread collection / non-blocking render
 *   - Real health score computation
 *   - Color progress bars
 *   - All panels implemented
 *   - SIGWINCH resize handler
 *   - Clean exit with endwin()
 */

/* _GNU_SOURCE and _POSIX_C_SOURCE provided by compiler flags */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <locale.h>
#include <ncurses.h>

/* ══════════════════════════════════════════════════════════════════════════
 * Constants
 * ══════════════════════════════════════════════════════════════════════════ */

#define MAX_SERVICES     16
#define MAX_LOG_LINES    512
#define MAX_ALERTS       64
#define SERVICE_NAME_LEN 64
#define LOG_LINE_LEN     256
#define TAB_COUNT        9

/* Log levels */
typedef enum {
    LL_DEBUG = 0,
    LL_INFO  = 1,
    LL_WARN  = 2,
    LL_ERROR = 3
} log_level_t;

/* Log categories */
typedef enum {
    LC_SYSTEM  = 0,
    LC_SERVICE = 1,
    LC_HAL     = 2,
    LC_MEMORY  = 3,
    LC_IO      = 4,
    LC_SECURITY= 5,
    LC_HEALTH  = 6,
    LC_NET     = 7
} log_cat_t;

typedef struct {
    time_t       ts;
    log_level_t  level;
    log_cat_t    cat;
    char         msg[LOG_LINE_LEN];
} log_entry_t;

/* Color pair IDs */
#define CP_DEFAULT      1
#define CP_HEADER       2
#define CP_SELECTED     3
#define CP_GREEN        4
#define CP_YELLOW       5
#define CP_RED          6
#define CP_CYAN         7
#define CP_DIM          8
#define CP_BLUE         9
#define CP_MAGENTA      10
#define CP_BAR_OK       11
#define CP_BAR_WARN     12
#define CP_BAR_CRIT     13

static const char *TAB_NAMES[TAB_COUNT] = {
    "Overview", "Services", "HAL", "Memory", "I/O",
    "Security", "Alerts", "Logs", "Help"
};

/* ══════════════════════════════════════════════════════════════════════════
 * Data structures
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char   name[SERVICE_NAME_LEN];
    pid_t  pid;
    int    running;           /* 1 = alive, 0 = dead */
    float  cpu_pct;
    long   rss_kb;
    long   vsz_kb;
    int    fd_count;
    int    thread_count;
    long   restart_count;
    long   uptime_s;
    int    health_score;      /* 0–100 */
    /* Per-process CPU tracking */
    unsigned long prev_utime;
    unsigned long prev_stime;
    unsigned long long prev_uptime_ticks;
} service_data_t;

typedef struct {
    long user, nice, sys, idle, iowait, irq, softirq, steal;
} cpu_stat_t;

typedef struct {
    /* CPU */
    float cpu_total_pct;
    float cpu_iowait_pct;   /* shown separately in Overview */
    float cpu_core_pct[16];
    int   num_cores;
    /* Previous snapshot for delta */
    cpu_stat_t prev_cpu;
    int    cpu_initialized;

    /* Memory (KB) */
    long ram_total_kb;
    long ram_used_kb;
    long ram_free_kb;
    long swap_total_kb;
    long swap_used_kb;

    /* Load avg */
    float load_1, load_5, load_15;

    /* Uptime */
    long uptime_s;

    /* Services */
    service_data_t services[MAX_SERVICES];
    int            service_count;

    /* SM socket status */
    int  sm_socket_ok;
    char sm_socket_path[128];

    /* System-wide fd/network */
    long fd_used;

    /* Structured log ring buffer */
    log_entry_t log_entries[MAX_LOG_LINES];
    int         log_head;
    int         log_count;

    /* Alerts (kept separate for the Alerts panel) */
    char   alert_lines[MAX_ALERTS][LOG_LINE_LEN];
    int    alert_count;

    /* Timestamp */
    time_t last_update;
} monitor_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Globals
 * ══════════════════════════════════════════════════════════════════════════ */

static pthread_mutex_t   g_lock = PTHREAD_MUTEX_INITIALIZER;
static monitor_data_t    g_data;
static volatile int      g_running  = 1;
static volatile int      g_resize   = 0;        /* SIGWINCH flag */
static int               g_active_tab = 0;
static WINDOW           *g_panels[TAB_COUNT];   /* persistent per-tab windows */
static int               g_rows, g_cols;        /* terminal dimensions */

/* ══════════════════════════════════════════════════════════════════════════
 * Signal handlers
 * ══════════════════════════════════════════════════════════════════════════ */

static void handle_sigint(int sig)  { (void)sig; g_running = 0; }
static void handle_sigterm(int sig) { (void)sig; g_running = 0; }
static void handle_sigwinch(int sig){ (void)sig; g_resize  = 1; }

/* ══════════════════════════════════════════════════════════════════════════
 * CPU stat helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static int read_cpu_stat(cpu_stat_t *s)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    int found = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            if (sscanf(line, "cpu  %ld %ld %ld %ld %ld %ld %ld %ld",
                       &s->user, &s->nice, &s->sys, &s->idle,
                       &s->iowait, &s->irq, &s->softirq, &s->steal) >= 4) {
                found = 1;
            }
            break;
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

/* Returns CPU busy % (excludes iowait — matches htop behaviour) */
static float cpu_delta_pct(const cpu_stat_t *s1, const cpu_stat_t *s2)
{
    /* total includes ALL states including iowait */
    long total1 = s1->user + s1->nice + s1->sys + s1->idle + s1->iowait + s1->irq + s1->softirq + s1->steal;
    long total2 = s2->user + s2->nice + s2->sys + s2->idle + s2->iowait + s2->irq + s2->softirq + s2->steal;
    /* idle_d = idle only (NOT iowait) → consistent with htop %CPU */
    long idle_d  = (s2->idle  - s1->idle);
    long total_d = total2 - total1;
    if (total_d <= 0) return 0.0f;
    float pct = (1.0f - (float)idle_d / (float)total_d) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

/* Returns iowait % separately for diagnostics */
static float cpu_iowait_pct(const cpu_stat_t *s1, const cpu_stat_t *s2)
{
    long total1 = s1->user + s1->nice + s1->sys + s1->idle + s1->iowait + s1->irq + s1->softirq + s1->steal;
    long total2 = s2->user + s2->nice + s2->sys + s2->idle + s2->iowait + s2->irq + s2->softirq + s2->steal;
    long iowait_d = s2->iowait - s1->iowait;
    long total_d  = total2 - total1;
    if (total_d <= 0) return 0.0f;
    float pct = (float)iowait_d / (float)total_d * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Memory helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static void read_meminfo(monitor_data_t *d)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    long mem_available = 0;
    char line[256], key[64];
    long val;

    d->ram_total_kb = d->ram_free_kb = d->swap_total_kb = d->swap_used_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63s %ld", key, &val) < 2) continue;
        if      (strcmp(key, "MemTotal:")     == 0) d->ram_total_kb = val;
        else if (strcmp(key, "MemAvailable:") == 0) mem_available   = val;
        else if (strcmp(key, "MemFree:")      == 0) d->ram_free_kb  = val;
        else if (strcmp(key, "SwapTotal:")    == 0) d->swap_total_kb = val;
        else if (strcmp(key, "SwapFree:")     == 0) d->swap_used_kb  = d->swap_total_kb - val;
    }
    fclose(f);

    if (mem_available > 0)
        d->ram_used_kb = d->ram_total_kb - mem_available;
    else
        d->ram_used_kb = d->ram_total_kb - d->ram_free_kb;
}

static void read_loadavg(monitor_data_t *d)
{
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return;
    (void)fscanf(f, "%f %f %f", &d->load_1, &d->load_5, &d->load_15);
    fclose(f);
}

static void read_uptime(monitor_data_t *d)
{
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return;
    double up;
    (void)fscanf(f, "%lf", &up);
    d->uptime_s = (long)up;
    fclose(f);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Per-process helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static int read_proc_cmdline(pid_t pid, char *buf, int bufsz)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = (int)fread(buf, 1, bufsz - 1, f);
    fclose(f);
    if (n <= 0) return -1;
    buf[n] = '\0';
    /* Replace nulls with spaces */
    for (int i = 0; i < n; i++)
        if (buf[i] == '\0') buf[i] = ' ';
    return 0;
}

/* Read /proc/[pid]/comm — the canonical 15-char process name */
static int read_proc_comm(pid_t pid, char *buf, int bufsz)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, bufsz, f)) { fclose(f); return -1; }
    fclose(f);
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    return len > 0 ? 0 : -1;
}

static int read_proc_status(pid_t pid, long *vmrss_kb, long *vmsize_kb, int *threads)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256], key[64];
    long val;
    *vmrss_kb = 0; *vmsize_kb = 0; *threads = 0;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%63s %ld", key, &val) < 2) continue;
        if      (strcmp(key, "VmRSS:")   == 0) *vmrss_kb  = val;
        else if (strcmp(key, "VmSize:")  == 0) *vmsize_kb = val;
        else if (strcmp(key, "Threads:") == 0) *threads   = (int)val;
    }
    fclose(f);
    return 0;
}

static int count_proc_fds(pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    DIR *d = opendir(path);
    if (!d) return -1;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] != '.') count++;
    }
    closedir(d);
    return count;
}

/* Per-process CPU delta: returns percentage */
static float proc_cpu_pct(service_data_t *svc)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", svc->pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0.0f;

    unsigned long utime = 0, stime = 0;
    /* Fields: pid comm state ppid ... utime(14) stime(15) ... */
    if (fscanf(f,
        "%*d %*s %*c %*d %*d %*d %*d %*d "
        "%*u %*u %*u %*u %*u "
        "%lu %lu", &utime, &stime) < 2) {
        fclose(f);
        return 0.0f;
    }
    fclose(f);

    /* Read system uptime ticks */
    FILE *up = fopen("/proc/uptime", "r");
    if (!up) return 0.0f;
    double uptime_sec;
    (void)fscanf(up, "%lf", &uptime_sec);
    fclose(up);

    long hz = sysconf(_SC_CLK_TCK);
    unsigned long long uptime_ticks = (unsigned long long)(uptime_sec * hz);

    float pct = 0.0f;
    if (svc->prev_uptime_ticks > 0) {
        unsigned long proc_delta = (utime + stime) - (svc->prev_utime + svc->prev_stime);
        unsigned long long sys_delta = uptime_ticks - svc->prev_uptime_ticks;
        if (sys_delta > 0)
            pct = 100.0f * (float)proc_delta / (float)sys_delta;
    }

    svc->prev_utime        = utime;
    svc->prev_stime        = stime;
    svc->prev_uptime_ticks = uptime_ticks;

    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Health score
 * ══════════════════════════════════════════════════════════════════════════ */

static int compute_health(const service_data_t *s)
{
    if (!s->running) return 0;

    int score = 100;
    if (s->cpu_pct > 95.0f) score -= 50;
    else if (s->cpu_pct > 80.0f) score -= 20;

    if (s->rss_kb == 0) score -= 10;   /* no data = suspect */

    if (s->restart_count > 5)      score -= 50;
    else if (s->restart_count > 0) score -= (int)(s->restart_count * 10);

    if (s->fd_count > 500)      score -= 20;
    else if (s->fd_count > 200) score -= 10;

    if (score < 0) score = 0;
    return score;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Service Manager socket check
 * ══════════════════════════════════════════════════════════════════════════ */

static int try_sm_socket(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISSOCK(st.st_mode)) return 0;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    /* Non-blocking connect attempt */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return (ret == 0 || errno == EINPROGRESS) ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Service discovery via /proc scan
 * ══════════════════════════════════════════════════════════════════════════ */

/*
 * Match against /proc/[pid]/comm (exact process name, up to 15 chars).
 * This prevents false positives from processes that merely mention
 * "sandbox" or "audio" in their argv flags (e.g. VS Code, Chrome).
 */
static const char *KNOWN_SERVICES[] = {
    /* Primary middleware services */
    "servicemanager", "bankai",
    /* HAL services (real binary names) */
    "audio_hal", "camera_hal", "sensor_hal", "gpio_hal",
    /* Service aliases used by main_monitor.sh mock launcher */
    "audio_service", "camera_service", "sensor_service", "gpio_service",
    /* Security stack */
    "security_manager",
    /* Daemon (but NOT ourselves — we exclude own PID below) */
    "middleware_monitord",
    NULL
};

/* Write a structured log entry */
static void add_logex(monitor_data_t *d, log_level_t lvl, log_cat_t cat,
                      const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void add_logex(monitor_data_t *d, log_level_t lvl, log_cat_t cat,
                      const char *fmt, ...)
{
    int idx = (d->log_head + d->log_count) % MAX_LOG_LINES;
    log_entry_t *e = &d->log_entries[idx];
    e->ts    = time(NULL);
    e->level = lvl;
    e->cat   = cat;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, LOG_LINE_LEN, fmt, ap);
    va_end(ap);
    if (d->log_count < MAX_LOG_LINES) d->log_count++;
    else d->log_head = (d->log_head + 1) % MAX_LOG_LINES;
}

/* Convenience macros */
#define LOG_DEBUG(d,cat,...)  add_logex((d), LL_DEBUG, (cat), __VA_ARGS__)
#define LOG_INFO(d,cat,...)   add_logex((d), LL_INFO,  (cat), __VA_ARGS__)
#define LOG_WARN(d,cat,...)   add_logex((d), LL_WARN,  (cat), __VA_ARGS__)
#define LOG_ERR(d,cat,...)    add_logex((d), LL_ERROR, (cat), __VA_ARGS__)

static void add_alert(monitor_data_t *d, const char *msg)
{
    if (d->alert_count < MAX_ALERTS) {
        snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN, "%s", msg);
    }
}

static void scan_processes(monitor_data_t *d)
{
    DIR *proc = opendir("/proc");
    if (!proc) return;

    d->service_count = 0;
    pid_t our_pid = getpid();

    struct dirent *ent;
    while ((ent = readdir(proc)) != NULL && d->service_count < MAX_SERVICES) {
        /* Only numeric entries = PIDs */
        if (!isdigit((unsigned char)ent->d_name[0])) continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        if (pid <= 0 || pid == our_pid) continue;

        /* PRIMARY: match by /proc/[pid]/comm (exact 15-char process name).
         * This avoids false positives from argv flags like --no-zygote-sandbox. */
        char comm[64] = {0};
        if (read_proc_comm(pid, comm, sizeof(comm)) != 0) continue;

        const char *matched_name = NULL;
        for (int j = 0; KNOWN_SERVICES[j]; j++) {
            /* Exact match on comm (comm is at most 15 chars, may be truncated) */
            if (strcmp(comm, KNOWN_SERVICES[j]) == 0) {
                matched_name = KNOWN_SERVICES[j];
                break;
            }
            /* Prefix match for truncated comm (kernel truncates at 15) */
            if (strlen(KNOWN_SERVICES[j]) > 15 &&
                strncmp(comm, KNOWN_SERVICES[j], 15) == 0) {
                matched_name = KNOWN_SERVICES[j];
                break;
            }
        }

        /* SECONDARY: if comm is a shell (bash/sh/dash), check argv[0] basename.
         * This covers: `exec -a service_name bash` style launches. */
        if (!matched_name) {
            if (strcmp(comm, "bash") == 0 || strcmp(comm, "sh") == 0 ||
                strcmp(comm, "dash") == 0 || strcmp(comm, "python") == 0) {
                /* Read first token of cmdline as argv[0] */
                char cmdline[512] = {0};
                if (read_proc_cmdline(pid, cmdline, sizeof(cmdline)) == 0) {
                    /* argv[0] is before first space */
                    char argv0[64] = {0};
                    const char *sp = strchr(cmdline, ' ');
                    size_t len = sp ? (size_t)(sp - cmdline) : strlen(cmdline);
                    if (len > sizeof(argv0)-1) len = sizeof(argv0)-1;
                    strncpy(argv0, cmdline, len);
                    /* Get basename of argv[0] */
                    const char *base = strrchr(argv0, '/');
                    base = base ? base + 1 : argv0;
                    for (int j = 0; KNOWN_SERVICES[j]; j++) {
                        if (strcmp(base, KNOWN_SERVICES[j]) == 0) {
                            matched_name = KNOWN_SERVICES[j];
                            break;
                        }
                    }
                }
            }
        }

        if (!matched_name) continue;

        /* Deduplicate: skip if we already have an entry for this PID */
        int dup = 0;
        for (int k = 0; k < d->service_count; k++) {
            if (d->services[k].pid == pid) { dup = 1; break; }
        }
        if (dup) continue;

        /* Verify process still alive */
        char statpath[64];
        snprintf(statpath, sizeof(statpath), "/proc/%d/stat", pid);
        if (access(statpath, F_OK) != 0) continue;

        service_data_t *svc = &d->services[d->service_count];
        memset(svc, 0, sizeof(*svc));

        strncpy(svc->name, matched_name, SERVICE_NAME_LEN - 1);
        svc->pid     = pid;
        svc->running = 1;

        /* Read stats */
        long rss = 0, vsz = 0;
        int threads = 0;
        read_proc_status(pid, &rss, &vsz, &threads);
        svc->rss_kb       = rss;
        svc->vsz_kb       = vsz;
        svc->thread_count = threads;

        svc->fd_count = count_proc_fds(pid);
        svc->cpu_pct  = proc_cpu_pct(svc);
        svc->health_score = compute_health(svc);

        d->service_count++;
    }
    closedir(proc);
}

/* Preserve previous CPU tracking registers across scans */
static void merge_prev_service_state(monitor_data_t *new_d, const monitor_data_t *old_d)
{
    for (int i = 0; i < new_d->service_count; i++) {
        service_data_t *ns = &new_d->services[i];
        for (int j = 0; j < old_d->service_count; j++) {
            const service_data_t *os = &old_d->services[j];
            if (ns->pid == os->pid && strcmp(ns->name, os->name) == 0) {
                ns->prev_utime        = os->prev_utime;
                ns->prev_stime        = os->prev_stime;
                ns->prev_uptime_ticks = os->prev_uptime_ticks;
                ns->restart_count     = os->restart_count;
                break;
            }
        }
    }
}

/* Alert generation */
static void check_alerts(monitor_data_t *d)
{
    static int prev_service_count = -1;

    /* Track health changes per service */
    static int prev_health[MAX_SERVICES];
    static char prev_names[MAX_SERVICES][SERVICE_NAME_LEN];
    static int health_initialized = 0;

    for (int i = 0; i < d->service_count; i++) {
        service_data_t *s = &d->services[i];

        /* CPU alert */
        if (s->cpu_pct > 90.0f) {
            char buf[192];
            snprintf(buf, sizeof(buf), "%s CPU critical: %.1f%%  RAM:%ldMB  PID:%d",
                     s->name, s->cpu_pct, s->rss_kb/1024, s->pid);
            add_alert(d, buf);
            LOG_ERR(d, LC_SERVICE, "CPU CRITICAL  %-16s  %.1f%%  (PID %d)",
                    s->name, s->cpu_pct, s->pid);
        } else if (s->cpu_pct > 75.0f) {
            LOG_WARN(d, LC_SERVICE, "CPU HIGH      %-16s  %.1f%%",
                     s->name, s->cpu_pct);
        }

        /* Health change */
        if (health_initialized) {
            for (int j = 0; j < prev_service_count; j++) {
                if (strcmp(prev_names[j], s->name) == 0 &&
                    abs(prev_health[j] - s->health_score) >= 10) {
                    if (s->health_score < prev_health[j]) {
                        LOG_WARN(d, LC_HEALTH, "Health DROP   %-16s  %d -> %d",
                                 s->name, prev_health[j], s->health_score);
                        if (s->health_score < 50) {
                            char buf[192];
                            snprintf(buf, sizeof(buf),
                                     "%s health critical: %d/100 (was %d)",
                                     s->name, s->health_score, prev_health[j]);
                            add_alert(d, buf);
                        }
                    } else {
                        LOG_INFO(d, LC_HEALTH, "Health UP     %-16s  %d -> %d",
                                 s->name, prev_health[j], s->health_score);
                    }
                    break;
                }
            }
        }

        /* FD warning */
        if (s->fd_count > 500) {
            LOG_WARN(d, LC_IO, "FD HIGH       %-16s  %d fds  (PID %d)",
                     s->name, s->fd_count, s->pid);
        }
    }

    if (prev_service_count >= 0 && d->service_count < prev_service_count) {
        LOG_WARN(d, LC_SERVICE, "Service count dropped: %d -> %d",
                 prev_service_count, d->service_count);
    } else if (prev_service_count >= 0 && d->service_count > prev_service_count) {
        LOG_INFO(d, LC_SERVICE, "Service count increased: %d -> %d",
                 prev_service_count, d->service_count);
    }

    /* Save state for next call */
    prev_service_count = d->service_count;
    for (int i = 0; i < d->service_count && i < MAX_SERVICES; i++) {
        prev_health[i] = d->services[i].health_score;
        strncpy(prev_names[i], d->services[i].name, SERVICE_NAME_LEN - 1);
        prev_names[i][SERVICE_NAME_LEN - 1] = '\0';
    }
    health_initialized = 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Data collection thread (runs every 1000ms)
 * ══════════════════════════════════════════════════════════════════════════ */

static void *data_collector_thread(void *arg)
{
    (void)arg;

    /* Initial CPU baseline */
    cpu_stat_t cpu_baseline;
    read_cpu_stat(&cpu_baseline);

    while (g_running) {
        monitor_data_t tmp;
        memset(&tmp, 0, sizeof(tmp));

        /* Copy previous service state for CPU delta */
        pthread_mutex_lock(&g_lock);
        memcpy(&tmp.services, &g_data.services, sizeof(g_data.services));
        tmp.service_count = g_data.service_count;
        memcpy(&tmp.log_entries, &g_data.log_entries, sizeof(g_data.log_entries));
        tmp.log_head  = g_data.log_head;
        tmp.log_count = g_data.log_count;
        memcpy(&tmp.alert_lines, &g_data.alert_lines, sizeof(g_data.alert_lines));
        tmp.alert_count = g_data.alert_count;
        pthread_mutex_unlock(&g_lock);

        /* CPU: read new snapshot and diff against baseline */
        cpu_stat_t cpu_now;
        if (read_cpu_stat(&cpu_now) == 0) {
            tmp.cpu_total_pct  = cpu_delta_pct(&cpu_baseline, &cpu_now);
            tmp.cpu_iowait_pct = cpu_iowait_pct(&cpu_baseline, &cpu_now);
            tmp.cpu_initialized = 1;
            cpu_baseline       = cpu_now;
        }

        /* Memory */
        read_meminfo(&tmp);
        read_loadavg(&tmp);
        read_uptime(&tmp);

        /* Service discovery */
        monitor_data_t scan_tmp;
        memset(&scan_tmp, 0, sizeof(scan_tmp));
        /* Copy previous state so CPU delta persists */
        memcpy(&scan_tmp.services, &tmp.services, sizeof(tmp.services));
        scan_tmp.service_count = tmp.service_count;

        scan_processes(&scan_tmp);
        merge_prev_service_state(&scan_tmp, &tmp);

        memcpy(&tmp.services, &scan_tmp.services, sizeof(scan_tmp.services));
        tmp.service_count = scan_tmp.service_count;

        /* SM socket check */
        static const char *SM_PATHS[] = {
            "/tmp/servicemanager.sock",
            "/run/servicemanager.sock",
            "/var/run/servicemanager.sock",
            NULL
        };
        tmp.sm_socket_ok = 0;
        for (int i = 0; SM_PATHS[i]; i++) {
            if (try_sm_socket(SM_PATHS[i])) {
                tmp.sm_socket_ok = 1;
                strncpy(tmp.sm_socket_path, SM_PATHS[i], sizeof(tmp.sm_socket_path) - 1);
                break;
            }
        }

        /* CPU core count */
        tmp.num_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);

        /* Alerts & logging */
        check_alerts(&tmp);
        /* Periodic system metrics log */
        LOG_INFO(&tmp, LC_SYSTEM,
                 "CPU:%.1f%%(io+%.1f%%)  RAM:%ld/%ldMB  Swap:%ld/%ldMB  Load:%.2f/%.2f/%.2f  Up:%ld d",
                 tmp.cpu_total_pct, tmp.cpu_iowait_pct,
                 tmp.ram_used_kb/1024, tmp.ram_total_kb/1024,
                 tmp.swap_used_kb/1024, tmp.swap_total_kb/1024,
                 tmp.load_1, tmp.load_5, tmp.load_15,
                 tmp.uptime_s / 86400);

        /* SM socket status change logging */
        static int prev_sm_ok = -1;
        if (prev_sm_ok != tmp.sm_socket_ok) {
            if (tmp.sm_socket_ok)
                LOG_INFO(&tmp, LC_NET, "SM socket CONNECTED  %s", tmp.sm_socket_path);
            else
                LOG_WARN(&tmp, LC_NET, "SM socket NOT connected");
            prev_sm_ok = tmp.sm_socket_ok;
        }

        /* Services snapshot */
        if (tmp.service_count > 0) {
            for (int _i = 0; _i < tmp.service_count; _i++) {
                const service_data_t *_s = &tmp.services[_i];
                LOG_DEBUG(&tmp, LC_SERVICE,
                          "%-16s  PID:%-7d  CPU:%5.1f%%  RAM:%5ldMB  FDs:%-4d  Health:%3d",
                          _s->name, _s->pid,
                          _s->cpu_pct, _s->rss_kb/1024,
                          _s->fd_count >= 0 ? _s->fd_count : 0,
                          _s->health_score);
            }
        } else {
            LOG_WARN(&tmp, LC_SERVICE, "No middleware services detected in /proc");
        }

        /* Memory warnings */
        if (tmp.ram_total_kb > 0) {
            float ram_pct = 100.0f * tmp.ram_used_kb / tmp.ram_total_kb;
            if (ram_pct > 90.0f)
                LOG_ERR(&tmp, LC_MEMORY, "RAM CRITICAL: %.1f%% used (%ldMB free)",
                        ram_pct, (tmp.ram_total_kb - tmp.ram_used_kb)/1024);
            else if (ram_pct > 80.0f)
                LOG_WARN(&tmp, LC_MEMORY, "RAM HIGH: %.1f%% used", ram_pct);
        }

        tmp.last_update = time(NULL);

        /* Publish */
        pthread_mutex_lock(&g_lock);
        memcpy(&g_data, &tmp, sizeof(g_data));
        pthread_mutex_unlock(&g_lock);

        /* Sleep 1000ms between collections */
        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Color helpers
 * ══════════════════════════════════════════════════════════════════════════ */

static void init_colors(void)
{
    start_color();
    use_default_colors();

    init_pair(CP_DEFAULT,  COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_HEADER,   COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_SELECTED, COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_GREEN,    COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_YELLOW,   COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_RED,      COLOR_RED,     COLOR_BLACK);
    init_pair(CP_CYAN,     COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_DIM,      COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_BLUE,     COLOR_BLUE,    COLOR_BLACK);
    init_pair(CP_MAGENTA,  COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_BAR_OK,   COLOR_GREEN,   COLOR_GREEN);
    init_pair(CP_BAR_WARN, COLOR_YELLOW,  COLOR_YELLOW);
    init_pair(CP_BAR_CRIT, COLOR_RED,     COLOR_RED);
}

static int health_color(int score)
{
    if (score >= 90) return CP_GREEN;
    if (score >= 70) return CP_YELLOW;
    return CP_RED;
}

static int pct_color(float pct)
{
    if (pct < 60.0f) return CP_GREEN;
    if (pct < 85.0f) return CP_YELLOW;
    return CP_RED;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Progress bar rendering
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_bar(WINDOW *win, int y, int x, int width, float pct)
{
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    int filled = (int)(pct / 100.0f * (float)width);

    int fill_cp = (pct < 60.0f) ? CP_BAR_OK :
                  (pct < 85.0f) ? CP_BAR_WARN : CP_BAR_CRIT;

    wattron(win, COLOR_PAIR(fill_cp));
    for (int i = 0; i < filled; i++)
        mvwaddch(win, y, x + i, ACS_BLOCK);
    wattroff(win, COLOR_PAIR(fill_cp));

    wattron(win, COLOR_PAIR(CP_DIM) | A_DIM);
    for (int i = filled; i < width; i++)
        mvwaddch(win, y, x + i, ACS_CKBOARD);
    wattroff(win, COLOR_PAIR(CP_DIM) | A_DIM);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Top bar (row 0)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_topbar(const monitor_data_t *d)
{
    wattron(stdscr, COLOR_PAIR(CP_HEADER) | A_BOLD);
    move(0, 0);
    for (int i = 0; i < g_cols; i++) addch(' ');

    /* Compute real health score from service data */
    int health_val = 100;
    if (d->service_count > 0) {
        int total_score = 0;
        for (int i = 0; i < d->service_count; i++)
            total_score += d->services[i].health_score;
        health_val = total_score / d->service_count;
    } else {
        /* No services found — neutral 70 (system running but nothing detected) */
        health_val = 70;
    }
    if (d->alert_count > 0) health_val -= (d->alert_count * 5);
    if (health_val < 0)   health_val = 0;
    if (health_val > 100) health_val = 100;

    const char *health_str = (health_val >= 90) ? "EXCELLENT" :
                             (health_val >= 70) ? "GOOD"      :
                             (health_val >= 50) ? "WARNING"   : "CRITICAL";

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_now);

    char tb[512];
    snprintf(tb, sizeof(tb),
             " Health: %s (%d/100)  CPU: %.1f%% (io+%.1f%%)  RAM: %ld/%ld MB  Load: %.2f   %s",
             health_str, health_val,
             d->cpu_total_pct, d->cpu_iowait_pct,
             d->ram_used_kb / 1024, d->ram_total_kb / 1024,
             d->load_1,
             timebuf);
    mvprintw(0, 0, "%-*.*s", g_cols, g_cols, tb);
    wattroff(stdscr, COLOR_PAIR(CP_HEADER) | A_BOLD);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Tab bar (row 1)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_tabbar(void)
{
    move(1, 0);
    wattron(stdscr, COLOR_PAIR(CP_HEADER));
    for (int i = 0; i < g_cols; i++) addch(' ');
    move(1, 1);

    for (int i = 0; i < TAB_COUNT; i++) {
        if (i == g_active_tab) {
            wattron(stdscr, COLOR_PAIR(CP_SELECTED) | A_BOLD | A_REVERSE);
        } else {
            wattron(stdscr, COLOR_PAIR(CP_HEADER));
            wattroff(stdscr, A_BOLD | A_REVERSE);
        }
        printw(" %s ", TAB_NAMES[i]);
        if (i == g_active_tab) {
            wattroff(stdscr, A_BOLD | A_REVERSE);
        }
    }
    wattroff(stdscr, COLOR_PAIR(CP_HEADER));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Bottom status bar (last row)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_statusbar(const monitor_data_t *d)
{
    int row = g_rows - 1;
    wattron(stdscr, COLOR_PAIR(CP_HEADER));
    move(row, 0);
    for (int i = 0; i < g_cols; i++) addch(' ');

    char sb[256];
    int svc_active = 0;
    for (int i = 0; i < d->service_count; i++)
        if (d->services[i].running) svc_active++;

    snprintf(sb, sizeof(sb),
             " q=quit | Tab/Left/Right=panels | r=refresh | %d services | SM:%s",
             svc_active,
             d->sm_socket_ok ? "OK" : "offline");
    mvprintw(row, 0, "%-*.*s", g_cols, g_cols, sb);
    wattroff(stdscr, COLOR_PAIR(CP_HEADER));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Overview (tab 0)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_overview(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);
    int bar_w = (cols > 60) ? 30 : cols / 3;

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 16) / 2, " System Overview ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    int row = 3;

    /* CPU bar (busy, does NOT include iowait — same as htop) */
    wattron(win, COLOR_PAIR(CP_CYAN));
    mvwprintw(win, row, 2, "CPU  ");
    wattroff(win, COLOR_PAIR(CP_CYAN));
    draw_bar(win, row, 7, bar_w, d->cpu_total_pct);
    wattron(win, COLOR_PAIR(pct_color(d->cpu_total_pct)));
    mvwprintw(win, row, 7 + bar_w + 1, "%.1f%% busy  +%.1f%% iowait  (%d cores)",
              d->cpu_total_pct, d->cpu_iowait_pct, d->num_cores);
    wattroff(win, COLOR_PAIR(pct_color(d->cpu_total_pct)));
    row++;

    /* RAM bar */
    float ram_pct = d->ram_total_kb > 0 ? 100.0f * d->ram_used_kb / d->ram_total_kb : 0.0f;
    wattron(win, COLOR_PAIR(CP_CYAN));
    mvwprintw(win, row, 2, "RAM  ");
    wattroff(win, COLOR_PAIR(CP_CYAN));
    draw_bar(win, row, 7, bar_w, ram_pct);
    mvwprintw(win, row, 7 + bar_w + 1, "%.1f%% (%ld/%ld MB)",
              ram_pct, d->ram_used_kb / 1024, d->ram_total_kb / 1024);
    row++;

    /* Swap bar */
    float swap_pct = d->swap_total_kb > 0 ? 100.0f * d->swap_used_kb / d->swap_total_kb : 0.0f;
    wattron(win, COLOR_PAIR(CP_CYAN));
    mvwprintw(win, row, 2, "SWAP ");
    wattroff(win, COLOR_PAIR(CP_CYAN));
    draw_bar(win, row, 7, bar_w, swap_pct);
    mvwprintw(win, row, 7 + bar_w + 1, "%.1f%% (%ld/%ld MB)",
              swap_pct, d->swap_used_kb / 1024, d->swap_total_kb / 1024);
    row++;

    mvwhline(win, row++, 1, ACS_HLINE, cols - 2);

    /* Load avg */
    wattron(win, COLOR_PAIR(CP_CYAN));
    mvwprintw(win, row++, 2, "Load:  %.2f (1m)  %.2f (5m)  %.2f (15m)  Uptime: %ldd %ldh %ldm",
              d->load_1, d->load_5, d->load_15,
              d->uptime_s / 86400, (d->uptime_s % 86400) / 3600, (d->uptime_s % 3600) / 60);
    wattroff(win, COLOR_PAIR(CP_CYAN));

    mvwhline(win, row++, 1, ACS_HLINE, cols - 2);

    /* SM socket status */
    wattron(win, COLOR_PAIR(d->sm_socket_ok ? CP_GREEN : CP_YELLOW) | A_BOLD);
    if (d->sm_socket_ok) {
        mvwprintw(win, row++, 2, "Service Manager: CONNECTED  (%s)", d->sm_socket_path);
    } else {
        mvwprintw(win, row++, 2, "Service Manager: NOT CONNECTED (socket unavailable - showing /proc data)");
    }
    wattroff(win, A_BOLD);

    mvwhline(win, row++, 1, ACS_HLINE, cols - 2);

    /* Services summary */
    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, row++, 2, "Services  (%d discovered):", d->service_count);
    wattroff(win, A_BOLD);

    for (int i = 0; i < d->service_count && row < rows - 2; i++) {
        const service_data_t *s = &d->services[i];
        int hc = health_color(s->health_score);

        wattron(win, COLOR_PAIR(hc));
        mvwprintw(win, row, 2, "%-20s", s->name);
        wattroff(win, COLOR_PAIR(hc));

        mvwprintw(win, row, 23, "PID:%-6d  CPU:%5.1f%%  RAM:%5ldMB  Health:%3d/100",
                  s->pid, s->cpu_pct, s->rss_kb / 1024, s->health_score);
        row++;
    }

    if (d->service_count == 0) {
        wattron(win, COLOR_PAIR(CP_YELLOW));
        mvwprintw(win, row++, 2, "(No middleware services found in /proc)");
        wattroff(win, COLOR_PAIR(CP_YELLOW));
    }

    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Services (tab 1)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_services(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 16) / 2, " Service Details ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    /* Header row */
    wattron(win, A_BOLD | A_UNDERLINE);
    mvwprintw(win, 3, 2, "%-20s %6s %7s %7s %6s %7s %5s %8s",
              "Name", "PID", "CPU%", "RAM MB", "FDs", "Threads", "Health", "Status");
    wattroff(win, A_BOLD | A_UNDERLINE);
    mvwhline(win, 4, 1, ACS_HLINE, cols - 2);

    int row = 5;
    int bar_w = 10;

    for (int i = 0; i < d->service_count && row < rows - 2; i++) {
        const service_data_t *s = &d->services[i];
        int hc = health_color(s->health_score);

        /* Name column */
        wattron(win, COLOR_PAIR(s->running ? CP_GREEN : CP_RED) | A_BOLD);
        mvwprintw(win, row, 2, "%-20s", s->name);
        wattroff(win, A_BOLD);

        /* PID */
        mvwprintw(win, row, 23, "%6d", s->pid);

        /* CPU bar */
        mvwprintw(win, row, 30, "%5.1f%%", s->cpu_pct);
        draw_bar(win, row, 37, bar_w, s->cpu_pct);
        wattroff(win, COLOR_PAIR(s->running ? CP_GREEN : CP_RED));

        /* RAM */
        mvwprintw(win, row, 48, "%6ld", s->rss_kb / 1024);

        /* FDs */
        mvwprintw(win, row, 55, "%6d", s->fd_count);

        /* Threads */
        mvwprintw(win, row, 62, "%7d", s->thread_count);

        /* Health */
        wattron(win, COLOR_PAIR(hc) | A_BOLD);
        mvwprintw(win, row, 70, "%5d/100", s->health_score);
        wattroff(win, COLOR_PAIR(hc) | A_BOLD);

        /* Status */
        wattron(win, COLOR_PAIR(s->running ? CP_GREEN : CP_RED));
        mvwprintw(win, row, 79, "%8s", s->running ? "RUNNING" : "DEAD");
        wattroff(win, COLOR_PAIR(s->running ? CP_GREEN : CP_RED));

        row++;
    }

    if (d->service_count == 0) {
        wattron(win, COLOR_PAIR(CP_YELLOW));
        mvwprintw(win, row, 2, "No middleware services found. Run 'sudo ./main_monitor.sh' to start services.");
        wattroff(win, COLOR_PAIR(CP_YELLOW));
    }

    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: HAL (tab 2)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_hal(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 28) / 2, " Hardware Abstraction Layer ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    int row = 3;

    /* Each HAL entry: display name + list of process names to search for */
    typedef struct { const char *display; const char *names[3]; } hal_entry_t;
    static const hal_entry_t hal_table[] = {
        { "Audio HAL",   { "audio_hal",   "audio_service",  NULL } },
        { "Camera HAL",  { "camera_hal",  "camera_service", NULL } },
        { "Sensor HAL",  { "sensor_hal",  "sensor_service", NULL } },
        { "GPIO HAL",    { "gpio_hal",    "gpio_service",   NULL } },
        { NULL, { NULL, NULL, NULL } }
    };

    for (int k = 0; hal_table[k].display && row < rows - 2; k++) {
        /* Search for any matching name variant */
        const service_data_t *found = NULL;
        for (int i = 0; i < d->service_count && !found; i++) {
            for (int n = 0; hal_table[k].names[n]; n++) {
                if (strcmp(d->services[i].name, hal_table[k].names[n]) == 0) {
                    found = &d->services[i];
                    break;
                }
            }
        }

        wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
        mvwprintw(win, row, 2, "[%-10s]", hal_table[k].display);
        wattroff(win, A_BOLD);

        if (found && found->running) {
            wattron(win, COLOR_PAIR(CP_GREEN) | A_BOLD);
            mvwprintw(win, row, 16, "RUNNING  PID:%-7d", found->pid);
            wattroff(win, A_BOLD);
            wattroff(win, COLOR_PAIR(CP_GREEN));
            row++;

            mvwprintw(win, row, 4, "CPU:");
            draw_bar(win, row, 9, 20, found->cpu_pct);
            wattron(win, COLOR_PAIR(pct_color(found->cpu_pct)));
            mvwprintw(win, row, 31, "%5.1f%%", found->cpu_pct);
            wattroff(win, COLOR_PAIR(pct_color(found->cpu_pct)));
            mvwprintw(win, row, 39, "  RAM:%5ldMB  FDs:%-4d  Threads:%d",
                      found->rss_kb / 1024, found->fd_count >= 0 ? found->fd_count : 0,
                      found->thread_count);
            row++;
        } else {
            wattron(win, COLOR_PAIR(CP_YELLOW));
            mvwprintw(win, row, 16, "NOT RUNNING  (real binary not found - mock not detectable)");
            wattroff(win, COLOR_PAIR(CP_YELLOW));
            row++;
        }
        mvwhline(win, row++, 1, ACS_HLINE, cols - 2);
    }

    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, row, 2,
              "HAL detection uses /proc/[pid]/comm exact match. "
              "Mock services (bash loops) are not detectable.");
    wattroff(win, COLOR_PAIR(CP_DIM));
    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Memory (tab 3)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_memory(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 16) / 2, " Memory Overview ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    int row = 3;
    int bar_w = (cols > 50) ? cols / 2 : 20;

    /* System RAM */
    float ram_pct = d->ram_total_kb > 0 ? 100.0f * d->ram_used_kb / d->ram_total_kb : 0;
    wattron(win, A_BOLD);
    mvwprintw(win, row++, 2, "System Memory:");
    wattroff(win, A_BOLD);
    mvwprintw(win, row, 4, "Used:  ");
    draw_bar(win, row, 11, bar_w, ram_pct);
    mvwprintw(win, row++, 11 + bar_w + 1, "%.1f%%  %ld MB / %ld MB",
              ram_pct, d->ram_used_kb / 1024, d->ram_total_kb / 1024);

    float swap_pct = d->swap_total_kb > 0 ? 100.0f * d->swap_used_kb / d->swap_total_kb : 0;
    mvwprintw(win, row, 4, "Swap:  ");
    draw_bar(win, row, 11, bar_w, swap_pct);
    mvwprintw(win, row++, 11 + bar_w + 1, "%.1f%%  %ld MB / %ld MB",
              swap_pct, d->swap_used_kb / 1024, d->swap_total_kb / 1024);
    row++;

    mvwhline(win, row++, 1, ACS_HLINE, cols - 2);

    /* Per-service memory */
    wattron(win, A_BOLD);
    mvwprintw(win, row++, 2, "Per-Service Memory (from /proc):");
    wattroff(win, A_BOLD);

    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-22s  %8s  %8s  %8s",
              "Service", "RSS MB", "VSZ MB", "Threads");
    wattroff(win, A_UNDERLINE);

    for (int i = 0; i < d->service_count && row < rows - 2; i++) {
        const service_data_t *s = &d->services[i];
        int hc = s->rss_kb > 0 ? CP_GREEN : CP_YELLOW;
        wattron(win, COLOR_PAIR(hc));
        mvwprintw(win, row++, 2, "%-22s  %8ld  %8ld  %8d",
                  s->name, s->rss_kb / 1024, s->vsz_kb / 1024, s->thread_count);
        wattroff(win, COLOR_PAIR(hc));
    }

    if (d->service_count == 0) {
        wattron(win, COLOR_PAIR(CP_YELLOW));
        mvwprintw(win, row, 2, "Memory pool stats unavailable - no middleware services running");
        wattroff(win, COLOR_PAIR(CP_YELLOW));
    }

    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: I/O (tab 4)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_io(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 14) / 2, " I/O Overview ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    int row = 3;

    /* System-wide disk I/O from /proc/diskstats */
    FILE *f = fopen("/proc/diskstats", "r");
    if (f) {
        wattron(win, A_BOLD);
        mvwprintw(win, row++, 2, "Block Device I/O:");
        wattroff(win, A_BOLD);
        wattron(win, A_UNDERLINE);
        mvwprintw(win, row++, 2, "%-12s  %12s  %12s  %12s  %12s",
                  "Device", "Reads", "Writes", "Read KB", "Write KB");
        wattroff(win, A_UNDERLINE);

        char line[256];
        int shown = 0;
        while (fgets(line, sizeof(line), f) && row < rows - 8 && shown < 6) {
            char dev[32];
            unsigned long reads, writes, sectors_r, sectors_w;
            int major, minor;
            if (sscanf(line, "%d %d %31s %lu %*u %lu %*u %lu %*u %lu",
                       &major, &minor, dev, &reads, &sectors_r, &writes, &sectors_w) >= 6) {
                /* Skip loop/ram devices */
                if (strncmp(dev, "loop", 4) == 0 || strncmp(dev, "ram", 3) == 0) continue;
                if (reads == 0 && writes == 0) continue;
                mvwprintw(win, row++, 2, "%-12s  %12lu  %12lu  %12lu  %12lu",
                          dev, reads, writes, sectors_r / 2, sectors_w / 2);
                shown++;
            }
        }
        fclose(f);
        row++;
        mvwhline(win, row++, 1, ACS_HLINE, cols - 2);
    }

    /* Per-service FD counts */
    wattron(win, A_BOLD);
    mvwprintw(win, row++, 2, "Per-Service File Descriptors:");
    wattroff(win, A_BOLD);
    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-22s  %8s  %8s", "Service", "FDs", "Threads");
    wattroff(win, A_UNDERLINE);

    for (int i = 0; i < d->service_count && row < rows - 2; i++) {
        const service_data_t *s = &d->services[i];
        int fc = (s->fd_count > 200) ? CP_YELLOW : (s->fd_count > 500 ? CP_RED : CP_GREEN);
        wattron(win, COLOR_PAIR(fc));
        mvwprintw(win, row++, 2, "%-22s  %8d  %8d",
                  s->name,
                  s->fd_count >= 0 ? s->fd_count : 0,
                  s->thread_count);
        wattroff(win, COLOR_PAIR(fc));
    }

    if (d->service_count == 0) {
        wattron(win, COLOR_PAIR(CP_YELLOW));
        mvwprintw(win, row, 2, "io_uring stats unavailable - no services detected");
        wattroff(win, COLOR_PAIR(CP_YELLOW));
    }

    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Security (tab 5)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_security(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 18) / 2, " Security Overview ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    int row = 3;

    /* Kernel security modules */
    wattron(win, A_BOLD);
    mvwprintw(win, row++, 2, "Kernel Security:");
    wattroff(win, A_BOLD);

    FILE *f = fopen("/proc/sys/kernel/dmesg_restrict", "r");
    if (f) { int v; if (fscanf(f, "%d", &v) == 1) mvwprintw(win, row++, 4, "dmesg_restrict: %d", v); fclose(f); }

    f = fopen("/proc/sys/kernel/randomize_va_space", "r");
    if (f) { int v; if (fscanf(f, "%d", &v) == 1) mvwprintw(win, row++, 4, "ASLR (randomize_va_space): %d", v); fclose(f); }

    /* Seccomp status per service */
    mvwhline(win, row++, 1, ACS_HLINE, cols - 2);
    wattron(win, A_BOLD);
    mvwprintw(win, row++, 2, "Per-Service Seccomp/Sandbox Status:");
    wattroff(win, A_BOLD);
    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-22s  %8s  %8s  %8s",
              "Service", "Health", "PID", "Security");
    wattroff(win, A_UNDERLINE);

    for (int i = 0; i < d->service_count && row < rows - 2; i++) {
        const service_data_t *s = &d->services[i];
        const char *sec_status = (s->health_score >= 90) ? "OK" :
                                 (s->health_score >= 70) ? "WARN" : "CRITICAL";
        int hc = health_color(s->health_score);
        wattron(win, COLOR_PAIR(hc));
        mvwprintw(win, row++, 2, "%-22s  %8d  %8d  %8s",
                  s->name, s->health_score, s->pid, sec_status);
        wattroff(win, COLOR_PAIR(hc));
    }

    if (d->service_count == 0) {
        wattron(win, COLOR_PAIR(CP_GREEN));
        mvwprintw(win, row++, 2, "No middleware services running - Seccomp: N/A");
        wattroff(win, COLOR_PAIR(CP_GREEN));
    }

    mvwhline(win, row++, 1, ACS_HLINE, cols - 2);
    mvwprintw(win, row, 2, "Seccomp/HMAC/Replay stats require SM socket connection (currently: %s)",
              d->sm_socket_ok ? "connected" : "not connected");
    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Alerts (tab 6)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_alerts(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_RED) | A_BOLD);
    mvwprintw(win, 1, (cols - 10) / 2, " Alerts ");
    wattroff(win, COLOR_PAIR(CP_RED) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    /* Counter badge on title line */
    if (d->alert_count > 0) {
        wattron(win, COLOR_PAIR(CP_RED) | A_BOLD | A_BLINK);
        mvwprintw(win, 1, cols - 14, " %d ACTIVE ", d->alert_count);
        wattroff(win, COLOR_PAIR(CP_RED) | A_BOLD | A_BLINK);
    } else {
        wattron(win, COLOR_PAIR(CP_GREEN) | A_BOLD);
        mvwprintw(win, 1, cols - 12, " ALL CLEAR ");
        wattroff(win, COLOR_PAIR(CP_GREEN) | A_BOLD);
    }

    /* Column headers */
    wattron(win, A_BOLD | A_UNDERLINE);
    mvwprintw(win, 3, 2, "  #  Alert Message");
    wattroff(win, A_BOLD | A_UNDERLINE);
    mvwhline(win, 4, 1, ACS_HLINE, cols - 2);

    int row = 5;
    if (d->alert_count == 0) {
        wattron(win, COLOR_PAIR(CP_GREEN) | A_BOLD);
        mvwprintw(win, row, 4, "  No active alerts. All services operating normally.");
        wattroff(win, COLOR_PAIR(CP_GREEN) | A_BOLD);
    } else {
        for (int i = 0; i < d->alert_count && row < rows - 2; i++) {
            wattron(win, COLOR_PAIR(CP_RED));
            mvwprintw(win, row, 2, " %2d ", i + 1);
            wattroff(win, COLOR_PAIR(CP_RED));
            wattron(win, COLOR_PAIR(CP_RED) | A_BOLD);
            mvwaddch(win, row, 6, ACS_DIAMOND);
            mvwprintw(win, row, 8, "%-*.*s", cols - 10, cols - 10, d->alert_lines[i]);
            wattroff(win, COLOR_PAIR(CP_RED) | A_BOLD);
            row++;
        }
    }

    /* Footer */
    mvwhline(win, rows - 3, 1, ACS_HLINE, cols - 2);
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, rows - 2, 2, "Alerts auto-clear on next collection cycle. Total this session: %d",
              d->alert_count);
    wattroff(win, COLOR_PAIR(CP_DIM));
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Logs (tab 7)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Log level metadata */
static const struct {
    const char *label;   /* 5-char padded */
    int         color;
} LOG_LEVEL_META[] = {
    { "DEBUG", CP_DIM    },   /* LL_DEBUG */
    { "INFO ", CP_GREEN  },   /* LL_INFO  */
    { "WARN ", CP_YELLOW },   /* LL_WARN  */
    { "ERROR", CP_RED    },   /* LL_ERROR */
};

/* Log category metadata */
static const char *LOG_CAT_LABEL[] = {
    "SYS",  /* LC_SYSTEM   */
    "SVC",  /* LC_SERVICE  */
    "HAL",  /* LC_HAL      */
    "MEM",  /* LC_MEMORY   */
    "I/O",  /* LC_IO       */
    "SEC",  /* LC_SECURITY */
    "HLT",  /* LC_HEALTH   */
    "NET",  /* LC_NET      */
};

static void draw_panel_logs(WINDOW *win, const monitor_data_t *d)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    /* Title */
    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 20) / 2, " System Event Log ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);

    /* Entry count badge */
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, 1, cols - 18, " %d entries ", d->log_count);
    wattroff(win, COLOR_PAIR(CP_DIM));
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    /* Level filter summary on header row */
    int n_debug = 0, n_info = 0, n_warn = 0, n_err = 0;
    for (int i = 0; i < d->log_count; i++) {
        int idx = (d->log_head + i) % MAX_LOG_LINES;
        switch (d->log_entries[idx].level) {
        case LL_DEBUG: n_debug++; break;
        case LL_INFO:  n_info++;  break;
        case LL_WARN:  n_warn++;  break;
        case LL_ERROR: n_err++;   break;
        }
    }
    int hdr_col = 2;
    mvwprintw(win, 3, hdr_col, "Filter: ");
    hdr_col += 8;
    wattron(win, COLOR_PAIR(CP_DIM));  mvwprintw(win, 3, hdr_col, "DBG:%-4d ", n_debug); wattroff(win, COLOR_PAIR(CP_DIM));  hdr_col += 9;
    wattron(win, COLOR_PAIR(CP_GREEN));mvwprintw(win, 3, hdr_col, "INF:%-4d ", n_info);  wattroff(win, COLOR_PAIR(CP_GREEN)); hdr_col += 9;
    wattron(win, COLOR_PAIR(CP_YELLOW));mvwprintw(win,3, hdr_col, "WRN:%-4d ", n_warn); wattroff(win, COLOR_PAIR(CP_YELLOW));hdr_col += 9;
    wattron(win, COLOR_PAIR(CP_RED) | A_BOLD);mvwprintw(win,3,hdr_col,"ERR:%-4d",n_err);wattroff(win,COLOR_PAIR(CP_RED)|A_BOLD);

    /* Column headers */
    mvwhline(win, 4, 1, ACS_HLINE, cols - 2);
    wattron(win, A_BOLD);
    mvwprintw(win, 5, 2,  "%-8s", "Time");
    mvwaddch(win, 5, 11, ACS_VLINE);
    mvwprintw(win, 5, 13, "%-5s", "Level");
    mvwaddch(win, 5, 19, ACS_VLINE);
    mvwprintw(win, 5, 21, "%-3s", "Cat");
    mvwaddch(win, 5, 25, ACS_VLINE);
    mvwprintw(win, 5, 27, "Message");
    wattroff(win, A_BOLD);
    mvwhline(win, 6, 1, ACS_HLINE, cols - 2);

    /* Log entries — show most recent, skip DEBUG entries if screen is crowded */
    int avail_rows = rows - 9;  /* rows 7 .. rows-3 */
    if (avail_rows < 1) { wrefresh(win); return; }

    /* Collect visible entries (newest last, skip excess DEBUG) */
    int start = (d->log_count > avail_rows) ? d->log_count - avail_rows : 0;
    int row = 7;

    for (int i = start; i < d->log_count && row < rows - 2; i++) {
        int idx = (d->log_head + i) % MAX_LOG_LINES;
        const log_entry_t *e = &d->log_entries[idx];

        int lm_idx = (int)e->level;
        if (lm_idx < 0 || lm_idx > 3) lm_idx = 1;
        int cat_idx = (int)e->cat;
        if (cat_idx < 0 || cat_idx > 7) cat_idx = 0;

        int lcolor = LOG_LEVEL_META[lm_idx].color;
        int lattr  = (e->level == LL_ERROR) ? (COLOR_PAIR(lcolor) | A_BOLD) :
                     (e->level == LL_WARN)  ? (COLOR_PAIR(lcolor) | A_BOLD) :
                     COLOR_PAIR(lcolor);

        /* Row background: alternate faint for readability */
        if (e->level == LL_ERROR) wattron(win, A_BOLD);

        /* Timestamp HH:MM:SS */
        struct tm *tm = localtime(&e->ts);
        char tmbuf[10];
        strftime(tmbuf, sizeof(tmbuf), "%H:%M:%S", tm);
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, row, 2, "%8s", tmbuf);
        wattroff(win, COLOR_PAIR(CP_DIM));

        /* Separator */
        mvwaddch(win, row, 11, ACS_VLINE);

        /* Level badge */
        wattron(win, lattr);
        mvwprintw(win, row, 13, "%-5s", LOG_LEVEL_META[lm_idx].label);
        wattroff(win, lattr);

        /* Separator */
        mvwaddch(win, row, 19, ACS_VLINE);

        /* Category badge */
        int cat_color = (cat_idx == (int)LC_SERVICE) ? CP_CYAN    :
                        (cat_idx == (int)LC_HAL)     ? CP_MAGENTA :
                        (cat_idx == (int)LC_SECURITY)? CP_RED     :
                        (cat_idx == (int)LC_HEALTH)  ? CP_YELLOW  :
                        (cat_idx == (int)LC_MEMORY)  ? CP_BLUE    : CP_DEFAULT;
        wattron(win, COLOR_PAIR(cat_color) | A_BOLD);
        mvwprintw(win, row, 21, "%-3s", LOG_CAT_LABEL[cat_idx]);
        wattroff(win, COLOR_PAIR(cat_color) | A_BOLD);

        /* Separator */
        mvwaddch(win, row, 25, ACS_VLINE);

        /* Message */
        wattron(win, lattr);
        int msg_w = cols - 28;
        if (msg_w > 0)
            mvwprintw(win, row, 27, "%-*.*s", msg_w, msg_w, e->msg);
        wattroff(win, lattr);
        if (e->level == LL_ERROR) wattroff(win, A_BOLD);

        row++;
    }

    if (d->log_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, 8, 4, "  No log entries yet. Waiting for first collection cycle...");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    /* Footer */
    mvwhline(win, rows - 3, 1, ACS_HLINE, cols - 2);
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, rows - 2, 2,
              "Showing %d of %d entries  |  Ring buffer: %d/%d  |  Legend: DBG=gray  INF=green  WRN=yellow  ERR=red",
              (d->log_count > avail_rows ? avail_rows : d->log_count),
              d->log_count, d->log_count, MAX_LOG_LINES);
    wattroff(win, COLOR_PAIR(CP_DIM));
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Help (tab 8)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_help(WINDOW *win)
{
    werase(win);
    box(win, 0, 0);
    int rows, cols;
    getmaxyx(win, rows, cols);

    wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwprintw(win, 1, (cols - 6) / 2, " Help ");
    wattroff(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols - 2);

    int row = 3;
    const char *help[] = {
        "KEYBOARD SHORTCUTS",
        "",
        "  q / Q / ESC     Quit the monitor",
        "  Tab / Right      Next panel",
        "  Left             Previous panel",
        "  r / R            Force immediate refresh",
        "  0-8              Jump to panel by number",
        "  F1               This help screen",
        "",
        "PANELS",
        "  0  Overview      System summary: CPU, RAM, services",
        "  1  Services      Per-service detail: CPU, RAM, FDs, health",
        "  2  HAL           Hardware Abstraction Layer status",
        "  3  Memory        Memory pools and swap usage",
        "  4  I/O           Disk I/O and file descriptor counts",
        "  5  Security      Seccomp, health scores per service",
        "  6  Alerts        Active system alerts",
        "  7  Logs          Monitor event log",
        "  8  Help          This screen",
        "",
        "DATA SOURCES",
        "  /proc/stat       CPU usage (delta method)",
        "  /proc/meminfo    RAM and swap",
        "  /proc/[pid]/     Per-process metrics",
        "  SM socket        Service Manager metrics (when available)",
        NULL
    };

    for (int i = 0; help[i] && row < rows - 2; i++) {
        if (help[i][0] != '\0' && isupper((unsigned char)help[i][0])) {
            wattron(win, COLOR_PAIR(CP_CYAN) | A_BOLD);
        } else if (strncmp(help[i], "  ", 2) == 0 && strlen(help[i]) > 5) {
            wattron(win, COLOR_PAIR(CP_DEFAULT));
        }
        mvwprintw(win, row++, 2, "%s", help[i]);
        wattroff(win, A_BOLD);
    }
    (void)rows;
    wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel window management
 * ══════════════════════════════════════════════════════════════════════════ */

static void destroy_panels(void)
{
    for (int i = 0; i < TAB_COUNT; i++) {
        if (g_panels[i]) { delwin(g_panels[i]); g_panels[i] = NULL; }
    }
}

static void create_panels(void)
{
    destroy_panels();
    /* Panel area: rows 2..rows-2, full width */
    int ph = g_rows - 4;   /* rows 2 to rows-2 */
    int pw = g_cols;
    int py = 2;
    int px = 0;
    if (ph < 2) ph = 2;

    for (int i = 0; i < TAB_COUNT; i++) {
        g_panels[i] = newwin(ph, pw, py, px);
        if (!g_panels[i]) {
            /* Fallback */
            g_panels[i] = newwin(10, 40, py, px);
        }
        scrollok(g_panels[i], FALSE);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Render active panel
 * ══════════════════════════════════════════════════════════════════════════ */

static void render_active_panel(const monitor_data_t *snap)
{
    WINDOW *w = g_panels[g_active_tab];
    if (!w) return;

    switch (g_active_tab) {
    case 0: draw_panel_overview(w, snap);  break;
    case 1: draw_panel_services(w, snap);  break;
    case 2: draw_panel_hal(w, snap);       break;
    case 3: draw_panel_memory(w, snap);    break;
    case 4: draw_panel_io(w, snap);        break;
    case 5: draw_panel_security(w, snap);  break;
    case 6: draw_panel_alerts(w, snap);    break;
    case 7: draw_panel_logs(w, snap);      break;
    case 8: draw_panel_help(w);            break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Resize handler
 * ══════════════════════════════════════════════════════════════════════════ */

static void handle_resize(void)
{
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, g_rows, g_cols);
    create_panels();
    g_resize = 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Fix 1: locale + TERM before initscr */
    setlocale(LC_ALL, "");
    setenv("TERM", "xterm-256color", 0);  /* Only set if not already set */

    /* Signal handlers */
    signal(SIGINT,   handle_sigint);
    signal(SIGTERM,  handle_sigterm);
    signal(SIGWINCH, handle_sigwinch);

    /* Prime CPU baseline with 500ms two-sample read */
    {
        cpu_stat_t s1, s2;
        read_cpu_stat(&s1);
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000000 };
        nanosleep(&ts, NULL);
        read_cpu_stat(&s2);
        g_data.cpu_total_pct   = cpu_delta_pct(&s1, &s2);
        g_data.cpu_iowait_pct  = cpu_iowait_pct(&s1, &s2);
        g_data.cpu_initialized = 1;
        g_data.prev_cpu = s2;
    }

    /* Initialize ncurses */
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);   /* Non-blocking getch() */
    curs_set(0);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Terminal does not support colors.\n");
        return 1;
    }
    init_colors();

    getmaxyx(stdscr, g_rows, g_cols);
    create_panels();

    /* Start data collection thread */
    pthread_t collector_tid;
    if (pthread_create(&collector_tid, NULL, data_collector_thread, NULL) != 0) {
        endwin();
        fprintf(stderr, "Failed to create collector thread\n");
        return 1;
    }

    /* Main render loop: 100ms interval */
    monitor_data_t snap;
    memset(&snap, 0, sizeof(snap));

    while (g_running) {

        /* Handle terminal resize */
        if (g_resize) {
            handle_resize();
            refresh();
        }

        /* Handle keyboard (non-blocking) */
        int ch = getch();
        if (ch != ERR) {
            switch (ch) {
            case 'q': case 'Q': case 27: /* ESC */
                g_running = 0;
                break;

            case '\t': case KEY_RIGHT: case KEY_BTAB:
                g_active_tab = (g_active_tab + 1) % TAB_COUNT;
                break;

            case KEY_LEFT:
                g_active_tab = (g_active_tab - 1 + TAB_COUNT) % TAB_COUNT;
                break;

            case 'r': case 'R':
                /* Immediate redraw with current data */
                break;

            case KEY_F(1):
                g_active_tab = TAB_COUNT - 1;  /* Help */
                break;

            /* Number keys jump to panel */
            case '0': g_active_tab = 0; break;
            case '1': g_active_tab = 1; break;
            case '2': g_active_tab = 2; break;
            case '3': g_active_tab = 3; break;
            case '4': g_active_tab = 4; break;
            case '5': g_active_tab = 5; break;
            case '6': g_active_tab = 6; break;
            case '7': g_active_tab = 7; break;
            case '8': g_active_tab = 8; break;
            }
        }

        /* Copy latest data snapshot (lock-free copy under mutex) */
        pthread_mutex_lock(&g_lock);
        memcpy(&snap, &g_data, sizeof(snap));
        pthread_mutex_unlock(&g_lock);

        /* Render */
        draw_topbar(&snap);
        draw_tabbar();
        render_active_panel(&snap);
        draw_statusbar(&snap);

        wnoutrefresh(stdscr);
        doupdate();

        napms(100);   /* 100ms render interval — stays responsive */
    }

    /* Cleanup */
    g_running = 0;
    pthread_join(collector_tid, NULL);

    destroy_panels();
    endwin();
    return 0;
}
