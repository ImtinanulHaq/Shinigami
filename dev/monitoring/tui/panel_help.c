/**
 * @file    panel_help.c
 * @brief   Help panel — keyboard shortcuts and system info.
 */
#include "panel_help.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void kv(int *row, int max_row, int kx, int vx,
               const char *key, const char *desc)
{
    if (*row > max_row) return;
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(*row, kx, "%-14s", key);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    mvprintw(*row, vx, "%s", desc);
    (*row)++;
}

void panel_help_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;
    (void)scroll;

    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "-- Middleware Monitor - Help ");
    mvhline(row, 30, ACS_HLINE, cols - 32);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;
    row++;

    /* ── Navigation ─────────────────────────────────────── */
    attron(A_BOLD);
    mvprintw(row++, 2, "Navigation");
    attroff(A_BOLD);
    mvhline(row++, 2, ACS_HLINE, 30);

    int kx = 4, vx = 20;
    kv(&row, max_row, kx, vx, "Tab / →",     "Next panel");
    kv(&row, max_row, kx, vx, "Shift+Tab / ←", "Previous panel");
    kv(&row, max_row, kx, vx, "1",           "Overview");
    kv(&row, max_row, kx, vx, "2",           "Services");
    kv(&row, max_row, kx, vx, "3",           "HAL");
    kv(&row, max_row, kx, vx, "4",           "Memory");
    kv(&row, max_row, kx, vx, "5",           "I/O");
    kv(&row, max_row, kx, vx, "6",           "Security");
    kv(&row, max_row, kx, vx, "7",           "Alerts");
    kv(&row, max_row, kx, vx, "8",           "Logs");
    kv(&row, max_row, kx, vx, "9",           "Traces");
    kv(&row, max_row, kx, vx, "0",           "Help (this page)");
    row++;

    attron(A_BOLD);
    if (row <= max_row) mvprintw(row++, 2, "Actions");
    attroff(A_BOLD);
    if (row <= max_row) mvhline(row++, 2, ACS_HLINE, 30);
    kv(&row, max_row, kx, vx, "r / R",       "Force refresh");
    kv(&row, max_row, kx, vx, "↑ / ↓",       "Scroll panel");
    kv(&row, max_row, kx, vx, "PgUp / PgDn", "Page up / down");
    kv(&row, max_row, kx, vx, "q / Q / Esc", "Quit");
    row++;

    /* ── System info ─────────────────────────────────────── */
    if (row + 5 <= max_row) {
        attron(A_BOLD);
        mvprintw(row++, 2, "System Info");
        attroff(A_BOLD);
        mvhline(row++, 2, ACS_HLINE, 30);

        uint64_t up = s->sysinfo.uptime_s;
        mvprintw(row++, 4, "Uptime:         %llud %02lluh %02llum %02llus",
                 (unsigned long long)up / 86400,
                 (unsigned long long)(up % 86400) / 3600,
                 (unsigned long long)(up % 3600)  / 60,
                 (unsigned long long)(up % 60));

        mvprintw(row++, 4, "CPU cores:      %u", s->sysinfo.num_cores);
        mvprintw(row++, 4, "Snapshot #%u     Collectors: %u live / %u stale / %u offline",
                 s->snapshot_seq,
                 s->collectors_live,
                 s->collectors_stale,
                 s->collectors_offline);
        mvprintw(row++, 4, "Services:       %u registered  |  Alerts: %u (CRIT:%u WARN:%u)",
                 s->service_count, s->alert_count, s->alerts_crit, s->alerts_warn);
        row++;
    }

    /* ── Collector status ─────────────────────────────────── */
    if (row + 3 <= max_row) {
        attron(A_BOLD);
        mvprintw(row++, 2, "Collector Status");
        attroff(A_BOLD);
        mvhline(row++, 2, ACS_HLINE, 50);

        for (int i = 0; i < COLLECTOR_COUNT && row <= max_row; i++) {
            const collector_info_t *c = &s->collectors[i];
            if (!c->name[0]) continue;
            int ccol = (c->state == COLLECTOR_STATE_LIVE)   ? COLOR_PAIR_GOOD
                     : (c->state == COLLECTOR_STATE_STALE)  ? COLOR_PAIR_WARNING
                     : (c->state == COLLECTOR_STATE_OFFLINE) ? COLOR_PAIR_CRITICAL
                     :                                         COLOR_PAIR_DEFAULT;
            attron(COLOR_PAIR(ccol));
            const char *cstate_str =
                c->state == COLLECTOR_STATE_LIVE      ? "LIVE    "
              : c->state == COLLECTOR_STATE_STALE     ? "STALE   "
              : c->state == COLLECTOR_STATE_OFFLINE   ? "OFFLINE "
              : c->state == COLLECTOR_STATE_CONNECTING? "CONNECT "
              : c->state == COLLECTOR_STATE_SYNCING   ? "SYNCING "
              :                                         "WAITING ";
            mvprintw(row++, 4, "%-20s  %s  interval:%ums  ticks:%llu  errs:%llu",
                     c->name, cstate_str, c->interval_ms,
                     (unsigned long long)c->collect_count,
                     (unsigned long long)c->error_count);
            attroff(COLOR_PAIR(ccol));
        }
    }
}
