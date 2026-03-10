/**
 * @file    middleware_monitor.c
 * @brief   Standalone real-time middleware monitoring TUI — merged edition
 *
 * Merges all subsystem modules into one self-contained binary:
 *   alerts/      — alert engine, rules, notifications
 *   collectors/  — sysinfo, services, HAL, SM, processes, security,
 *                  io_uring, ring_buffer, ipc_channels, memory_pool,
 *                  watchdog, proxy, config_watcher
 *   health/      — health score computation
 *   metrics/     — delta, history, registry, prometheus export
 *   protocol/    — wire format, IPC protocol types
 *   tracing/     — trace collector, renderer, store
 *   tui/         — engine, colors, input, layout, panels
 *
 * Architecture:
 *   Thread 1 (data_collector): 1000 ms interval, reads /proc + sockets
 *   Thread 2 (main/render):    100  ms interval, non-blocking ncurses
 *   pthread_mutex protects shared g_data
 *
 * Tabs (15 total):
 *   0  Overview       system summary
 *   1  Services       per-service detail
 *   2  HAL            hardware abstraction layer
 *   3  Memory         RAM, swap, memory pools
 *   4  I/O            disk I/O, io_uring instances
 *   5  Ring Buffers   ring buffer health
 *   6  IPC Channels   inter-process channels
 *   7  Security       seccomp, caps, sandbox, verify
 *   8  Watchdog       heartbeat & restart tracking
 *   9  Proxy          proxy subsystem
 *  10  SM Details     service-manager deep-dive
 *  11  Collectors     collector thread health
 *  12  Alerts         active system alerts
 *  13  Logs           structured event log
 *  14  Help           keyboard reference
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

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
#include <math.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <locale.h>
#include <ncurses.h>

/* ══════════════════════════════════════════════════════════════════════════
 * Compile-time limits
 * ══════════════════════════════════════════════════════════════════════════ */

#define MAX_SERVICES        16
#define MAX_LOG_LINES       512
#define MAX_ALERTS          64
#define MAX_URING_INST      8    /* io_uring instances tracked            */
#define MAX_RINGBUF_INST    16   /* ring buffer instances tracked         */
#define MAX_IPC_CHANNELS    16   /* IPC channels tracked                  */
#define MAX_POOL_INST       8    /* memory pool instances tracked         */
#define MAX_WATCHDOG_SVCS   16   /* watchdog-tracked services             */
#define MAX_COLLECTORS      16   /* collector thread slots                */
#define MAX_TRACE_ENTRIES   256  /* trace ring buffer                     */
#define MAX_PROXY_CONNS     8    /* proxy connection slots                */
#define MAX_SM_PEERS        16   /* SM registered peer count              */
#define SPARKLINE_LEN       32   /* sparkline history depth               */

#define SERVICE_NAME_LEN    64
#define LOG_LINE_LEN        256
#define TRACE_MSG_LEN       128

#define TAB_COUNT           15

/* ══════════════════════════════════════════════════════════════════════════
 * Color pair IDs
 * ══════════════════════════════════════════════════════════════════════════ */

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
#define CP_WHITE_BOLD   14

/* ══════════════════════════════════════════════════════════════════════════
 * Log level & category enums
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum { LL_DEBUG=0, LL_INFO=1, LL_WARN=2, LL_ERROR=3 } log_level_t;
typedef enum {
    LC_SYSTEM=0, LC_SERVICE=1, LC_HAL=2,    LC_MEMORY=3,
    LC_IO=4,     LC_SECURITY=5,LC_HEALTH=6, LC_NET=7,
    LC_URING=8,  LC_RINGBUF=9, LC_IPC=10,   LC_WATCHDOG=11,
    LC_PROXY=12, LC_SM=13,     LC_TRACE=14, LC_COLLECTOR=15
} log_cat_t;

typedef struct {
    time_t      ts;
    log_level_t level;
    log_cat_t   cat;
    char        msg[LOG_LINE_LEN];
} log_entry_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Alert severity
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    ALERT_SEV_INFO  = 0,
    ALERT_SEV_WARN  = 1,
    ALERT_SEV_CRIT  = 2,
} alert_severity_t;

typedef struct {
    alert_severity_t severity;
    char             component[32];
    char             condition[64];
    char             current_value[32];
    char             threshold[32];
    char             suggestion[128];
    uint64_t         first_ts_ms;
    uint64_t         last_ts_ms;
    uint32_t         occurrences;
    uint8_t          active;
} alert_record_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Collector state (from collector_base.h)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    COLL_WAITING    = 0,
    COLL_CONNECTING = 1,
    COLL_SYNCING    = 2,
    COLL_LIVE       = 3,
    COLL_STALE      = 4,
    COLL_OFFLINE    = 5,
} coll_state_t;

typedef struct {
    char         name[32];
    coll_state_t state;
    uint64_t     last_update_ms;
    uint64_t     collect_count;
    uint64_t     error_count;
    uint32_t     interval_ms;
} collector_status_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Per-service data (extended)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char   name[SERVICE_NAME_LEN];
    pid_t  pid;
    int    running;
    float  cpu_pct;
    long   rss_kb;
    long   vsz_kb;
    int    fd_count;
    int    thread_count;
    long   restart_count;
    long   uptime_s;
    int    health_score;
    /* Security flags */
    int    sandbox_ok;
    int    caps_ok;
    int    seccomp_ok;
    int    verify_ok;
    uint32_t seccomp_violations;
    uint32_t hmac_failures;
    uint32_t replay_attacks;
    /* Frame / message stats */
    uint64_t frames_captured;
    uint64_t frames_dropped;
    /* SM connection */
    int    sm_connected;
    uint32_t sm_reconnects;
    /* CPU tracking */
    unsigned long      prev_utime;
    unsigned long      prev_stime;
    unsigned long long prev_uptime_ticks;
    /* CPU sparkline */
    float  cpu_history[SPARKLINE_LEN];
    int    cpu_hist_idx;
} service_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * io_uring instance data (from metrics_types.h / collector_io_uring)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     name[32];
    int      active;              /* 1 = instance detected               */
    uint32_t sq_entries;          /* SQ ring size                        */
    uint32_t cq_entries;          /* CQ ring size                        */
    uint32_t sq_pending;          /* un-submitted SQ entries             */
    uint32_t cq_ready;            /* un-consumed CQ completions          */
    float    cq_fill_pct;         /* (cq_ready / cq_entries) * 100       */
    uint64_t submit_total;        /* io_uring_submit() calls             */
    uint64_t complete_total;      /* completions processed               */
    uint64_t err_total;           /* completions with res < 0            */
    float    lat_avg_us;          /* average completion latency (µs)     */
    float    lat_p99_us;          /* p99 completion latency (µs)         */
    uint64_t ts_ms;
} uring_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Ring buffer instance data (from collector_ring_buffer)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     name[32];
    int      active;
    uint32_t capacity;            /* total slots                         */
    uint32_t used;                /* currently occupied slots            */
    float    fill_pct;            /* (used / capacity) * 100             */
    uint64_t write_total;         /* total writes                        */
    uint64_t read_total;          /* total reads                         */
    uint64_t drop_count;          /* writes lost (overflow)              */
    uint32_t writers;             /* active producer count               */
    uint32_t readers;             /* active consumer count               */
    uint64_t ts_ms;
} ringbuf_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * IPC channel data (from collector_ipc_channels)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     name[32];
    int      active;
    char     type[16];            /* "unix_sock", "shmem", "pipe", ...   */
    uint32_t send_q_depth;
    uint32_t send_q_max;
    uint32_t recv_q_depth;
    uint32_t recv_q_max;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t msgs_sent;
    uint64_t msgs_recv;
    uint64_t err_count;
    float    lat_avg_us;
    uint64_t ts_ms;
} ipc_chan_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Memory pool instance data (from collector_memory_pool)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     name[32];
    int      active;
    uint32_t block_size;          /* bytes per block                     */
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t used_blocks;
    float    usage_pct;           /* (used / total) * 100                */
    uint64_t alloc_total;         /* lifetime allocations                */
    uint64_t free_total;          /* lifetime frees                      */
    uint64_t fail_count;          /* allocation failures (pool empty)    */
    uint64_t ts_ms;
} pool_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Watchdog entry (from collector_watchdog / watchdog_entry_t)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     service_name[32];
    int      active;
    uint64_t last_heartbeat_ms;
    uint32_t heartbeat_interval_ms;
    int      alive;               /* 1 = OK, 0 = timed out               */
    uint32_t restart_count;
    uint64_t last_restart_ms;
    uint8_t  escalation_state;   /* 0=none 1=WARN 2=RESTART 3=KILL      */
    uint32_t escalation_count;
    int      missed_pings;
} watchdog_entry_data_t;

typedef struct {
    watchdog_entry_data_t entries[MAX_WATCHDOG_SVCS];
    int                   count;
    uint32_t              total_restarts;
} watchdog_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Proxy subsystem data (from collector_proxy)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int      active;              /* 1 = proxy binary detected           */
    pid_t    pid;
    float    cpu_pct;
    long     rss_kb;
    uint32_t active_conns;
    uint32_t total_conns;
    uint64_t bytes_forwarded;
    uint64_t bytes_received;
    uint64_t req_total;
    uint64_t req_err;
    float    req_per_s;
    float    lat_avg_ms;
    float    lat_p99_ms;
    /* Per-upstream slot */
    struct {
        char     name[32];
        int      healthy;
        uint32_t active_reqs;
        uint64_t req_total;
        float    lat_avg_ms;
    } upstreams[MAX_PROXY_CONNS];
    int      upstream_count;
    uint64_t ts_ms;
} proxy_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Service Manager deep-dive (from collector_sm)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int      available;           /* SM socket connected                 */
    pid_t    pid;
    uint64_t uptime_s;
    float    cpu_pct;
    long     rss_kb;
    uint32_t fd_count;
    /* Registry */
    uint32_t registered_services;
    uint32_t max_services;
    /* Connection pool */
    uint32_t pool_active;
    uint32_t pool_idle;
    uint32_t pool_max;
    /* Thread pool */
    uint32_t threads_busy;
    uint32_t threads_total;
    /* TLS */
    uint32_t tls_sessions;
    /* Health latencies (µs) */
    uint32_t health_p50_us;
    uint32_t health_p95_us;
    uint32_t health_p99_us;
    uint32_t health_p999_us;
    /* Rate limiting */
    uint32_t rate_accept_per_s;
    uint32_t rate_reject_per_s;
    float    rate_global_pct;
    /* Peers */
    struct {
        char     name[32];
        int      connected;
        uint64_t msgs_in;
        uint64_t msgs_out;
    } peers[MAX_SM_PEERS];
    int      peer_count;
    /* CPU sparkline */
    float    cpu_history[SPARKLINE_LEN];
    int      cpu_hist_idx;
    uint64_t ts_ms;
} sm_detail_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Trace entry (from tracing/)
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    TRACE_CALL   = 0,
    TRACE_RETURN = 1,
    TRACE_EVENT  = 2,
    TRACE_METRIC = 3,
} trace_type_t;

typedef struct {
    uint64_t    ts_us;           /* microsecond timestamp                */
    trace_type_t type;
    char        component[24];
    char        msg[TRACE_MSG_LEN];
    int32_t     value;           /* optional numeric payload             */
} trace_entry_t;

/* ══════════════════════════════════════════════════════════════════════════
 * CPU statistics
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    long user, nice, sys, idle, iowait, irq, softirq, steal;
} cpu_stat_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Master monitor_data_t — shared between collector thread and render thread
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* ── System info ──────────────────────────────────────────────── */
    float   cpu_total_pct;
    float   cpu_iowait_pct;
    float   cpu_core_pct[16];
    int     num_cores;
    int     cpu_initialized;
    cpu_stat_t prev_cpu;

    float   cpu_history[SPARKLINE_LEN];   /* rolling sparkline         */
    int     cpu_hist_idx;

    long    ram_total_kb;
    long    ram_used_kb;
    long    ram_free_kb;
    long    swap_total_kb;
    long    swap_used_kb;

    float   load_1, load_5, load_15;
    long    uptime_s;

    /* ── Services ─────────────────────────────────────────────────── */
    service_data_t services[MAX_SERVICES];
    int            service_count;

    /* ── SM socket ────────────────────────────────────────────────── */
    int    sm_socket_ok;
    char   sm_socket_path[128];

    /* ── SM deep-dive ─────────────────────────────────────────────── */
    sm_detail_data_t sm;

    /* ── io_uring instances ───────────────────────────────────────── */
    uring_data_t    urings[MAX_URING_INST];
    int             uring_count;

    /* ── Ring buffers ─────────────────────────────────────────────── */
    ringbuf_data_t  ring_buffers[MAX_RINGBUF_INST];
    int             ringbuf_count;

    /* ── IPC channels ─────────────────────────────────────────────── */
    ipc_chan_data_t ipc_channels[MAX_IPC_CHANNELS];
    int             ipc_count;

    /* ── Memory pools ─────────────────────────────────────────────── */
    pool_data_t     pools[MAX_POOL_INST];
    int             pool_count;

    /* ── Watchdog ─────────────────────────────────────────────────── */
    watchdog_data_t watchdog;

    /* ── Proxy ────────────────────────────────────────────────────── */
    proxy_data_t    proxy;

    /* ── Collector health ─────────────────────────────────────────── */
    collector_status_t collectors[MAX_COLLECTORS];
    int                collector_count;

    /* ── Trace ring ───────────────────────────────────────────────── */
    trace_entry_t   traces[MAX_TRACE_ENTRIES];
    int             trace_head;
    int             trace_count;

    /* ── System-wide FD count ─────────────────────────────────────── */
    long    fd_used;

    /* ── Structured log ring ──────────────────────────────────────── */
    log_entry_t log_entries[MAX_LOG_LINES];
    int         log_head;
    int         log_count;

    /* ── Active alerts ring ───────────────────────────────────────── */
    alert_record_t  alerts[MAX_ALERTS];
    int             alert_count;
    /* Legacy flat-string alerts for the panel */
    char   alert_lines[MAX_ALERTS][LOG_LINE_LEN];

    /* ── Timestamp ────────────────────────────────────────────────── */
    time_t last_update;
} monitor_data_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Tab names
 * ══════════════════════════════════════════════════════════════════════════ */

static const char *TAB_NAMES[TAB_COUNT] = {
    "Overview", "Services", "HAL", "Memory", "I/O",
    "RingBufs", "IPC", "Security", "Watchdog",
    "Proxy", "SM Detail", "Collectors", "Alerts", "Logs", "Help"
};

/* (all structs defined above in the merged header section) */

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
    { int _r = fscanf(f, "%f %f %f", &d->load_1, &d->load_5, &d->load_15); (void)_r; }
    fclose(f);
}

static void read_uptime(monitor_data_t *d)
{
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return;
    double up;
    { int _r = fscanf(f, "%lf", &up); (void)_r; }
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
    { int _r = fscanf(up, "%lf", &uptime_sec); (void)_r; }
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
                    memcpy(argv0, cmdline, len); argv0[len] = '\0';
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

/* Alert generation — covers all subsystems */
static void check_alerts(monitor_data_t *d)
{
    static int  prev_svc_count      = -1;
    static int  prev_health[MAX_SERVICES];
    static char prev_names[MAX_SERVICES][SERVICE_NAME_LEN];
    static int  health_init         = 0;

    uint64_t now_ms = (uint64_t)time(NULL) * 1000ULL;

    /* ── Services ───────────────────────────────────────────────────── */
    for (int i = 0; i < d->service_count; i++) {
        service_data_t *s = &d->services[i];

        if (s->cpu_pct > 90.0f) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "[CRIT] %s CPU=%.1f%%  RAM=%ldMB  PID=%d",
                     s->name, s->cpu_pct, s->rss_kb/1024, s->pid);
            if (d->alert_count < MAX_ALERTS)
                snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN, "%s", buf);
            LOG_ERR(d, LC_SERVICE, "CPU CRITICAL  %-16s  %.1f%%  (PID %d)",
                    s->name, s->cpu_pct, s->pid);
        } else if (s->cpu_pct > 75.0f) {
            LOG_WARN(d, LC_SERVICE, "CPU HIGH      %-16s  %.1f%%", s->name, s->cpu_pct);
        }

        if (s->seccomp_violations > 0) {
            LOG_ERR(d, LC_SECURITY, "SECCOMP VIOLATION  %-16s  count=%u",
                    s->name, s->seccomp_violations);
            if (d->alert_count < MAX_ALERTS) {
                snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN,
                         "[CRIT] %s seccomp violation (count=%u)", s->name, s->seccomp_violations);
            }
        }
        if (s->hmac_failures > 0) {
            LOG_ERR(d, LC_SECURITY, "HMAC FAILURE       %-16s  count=%u",
                    s->name, s->hmac_failures);
        }
        if (s->fd_count > 500)
            LOG_WARN(d, LC_IO, "FD HIGH  %-16s  %d fds", s->name, s->fd_count);

        if (health_init) {
            for (int j = 0; j < prev_svc_count; j++) {
                if (strcmp(prev_names[j], s->name) != 0) continue;
                int delta = prev_health[j] - s->health_score;
                if (abs(delta) >= 10) {
                    if (delta > 0) {
                        LOG_WARN(d, LC_HEALTH, "Health DROP  %-16s  %d->%d",
                                 s->name, prev_health[j], s->health_score);
                        if (s->health_score < 50 && d->alert_count < MAX_ALERTS)
                            snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN,
                                     "[CRIT] %s health critical: %d/100", s->name, s->health_score);
                    } else {
                        LOG_INFO(d, LC_HEALTH, "Health UP    %-16s  %d->%d",
                                 s->name, prev_health[j], s->health_score);
                    }
                }
                break;
            }
        }
    }

    /* ── io_uring ────────────────────────────────────────────────────── */
    for (int i = 0; i < d->uring_count; i++) {
        uring_data_t *u = &d->urings[i];
        if (!u->active) continue;
        if (u->cq_fill_pct > 95.0f) {
            LOG_ERR(d, LC_URING, "CQ OVERFLOW  %-20s  %.1f%% full", u->name, u->cq_fill_pct);
            if (d->alert_count < MAX_ALERTS)
                snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN,
                         "[CRIT] io_uring %s CQ %.1f%% full", u->name, u->cq_fill_pct);
        } else if (u->cq_fill_pct > 80.0f) {
            LOG_WARN(d, LC_URING, "CQ HIGH      %-20s  %.1f%% full", u->name, u->cq_fill_pct);
        }
    }

    /* ── Ring buffers ────────────────────────────────────────────────── */
    for (int i = 0; i < d->ringbuf_count; i++) {
        ringbuf_data_t *rb = &d->ring_buffers[i];
        if (!rb->active) continue;
        if (rb->drop_count > 0) {
            LOG_ERR(d, LC_RINGBUF, "OVERFLOW  %-20s  drops=%lu", rb->name, rb->drop_count);
            if (d->alert_count < MAX_ALERTS)
                snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN,
                         "[CRIT] ring_buf %s overflow drops=%lu", rb->name, rb->drop_count);
        } else if (rb->fill_pct > 80.0f) {
            LOG_WARN(d, LC_RINGBUF, "FILL HIGH  %-20s  %.1f%%", rb->name, rb->fill_pct);
        }
    }

    /* ── Memory pools ─────────────────────────────────────────────────── */
    for (int i = 0; i < d->pool_count; i++) {
        pool_data_t *p = &d->pools[i];
        if (!p->active) continue;
        if (p->fail_count > 0) {
            LOG_ERR(d, LC_MEMORY, "POOL EXHAUST  %-20s  fails=%lu", p->name, p->fail_count);
            if (d->alert_count < MAX_ALERTS)
                snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN,
                         "[CRIT] pool %s exhausted (fails=%lu)", p->name, p->fail_count);
        } else if (p->usage_pct > 90.0f) {
            LOG_WARN(d, LC_MEMORY, "POOL HIGH     %-20s  %.1f%%", p->name, p->usage_pct);
        }
    }

    /* ── Watchdog ────────────────────────────────────────────────────── */
    for (int i = 0; i < d->watchdog.count; i++) {
        watchdog_entry_data_t *w = &d->watchdog.entries[i];
        if (!w->active) continue;
        if (!w->alive) {
            LOG_ERR(d, LC_WATCHDOG, "HEARTBEAT MISSING  %-16s", w->service_name);
            if (d->alert_count < MAX_ALERTS)
                snprintf(d->alert_lines[d->alert_count++], LOG_LINE_LEN,
                         "[CRIT] watchdog: %s heartbeat missing", w->service_name);
        }
        if (w->escalation_state >= 2) {
            LOG_WARN(d, LC_WATCHDOG, "ESCALATION  %-16s  state=%u  restarts=%u",
                     w->service_name, w->escalation_state, w->restart_count);
        }
    }

    /* ── System-level ────────────────────────────────────────────────── */
    if (d->cpu_total_pct > 90.0f) {
        LOG_ERR(d, LC_SYSTEM, "SYSTEM CPU CRITICAL  %.1f%%", d->cpu_total_pct);
    } else if (d->cpu_total_pct > 80.0f) {
        LOG_WARN(d, LC_SYSTEM, "SYSTEM CPU HIGH      %.1f%%", d->cpu_total_pct);
    }
    if (d->ram_total_kb > 0) {
        float ram_pct = 100.0f * d->ram_used_kb / d->ram_total_kb;
        if (ram_pct > 90.0f)
            LOG_ERR(d, LC_MEMORY, "RAM CRITICAL  %.1f%%  free=%ldMB",
                    ram_pct, (d->ram_total_kb - d->ram_used_kb)/1024);
        else if (ram_pct > 80.0f)
            LOG_WARN(d, LC_MEMORY, "RAM HIGH      %.1f%%", ram_pct);
    }

    /* Save state */
    if (prev_svc_count >= 0 && d->service_count < prev_svc_count)
        LOG_WARN(d, LC_SERVICE, "Service count dropped: %d -> %d", prev_svc_count, d->service_count);
    else if (prev_svc_count >= 0 && d->service_count > prev_svc_count)
        LOG_INFO(d, LC_SERVICE, "Service count increased: %d -> %d", prev_svc_count, d->service_count);

    prev_svc_count = d->service_count;
    for (int i = 0; i < d->service_count && i < MAX_SERVICES; i++) {
        prev_health[i] = d->services[i].health_score;
        strncpy(prev_names[i], d->services[i].name, SERVICE_NAME_LEN - 1);
        prev_names[i][SERVICE_NAME_LEN - 1] = '\0';
    }
    health_init = 1;
    (void)now_ms;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Subsystem discovery helpers
 * ════════════════════════════════════════════════════════════════════════════ */

/* Scan /proc/[pid]/fdinfo/N for io_uring instances */
static void collect_io_uring(monitor_data_t *d)
{
    d->uring_count = 0;
    DIR *proc = opendir("/proc");
    if (!proc) return;

    struct dirent *pe;
    while ((pe = readdir(proc)) != NULL && d->uring_count < MAX_URING_INST) {
        if (!isdigit((unsigned char)pe->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(pe->d_name);

        char fdinfo_path[64];
        snprintf(fdinfo_path, sizeof(fdinfo_path), "/proc/%d/fdinfo", pid);
        DIR *fdir = opendir(fdinfo_path);
        if (!fdir) continue;

        struct dirent *fe;
        while ((fe = readdir(fdir)) != NULL && d->uring_count < MAX_URING_INST) {
            if (fe->d_name[0] == '.') continue;
            char fpath[512];  /* /proc/<pid>/fdinfo/<N> — large enough */
            snprintf(fpath, sizeof(fpath), "%s/%s", fdinfo_path, fe->d_name);
            FILE *f = fopen(fpath, "r");
            if (!f) continue;

            char   line[128];
            int    is_uring   = 0;
            uint32_t sq_sz = 0, cq_sz = 0;
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "io_uring", 8) == 0) { is_uring = 1; }
                if (strncmp(line, "SqRingEntries:", 14) == 0)
                    sscanf(line + 14, " %u", &sq_sz);
                if (strncmp(line, "CqRingEntries:", 14) == 0)
                    sscanf(line + 14, " %u", &cq_sz);
            }
            fclose(f);

            if (is_uring || sq_sz > 0) {
                uring_data_t *u = &d->urings[d->uring_count];
                memset(u, 0, sizeof(*u));
                /* fd entries are small integers — name fits in 32 bytes */
                snprintf(u->name, sizeof(u->name), "pid%d-fd%.8s", pid, fe->d_name);
                u->active      = 1;
                u->sq_entries  = sq_sz ? sq_sz : 64;
                u->cq_entries  = cq_sz ? cq_sz : 128;
                u->ts_ms       = (uint64_t)time(NULL) * 1000;
                d->uring_count++;
            }
        }
        closedir(fdir);
    }
    closedir(proc);
}

/* Populate ring buffer records from known shm paths */
static void collect_ring_buffers(monitor_data_t *d)
{
    static const char *known_shmem[] = {
        "/dev/shm/sm_ring",  "/dev/shm/audio_ring",
        "/dev/shm/camera_ring", "/dev/shm/sensor_ring",
        "/dev/shm/gpio_ring", NULL
    };
    d->ringbuf_count = 0;
    for (int i = 0; known_shmem[i] && d->ringbuf_count < MAX_RINGBUF_INST; i++) {
        struct stat st;
        if (stat(known_shmem[i], &st) != 0) continue;
        ringbuf_data_t *rb = &d->ring_buffers[d->ringbuf_count];
        memset(rb, 0, sizeof(*rb));
        const char *base = strrchr(known_shmem[i], '/');
        strncpy(rb->name, base ? base+1 : known_shmem[i], sizeof(rb->name)-1);
        rb->active   = 1;
        rb->capacity = (uint32_t)(st.st_size > 0 ? st.st_size / 64 : 256);
        rb->ts_ms    = (uint64_t)time(NULL)*1000;
        d->ringbuf_count++;
    }
}

/* Populate IPC channel records from unix socket paths */
static void collect_ipc_channels(monitor_data_t *d)
{
    static const struct { const char *path; const char *name; } known_ipc[] = {
        { "/tmp/servicemanager.sock",     "SM main"       },
        { "/tmp/audio_hal.sock",          "audio HAL"     },
        { "/tmp/camera_hal.sock",         "camera HAL"    },
        { "/tmp/sensor_hal.sock",         "sensor HAL"    },
        { "/tmp/gpio_hal.sock",           "gpio HAL"      },
        { "/tmp/security_manager.sock",   "security mgr"  },
        { NULL, NULL }
    };
    d->ipc_count = 0;
    for (int i = 0; known_ipc[i].path && d->ipc_count < MAX_IPC_CHANNELS; i++) {
        ipc_chan_data_t *ic = &d->ipc_channels[d->ipc_count];
        memset(ic, 0, sizeof(*ic));
        strncpy(ic->name, known_ipc[i].name, sizeof(ic->name)-1);
        strncpy(ic->type, "unix_sock", sizeof(ic->type)-1);
        struct stat st;
        ic->active = (stat(known_ipc[i].path, &st) == 0 && S_ISSOCK(st.st_mode)) ? 1 : 0;
        ic->ts_ms  = (uint64_t)time(NULL)*1000;
        d->ipc_count++;
    }
}

/* Populate memory pool records from /proc/meminfo + known shm */
static void collect_memory_pools(monitor_data_t *d)
{
    static const char *pool_shm[] = {
        "/dev/shm/sm_pool",  "/dev/shm/audio_pool", "/dev/shm/msg_pool", NULL
    };
    d->pool_count = 0;
    for (int i = 0; pool_shm[i] && d->pool_count < MAX_POOL_INST; i++) {
        struct stat st;
        if (stat(pool_shm[i], &st) != 0) continue;
        pool_data_t *p = &d->pools[d->pool_count];
        memset(p, 0, sizeof(*p));
        const char *base = strrchr(pool_shm[i], '/');
        strncpy(p->name, base ? base+1 : pool_shm[i], sizeof(p->name)-1);
        p->active       = 1;
        p->block_size   = 64;
        p->total_blocks = (uint32_t)(st.st_size / 64);
        p->used_blocks  = p->total_blocks / 2;  /* estimate until IPC connected */
        p->free_blocks  = p->total_blocks - p->used_blocks;
        p->usage_pct    = p->total_blocks > 0
                          ? 100.0f * p->used_blocks / p->total_blocks : 0.0f;
        p->ts_ms        = (uint64_t)time(NULL)*1000;
        d->pool_count++;
    }
}

/* Populate watchdog from /proc scan + heartbeat files */
static void collect_watchdog(monitor_data_t *d)
{
    d->watchdog.count         = 0;
    d->watchdog.total_restarts= 0;

    for (int i = 0; i < d->service_count; i++) {
        const service_data_t *s = &d->services[i];
        watchdog_entry_data_t *w = &d->watchdog.entries[d->watchdog.count];
        memset(w, 0, sizeof(*w));
        strncpy(w->service_name, s->name, sizeof(w->service_name)-1);
        w->active                = 1;
        w->alive                 = s->running;
        w->last_heartbeat_ms     = (uint64_t)time(NULL)*1000;
        w->heartbeat_interval_ms = 5000;
        w->restart_count         = (uint32_t)s->restart_count;
        w->escalation_state      = s->health_score < 50 ? 2 :
                                   s->health_score < 70 ? 1 : 0;
        d->watchdog.total_restarts += w->restart_count;
        d->watchdog.count++;
    }
}

/* Populate proxy data from /proc/[pid]/comm = proxy or middleware_proxy */
static void collect_proxy(monitor_data_t *d)
{
    memset(&d->proxy, 0, sizeof(d->proxy));
    static const char *proxy_names[] = { "proxy", "middleware_proxy", NULL };

    DIR *proc = opendir("/proc");
    if (!proc) return;

    struct dirent *pe;
    while ((pe = readdir(proc)) != NULL) {
        if (!isdigit((unsigned char)pe->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(pe->d_name);
        char comm[32] = {0};
        if (read_proc_comm(pid, comm, sizeof(comm)) != 0) continue;
        for (int i = 0; proxy_names[i]; i++) {
            if (strcmp(comm, proxy_names[i]) == 0) {
                d->proxy.active = 1;
                d->proxy.pid    = pid;
                long rss = 0, vsz = 0; int thr = 0;
                read_proc_status(pid, &rss, &vsz, &thr);
                d->proxy.rss_kb = rss;
                d->proxy.ts_ms  = (uint64_t)time(NULL)*1000;
                break;
            }
        }
        if (d->proxy.active) break;
    }
    closedir(proc);
}

/* Populate SM detail — reads process stats, expands sm_socket info */
static void collect_sm_detail(monitor_data_t *d)
{
    sm_detail_data_t *sm = &d->sm;
    int prev_avail = sm->available;
    memset(sm, 0, sizeof(*sm));
    sm->available = d->sm_socket_ok ? 1 : 0;

    /* Find SM process */
    for (int i = 0; i < d->service_count; i++) {
        if (strcmp(d->services[i].name, "servicemanager") == 0 ||
            strcmp(d->services[i].name, "bankai") == 0) {
            sm->pid     = d->services[i].pid;
            sm->rss_kb  = d->services[i].rss_kb;
            sm->cpu_pct = d->services[i].cpu_pct;
            /* CPU sparkline */
            if (sm->cpu_hist_idx < SPARKLINE_LEN)
                sm->cpu_history[sm->cpu_hist_idx++] = sm->cpu_pct;
            break;
        }
    }
    sm->registered_services = (uint32_t)d->service_count;
    sm->max_services         = MAX_SERVICES;
    sm->pool_max             = 32;
    sm->threads_total        = 4;
    sm->ts_ms                = (uint64_t)time(NULL)*1000;

    /* Log SM reconnect */
    if (prev_avail == 0 && sm->available == 1)
        LOG_INFO(d, LC_SM, "SM socket reconnected  %s", d->sm_socket_path);
    else if (prev_avail == 1 && sm->available == 0)
        LOG_WARN(d, LC_SM, "SM socket connection lost");
}

/* Populate static collector status table */
static void collect_collector_status(monitor_data_t *d)
{
    static const struct { const char *name; uint32_t interval_ms; } tbl[] = {
        { "sysinfo",        1000 }, { "services",       1000 },
        { "hal",            2000 }, { "sm",             2000 },
        { "processes",      2000 }, { "security",       5000 },
        { "io_uring",       1000 }, { "ring_buffer",    1000 },
        { "ipc_channels",   2000 }, { "memory_pool",    2000 },
        { "watchdog",       1000 }, { "proxy",          2000 },
        { "config_watcher", 5000 }, { NULL, 0 }
    };
    d->collector_count = 0;
    uint64_t now_ms = (uint64_t)time(NULL) * 1000;
    for (int i = 0; tbl[i].name && d->collector_count < MAX_COLLECTORS; i++) {
        collector_status_t *cs = &d->collectors[d->collector_count++];
        strncpy(cs->name, tbl[i].name, sizeof(cs->name)-1);
        cs->interval_ms    = tbl[i].interval_ms;
        cs->last_update_ms = now_ms;
        cs->state          = COLL_LIVE;   /* Will be OFFLINE when SM unavail */
        cs->collect_count  = 1;
    }
    /* Mark SM-dependent collectors as OFFLINE if socket down */
    if (!d->sm_socket_ok) {
        for (int i = 0; i < d->collector_count; i++) {
            if (strcmp(d->collectors[i].name, "sm") == 0 ||
                strcmp(d->collectors[i].name, "security") == 0)
                d->collectors[i].state = COLL_OFFLINE;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * Data collection thread (runs every 1000 ms)
 * ════════════════════════════════════════════════════════════════════════════ */

static void *data_collector_thread(void *arg)
{
    (void)arg;

    cpu_stat_t cpu_baseline;
    read_cpu_stat(&cpu_baseline);

    while (g_running) {
        monitor_data_t tmp;
        memset(&tmp, 0, sizeof(tmp));

        /* Copy previous state under lock */
        pthread_mutex_lock(&g_lock);
        memcpy(tmp.services,     g_data.services,     sizeof(g_data.services));
        tmp.service_count      = g_data.service_count;
        memcpy(tmp.log_entries,  g_data.log_entries,  sizeof(g_data.log_entries));
        tmp.log_head           = g_data.log_head;
        tmp.log_count          = g_data.log_count;
        memcpy(tmp.alert_lines,  g_data.alert_lines,  sizeof(g_data.alert_lines));
        tmp.alert_count        = g_data.alert_count;
        memcpy(tmp.cpu_history,  g_data.cpu_history,  sizeof(g_data.cpu_history));
        tmp.cpu_hist_idx       = g_data.cpu_hist_idx;
        pthread_mutex_unlock(&g_lock);

        /* ── CPU ──────────────────────────────────────────────────────── */
        cpu_stat_t cpu_now;
        if (read_cpu_stat(&cpu_now) == 0) {
            tmp.cpu_total_pct  = cpu_delta_pct(&cpu_baseline, &cpu_now);
            tmp.cpu_iowait_pct = cpu_iowait_pct(&cpu_baseline, &cpu_now);
            tmp.cpu_initialized = 1;
            cpu_baseline        = cpu_now;
            /* Sparkline */
            tmp.cpu_history[tmp.cpu_hist_idx % SPARKLINE_LEN] = tmp.cpu_total_pct;
            tmp.cpu_hist_idx++;
        }

        /* ── Memory / load / uptime ───────────────────────────────────── */
        read_meminfo(&tmp);
        read_loadavg(&tmp);
        read_uptime(&tmp);
        tmp.num_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);

        /* ── Services ─────────────────────────────────────────────────── */
        {
            monitor_data_t stmp;
            memset(&stmp, 0, sizeof(stmp));
            memcpy(stmp.services, tmp.services, sizeof(tmp.services));
            stmp.service_count = tmp.service_count;
            scan_processes(&stmp);
            merge_prev_service_state(&stmp, &tmp);
            memcpy(tmp.services, stmp.services, sizeof(stmp.services));
            tmp.service_count = stmp.service_count;
        }

        /* ── SM socket ────────────────────────────────────────────────── */
        static const char *SM_PATHS[] = {
            "/tmp/servicemanager.sock", "/run/servicemanager.sock",
            "/var/run/servicemanager.sock", NULL
        };
        tmp.sm_socket_ok = 0;
        for (int i = 0; SM_PATHS[i]; i++) {
            if (try_sm_socket(SM_PATHS[i])) {
                tmp.sm_socket_ok = 1;
                strncpy(tmp.sm_socket_path, SM_PATHS[i], sizeof(tmp.sm_socket_path)-1);
                break;
            }
        }

        /* ── Subsystem collectors ─────────────────────────────────────── */
        collect_io_uring(&tmp);
        collect_ring_buffers(&tmp);
        collect_ipc_channels(&tmp);
        collect_memory_pools(&tmp);
        collect_watchdog(&tmp);
        collect_proxy(&tmp);
        collect_sm_detail(&tmp);
        collect_collector_status(&tmp);

        /* ── Alerts + logs ────────────────────────────────────────────── */
        tmp.alert_count = 0;   /* reset per-cycle */
        check_alerts(&tmp);

        LOG_INFO(&tmp, LC_SYSTEM,
                 "CPU:%.1f%%(io+%.1f%%)  RAM:%ld/%ldMB  Swap:%ld/%ldMB  Load:%.2f  Up:%ldd",
                 tmp.cpu_total_pct, tmp.cpu_iowait_pct,
                 tmp.ram_used_kb/1024, tmp.ram_total_kb/1024,
                 tmp.swap_used_kb/1024, tmp.swap_total_kb/1024,
                 tmp.load_1, tmp.uptime_s/86400);

        static int prev_sm_ok = -1;
        if (prev_sm_ok != tmp.sm_socket_ok) {
            if (tmp.sm_socket_ok)
                LOG_INFO(&tmp, LC_NET, "SM socket CONNECTED  %s", tmp.sm_socket_path);
            else
                LOG_WARN(&tmp, LC_NET, "SM socket NOT connected");
            prev_sm_ok = tmp.sm_socket_ok;
        }

        for (int i = 0; i < tmp.service_count; i++) {
            const service_data_t *s = &tmp.services[i];
            LOG_DEBUG(&tmp, LC_SERVICE,
                      "%-16s  PID:%-7d  CPU:%5.1f%%  RAM:%5ldMB  FDs:%-4d  H:%3d",
                      s->name, s->pid, s->cpu_pct, s->rss_kb/1024,
                      s->fd_count >= 0 ? s->fd_count : 0, s->health_score);
        }
        if (tmp.service_count == 0)
            LOG_WARN(&tmp, LC_SERVICE, "No middleware services detected in /proc");

        tmp.last_update = time(NULL);

        /* ── Publish ──────────────────────────────────────────────────── */
        pthread_mutex_lock(&g_lock);
        memcpy(&g_data, &tmp, sizeof(g_data));
        pthread_mutex_unlock(&g_lock);

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

    init_pair(CP_DEFAULT,    COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_HEADER,     COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_SELECTED,   COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_GREEN,      COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_YELLOW,     COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_RED,        COLOR_RED,     COLOR_BLACK);
    init_pair(CP_CYAN,       COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_DIM,        COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_BLUE,       COLOR_BLUE,    COLOR_BLACK);
    init_pair(CP_MAGENTA,    COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_BAR_OK,     COLOR_GREEN,   COLOR_GREEN);
    init_pair(CP_BAR_WARN,   COLOR_YELLOW,  COLOR_YELLOW);
    init_pair(CP_BAR_CRIT,   COLOR_RED,     COLOR_RED);
    init_pair(CP_WHITE_BOLD, COLOR_WHITE,   COLOR_BLACK);
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
            mvwprintw(win, row, 16, "NOT RUNNING  (process not detected in /proc)");
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
 * Panel: I/O & io_uring (tab 4)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_io(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-22)/2, " I/O & io_uring Detail ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row = 3;

    /* ── Disk I/O ─────────────────────────────────────────────────── */
    wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Block Device I/O:"); wattroff(win, A_BOLD);
    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-12s %10s %10s %10s %10s", "Device","Reads","Writes","RdKB","WrKB");
    wattroff(win, A_UNDERLINE);
    FILE *f = fopen("/proc/diskstats","r");
    if (f) {
        char line[256]; int shown=0;
        while (fgets(line, sizeof(line), f) && row < rows-14 && shown < 5) {
            char dev[32]; unsigned long r,w,sr,sw; int maj,min;
            if (sscanf(line,"%d %d %31s %lu %*u %lu %*u %lu %*u %lu",
                       &maj,&min,dev,&r,&sr,&w,&sw) >= 6) {
                if (strncmp(dev,"loop",4)==0||strncmp(dev,"ram",3)==0) continue;
                if (r==0&&w==0) continue;
                mvwprintw(win,row++,2,"%-12s %10lu %10lu %10lu %10lu",dev,r,w,sr/2,sw/2);
                shown++;
            }
        }
        fclose(f);
    }
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);

    /* ── io_uring instances ───────────────────────────────────────── */
    wattron(win, A_BOLD);
    mvwprintw(win, row++, 2, "io_uring Instances (%d detected):", d->uring_count);
    wattroff(win, A_BOLD);
    if (d->uring_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, row++, 4, "(none detected - no middleware io_uring users running)");
        wattroff(win, COLOR_PAIR(CP_DIM));
    } else {
        wattron(win, A_UNDERLINE);
        mvwprintw(win, row++, 2, "%-22s %6s %6s %6s %6s %8s %8s",
                  "Name","SQ","CQ","Pend","Ready","CQ Fill%","Errs");
        wattroff(win, A_UNDERLINE);
        for (int i = 0; i < d->uring_count && row < rows-4; i++) {
            const uring_data_t *u = &d->urings[i];
            int cp = u->cq_fill_pct > 80.0f ? CP_RED :
                     u->cq_fill_pct > 60.0f ? CP_YELLOW : CP_GREEN;
            wattron(win, COLOR_PAIR(cp));
            mvwprintw(win, row++, 2, "%-22s %6u %6u %6u %6u %7.1f%% %8lu",
                      u->name, u->sq_entries, u->cq_entries,
                      u->sq_pending, u->cq_ready, u->cq_fill_pct, u->err_total);
            wattroff(win, COLOR_PAIR(cp));
        }
    }
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);

    /* ── Per-service FDs ──────────────────────────────────────────── */
    wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Per-Service File Descriptors:"); wattroff(win, A_BOLD);
    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-22s %8s %8s", "Service","FDs","Threads");
    wattroff(win, A_UNDERLINE);
    for (int i = 0; i < d->service_count && row < rows-2; i++) {
        const service_data_t *s = &d->services[i];
        int fc = s->fd_count>500 ? CP_RED : s->fd_count>200 ? CP_YELLOW : CP_GREEN;
        wattron(win, COLOR_PAIR(fc));
        mvwprintw(win, row++, 2, "%-22s %8d %8d",
                  s->name, s->fd_count>=0?s->fd_count:0, s->thread_count);
        wattroff(win, COLOR_PAIR(fc));
    }
    if (d->service_count == 0) {
        wattron(win, COLOR_PAIR(CP_YELLOW));
        mvwprintw(win, row, 2, "io_uring stats unavailable - no services detected");
        wattroff(win, COLOR_PAIR(CP_YELLOW));
    }
    (void)rows; wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Ring Buffers (tab 5)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_ringbufs(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-22)/2, " Ring Buffer Status ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row=3, bw=20;

    if (d->ringbuf_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, row++, 2, "(no ring buffers detected - /dev/shm/ entries not found)");
        wattroff(win, COLOR_PAIR(CP_DIM));
    } else {
        wattron(win, A_UNDERLINE);
        mvwprintw(win, row++, 2, "%-20s %8s %8s %8s %8s %8s",
                  "Name","Cap","Used","Fill%","Writes","Drops");
        wattroff(win, A_UNDERLINE);
        for (int i = 0; i < d->ringbuf_count && row < rows-6; i++) {
            const ringbuf_data_t *rb = &d->ring_buffers[i];
            int cp = rb->drop_count>0 ? CP_RED :
                     rb->fill_pct>80.0f ? CP_YELLOW : CP_GREEN;
            wattron(win, COLOR_PAIR(cp)|A_BOLD);
            mvwprintw(win, row, 2, "%-20s", rb->name);
            wattroff(win, A_BOLD);
            mvwprintw(win, row, 23, "%8u %8u", rb->capacity, rb->used);
            wattroff(win, COLOR_PAIR(cp));

            /* Fill bar */
            draw_bar(win, row, 41, bw, rb->fill_pct);
            wattron(win, COLOR_PAIR(cp));
            mvwprintw(win, row, 41+bw+1, "%5.1f%%  wr:%lu  drops:%lu",
                      rb->fill_pct, rb->write_total, rb->drop_count);
            wattroff(win, COLOR_PAIR(cp));
            row++;
        }
    }
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, rows-2, 2,
              "Ring buffers detected via /dev/shm/.  Connect SM socket for live counters.");
    wattroff(win, COLOR_PAIR(CP_DIM));
    (void)rows; (void)bw; wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: IPC Channels (tab 6)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_ipc(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-22)/2, " IPC Channel Status ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row=3;

    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-18s %-10s %-6s %10s %10s %10s %10s",
              "Channel","Type","State","BytesSent","BytesRecv","MsgsSent","MsgsRecv");
    wattroff(win, A_UNDERLINE);

    for (int i = 0; i < d->ipc_count && row < rows-4; i++) {
        const ipc_chan_data_t *ic = &d->ipc_channels[i];
        int cp = ic->active ? CP_GREEN : CP_YELLOW;
        wattron(win, COLOR_PAIR(cp));
        mvwprintw(win, row++, 2, "%-18s %-10s %-6s %10lu %10lu %10lu %10lu",
                  ic->name, ic->type,
                  ic->active ? "UP" : "DOWN",
                  ic->bytes_sent, ic->bytes_recv,
                  ic->msgs_sent,  ic->msgs_recv);
        wattroff(win, COLOR_PAIR(cp));
    }
    if (d->ipc_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, row++, 2, "(no IPC channels detected)");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    mvwhline(win, row++, 1, ACS_HLINE, cols-2);

    /* Queue depth summary */
    if (d->ipc_count > 0) {
        wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Queue Depths:"); wattroff(win, A_BOLD);
        for (int i = 0; i < d->ipc_count && row < rows-3; i++) {
            const ipc_chan_data_t *ic = &d->ipc_channels[i];
            if (!ic->active) continue;
            float sq_pct = ic->send_q_max > 0
                           ? 100.0f*ic->send_q_depth/ic->send_q_max : 0.0f;
            mvwprintw(win, row, 4, "%-18s  SQ:", ic->name);
            draw_bar(win, row, 28, 20, sq_pct);
            mvwprintw(win, row++, 50, " %u/%u", ic->send_q_depth, ic->send_q_max);
        }
    }

    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, rows-2, 2, "Channel state from socket presence. Full metrics require SM connection.");
    wattroff(win, COLOR_PAIR(CP_DIM));
    (void)rows; wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Watchdog (tab 8)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_watchdog(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-22)/2, " Watchdog Status ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);

    /* badge */
    int dead = 0;
    for (int i = 0; i < d->watchdog.count; i++)
        if (!d->watchdog.entries[i].alive) dead++;
    if (dead > 0) {
        wattron(win, COLOR_PAIR(CP_RED)|A_BOLD|A_BLINK);
        mvwprintw(win, 1, cols-16, " %d TIMED-OUT ", dead);
        wattroff(win, COLOR_PAIR(CP_RED)|A_BOLD|A_BLINK);
    } else {
        wattron(win, COLOR_PAIR(CP_GREEN)|A_BOLD);
        mvwprintw(win, 1, cols-14, " ALL ALIVE ");
        wattroff(win, COLOR_PAIR(CP_GREEN)|A_BOLD);
    }

    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row=3;

    /* Summary */
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, row++, 2, "Tracked: %d   Total restarts: %u",
              d->watchdog.count, d->watchdog.total_restarts);
    wattroff(win, COLOR_PAIR(CP_DIM));
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);

    /* Table */
    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-20s %-8s %-10s %8s %10s %8s",
              "Service","State","Escalation","Restarts","Interval ms","Missed");
    wattroff(win, A_UNDERLINE);

    static const char *esc_names[] = { "none","WARN","RESTART","KILL" };

    for (int i = 0; i < d->watchdog.count && row < rows-4; i++) {
        const watchdog_entry_data_t *w = &d->watchdog.entries[i];
        int cp = !w->alive ? CP_RED :
                 w->escalation_state >= 2 ? CP_YELLOW : CP_GREEN;
        wattron(win, COLOR_PAIR(cp)|A_BOLD);
        mvwprintw(win, row, 2, "%-20s", w->service_name);
        wattroff(win, A_BOLD);
        wattron(win, COLOR_PAIR(cp));
        mvwprintw(win, row++, 23, "%-8s %-10s %8u %10u %8d",
                  w->alive ? "ALIVE" : "DEAD",
                  w->escalation_state < 4 ? esc_names[w->escalation_state] : "?",
                  w->restart_count,
                  w->heartbeat_interval_ms,
                  w->missed_pings);
        wattroff(win, COLOR_PAIR(cp));
    }
    if (d->watchdog.count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, row, 2, "(no watchdog entries - services not running)");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, rows-2, 2,
              "Watchdog state derived from /proc scan. Live heartbeats require SM socket.");
    wattroff(win, COLOR_PAIR(CP_DIM));
    (void)rows; wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Proxy (tab 9)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_proxy(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-18)/2, " Proxy Subsystem ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row=3;
    const proxy_data_t *p = &d->proxy;

    if (!p->active) {
        wattron(win, COLOR_PAIR(CP_YELLOW));
        mvwprintw(win, row, 2, "Proxy process not detected. Start proxy to see metrics.");
        wattroff(win, COLOR_PAIR(CP_YELLOW));
        wrefresh(win); return;
    }

    wattron(win, COLOR_PAIR(CP_GREEN)|A_BOLD);
    mvwprintw(win, row++, 2, "Proxy RUNNING  PID: %d  RSS: %ld MB  CPU: %.1f%%",
              p->pid, p->rss_kb/1024, p->cpu_pct);
    wattroff(win, COLOR_PAIR(CP_GREEN)|A_BOLD);
    row++;

    mvwprintw(win, row++, 2, "  Active connections : %u", p->active_conns);
    mvwprintw(win, row++, 2, "  Total connections  : %u", p->total_conns);
    mvwprintw(win, row++, 2, "  Requests/s        : %.1f", p->req_per_s);
    mvwprintw(win, row++, 2, "  Requests total    : %lu", p->req_total);
    mvwprintw(win, row++, 2, "  Errors            : %lu", p->req_err);
    mvwprintw(win, row++, 2, "  Bytes fwd         : %lu", p->bytes_forwarded);
    mvwprintw(win, row++, 2, "  Latency avg/p99   : %.2f ms / %.2f ms",
              p->lat_avg_ms, p->lat_p99_ms);
    row++;
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);

    if (p->upstream_count > 0) {
        wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Upstream Backends:"); wattroff(win, A_BOLD);
        wattron(win, A_UNDERLINE);
        mvwprintw(win, row++, 2, "%-24s %-8s %10s %12s", "Name","Health","Reqs","AvgLat ms");
        wattroff(win, A_UNDERLINE);
        for (int i = 0; i < p->upstream_count && row < rows-3; i++) {
            int cp = p->upstreams[i].healthy ? CP_GREEN : CP_RED;
            wattron(win, COLOR_PAIR(cp));
            mvwprintw(win, row++, 2, "%-24s %-8s %10u %12.2f",
                      p->upstreams[i].name,
                      p->upstreams[i].healthy ? "UP" : "DOWN",
                      p->upstreams[i].active_reqs,
                      p->upstreams[i].lat_avg_ms);
            wattroff(win, COLOR_PAIR(cp));
        }
    }
    (void)rows; wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: SM Detail (tab 10)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_sm_detail(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-26)/2, " Service Manager Deep-Dive ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row=3;
    const sm_detail_data_t *sm = &d->sm;

    /* Connection status */
    int cp = sm->available ? CP_GREEN : CP_YELLOW;
    wattron(win, COLOR_PAIR(cp)|A_BOLD);
    mvwprintw(win, row++, 2, "Socket: %s  PID: %d  RSS: %ld MB  CPU: %.1f%%",
              sm->available ? "CONNECTED" : "OFFLINE",
              sm->pid, sm->rss_kb/1024, sm->cpu_pct);
    wattroff(win, COLOR_PAIR(cp)|A_BOLD);
    row++;

    /* Registry */
    mvwprintw(win, row++, 2, "  Registered services : %u / %u",
              sm->registered_services, sm->max_services);
    /* Registry fill bar */
    float reg_pct = sm->max_services > 0
                    ? 100.0f*sm->registered_services/sm->max_services : 0.0f;
    mvwprintw(win, row, 4, "Registry: ");
    draw_bar(win, row++, 14, 24, reg_pct);

    /* Connection pool */
    mvwprintw(win, row++, 2, "  Pool  active/idle/max : %u / %u / %u",
              sm->pool_active, sm->pool_idle, sm->pool_max);

    /* Thread pool */
    mvwprintw(win, row++, 2, "  Threads busy/total    : %u / %u",
              sm->threads_busy, sm->threads_total);

    /* TLS */
    mvwprintw(win, row++, 2, "  TLS sessions          : %u", sm->tls_sessions);
    row++;

    /* Health check latencies */
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);
    wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Health Check Latencies (us):"); wattroff(win, A_BOLD);
    mvwprintw(win, row++, 4, "p50 :%6u   p95 :%6u   p99 :%6u   p999:%6u",
              sm->health_p50_us, sm->health_p95_us,
              sm->health_p99_us, sm->health_p999_us);
    row++;

    /* Rate limiting */
    wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Rate Limiting:"); wattroff(win, A_BOLD);
    mvwprintw(win, row++, 4, "Accept/s: %u   Reject/s: %u   Global: %.1f%%",
              sm->rate_accept_per_s, sm->rate_reject_per_s, sm->rate_global_pct);
    row++;

    /* CPU sparkline */
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);
    wattron(win, A_BOLD); mvwprintw(win, row++, 2, "CPU Sparkline:"); wattroff(win, A_BOLD);
    int spark_w = (cols > 50) ? cols-10 : 40;
    if (spark_w > SPARKLINE_LEN) spark_w = SPARKLINE_LEN;
    for (int i = 0; i < spark_w && row < rows-4; i++) {
        float v = sm->cpu_history[(sm->cpu_hist_idx + i) % SPARKLINE_LEN];
        int cp2 = v>80.0f ? CP_RED : v>60.0f ? CP_YELLOW : CP_GREEN;
        int bar_h = (int)(v/100.0f*5);
        wattron(win, COLOR_PAIR(cp2));
        /* ASCII sparkline: space + 5 levels using pipe/block chars safe on all terminals */
        const char *blks[] = {" ", ".", ":", "|", "I", "#"};
        mvwprintw(win, row, 4+i, "%s", blks[bar_h < 5 ? bar_h : 5]);
        wattroff(win, COLOR_PAIR(cp2));
    }
    row++;

    /* Peers */
    if (sm->peer_count > 0 && row < rows-4) {
        mvwhline(win, row++, 1, ACS_HLINE, cols-2);
        wattron(win, A_BOLD); mvwprintw(win, row++, 2, "Registered Peers:"); wattroff(win, A_BOLD);
        wattron(win, A_UNDERLINE);
        mvwprintw(win, row++, 2, "%-24s %-10s %12s %12s", "Name","State","Msgs In","Msgs Out");
        wattroff(win, A_UNDERLINE);
        for (int i = 0; i < sm->peer_count && row < rows-2; i++) {
            int pc = sm->peers[i].connected ? CP_GREEN : CP_YELLOW;
            wattron(win, COLOR_PAIR(pc));
            mvwprintw(win, row++, 2, "%-24s %-10s %12lu %12lu",
                      sm->peers[i].name,
                      sm->peers[i].connected ? "connected" : "offline",
                      sm->peers[i].msgs_in, sm->peers[i].msgs_out);
            wattroff(win, COLOR_PAIR(pc));
        }
    }
    (void)rows; wrefresh(win);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Panel: Collectors (tab 11)
 * ══════════════════════════════════════════════════════════════════════════ */

static void draw_panel_collectors(WINDOW *win, const monitor_data_t *d)
{
    werase(win); box(win,0,0);
    int rows, cols; getmaxyx(win, rows, cols);
    wattron(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwprintw(win, 1, (cols-22)/2, " Collector Thread Health ");
    wattroff(win, COLOR_PAIR(CP_CYAN)|A_BOLD);
    mvwhline(win, 2, 1, ACS_HLINE, cols-2);
    int row=3;

    static const char *state_str[] = {
        "WAITING","CONNECT","SYNCING","LIVE","STALE","OFFLINE"
    };

    int live=0; for (int i=0;i<d->collector_count;i++) if (d->collectors[i].state==COLL_LIVE) live++;
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, row++, 2, "Total: %d   Live: %d   Offline: %d",
              d->collector_count, live, d->collector_count-live);
    wattroff(win, COLOR_PAIR(CP_DIM));
    mvwhline(win, row++, 1, ACS_HLINE, cols-2);

    wattron(win, A_UNDERLINE);
    mvwprintw(win, row++, 2, "%-20s %-9s %10s %10s %10s",
              "Collector","State","Interval ms","Collections","Errors");
    wattroff(win, A_UNDERLINE);

    for (int i = 0; i < d->collector_count && row < rows-3; i++) {
        const collector_status_t *cs = &d->collectors[i];
        int cp = cs->state == COLL_LIVE   ? CP_GREEN  :
                 cs->state == COLL_STALE  ? CP_YELLOW : CP_RED;
        const char *st = (cs->state <= COLL_OFFLINE)
                         ? state_str[cs->state] : "?";
        wattron(win, COLOR_PAIR(cp));
        mvwprintw(win, row++, 2, "%-20s %-9s %10u %10lu %10lu",
                  cs->name, st, cs->interval_ms,
                  cs->collect_count, cs->error_count);
        wattroff(win, COLOR_PAIR(cp));
    }
    if (d->collector_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, row, 2, "(collector table empty)");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, rows-2, 2,
              "Collector states: LIVE=green  STALE=yellow  OFFLINE=red");
    wattroff(win, COLOR_PAIR(CP_DIM));
    (void)rows; wrefresh(win);
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

/* Log category metadata — must stay in sync with log_cat_t enum */
static const char *LOG_CAT_LABEL[] = {
    "SYS",   /* LC_SYSTEM    */
    "SVC",   /* LC_SERVICE   */
    "HAL",   /* LC_HAL       */
    "MEM",   /* LC_MEMORY    */
    "I/O",   /* LC_IO        */
    "SEC",   /* LC_SECURITY  */
    "HLT",   /* LC_HEALTH    */
    "NET",   /* LC_NET       */
    "URN",   /* LC_URING     */
    "RBF",   /* LC_RINGBUF   */
    "IPC",   /* LC_IPC       */
    "WDG",   /* LC_WATCHDOG  */
    "PRX",   /* LC_PROXY     */
    "SM ",   /* LC_SM        */
    "TRC",   /* LC_TRACE     */
    "COL",   /* LC_COLLECTOR */
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
        "  0-9              Jump to panel 0-9 directly",
        "  F1               This help screen",
        "",
        "PANELS (15 total)",
        "   0  Overview      System summary: CPU, RAM, services, sparkline",
        "   1  Services      Per-service detail: CPU, RAM, FDs, security flags",
        "   2  HAL           Hardware Abstraction Layer - audio/camera/gpio/sensor",
        "   3  Memory        Memory pools, swap, slab, usage bars",
        "   4  I/O           Disk I/O, io_uring instances, per-service FDs",
        "   5  Ring Bufs     Ring buffer fill%, write/read/drop counters",
        "   6  IPC           Unix socket channels, queue depths, byte flow",
        "   7  Security      Seccomp, capabilities, HMAC, health scores",
        "   8  Watchdog      Heartbeat status, escalation state, restart counts",
        "   9  Proxy         Proxy process, connections, latencies, upstreams",
        "  10  SM Detail     Service Manager: registry, pool, TLS, peers, latencies",
        "  11  Collectors    Collector thread health: LIVE/STALE/OFFLINE per source",
        "  12  Alerts        Active system alerts with severity, threshold, suggestion",
        "  13  Logs          Structured event log: level, category, timestamp, message",
        "  14  Help          This screen",
        "",
        "DATA SOURCES",
        "  /proc/stat         CPU usage (delta method, 1s interval)",
        "  /proc/meminfo      RAM, swap, buffers, cached",
        "  /proc/[pid]/stat   Per-process CPU, threads",
        "  /proc/[pid]/status Per-process RSS, VmPeak",
        "  /proc/[pid]/fdinfo io_uring SQ/CQ detection",
        "  /dev/shm/          Ring buffer & pool detection",
        "  Unix sockets       IPC channel state detection",
        "  SM socket          Service Manager metrics (when process running)",
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
    case  0: draw_panel_overview(w, snap);    break;
    case  1: draw_panel_services(w, snap);    break;
    case  2: draw_panel_hal(w, snap);         break;
    case  3: draw_panel_memory(w, snap);      break;
    case  4: draw_panel_io(w, snap);          break;  /* I/O + io_uring */
    case  5: draw_panel_ringbufs(w, snap);    break;  /* Ring Buffers   */
    case  6: draw_panel_ipc(w, snap);         break;  /* IPC Channels   */
    case  7: draw_panel_security(w, snap);    break;  /* Security       */
    case  8: draw_panel_watchdog(w, snap);    break;  /* Watchdog       */
    case  9: draw_panel_proxy(w, snap);       break;  /* Proxy          */
    case 10: draw_panel_sm_detail(w, snap);   break;  /* SM Detail      */
    case 11: draw_panel_collectors(w, snap);  break;  /* Collectors     */
    case 12: draw_panel_alerts(w, snap);      break;  /* Alerts         */
    case 13: draw_panel_logs(w, snap);        break;  /* Logs           */
    case 14: draw_panel_help(w);              break;  /* Help           */
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
