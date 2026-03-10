/**
 * @file    alert_rules.c
 * @brief   Alert rule evaluation implementation.
 */
#include "alert_rules.h"
#include "../health/health_score.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Helper to format alert messages */
static void format_msg(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);
}

/* Helper wrapper for alert_fire with simpler interface */
static void fire_alert(alert_state_t *st, alert_severity_t sev,
                        const char *component, const char *condition,
                        const char *current, const char *thresh,
                        const char *suggestion, uint64_t ts)
{
    alert_fire(st, sev, component, condition, current, thresh, suggestion, ts);
}

uint32_t alert_rules_evaluate(const mon_snapshot_t *snapshot,
                                alert_state_t *alert_st,
                                uint64_t timestamp)
{
    uint32_t alerts_fired = 0;
    char msg[256];

    /* ═══ CRITICAL RULES ═══════════════════════════════════════════════════ */

    /* Rule 1: Service restart detected */
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->restart_count > 0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%u", s->restart_count);
            snprintf(thresh, sizeof(thresh), "0");
            alert_fire(alert_st, ALERT_SEV_CRIT, s->name, "Service restart",
                       current, thresh, "Investigate service crash cause", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 2: Seccomp violation */
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->seccomp_violations > 0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%u", s->seccomp_violations);
            snprintf(thresh, sizeof(thresh), "0");
            alert_fire(alert_st, ALERT_SEV_CRIT, s->name, "Seccomp violation",
                       current, thresh, "Check seccomp filter configuration", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 3: HMAC verification failure */
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->hmac_failures > 0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%u", s->hmac_failures);
            snprintf(thresh, sizeof(thresh), "0");
            alert_fire(alert_st, ALERT_SEV_CRIT, s->name, "HMAC failure",
                       current, thresh, "Check message authentication", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 4: Memory pool exhaustion - check actual pools */
    for (uint32_t i = 0; i < POOL_MAX; i++) {
        const pool_metrics_t *p = &snapshot->pools[i];
        if (p->name[0] && p->fail_count > 0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%lu", p->fail_count);
            snprintf(thresh, sizeof(thresh), "0");
            alert_fire(alert_st, ALERT_SEV_CRIT, p->name, "Memory pool exhaustion",
                       current, thresh, "Increase pool size or reduce allocations", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 5: io_uring CQ overflow - check actual urings */
    for (uint32_t i = 0; i < URING_MAX; i++) {
        const uring_metrics_t *u = &snapshot->urings[i];
        if (u->name[0] && u->cq_fill_pct > 95.0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%.1f%%", u->cq_fill_pct);
            snprintf(thresh, sizeof(thresh), "95%%");
            alert_fire(alert_st, ALERT_SEV_CRIT, u->name, "io_uring CQ near full",
                       current, thresh, "Increase CQ depth or process completions faster", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 6: Ring buffer overflow */
    for (uint32_t i = 0; i < RINGBUF_MAX; i++) {
        const ringbuf_metrics_t *rb = &snapshot->ring_buffers[i];
        if (rb->name[0] && rb->drop_count > 0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%lu", rb->drop_count);
            snprintf(thresh, sizeof(thresh), "0");
            alert_fire(alert_st, ALERT_SEV_CRIT, rb->name, "Ring buffer overflow",
                       current, thresh, "Increase buffer size or reduce write rate", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 7: Health score < 50 */
    int sys_health = health_compute_system_score(snapshot->services, SERVICE_MAX);
    if (sys_health < 50) {
        char current[32], thresh[32];
        snprintf(current, sizeof(current), "%d", sys_health);
        snprintf(thresh, sizeof(thresh), "50");
        alert_fire(alert_st, ALERT_SEV_CRIT, "system", "Health critical",
                   current, thresh, "Check service health and system resources", timestamp);
        alerts_fired++;
    }

    /* ═══ WARNING RULES ════════════════════════════════════════════════════ */

    /* Rule 8: Health score 50-70 */
    if (sys_health >= 50 && sys_health < 70) {
        char current[32], thresh[32];
        snprintf(current, sizeof(current), "%d", sys_health);
        snprintf(thresh, sizeof(thresh), "70");
        alert_fire(alert_st, ALERT_SEV_WARN, "system", "Health degraded",
                   current, thresh, "Review service performance", timestamp);
        alerts_fired++;
    }

    /* Rule 9: High CPU usage */
    if (snapshot->sysinfo.cpu_total_pct > 80.0) {
        char current[32], thresh[32];
        snprintf(current, sizeof(current), "%.1f%%", snapshot->sysinfo.cpu_total_pct);
        snprintf(thresh, sizeof(thresh), "80%%");
        alert_fire(alert_st, ALERT_SEV_WARN, "system", "High CPU",
                   current, thresh, "Identify CPU-intensive processes", timestamp);
        alerts_fired++;
    }

    /* Rule 10: High RAM usage */
    double ram_pct = (double)snapshot->sysinfo.ram_used_bytes /
                     (double)snapshot->sysinfo.ram_total_bytes * 100.0;
    if (ram_pct > 85.0) {
        char current[32], thresh[32];
        snprintf(current, sizeof(current), "%.1f%%", ram_pct);
        snprintf(thresh, sizeof(thresh), "85%%");
        alert_fire(alert_st, ALERT_SEV_WARN, "system", "High RAM",
                   current, thresh, "Check for memory leaks", timestamp);
        alerts_fired++;
    }

    /* Rule 11: FD leak detected */
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->fd_count > 900) {  /* Arbitrary high threshold */
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%u", s->fd_count);
            snprintf(thresh, sizeof(thresh), "900");
            alert_fire(alert_st, ALERT_SEV_WARN, s->name, "FD usage high",
                       current, thresh, "Check for file descriptor leaks", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 12: Service latency issues - check if health score is affected */
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->name[0] && s->health_score < 70) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%u", s->health_score);
            snprintf(thresh, sizeof(thresh), "70");
            alert_fire(alert_st, ALERT_SEV_WARN, s->name, "Service health degraded",
                       current, thresh, "Check service performance metrics", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 13: Message drop rate > 1% */
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->frames_captured > 100 && s->frames_dropped > 0) {
            double drop_rate = (double)s->frames_dropped / (double)s->frames_captured *100.0;
            if (drop_rate > 1.0) {
                char current[32], thresh[32];
                snprintf(current, sizeof(current), "%.1f%%", drop_rate);
                snprintf(thresh, sizeof(thresh), "1%%");
                alert_fire(alert_st, ALERT_SEV_WARN, s->name, "High drop rate",
                           current, thresh, "Investigate processing bottlenecks", timestamp);
                alerts_fired++;
            }
        }
    }

    /* Rule 14: HAL error rate > 5% */
    for (uint32_t i = 0; i < HAL_MAX_DEVICES; i++) {
        const hal_metrics_t *hd = &snapshot->hal[i];
        if (hd->device_name[0] && hd->error_count > 50) {  /* Simple threshold */
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%lu", hd->error_count);
            snprintf(thresh, sizeof(thresh), "50");
            alert_fire(alert_st, ALERT_SEV_WARN, hd->device_name, "High HAL errors",
                       current, thresh, "Check device health", timestamp);
            alerts_fired++;
        }
    }

    /* Rule 15: IPC queue depth > 80% capacity */
    for (uint32_t i = 0; i < IPC_CHAN_MAX; i++) {
        const ipc_metrics_t *ic = &snapshot->ipc_channels[i];
        if (ic->name[0] && ic->send_q_depth > 0) {
            double depth_pct = (double)ic->send_q_depth /
                               (double)(ic->send_q_max + 1) * 100.0;
            if (depth_pct > 80.0) {
                char current[32], thresh[32];
                snprintf(current, sizeof(current), "%.1f%%", depth_pct);
                snprintf(thresh, sizeof(thresh), "80%%");
                alert_fire(alert_st, ALERT_SEV_WARN, ic->name, "IPC queue high",
                           current, thresh, "Process messages faster or increase queue size", timestamp);
                alerts_fired++;
            }
        }
    }

    /* Rule 21: Swap usage > 50% */
    if (snapshot->sysinfo.swap_total_bytes > 0) {
        double swap_pct = (double)snapshot->sysinfo.swap_used_bytes /
                          (double)snapshot->sysinfo.swap_total_bytes * 100.0;
        if (swap_pct > 50.0) {
            char current[32], thresh[32];
            snprintf(current, sizeof(current), "%.1f%%", swap_pct);
            snprintf(thresh, sizeof(thresh), "50%%");
            alert_fire(alert_st, ALERT_SEV_WARN, "system", "High swap",
                       current, thresh, "Add more RAM or reduce memory usage", timestamp);
            alerts_fired++;
        }
    }

    /* ═══ INFO RULES ═══════════════════════════════════════════════════════ */

    /* Rule 24: Health score 70-90 */
    if (sys_health >= 70 && sys_health < 90) {
        char current[32], thresh[32];
        snprintf(current, sizeof(current), "%d", sys_health);
        snprintf(thresh, sizeof(thresh), "90");
        alert_fire(alert_st,ALERT_SEV_INFO, "system", "Health info",
                   current, thresh, "System operating normally", timestamp);
        alerts_fired++;
    }

    return alerts_fired;
}
