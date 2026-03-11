/**
 * @file    panel_security.c
 * @brief   Security panel — per-service flags, cgroups, event stream.
 */
#include "panel_security.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static const char *sec_event_level(uint8_t lvl)
{
    switch (lvl) {
    case 0: return "INFO";
    case 1: return "WARN";
    case 2: return "ERR ";
    case 3: return "CRIT";
    default: return "????";
    }
}

static int sec_event_color(uint8_t lvl)
{
    switch (lvl) {
    case 0: return COLOR_PAIR_INFO;
    case 1: return COLOR_PAIR_WARNING;
    case 2: return COLOR_PAIR_CRITICAL;
    case 3: return COLOR_PAIR_CRITICAL;
    default: return COLOR_PAIR_DEFAULT;
    }
}

static void flag_cell(int y, int x, const char *lbl, int ok)
{
    attron(COLOR_PAIR(ok ? COLOR_PAIR_GOOD : COLOR_PAIR_CRITICAL));
    mvprintw(y, x, " %s:%c", lbl, ok ? 'Y' : 'N');
    attroff(COLOR_PAIR(ok ? COLOR_PAIR_GOOD : COLOR_PAIR_CRITICAL));
}

void panel_security_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;
    (void)scroll;

    /* ── Per-service security status ─────────────────────────── */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "-- Per-Service Security ");
    mvhline(row, 26, ACS_HLINE, cols - 28);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    if (row <= max_row) {
        attron(A_BOLD);
        mvprintw(row++, 4, "%-16s  SBX  CAP  VRF  SCP  SecViol  HMACFail  Replay",
                 "Service");
        attroff(A_BOLD);
        if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
    }

    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *sv = &s->services[i];
        if (!sv->name[0] || row > max_row) continue;

        int anyBad = (!sv->sandbox_ok || !sv->caps_ok || !sv->verify_ok || !sv->seccomp_ok
                      || sv->seccomp_violations || sv->hmac_failures || sv->replay_attacks);
        int basecol = anyBad ? COLOR_PAIR_WARNING : COLOR_PAIR_GOOD;

        attron(COLOR_PAIR(basecol));
        mvprintw(row, 4, "%-16s", sv->name);
        attroff(COLOR_PAIR(basecol));

        flag_cell(row, 21, "SBX", sv->sandbox_ok);
        flag_cell(row, 30, "CAP", sv->caps_ok);
        flag_cell(row, 39, "VRF", sv->verify_ok);
        flag_cell(row, 48, "SCP", sv->seccomp_ok);

        int vc = sv->seccomp_violations ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD;
        attron(COLOR_PAIR(vc));
        mvprintw(row, 57, " %8u", sv->seccomp_violations);
        attroff(COLOR_PAIR(vc));

        int hc = sv->hmac_failures ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD;
        attron(COLOR_PAIR(hc));
        mvprintw(row, 66, " %9u", sv->hmac_failures);
        attroff(COLOR_PAIR(hc));

        int rc2 = sv->replay_attacks ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD;
        attron(COLOR_PAIR(rc2));
        mvprintw(row, 76, " %7u", sv->replay_attacks);
        attroff(COLOR_PAIR(rc2));
        row++;
    }
    row++;

    /* ── Aggregate tokens ────────────────────────────────────── */
    if (row + 2 <= max_row) {
        attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
        mvprintw(row, 2, "-- Token Stats ");
        mvhline(row, 17, ACS_HLINE, cols - 19);
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
        row++;

        mvprintw(row++, 4,
                 "Tokens issued: %llu  |  Refreshed: %llu  |  Privilege drops: %llu",
                 (unsigned long long)s->security.total_tokens_issued,
                 (unsigned long long)s->security.total_tokens_refreshed,
                 (unsigned long long)s->security.total_privilege_drops);
        row++;
    }

    /* ── Cgroup limits ──────────────────────────────────────── */
    {
        int has_cg = 0;
        for (uint32_t i = 0; i < SERVICE_MAX; i++)
            if (s->security.cgroups[i].mem_limit_bytes) { has_cg = 1; break; }

        if (has_cg && row + 3 <= max_row) {
            attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
            mvprintw(row, 2, "-- Cgroups ");
            mvhline(row, 13, ACS_HLINE, cols - 15);
            attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
            row++;
            if (row <= max_row) {
                attron(A_BOLD);
                mvprintw(row++, 4, "%-16s %12s %12s %10s %10s %6s %6s",
                         "Service", "MemUsed", "MemLimit", "CPUUsed%", "CPULim%", "PIDs", "PIDLim");
                attroff(A_BOLD);
                if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
            }
            for (uint32_t i = 0; i < SERVICE_MAX; i++) {
                const service_metrics_t *sv = &s->services[i];
                if (!sv->name[0] || row > max_row) continue;
                const __typeof__(s->security.cgroups[i]) *cg = &s->security.cgroups[i];
                mvprintw(row++, 4, "%-16s %10lu MB %10lu MB %9.1f%% %9.1f%% %6u %6u",
                         sv->name,
                         cg->mem_used_bytes  / (1024*1024),
                         cg->mem_limit_bytes / (1024*1024),
                         (double)cg->cpu_used_pct,
                         (double)cg->cpu_limit_pct,
                         cg->pid_count, cg->pid_limit);
            }
            row++;
        }
    }

    /* ── Security event stream ───────────────────────────────── */
    if (row + 3 <= max_row && s->security.event_count > 0) {
        attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
        mvprintw(row, 2, "-- Recent Security Events (%u) ", s->security.event_count);
        mvhline(row, 32 + (int)snprintf(NULL, 0, "%u", s->security.event_count),
                ACS_HLINE, cols - 36);
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
        row++;

        uint32_t start = 0;
        if (s->security.event_count > (uint32_t)(max_row - row))
            start = s->security.event_count - (uint32_t)(max_row - row);

        for (uint32_t i = start; i < s->security.event_count && row <= max_row; i++) {
            const __typeof__(s->security.events[i]) *ev = &s->security.events[i];
            int ecol = sec_event_color(ev->level);
            attron(COLOR_PAIR(ecol));
            mvprintw(row++, 4, "[%s] %-14s %s",
                     sec_event_level(ev->level), ev->source, ev->message);
            attroff(COLOR_PAIR(ecol));
        }
    }
}
