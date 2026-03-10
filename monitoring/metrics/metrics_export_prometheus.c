/**
 * @file    metrics_export_prometheus.c
 * @brief   Prometheus text format renderer for the full middleware snapshot.
 */
#include "metrics_export_prometheus.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Macro helpers for safe snprintf appending ───────────────────────────── */

#define PROM_APPEND(buf, pos, len, ...) \
    do { \
        int _r = snprintf((buf) + (pos), (len) - (pos), __VA_ARGS__); \
        if (_r < 0 || (size_t)_r >= (len) - (pos)) return -1; \
        (pos) += (size_t)_r; \
    } while (0)

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char *service_name_by_idx(uint32_t i)
{
    static const char *names[] = {
        "audio_service", "camera_service", "sensor_service", "gpio_service"
    };
    if (i < 4) return names[i];
    return "unknown";
}

/* ── Main renderer ───────────────────────────────────────────────────────── */

uint32_t prometheus_render(const mon_snapshot_t *snap,
                           char *buf, uint32_t bufsiz)
{
    if (!snap || !buf || bufsiz == 0) return 0;
    size_t pos = 0;

    /* ─── System Metrics ─────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_system_cpu_total_percent Total CPU usage percent\n"
        "# TYPE middleware_system_cpu_total_percent gauge\n"
        "middleware_system_cpu_total_percent %.2f\n\n",
        (double)snap->sysinfo.cpu_total_pct);

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_system_ram_used_bytes RAM used bytes\n"
        "# TYPE middleware_system_ram_used_bytes gauge\n"
        "middleware_system_ram_used_bytes %llu\n\n",
        (unsigned long long)snap->sysinfo.ram_used_bytes);

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_system_ram_total_bytes RAM total bytes\n"
        "# TYPE middleware_system_ram_total_bytes gauge\n"
        "middleware_system_ram_total_bytes %llu\n\n",
        (unsigned long long)snap->sysinfo.ram_total_bytes);

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_system_load_1m Load average 1 minute\n"
        "# TYPE middleware_system_load_1m gauge\n"
        "middleware_system_load_1m %.2f\n\n",
        (double)snap->sysinfo.load_1);

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_system_uptime_seconds System uptime in seconds\n"
        "# TYPE middleware_system_uptime_seconds counter\n"
        "middleware_system_uptime_seconds %llu\n\n",
        (unsigned long long)snap->sysinfo.uptime_s);

    /* ─── System Health Score ────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_system_health_score Composite system health score 0-100\n"
        "# TYPE middleware_system_health_score gauge\n"
        "middleware_system_health_score %u\n\n",
        (unsigned)snap->system_health.score);

    /* ─── Service CPU / Memory ───────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_service_cpu_percent CPU usage percentage per service\n"
        "# TYPE middleware_service_cpu_percent gauge\n");
    for (uint32_t i = 0; i < snap->service_count && i < SERVICE_MAX; i++) {
        const service_metrics_t *svc = &snap->services[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_service_cpu_percent{service=\"%s\",pid=\"%u\"} %.2f\n",
            svc->name, svc->pid, (double)svc->cpu_pct);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_service_rss_bytes Resident set size per service\n"
        "# TYPE middleware_service_rss_bytes gauge\n");
    for (uint32_t i = 0; i < snap->service_count && i < SERVICE_MAX; i++) {
        const service_metrics_t *svc = &snap->services[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_service_rss_bytes{service=\"%s\",pid=\"%u\"} %llu\n",
            svc->name, svc->pid, (unsigned long long)svc->rss_bytes);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_service_fd_count Open file descriptors per service\n"
        "# TYPE middleware_service_fd_count gauge\n");
    for (uint32_t i = 0; i < snap->service_count && i < SERVICE_MAX; i++) {
        const service_metrics_t *svc = &snap->services[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_service_fd_count{service=\"%s\",pid=\"%u\"} %u\n",
            svc->name, svc->pid, svc->fd_count);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_service_health_score Per-service health score 0-100\n"
        "# TYPE middleware_service_health_score gauge\n");
    for (uint32_t i = 0; i < snap->service_count && i < SERVICE_MAX; i++) {
        const service_metrics_t *svc = &snap->services[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_service_health_score{service=\"%s\"} %u\n",
            svc->name, svc->health_score);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_service_restart_count Total restarts per service\n"
        "# TYPE middleware_service_restart_count counter\n");
    for (uint32_t i = 0; i < snap->service_count && i < SERVICE_MAX; i++) {
        const service_metrics_t *svc = &snap->services[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_service_restart_count{service=\"%s\"} %u\n",
            svc->name, svc->restart_count);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    /* ─── Memory Pools ───────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_memory_pool_used_blocks Currently used blocks\n"
        "# TYPE middleware_memory_pool_used_blocks gauge\n");
    for (uint32_t i = 0; i < snap->pool_count && i < POOL_MAX; i++) {
        const pool_metrics_t *p = &snap->pools[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_memory_pool_used_blocks{pool=\"%s\",block_size=\"%u\"} %u\n",
            p->name, p->block_size, p->used_blocks);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_memory_pool_free_blocks Free blocks available\n"
        "# TYPE middleware_memory_pool_free_blocks gauge\n");
    for (uint32_t i = 0; i < snap->pool_count && i < POOL_MAX; i++) {
        const pool_metrics_t *p = &snap->pools[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_memory_pool_free_blocks{pool=\"%s\",block_size=\"%u\"} %u\n",
            p->name, p->block_size, p->free_blocks);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_memory_pool_alloc_failures_total Total allocation failures\n"
        "# TYPE middleware_memory_pool_alloc_failures_total counter\n");
    for (uint32_t i = 0; i < snap->pool_count && i < POOL_MAX; i++) {
        const pool_metrics_t *p = &snap->pools[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_memory_pool_alloc_failures_total{pool=\"%s\",block_size=\"%u\"} %llu\n",
            p->name, p->block_size, (unsigned long long)p->fail_count);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    /* ─── io_uring ───────────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_uring_sq_fill_ratio SQ ring fill ratio 0-1\n"
        "# TYPE middleware_uring_sq_fill_ratio gauge\n");
    for (uint32_t i = 0; i < snap->uring_count && i < URING_MAX; i++) {
        const uring_metrics_t *u = &snap->urings[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_uring_sq_fill_ratio{loop=\"%s\"} %.4f\n",
            u->name, (double)u->sq_fill_pct / 100.0);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_uring_completion_latency_seconds io_uring completion latency\n"
        "# TYPE middleware_uring_completion_latency_seconds histogram\n");
    for (uint32_t i = 0; i < snap->uring_count && i < URING_MAX; i++) {
        const uring_metrics_t *u = &snap->urings[i];
        /* Emit histogram buckets */
        for (uint32_t b = 0; b < HISTOGRAM_BUCKETS; b++) {
            if (u->hist_bucket_limits[b] <= 0.0) continue;
            PROM_APPEND(buf, pos, bufsiz,
                "middleware_uring_completion_latency_seconds_bucket"
                "{loop=\"%s\",le=\"%.6f\"} %llu\n",
                u->name,
                u->hist_bucket_limits[b] / 1e6,   /* us → s */
                (unsigned long long)u->hist_buckets[b]);
        }
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_uring_completion_latency_seconds_bucket"
            "{loop=\"%s\",le=\"+Inf\"} %llu\n",
            u->name, (unsigned long long)u->hist_count);
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_uring_completion_latency_seconds_sum{loop=\"%s\"} %.6f\n",
            u->name, (double)u->hist_sum_us / 1e6);
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_uring_completion_latency_seconds_count{loop=\"%s\"} %llu\n\n",
            u->name, (unsigned long long)u->hist_count);
    }

    /* ─── Ring Buffers ───────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_ringbuf_fill_ratio Ring buffer fill ratio 0-1\n"
        "# TYPE middleware_ringbuf_fill_ratio gauge\n");
    for (uint32_t i = 0; i < snap->ringbuf_count && i < RINGBUF_MAX; i++) {
        const ringbuf_metrics_t *r = &snap->ring_buffers[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_ringbuf_fill_ratio{buffer=\"%s\"} %.4f\n",
            r->name, (double)r->fill_pct / 100.0);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_ringbuf_drops_total Total dropped items\n"
        "# TYPE middleware_ringbuf_drops_total counter\n");
    for (uint32_t i = 0; i < snap->ringbuf_count && i < RINGBUF_MAX; i++) {
        const ringbuf_metrics_t *r = &snap->ring_buffers[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_ringbuf_drops_total{buffer=\"%s\"} %llu\n",
            r->name, (unsigned long long)r->drop_count);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    /* ─── Security Violations ────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_security_violations_total Security violations by type\n"
        "# TYPE middleware_security_violations_total counter\n");
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const char *sn = service_name_by_idx(i);
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_security_violations_total{service=\"%s\",type=\"seccomp\"} %u\n",
            sn, snap->security.seccomp_violations[i]);
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_security_violations_total{service=\"%s\",type=\"hmac_fail\"} %u\n",
            sn, snap->security.hmac_failures[i]);
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_security_violations_total{service=\"%s\",type=\"replay\"} %u\n",
            sn, snap->security.replay_attacks[i]);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    /* ─── Service Manager ────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_sm_health_p99_us SM health check P99 latency us\n"
        "# TYPE middleware_sm_health_p99_us gauge\n"
        "middleware_sm_health_p99_us %u\n\n",
        snap->sm.health_p99_us);

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_sm_registered_services Registered services count\n"
        "# TYPE middleware_sm_registered_services gauge\n"
        "middleware_sm_registered_services %u\n\n",
        snap->sm.registered_services);

    /* ─── Alert Counts ───────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_alerts_active Active alerts by severity\n"
        "# TYPE middleware_alerts_active gauge\n"
        "middleware_alerts_active{severity=\"warn\"} %u\n"
        "middleware_alerts_active{severity=\"crit\"} %u\n\n",
        snap->alerts_warn, snap->alerts_crit);

    /* ─── Watchdog Metrics ───────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_watchdog_restarts_total Watchdog-triggered restarts\n"
        "# TYPE middleware_watchdog_restarts_total counter\n");
    for (uint32_t i = 0; i < snap->watchdog.count && i < SERVICE_MAX; i++) {
        const watchdog_entry_t *w = &snap->watchdog.entries[i];
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_watchdog_restarts_total{service=\"%s\"} %u\n",
            w->service_name, w->restart_count);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    /* ─── IPC Channels ───────────────────────────────────────────────────── */
    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_ipc_messages_per_second IPC channel message rate\n"
        "# TYPE middleware_ipc_messages_per_second gauge\n");
    for (uint32_t i = 0; i < snap->ipc_count && i < IPC_CHAN_MAX; i++) {
        const ipc_metrics_t *ch = &snap->ipc_channels[i];
        /* Build a safe label by encoding name */
        char lname[48];
        size_t li = 0;
        for (size_t ci = 0; ch->name[ci] && li < sizeof(lname)-1; ci++) {
            char c = ch->name[ci];
            lname[li++] = (c == '"' || c == '\\' || c == '\n') ? '_' : c;
        }
        lname[li] = '\0';
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_ipc_messages_per_second{channel=\"%s\"} %.1f\n",
            lname, ch->msgs_per_s);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    PROM_APPEND(buf, pos, bufsiz,
        "# HELP middleware_ipc_drop_total IPC channel drops total\n"
        "# TYPE middleware_ipc_drop_total counter\n");
    for (uint32_t i = 0; i < snap->ipc_count && i < IPC_CHAN_MAX; i++) {
        const ipc_metrics_t *ch = &snap->ipc_channels[i];
        char lname[48];
        size_t li = 0;
        for (size_t ci = 0; ch->name[ci] && li < sizeof(lname)-1; ci++) {
            char c = ch->name[ci]; lname[li++] = (c == '"') ? '_' : c;
        }
        lname[li] = '\0';
        PROM_APPEND(buf, pos, bufsiz,
            "middleware_ipc_drop_total{channel=\"%s\"} %llu\n",
            lname, (unsigned long long)ch->drop_count);
    }
    PROM_APPEND(buf, pos, bufsiz, "\n");

    /* Terminate with # EOF comment for Prometheus parser */
    PROM_APPEND(buf, pos, bufsiz, "# EOF\n");
    return (ssize_t)pos;
}

/* ── Health JSON renderer ─────────────────────────────────────────────────── */

ssize_t metrics_health_json_render(const mon_snapshot_t *snap,
                                    char *buf, size_t bufsiz)
{
    if (!snap || !buf || bufsiz == 0) return -1;
    size_t pos = 0;

    const char *sys_status = "healthy";
    if (snap->system_health.score < 70)  sys_status = "critical";
    else if (snap->system_health.score < 90) sys_status = "degraded";

    /* Build ISO-8601 timestamp */
    time_t t = (time_t)(snap->snapshot_ts_ms / 1000);
    uint32_t ms = (uint32_t)(snap->snapshot_ts_ms % 1000);
    struct tm tm_val;
    gmtime_r(&t, &tm_val);
    char ts_buf[32];
    snprintf(ts_buf, sizeof(ts_buf),
             "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
             tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, ms);

    PROM_APPEND(buf, pos, bufsiz,
        "{\n"
        "  \"status\": \"%s\",\n"
        "  \"score\": %u,\n"
        "  \"reason\": \"%s\",\n"
        "  \"services\": {\n",
        sys_status, snap->system_health.score, snap->system_health.reason);

    for (uint32_t i = 0; i < snap->service_count && i < SERVICE_MAX; i++) {
        const service_metrics_t *svc = &snap->services[i];
        const char *svc_status = "healthy";
        if (svc->health_score < 70)  svc_status = "critical";
        else if (svc->health_score < 90) svc_status = "degraded";
        PROM_APPEND(buf, pos, bufsiz,
            "    \"%s\": {\"status\": \"%s\", \"score\": %u}%s\n",
            svc->name, svc_status, svc->health_score,
            (i + 1 < snap->service_count) ? "," : "");
    }

    PROM_APPEND(buf, pos, bufsiz,
        "  },\n"
        "  \"timestamp\": \"%s\"\n"
        "}\n",
        ts_buf);

    return (ssize_t)pos;
}
