/**
 * @file    panel_overview.c
 * @brief   Overview panel implementation.
 */
#include "panel_overview.h"
#include "ui_colors.h"
#include "../health/health_score.h"
#include <ncurses.h>
#include <stdio.h>

void panel_overview_render(const mon_snapshot_t *snapshot, int y, int h, int cols)
{
    int row = y;

    /* Title */
    mvprintw(row++, 2, "═══ System Overview ═══");
    row++;

    /* Service Manager */
    mvprintw(row++, 4, "Service Manager: %u PID | Services: %u/%u registered | CPU: %.1f%%",
             snapshot->sm.pid, snapshot->sm.registered_services,
             snapshot->sm.max_services, (double)snapshot->sm.cpu_pct);

    /* Watchdog */
    uint32_t watchdog_alive = 0;
    uint32_t watchdog_dead = 0;
    for (uint32_t i = 0; i < snapshot->watchdog.count; i++) {
        if (snapshot->watchdog.entries[i].alive) {
            watchdog_alive++;
        } else {
            watchdog_dead++;
        }
    }
    mvprintw(row++, 4, "Watchdog: %u services monitored | %u alive, %u dead | restarts: %u",
             snapshot->watchdog.count, watchdog_alive, watchdog_dead,
             snapshot->watchdog.total_restarts);
row++;

    /* Services */
    mvprintw(row++, 4, "Services:");
    for (uint32_t i = 0; i < SERVICE_MAX && i < 4; i++) {
        const service_metrics_t *s = &snapshot->services[i];
        if (s->name[0] == '\0') continue;

        int svc_health = health_compute_service_score(s);
        int color = health_score_to_color(svc_health);

        attron(COLOR_PAIR(color));
        mvprintw(row++, 6, "%-16s CPU: %5.1f%% | RAM: %4lu MB | Health: %3d/100",
                 s->name, (double)s->cpu_pct, s->rss_bytes / (1024*1024), svc_health);
        attroff(COLOR_PAIR(color));
    }
    row++;

    /* HAL */
    uint32_t hal_devices = snapshot->hal_count;
    uint64_t hal_total_bytes = 0;
    uint64_t hal_total_errors = 0;
    for (uint32_t i = 0; i < hal_devices; i++) {
        hal_total_bytes += snapshot->hal[i].bytes_read + snapshot->hal[i].bytes_written;
        hal_total_errors += snapshot->hal[i].error_count;
    }
    mvprintw(row++, 4, "HAL: %u devices | throughput: %.1f MB/s | errors: %lu",
             hal_devices, hal_total_bytes / 1048576.0, hal_total_errors);
    row++;

    /* Security */
    mvprintw(row++, 4, "Security:");
    /* Sum up violations across all services */
    uint32_t total_seccomp = 0, total_hmac = 0, total_replay = 0;
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        total_seccomp += snapshot->security.seccomp_violations[i];
        total_hmac += snapshot->security.hmac_failures[i];
        total_replay += snapshot->security.replay_attacks[i];
    }
    mvprintw(row++, 6, "Seccomp violations: %u | HMAC failures: %u | Replay attacks: %u",
             total_seccomp, total_hmac, total_replay);
    row++;

    /* Memory Pools */
    uint32_t total_pools = 0;
    double total_allocs_rate = 0.0;
    uint64_t total_failures = 0;
    for (uint32_t i = 0; i < POOL_MAX; i++) {
        if (snapshot->pools[i].name[0]) {
            total_pools++;
            total_allocs_rate += snapshot->pools[i].alloc_per_s;
            total_failures += snapshot->pools[i].fail_count;
        }
    }
    mvprintw(row++, 4, "Memory Pools: %u pools | %.1f allocs/s | %lu failures",
             total_pools, total_allocs_rate, total_failures);

    /* I/O Rings */
    uint32_t total_urings = 0;
    double total_completion_rate = 0.0;
    for (uint32_t i = 0; i < URING_MAX; i++) {
        if (snapshot->urings[i].name[0]) {
            total_urings++;
            total_completion_rate += snapshot->urings[i].completions_per_s;
        }
    }
    mvprintw(row++, 4, "io_uring: %u rings | %.1f completions/s",
             total_urings, total_completion_rate);

    /* Ring Buffers */
    uint32_t total_ringbufs = 0;
    double total_throughput = 0.0;
    for (uint32_t i = 0; i < RINGBUF_MAX; i++) {
        if (snapshot->ring_buffers[i].name[0]) {
            total_ringbufs++;
            total_throughput += snapshot->ring_buffers[i].throughput_bytes_s;
        }
    }
    mvprintw(row++, 4, "Ring Buffers: %u buffers | %.1f MB/s throughput",
             total_ringbufs, total_throughput / 1048576.0);

    (void)h;  /* Unused */
    (void)cols;
}
