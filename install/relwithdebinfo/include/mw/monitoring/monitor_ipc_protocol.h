/**
 * @file    monitor_ipc_protocol.h
 * @brief   Complete wire-format definitions for the monitord ↔ tui IPC.
 *
 * All structs are packed with fixed-width types.  No dynamic allocation
 * occurs in the serialisation path — the snapshot is a single flat struct.
 *
 * Message flow:
 *   TUI  → monitord : MON_MSG_HELLO
 *   monitord → TUI  : MON_MSG_WELCOME  (or closes socket on version mismatch)
 *   TUI  → monitord : MON_MSG_SNAPSHOT_REQ   (every refresh cycle)
 *   monitord → TUI  : MON_MSG_SNAPSHOT_RESP  (full mon_snapshot_t payload)
 *   TUI  → monitord : MON_MSG_SUBSCRIBE_LOGS / ALERTS / TRACES  (optional)
 *   monitord → TUI  : MON_MSG_LOG_EVENT / ALERT_EVENT / TRACE_EVENT  (pushed)
 *   Either side     : MON_MSG_GOODBYE
 *
 * @thread_safety  Structs are value types — sharing requires external sync.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "monitor_protocol_ver.h"

/* ── Sizing constants ─────────────────────────────────────────────────────── */

#define HAL_MAX_DEVICES         8
#define SERVICE_MAX             4
#define POOL_MAX                8
#define URING_MAX               8
#define RINGBUF_MAX             8
#define PROXY_MAX               8
#define IPC_CHAN_MAX             16
#define TRACE_STORE_MAX         1000
#define TRACE_SPAN_MAX          16      /**< Max spans per trace. */
#define ALERT_STORE_MAX         256
#define LOG_LINE_MAX            256
#define SECCOMP_SYSCALL_MAX     64
#define CONFIG_KEY_MAX          32
#define CONFIG_WATCH_MAX        8
#define COLLECTOR_COUNT         14
#define HEALTH_HISTORY_LEN      3600    /**< 1 h @ 1 s resolution. */
#define SPARKLINE_LEN           64      /**< Points stored for sparklines. */
#define HISTOGRAM_BUCKETS       16

/* ── Message type enumeration ─────────────────────────────────────────────── */

typedef enum {
    MON_MSG_HELLO           = 0x01,
    MON_MSG_WELCOME         = 0x02,
    MON_MSG_SNAPSHOT_REQ    = 0x10,
    MON_MSG_SNAPSHOT_RESP   = 0x11,
    MON_MSG_SUBSCRIBE_LOGS  = 0x20,
    MON_MSG_LOG_EVENT       = 0x21,
    MON_MSG_SUBSCRIBE_ALERTS= 0x22,
    MON_MSG_ALERT_EVENT     = 0x23,
    MON_MSG_SUBSCRIBE_TRACES= 0x24,
    MON_MSG_TRACE_EVENT     = 0x25,
    MON_MSG_GOODBYE         = 0xFF,
} mon_msg_type_t;

/* ── Frame header — prefix every message ──────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t    magic;      /**< MON_MSG_MAGIC = 0x4D4F4E00. */
    uint8_t     type;       /**< mon_msg_type_t. */
    uint8_t     version;    /**< MON_PROTOCOL_VERSION. */
    uint16_t    flags;      /**< Reserved, must be 0. */
    uint32_t    length;     /**< Payload bytes following this header. */
    uint32_t    seq;        /**< Monotonic per-connection sequence number. */
} mon_msg_header_t;

/* ── HELLO / WELCOME ──────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;
    uint8_t  protocol_minor;
    uint16_t capabilities;      /**< Bitfield: bit0=logs, bit1=alerts, bit2=traces. */
    char     client_name[32];   /**< Human-readable TUI process name. */
} mon_hello_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;
    uint8_t  protocol_minor;
    uint16_t capabilities;
    uint32_t snapshot_size;     /**< sizeof(mon_snapshot_t) — TUI sanity-checks this. */
    char     daemon_version[32];
} mon_welcome_payload_t;

/* ── Collector state ──────────────────────────────────────────────────────── */

typedef enum {
    COLLECTOR_STATE_WAITING     = 0,
    COLLECTOR_STATE_CONNECTING  = 1,
    COLLECTOR_STATE_SYNCING     = 2,
    COLLECTOR_STATE_LIVE        = 3,
    COLLECTOR_STATE_STALE       = 4,
    COLLECTOR_STATE_OFFLINE     = 5,
} collector_state_t;

typedef struct __attribute__((packed)) {
    char                name[32];
    collector_state_t   state;
    uint64_t            last_update_ms;
    uint64_t            last_offline_ms;
    uint32_t            interval_ms;
    uint64_t            collect_count;
    uint64_t            error_count;
} collector_info_t;

/* ── Per-subsystem metric structs ─────────────────────────────────────────── */

/** System-wide CPU / memory / load. */
typedef struct __attribute__((packed)) {
    /* Per-core usage (up to 16 cores) */
    float    core_pct[16];
    uint8_t  num_cores;
    float    cpu_total_pct;

    /* Memory */
    uint64_t ram_total_bytes;
    uint64_t ram_used_bytes;
    uint64_t swap_total_bytes;
    uint64_t swap_used_bytes;

    /* Load average */
    float    load_1;
    float    load_5;
    float    load_15;

    /* File descriptors (system-wide) */
    uint32_t fd_used;
    uint32_t fd_max;

    /* Uptime seconds */
    uint64_t uptime_s;

    /* Sparkline histories (SPARKLINE_LEN points each) */
    float    cpu_history[SPARKLINE_LEN];
    float    ram_history[SPARKLINE_LEN];

    uint64_t ts_ms;           /**< Timestamp of this snapshot. */
} sysinfo_metrics_t;

/** Service Manager aggregate metrics. */
typedef struct __attribute__((packed)) {
    uint32_t pid;
    uint64_t uptime_s;
    float    cpu_pct;
    uint64_t rss_bytes;
    uint64_t vsz_bytes;
    uint32_t fd_count;
    uint32_t fd_max;

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

    /* TLS sessions */
    uint32_t tls_sessions;

    /* Health check latencies (microseconds) */
    uint32_t health_p50_us;
    uint32_t health_p95_us;
    uint32_t health_p99_us;
    uint32_t health_p999_us;

    /* Rate limiting */
    uint32_t rate_accept_per_s;
    uint32_t rate_reject_per_s;
    float    rate_global_pct;

    /* Sparklines */
    float    cpu_history[SPARKLINE_LEN];

    uint64_t ts_ms;
} sm_metrics_t;

/** Per-service watchdog entry. */
typedef struct __attribute__((packed)) {
    char     service_name[32];
    uint64_t last_heartbeat_ms;   /**< absolute ms timestamp */
    uint32_t heartbeat_interval_ms;
    uint8_t  alive;               /**< 1 = OK, 0 = timed out */
    uint32_t restart_count;
    uint64_t last_restart_ms;
    uint8_t  escalation_state;    /**< 0=none, 1=WARN, 2=RESTART, 3=KILL */
    uint32_t escalation_count;
} watchdog_entry_t;

typedef struct __attribute__((packed)) {
    watchdog_entry_t entries[SERVICE_MAX];
    uint32_t         count;
    uint32_t         total_restarts;
    uint64_t         ts_ms;
} watchdog_metrics_t;

/** HAL device types. */
typedef enum {
    HAL_TYPE_AUDIO  = 0,
    HAL_TYPE_CAMERA = 1,
    HAL_TYPE_SENSOR = 2,
    HAL_TYPE_GPIO   = 3,
} hal_type_t;

/** Per HAL device. */
typedef struct __attribute__((packed)) {
    char     device_name[32];
    hal_type_t type;
    uint8_t  state;           /**< 0=CLOSED, 1=OPEN, 2=ACTIVE, 3=ERROR */
    int32_t  fd;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t error_count;
    uint32_t lat_us;

    /* Audio-specific */
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bit_depth;
    uint32_t period_frames;
    uint32_t buffer_frames;
    uint32_t underruns;
    uint32_t overruns;

    /* Camera-specific */
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    char     pixel_fmt[8];
    uint32_t bufs_queued;
    uint64_t frames_captured;
    uint64_t frames_dropped;
    uint32_t gaps;

    /* Sensor-specific */
    uint32_t sample_rate_hz;
    float    scale;
    float    x, y, z;
    char     sensor_type[16];

    /* GPIO-specific */
    uint32_t num_pins;
    char     pin_states[16][32];  /**< "pin17:OUT:HIGH:0evt" */

    uint32_t refs;
    uint64_t ts_ms;
} hal_metrics_t;

/** Per-service daemon metrics. */
typedef struct __attribute__((packed)) {
    char     name[32];
    uint32_t pid;
    uint8_t  running;         /**< 0=dead, 1=running */
    uint64_t uptime_s;
    float    cpu_pct;
    uint64_t rss_bytes;
    uint64_t vsz_bytes;
    uint32_t fd_count;
    uint32_t thread_count;
    uint32_t sig_count;
    uint32_t restart_count;

    /* SM connection */
    uint8_t  sm_connected;
    uint32_t sm_reconnects;

    /* Config */
    uint32_t cfg_reload_count;

    /* Security flags */
    uint8_t  sandbox_ok;
    uint8_t  caps_ok;
    uint8_t  verify_ok;
    uint8_t  seccomp_ok;
    uint32_t seccomp_violations;
    uint32_t hmac_failures;
    uint32_t replay_attacks;

    /* Service-specific metrics */
    uint64_t frames_captured;    /* audio/camera */
    uint64_t frames_dropped;
    float    rb_fill_pct;
    uint64_t readings_count;     /* sensor */
    uint32_t gpio_events;        /* gpio */

    /* Ring buffer health */
    uint64_t rb_write_per_s;
    uint64_t rb_read_per_s;
    uint64_t rb_drops;

    /* Health score */
    uint32_t health_score;
    uint8_t  health_color;  /**< 0=green 1=yellow 2=red */

    /* Sparklines */
    float    cpu_history[SPARKLINE_LEN];
    float    ram_history[SPARKLINE_LEN];

    /* Seccomp whitelist (null-separated list) */
    char     seccomp_whitelist[512];

    uint64_t ts_ms;
} service_metrics_t;

/** Memory pool snapshot. */
typedef struct __attribute__((packed)) {
    char     name[32];
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t peak_used;
    double   alloc_per_s;
    uint64_t fail_count;
    float    fragmentation_pct;
    uint32_t avg_scan_slots;
    float    usage_pct;
    float    history[SPARKLINE_LEN];
    uint64_t ts_ms;
} pool_metrics_t;

/** io_uring loop metrics. */
typedef struct __attribute__((packed)) {
    char     name[32];
    uint32_t sq_depth;
    uint32_t cq_depth;
    float    sq_fill_pct;
    float    cq_fill_pct;
    double   submits_per_s;
    double   completions_per_s;
    uint8_t  sqpoll_enabled;
    uint32_t pending_ops;
    uint32_t pending_read;
    uint32_t pending_write;
    uint32_t pending_timeout;
    double   wakeups_per_s;

    /* Latency histogram buckets (us) */
    uint32_t lat_p50_us;
    uint32_t lat_p95_us;
    uint32_t lat_p99_us;
    uint32_t lat_p999_us;
    uint64_t hist_buckets[HISTOGRAM_BUCKETS];
    double   hist_bucket_limits[HISTOGRAM_BUCKETS]; /* us */
    uint64_t hist_sum_us;
    uint64_t hist_count;

    float    cq_history[SPARKLINE_LEN];
    uint64_t ts_ms;
} uring_metrics_t;

/** Ring buffer metrics. */
typedef struct __attribute__((packed)) {
    char     name[32];
    uint32_t item_size;
    uint32_t capacity;
    uint32_t used;
    float    fill_pct;
    double   writes_per_s;
    double   reads_per_s;
    uint64_t drop_count;
    uint64_t wrap_count;
    uint64_t prev_wrap_count;   /**< For delta monitoring. */
    double   throughput_bytes_s;
    float    fill_history[SPARKLINE_LEN];
    uint64_t ts_ms;
} ringbuf_metrics_t;

/** Security module metrics. */
typedef struct __attribute__((packed)) {
    /* Per-service security status (parallel to service_metrics_t) */
    uint32_t seccomp_violations[SERVICE_MAX];
    uint32_t hmac_failures[SERVICE_MAX];
    uint32_t replay_attacks[SERVICE_MAX];

    /* Aggregate */
    uint64_t total_tokens_issued;
    uint64_t total_tokens_refreshed;
    uint64_t total_privilege_drops;

    /* Live security event stream buffer (last N events) */
    uint32_t event_count;
    struct __attribute__((packed)) {
        uint64_t ts_ms;
        uint8_t  level;       /**< 0=INFO, 1=WARN, 2=ERROR, 3=CRIT */
        char     source[16];
        char     message[128];
    } events[64];

    /* Cgroup resource usage */
    struct __attribute__((packed)) {
        uint64_t mem_limit_bytes;
        uint64_t mem_used_bytes;
        float    cpu_limit_pct;
        float    cpu_used_pct;
        uint32_t pid_limit;
        uint32_t pid_count;
    } cgroups[SERVICE_MAX];

    uint64_t ts_ms;
} security_metrics_t;

/** Proxy layer metrics (per proxy instance). */
typedef struct __attribute__((packed)) {
    char     name[32];
    uint8_t  connected;
    uint32_t reconnect_count;
    double   msg_per_s;
    uint32_t queue_depth;
    uint32_t callback_drops;
    double   lat_p99_ms;
    uint64_t ts_ms;
} proxy_metrics_t;

/** IPC channel metrics. */
typedef struct __attribute__((packed)) {
    char     name[48];        /**< e.g. "SM ↔ audio_svc" */
    uint8_t  state;           /**< 0=down, 1=ok, 2=warn(queue>=80%), 3=error */
    uint32_t send_q_depth;
    uint32_t send_q_max;
    uint32_t recv_q_depth;
    uint32_t recv_q_max;
    double   msgs_per_s;
    float    lat_ms;
    uint64_t drop_count;
    uint32_t reconnect_count;
    double   ser_p99_ms;
    double   deser_p99_ms;
    uint64_t ts_ms;
} ipc_metrics_t;

/* ── Distributed tracing ──────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    char     component[16];
    uint64_t enter_us;        /**< Microseconds offset from trace start. */
    uint64_t exit_us;
    uint32_t status;          /**< 0=OK, 1=SLOW, 2=ERROR */
    char     annotation[64];  /**< E.g. "HAL SLOW: ALSA period wait". */
} trace_span_t;

typedef struct __attribute__((packed)) {
    uint32_t     trace_id;
    uint64_t     start_ts_ms;
    uint32_t     total_us;
    uint8_t      span_count;
    trace_span_t spans[TRACE_SPAN_MAX];
    uint8_t      status;      /**< 0=OK, 1=SLOW, 2=ERROR */
    uint32_t     bottleneck_span_idx;
} trace_record_t;

/** ── Alert record ────────────────────────────────────────────────────────── */

typedef enum {
    ALERT_SEV_INFO  = 0,
    ALERT_SEV_WARN  = 1,
    ALERT_SEV_CRIT  = 2,
} alert_severity_t;

typedef struct __attribute__((packed)) {
    uint64_t          first_ts_ms;
    uint64_t          last_ts_ms;
    alert_severity_t  severity;
    char              component[32];
    char              condition[64];
    char              current_value[32];
    char              threshold[32];
    char              suggestion[128];
    uint32_t          occurrences;   /**< Dedup counter. */
    uint8_t           active;
} alert_record_t;

/* ── Config watcher ───────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    char     path[128];
    uint64_t mtime_ms;
    uint64_t last_reload_ms;
    uint8_t  unauthorized_change;    /**< Modified without matching SIGHUP. */
    uint8_t  reload_pending;
    /* Last change diff: "key old→new" */
    char     last_diff[256];
} config_watch_entry_t;

/* ── Health scores ────────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  score;                   /**< 0–100. */
    uint8_t  color;                   /**< 0=green, 1=yellow, 2=red. */
    char     reason[128];
    float    history[HEALTH_HISTORY_LEN]; /**< 1s resolution, 3600 s. */
    uint64_t ts_ms;
} health_score_t;

/* ── Complete snapshot ────────────────────────────────────────────────────── */

/**
 * @brief  The single flat struct sent as MON_MSG_SNAPSHOT_RESP payload.
 *
 * The TUI receives this atomically and renders entirely from it — no partial
 * view of state is ever possible.  Lock ordering in monitord for serialising:
 * sysinfo → hal → ipc_channels → io_uring → memory_pool → proxy →
 * ring_buffer → security → services → sm → traces → watchdog.
 * (Alphabetical by struct field name.)
 */
typedef struct __attribute__((packed)) {
    /* --- Header --- */
    uint64_t            snapshot_ts_ms;     /**< When this snapshot was taken. */
    uint32_t            snapshot_seq;

    /* --- Collector status --- */
    collector_info_t    collectors[COLLECTOR_COUNT];
    uint32_t            collectors_live;
    uint32_t            collectors_stale;
    uint32_t            collectors_offline;

    /* --- Subsystem metrics (alphabetical) --- */
    config_watch_entry_t config_watches[CONFIG_WATCH_MAX];
    uint32_t             config_watch_count;

    hal_metrics_t       hal[HAL_MAX_DEVICES];
    uint32_t            hal_count;

    ipc_metrics_t       ipc_channels[IPC_CHAN_MAX];
    uint32_t            ipc_count;

    uring_metrics_t     urings[URING_MAX];
    uint32_t            uring_count;

    pool_metrics_t      pools[POOL_MAX];
    uint32_t            pool_count;

    proxy_metrics_t     proxies[PROXY_MAX];
    uint32_t            proxy_count;

    ringbuf_metrics_t   ring_buffers[RINGBUF_MAX];
    uint32_t            ringbuf_count;

    security_metrics_t  security;

    service_metrics_t   services[SERVICE_MAX];
    uint32_t            service_count;

    sm_metrics_t        sm;

    /* Trace store: last TRACE_STORE_MAX completed traces */
    trace_record_t      traces[TRACE_STORE_MAX];
    uint32_t            trace_count;
    uint32_t            trace_head;   /**< Circular head pointer. */

    watchdog_metrics_t  watchdog;

    sysinfo_metrics_t   sysinfo;

    /* --- Health --- */
    health_score_t      service_health[SERVICE_MAX];
    health_score_t      system_health;

    /* --- Alerts (recent ALERT_STORE_MAX) --- */
    alert_record_t      alerts[ALERT_STORE_MAX];
    uint32_t            alert_count;
    uint32_t            alerts_warn;
    uint32_t            alerts_crit;

    /* --- Display state --- */
    uint8_t             delta_mode;     /**< 1 if daemon was asked for delta mode. */
} mon_snapshot_t;

/* ── Subscribe / log event payload ───────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  sources;     /**< Bitfield: bit0=SM,1=AUD,2=CAM,3=SEN,4=GPIO,5=HAL,6=SEC,7=PRXY */
    uint8_t  min_level;  /**< Minimum log level to receive. */
} mon_subscribe_logs_t;

typedef struct __attribute__((packed)) {
    uint64_t ts_ms;
    uint8_t  level;       /**< 0=DEBUG,1=INFO,2=WARN,3=ERROR,4=CRIT */
    char     source[12];
    char     message[LOG_LINE_MAX];
} mon_log_event_t;

typedef struct __attribute__((packed)) {
    uint64_t         ts_ms;
    alert_record_t   alert;
} mon_alert_event_t;

typedef struct __attribute__((packed)) {
    uint64_t       ts_ms;
    trace_record_t trace;
} mon_trace_event_t;

/* ── Snapshot request ─────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t refresh_ms;   /**< Client's current refresh rate — informational. */
} mon_snapshot_req_t;
