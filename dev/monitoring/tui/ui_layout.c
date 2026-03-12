/**
 * @file    ui_layout.c
 * @brief   Grid layout — 3-column × 2-row panel arrangement.
 *
 *  ┌─ SHINIGAMI MONITOR ─── HH:MM:SS ──────────────── ● LIVE ─┐
 *  ├──────────────────┬──────────────────────┬─────────────────┤
 *  │ SERVICES (L-top) │ CPU GRAPH  (C-top)   │ ALERTS  (R-top) │
 *  ├──────────────────┼──────────────────────┼─────────────────┤
 *  │ HEALTH   (L-bot) │ MEMORY BARS (C-bot)  │ SYSTEM  (R-bot) │
 *  ├──────────────────┴──────────────────────┴─────────────────┤
 *  │  shinigami-mon v1.0.0            q:quit  r:restart  h:help│
 *  └──────────────────────────────────────────────────────────-─┘
 *
 * Column widths: left=30%  center=40%  right=30%.
 * All panel rendering is done by the static helpers below so that
 * no extra source files are needed.
 */
#define _POSIX_C_SOURCE 200809L
#include "ui_layout.h"
#include "ui_colors.h"
#include "ui_engine.h"
#include "ui_input.h"
#include "panel_topbar.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* scroll offset for the alerts panel (↑↓ keys) */
static int g_scroll_offset = 0;
/* kept for API compatibility with tui_main.c — unused in grid layout */
static int g_current_tab   = 0;

/* ═══════════════════════════════════════════════════════════════════════
 * Draw helpers
 * ═══════════════════════════════════════════════════════════════════════ */

/** Draw a dim-green box border at (y, x) with size (h × w). */
static void draw_box(int y, int x, int h, int w)
{
    if (h < 2 || w < 2) return;
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvaddch(y,       x,       ACS_ULCORNER);
    mvaddch(y,       x+w-1,   ACS_URCORNER);
    mvaddch(y+h-1,   x,       ACS_LLCORNER);
    mvaddch(y+h-1,   x+w-1,   ACS_LRCORNER);
    for (int i = 1; i < w-1; i++) {
        mvaddch(y,     x+i, ACS_HLINE);
        mvaddch(y+h-1, x+i, ACS_HLINE);
    }
    for (int i = 1; i < h-1; i++) {
        mvaddch(y+i, x,     ACS_VLINE);
        mvaddch(y+i, x+w-1, ACS_VLINE);
    }
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
}

/**
 * Overwrite the top border with a title:
 *   ┌─ TITLE ────────────────────────┐
 */
static void draw_panel_title(int y, int x, int w, const char *title)
{
    int max_len = w - 6;
    if (max_len < 1) return;
    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvprintw(y, x + 2, " %.*s ", max_len, title);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
}

/* ═══════════════════════════════════════════════════════════════════════
 * PANEL 1 — SERVICES  (left column, top half)
 *
 *   Name          CPU     MEM
 *   audio         1.7%   14MB
 *   camera        2.7%   22MB
 * ═══════════════════════════════════════════════════════════════════════ */
static void render_services(const mon_snapshot_t *s,
                            int y, int h, int x, int w)
{
    draw_box(y, x, h, w);
    draw_panel_title(y, x, w, "SERVICES");

    int row     = y + 1;
    int max_row = y + h - 2;

    /* Column widths inside the border (x+2 .. x+w-2) */
    int inner  = w - 4;              /* content width */
    int cpu_w  = 6;                  /* "99.9%" */
    int mem_w  = 6;                  /* "999MB" */
    int name_w = inner - cpu_w - mem_w - 2;
    if (name_w < 3) name_w = 3;

    /* Column header */
    if (row <= max_row) {
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT) | A_BOLD);
        mvprintw(row, x + 2, "%-*s %*s %*s",
                 name_w, "Name", cpu_w, "CPU", mem_w, "MEM");
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_DEFAULT));
        row++;
    }

    /* Per-service rows */
    for (uint32_t i = 0; i < SERVICE_MAX && row <= max_row; i++) {
        const service_metrics_t *sv = &s->services[i];
        if (!sv->name[0]) continue;

        /* Name: green+bold if running, red+bold if not */
        int name_col = sv->running ? COLOR_PAIR_GOOD : COLOR_PAIR_CRITICAL;
        attron(COLOR_PAIR(name_col) | A_BOLD);
        mvprintw(row, x + 2, "%-*.*s", name_w, name_w, sv->name);
        attroff(A_BOLD | COLOR_PAIR(name_col));

        /* CPU: colour by threshold */
        int cpu_col = (sv->cpu_pct >= 80.0f) ? COLOR_PAIR_CRITICAL
                    : (sv->cpu_pct >= 50.0f) ? COLOR_PAIR_WARNING
                    :                           COLOR_PAIR_GOOD;
        attron(COLOR_PAIR(cpu_col));
        if (sv->cpu_pct >= 80.0f) attron(A_BLINK);
        mvprintw(row, x + 2 + name_w + 1,
                 "%*.1f%%", cpu_w - 1, (double)sv->cpu_pct);
        if (sv->cpu_pct >= 80.0f) attroff(A_BLINK);
        attroff(COLOR_PAIR(cpu_col));

        /* MEM: always bright green */
        unsigned long mb = (unsigned long)(sv->rss_bytes / (1024UL * 1024UL));
        attron(COLOR_PAIR(COLOR_PAIR_GOOD));
        mvprintw(row, x + 2 + name_w + 1 + cpu_w + 1,
                 "%*luMB", mem_w - 2, mb);
        attroff(COLOR_PAIR(COLOR_PAIR_GOOD));

        row++;
    }

    if (row == y + 2) {   /* nothing was printed */
        if (row <= max_row) {
            attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
            mvprintw(row, x + 2, "(no services)");
            attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * PANEL 2 — HEALTH BARS  (left column, bottom half)
 *
 *   [████████░░]  99
 *   [███████░░░]  94
 *   [████░░░░░░]  72  ← yellow
 * ═══════════════════════════════════════════════════════════════════════ */
static void render_health(const mon_snapshot_t *s,
                          int y, int h, int x, int w)
{
    draw_box(y, x, h, w);
    draw_panel_title(y, x, w, "HEALTH");

    int row     = y + 1;
    int max_row = y + h - 2;

    /* Bar width: inner(w-4) minus "[" "]" (2) minus " 100"(4) */
    int bar_w = w - 4 - 2 - 4;
    if (bar_w < 4)  bar_w = 4;
    if (bar_w > 14) bar_w = 14;

    for (uint32_t i = 0; i < SERVICE_MAX && row <= max_row; i++) {
        const service_metrics_t *sv = &s->services[i];
        if (!sv->name[0]) continue;

        int score  = (int)sv->health_score;
        int filled = (int)(score / 100.0 * bar_w + 0.5);
        if (filled > bar_w) filled = bar_w;

        /* Colour by score */
        int  col      = (score >= 70) ? COLOR_PAIR_GOOD
                      : (score >= 50) ? COLOR_PAIR_WARNING
                      :                 COLOR_PAIR_CRITICAL;
        int  do_blink = (score >= 50 && score < 70);

        /* "[" */
        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        mvaddch(row, x + 2, '[');
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

        /* filled / empty blocks */
        attron(COLOR_PAIR(col) | A_BOLD);
        if (do_blink) attron(A_BLINK);
        for (int j = 0; j < bar_w; j++)
            addstr(j < filled ? "\xe2\x96\x88" : "\xe2\x96\x91");
        if (do_blink) attroff(A_BLINK);
        attroff(A_BOLD | COLOR_PAIR(col));

        /* "]" + score */
        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        addch(']');
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

        attron(COLOR_PAIR(col) | A_BOLD);
        printw(" %3d", score);
        attroff(A_BOLD | COLOR_PAIR(col));

        row++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * PANEL 3 — CPU GRAPH  (center column, top half)
 *
 *   Multi-row vertical bar chart using ▁▂▃▄▅▆▇█ block characters.
 *   Source: sysinfo.cpu_history[SPARKLINE_LEN] (64 points, newest last).
 * ═══════════════════════════════════════════════════════════════════════ */
/* Eight fractional block chars (indices 0..7 → ▁..█, 0 = space). */
static const char *s_blk[] = {
    " ",
    "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84",
    "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"
};

static void render_cpu_graph(const mon_snapshot_t *s,
                             int y, int h, int x, int w)
{
    draw_box(y, x, h, w);
    draw_panel_title(y, x, w, "CPU TOTAL");

    /* Current value on the right side of the title row */
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%.1f%%", (double)s->sysinfo.cpu_total_pct);
    attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
    mvprintw(y, x + w - (int)strlen(vbuf) - 2, "%s", vbuf);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));

    int graph_h = h - 2;   /* rows inside the border */
    int graph_w = w - 2;   /* cols inside the border */
    if (graph_h <= 0 || graph_w <= 0) return;

    /* Copy history out of the packed struct to avoid -Waddress-of-packed-member */
    float hist[SPARKLINE_LEN];
    for (int i = 0; i < SPARKLINE_LEN; i++)
        hist[i] = s->sysinfo.cpu_history[i];

    attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);

    for (int col = 0; col < graph_w; col++) {
        int   src = SPARKLINE_LEN - graph_w + col;
        float pct = (src >= 0 && src < SPARKLINE_LEN) ? hist[src] : 0.0f;
        if (pct < 0.0f)   pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;

        float total    = pct / 100.0f * (float)graph_h;
        int   full     = (int)total;
        int   part_idx = (int)((total - (float)full) * 8.0f);
        if (part_idx > 7) part_idx = 7;

        for (int row = 0; row < graph_h; row++) {
            int bot = graph_h - 1 - row;   /* 0 = bottom row */
            int py  = y + 1 + row;
            int px  = x + 1 + col;

            if (bot < full) {
                mvaddstr(py, px, s_blk[8]);          /* full block ▊ */
            } else if (bot == full && part_idx > 0) {
                mvaddstr(py, px, s_blk[part_idx]);   /* partial block */
            } else {
                mvaddch(py, px, ' ');
            }
        }
    }

    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));

    /* Y-axis hints */
    if (graph_h >= 3) {
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        mvprintw(y + 1,              x + w - 5, "100%%");
        mvprintw(y + 1 + graph_h/2,  x + w - 4, " 50%%");
        mvprintw(y + h - 2,          x + w - 3, "  0%%");
        attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * PANEL 4 — MEMORY BARS  (center column, bottom half)
 *
 *   audio   [██████░░░░░░░░░░░░]  14MB
 *   camera  [████████░░░░░░░░░░]  22MB
 * ═══════════════════════════════════════════════════════════════════════ */
static void render_memory(const mon_snapshot_t *s,
                          int y, int h, int x, int w)
{
    draw_box(y, x, h, w);
    draw_panel_title(y, x, w, "MEMORY / SERVICE");

    int row     = y + 1;
    int max_row = y + h - 2;

    /* Find max RSS for proportional scaling */
    uint64_t max_rss = 1;
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        if (s->services[i].name[0] && s->services[i].rss_bytes > max_rss)
            max_rss = s->services[i].rss_bytes;
    }

    int name_w = 6;   /* "audio " */
    int val_w  = 7;   /* " 999MB" */
    /* inner(w-4) - name - space - "["…"]"(2) - space - val */
    int bar_w  = (w - 4) - name_w - 1 - 2 - 1 - val_w;
    if (bar_w < 4) bar_w = 4;

    for (uint32_t i = 0; i < SERVICE_MAX && row <= max_row; i++) {
        const service_metrics_t *sv = &s->services[i];
        if (!sv->name[0]) continue;

        unsigned long mb  = (unsigned long)(sv->rss_bytes / (1024UL * 1024UL));
        int filled = (int)((double)sv->rss_bytes / (double)max_rss
                           * bar_w + 0.5);
        if (filled > bar_w) filled = bar_w;

        int pct_of_max = (int)((double)sv->rss_bytes / (double)max_rss
                               * 100.0 + 0.5);
        int bar_col = (pct_of_max > 80) ? COLOR_PAIR_WARNING : COLOR_PAIR_GOOD;

        /* Name */
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        mvprintw(row, x + 2, "%-*.*s", name_w, name_w, sv->name);
        attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));

        /* Bar */
        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        addch('[');
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
        attron(COLOR_PAIR(bar_col) | A_BOLD);
        for (int j = 0; j < bar_w; j++)
            addstr(j < filled ? "\xe2\x96\x88" : "\xe2\x96\x91");
        attroff(A_BOLD | COLOR_PAIR(bar_col));
        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        addch(']');
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

        /* Value */
        attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
        printw("%4luMB", mb);
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));

        row++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * PANEL 5 — ALERTS  (right column, top half)
 *
 *   ● WARN  9:00:16 AM
 *       Memory threshold 85% on camera_service
 *
 *   ● CRIT  9:00:07 AM
 *       sensor_service unresponsive
 * ═══════════════════════════════════════════════════════════════════════ */
static const char *sev_label(alert_severity_t sv)
{
    switch (sv) {
    case ALERT_SEV_INFO: return "INFO";
    case ALERT_SEV_WARN: return "WARN";
    case ALERT_SEV_CRIT: return "CRIT";
    default:             return "????";
    }
}

static int sev_cpair(alert_severity_t sv)
{
    switch (sv) {
    case ALERT_SEV_INFO: return COLOR_PAIR_INFO;
    case ALERT_SEV_WARN: return COLOR_PAIR_WARNING;
    case ALERT_SEV_CRIT: return COLOR_PAIR_CRITICAL;
    default:             return COLOR_PAIR_DEFAULT;
    }
}

static void render_alerts(const mon_snapshot_t *s,
                          int y, int h, int x, int w, int scroll)
{
    draw_box(y, x, h, w);
    draw_panel_title(y, x, w, "ALERTS");

    int row     = y + 1;
    int max_row = y + h - 2;
    int msg_w   = w - 6;
    if (msg_w < 4) msg_w = 4;

    if (s->alert_count == 0) {
        if (row <= max_row) {
            attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
            mvprintw(row, x + 2, "OK  All services healthy");
            attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));
        }
        return;
    }

    /* Iterate newest-first (array is oldest-first → reverse) */
    int total = (int)s->alert_count;
    if (total > (int)ALERT_STORE_MAX) total = (int)ALERT_STORE_MAX;

    int shown = 0;
    for (int i = total - 1; i >= 0 && row + 1 <= max_row; i--) {
        const alert_record_t *a = &s->alerts[i];
        if (!a->component[0]) continue;
        if (shown++ < scroll) continue;

        int cp = sev_cpair(a->severity);

        /* ● dot (uses bullet char) */
        attron(COLOR_PAIR(cp) | A_BOLD);
        mvaddch(row, x + 2, ACS_BULLET);
        attroff(A_BOLD | COLOR_PAIR(cp));
        addch(' ');

        /* Severity badge — reversed for colored background */
        attron(COLOR_PAIR(cp) | A_REVERSE | A_BOLD);
        printw(" %-4s ", sev_label(a->severity));
        attroff(A_BOLD | A_REVERSE | COLOR_PAIR(cp));

        /* Timestamp */
        time_t     ts  = (time_t)(a->last_ts_ms / 1000);
        struct tm *tmi = localtime(&ts);
        char       tbuf[20] = "--:--:--";
        if (tmi) strftime(tbuf, sizeof(tbuf), "%I:%M:%S %p", tmi);
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        printw(" %s", tbuf);
        attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        row++;

        /* Message line (4-space indent) */
        if (row <= max_row) {
            attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
            mvprintw(row, x + 5, "%.*s", msg_w, a->condition);
            attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));
            row++;
        }
        /* Blank separator */
        row++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * PANEL 6 — SYSTEM  (right column, bottom half)
 *
 *   Services         5 running
 *   IPC broker       ✓ online
 *   Auth             HMAC valid
 *   Uptime           2h 14m
 *   CPU              12.3%
 *   RAM              512/1024 MB
 * ═══════════════════════════════════════════════════════════════════════ */
static void render_sysinfo(const mon_snapshot_t *s,
                           int y, int h, int x, int w)
{
    draw_box(y, x, h, w);
    draw_panel_title(y, x, w, "SYSTEM");

    int row     = y + 1;
    int max_row = y + h - 2;

/* Macro: label on the left, right-aligned green value */
#define SYSROW(lbl, fmt, ...) do {                                       \
    if (row > max_row) break;                                            \
    attron(COLOR_PAIR(COLOR_PAIR_DIM));                                  \
    mvprintw(row, x + 2, "%s", lbl);                                    \
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));                                 \
    attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);                       \
    char _sv[48]; snprintf(_sv, sizeof(_sv), fmt, ##__VA_ARGS__);       \
    mvprintw(row, x + w - (int)strlen(_sv) - 2, "%s", _sv);            \
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));                      \
    row++;                                                               \
} while (0)

    /* Count running services */
    uint32_t running = 0;
    for (uint32_t i = 0; i < SERVICE_MAX; i++)
        if (s->services[i].name[0] && s->services[i].running) running++;

    /* IPC broker: any service with sm_connected */
    int ipc_ok = 0;
    for (uint32_t i = 0; i < SERVICE_MAX; i++)
        if (s->services[i].sm_connected) { ipc_ok = 1; break; }

    /* Auth: total HMAC failures */
    uint32_t hmac = 0;
    for (uint32_t i = 0; i < SERVICE_MAX; i++)
        hmac += s->services[i].hmac_failures;

    /* Uptime (from sysinfo) */
    uint64_t up = s->sysinfo.uptime_s;
    char upbuf[32];
    if      (up >= 86400)
        snprintf(upbuf, sizeof(upbuf), "%llud %lluh %llum",
                 (unsigned long long)up / 86400,
                 (unsigned long long)(up % 86400) / 3600,
                 (unsigned long long)(up % 3600) / 60);
    else if (up >= 3600)
        snprintf(upbuf, sizeof(upbuf), "%lluh %llum",
                 (unsigned long long)up / 3600,
                 (unsigned long long)(up % 3600) / 60);
    else if (up >= 60)
        snprintf(upbuf, sizeof(upbuf), "%llum %llus",
                 (unsigned long long)up / 60,
                 (unsigned long long)up % 60);
    else
        snprintf(upbuf, sizeof(upbuf), "%llus", (unsigned long long)up);

    SYSROW("Services",   "%u running",  running);
    SYSROW("IPC broker", "%s",          ipc_ok ? "\xe2\x9c\x93 online" : "\xe2\x9c\x97 offline");
    SYSROW("Auth",       "%s",          hmac == 0 ? "HMAC valid" : "HMAC FAIL");
    SYSROW("Uptime",     "%s",          upbuf);
    SYSROW("CPU",        "%.1f%%",      (double)s->sysinfo.cpu_total_pct);

    if (row <= max_row) {
        unsigned long used  = (unsigned long)(s->sysinfo.ram_used_bytes  / (1024UL*1024UL));
        unsigned long total = (unsigned long)(s->sysinfo.ram_total_bytes / (1024UL*1024UL));
        SYSROW("RAM", "%lu/%lu MB", used, total);
    }

#undef SYSROW
}

/* ═══════════════════════════════════════════════════════════════════════
 * Bottom bar
 * ═══════════════════════════════════════════════════════════════════════ */
static void render_bottombar(int row, int cols)
{
    move(row, 0);
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    for (int i = 0; i < cols; i++) addch(' ');
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    mvprintw(row, 1, "shinigami-mon v1.0.0");
    const char *hints = "q:quit  r:restart  h:help";
    mvprintw(row, cols - (int)strlen(hints) - 1, "%s", hints);
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
}

/* ═══════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════ */
void ui_layout_render(const mon_snapshot_t *snapshot)
{
    /* erase() — no hardware clear, ncurses diffs old vs new → no flicker */
    erase();

    int rows, cols;
    ui_engine_get_size(&rows, &cols);

    /* ── Column geometry: ~30% | ~40% | ~30% ──────────────────────── */
    int left_w  = cols * 30 / 100;
    if (left_w  < 22) left_w  = 22;
    int right_w = cols * 30 / 100;
    if (right_w < 22) right_w = 22;
    int center_w = cols - left_w - right_w;
    if (center_w < 20) center_w = 20;

    /* ── Row geometry ────────────────────────────────────────────── */
    int content_top = 1;       /* row 0 = top bar   */
    int content_bot = rows - 2;/* rows-1 = bottom bar */
    int content_h   = content_bot - content_top;
    if (content_h < 4) content_h = 4;
    int top_h = content_h / 2;
    int bot_h = content_h - top_h;

    int left_x   = 0;
    int center_x = left_w;
    int right_x  = left_w + center_w;

    /* ── Top bar (row 0) ─────────────────────────────────────────── */
    panel_topbar_render(snapshot, 0, cols);

    /* ── Six panels ──────────────────────────────────────────────── */
    render_services (snapshot, content_top,         top_h, left_x,   left_w);
    render_health   (snapshot, content_top + top_h, bot_h, left_x,   left_w);
    render_cpu_graph(snapshot, content_top,         top_h, center_x, center_w);
    render_memory   (snapshot, content_top + top_h, bot_h, center_x, center_w);
    render_alerts   (snapshot, content_top,         top_h, right_x,  right_w,
                     g_scroll_offset);
    render_sysinfo  (snapshot, content_top + top_h, bot_h, right_x,  right_w);

    /* ── Bottom bar (last row) ────────────────────────────────────── */
    render_bottombar(rows - 1, cols);
}

void ui_layout_set_tab(int tab_index)
{
    g_current_tab   = tab_index;
    g_scroll_offset = 0;   /* reset alerts scroll when "switching panels" */
}

int ui_layout_get_tab(void)
{
    return g_current_tab;
}

/**
 * Translate UI_INPUT_* direction constants to scroll deltas.
 * UP = scroll the alerts list toward newer alerts (offset–).
 * DOWN = scroll toward older alerts (offset+).
 */
void ui_layout_scroll(int direction)
{
    int delta = 0;
    switch (direction) {
    case UI_INPUT_UP:   delta = -1; break;
    case UI_INPUT_DOWN: delta = +1; break;
    case UI_INPUT_PGUP: delta = -5; break;
    case UI_INPUT_PGDN: delta = +5; break;
    default:            break;
    }
    g_scroll_offset += delta;
    if (g_scroll_offset < 0) g_scroll_offset = 0;
}
