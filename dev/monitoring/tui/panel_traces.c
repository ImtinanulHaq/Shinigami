/**
 * @file    panel_traces.c
 * @brief   Traces panel — recent distributed traces with span timeline.
 */
#include "panel_traces.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static const char *trace_status_str(uint8_t st)
{
    switch (st) {
    case 0: return "OK  ";
    case 1: return "SLOW";
    case 2: return "ERR ";
    default: return "????";
    }
}

static int trace_status_color(uint8_t st)
{
    switch (st) {
    case 0: return COLOR_PAIR_GOOD;
    case 1: return COLOR_PAIR_WARNING;
    case 2: return COLOR_PAIR_CRITICAL;
    default: return COLOR_PAIR_DEFAULT;
    }
}

/* Draw a span as a proportional bar on a given row. */
static void draw_span_bar(int y, int x, int width,
                           uint64_t span_start, uint64_t span_end,
                           uint64_t total_us, uint8_t status)
{
    if (total_us == 0 || width <= 0) return;

    int bar_x = x + (int)((double)span_start / total_us * width);
    int bar_w = (int)((double)(span_end - span_start) / total_us * width);
    if (bar_w < 1) bar_w = 1;
    if (bar_x + bar_w > x + width) bar_w = x + width - bar_x;

    int col = trace_status_color(status);
    attron(COLOR_PAIR(col));
    for (int i = 0; i < bar_w; i++)
        mvaddch(y, bar_x + i, '#');
    attroff(COLOR_PAIR(col));
}

void panel_traces_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;

    /* Header */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "-- Distributed Traces (%u stored) ", s->trace_count);
    mvhline(row, 36, ACS_HLINE, cols - 38);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    if (s->trace_count == 0) {
        if (row <= max_row) mvprintw(row++, 4, "(no traces recorded yet)");
        return;
    }

    /* Column header */
    if (row <= max_row) {
        attron(A_BOLD);
        mvprintw(row++, 2, "%8s  %-6s  %10s  %3s  Timeline",
                 "TraceID", "Status", "Total us", "Spn");
        attroff(A_BOLD);
        if (row <= max_row) mvhline(row++, 2, ACS_HLINE, cols - 4);
    }

    /* Show last N traces (newest at top), scrollable */
    uint32_t total = s->trace_count;
    int timeline_x = 33;
    int timeline_w = cols - timeline_x - 2;
    if (timeline_w < 10) timeline_w = 10;

    int shown = 0;
    /* Iterate in reverse order (newest first, using circular head) */
    for (uint32_t n = 0; n < total && row + 3 <= max_row; n++) {
        /* Circular read: newest = (head - 1 - n) mod TRACE_STORE_MAX */
        int idx = ((int)s->trace_head - 1 - (int)n + TRACE_STORE_MAX) % TRACE_STORE_MAX;
        const trace_record_t *tr = &s->traces[idx];
        if (shown++ < scroll) continue;

        int sc = trace_status_color(tr->status);
        attron(COLOR_PAIR(sc) | A_BOLD);
        mvprintw(row, 2, "%8u  ", tr->trace_id);
        attroff(A_BOLD | COLOR_PAIR(sc));

        attron(COLOR_PAIR(sc));
        printw("%-6s  ", trace_status_str(tr->status));
        attroff(COLOR_PAIR(sc));

        printw("%10u  %3u  ", tr->total_us, tr->span_count);

        /* Timeline bar header row */
        /* Draw a ruler on the first trace */
        if (shown == 1 && row <= max_row) {
            mvprintw(row, timeline_x, "[");
            mvprintw(row, timeline_x + timeline_w + 1, "]");
        }

        row++;

        /* Render each span as a bar on a sub-row */
        for (uint8_t sp = 0; sp < tr->span_count && row <= max_row; sp++) {
            const trace_span_t *span = &tr->spans[sp];
            int span_col = trace_status_color(span->status);

            /* Label */
            attron(COLOR_PAIR(span_col));
            mvprintw(row, 6, "  [sp%02u] %-12s  %5llu-%5llu us",
                     sp, span->component,
                     (unsigned long long)span->enter_us,
                     (unsigned long long)span->exit_us);
            attroff(COLOR_PAIR(span_col));

            /* Bar */
            if (tr->total_us > 0) {
                mvprintw(row, timeline_x, "[");
                draw_span_bar(row, timeline_x + 1, timeline_w,
                              span->enter_us, span->exit_us,
                              tr->total_us, span->status);
                mvprintw(row, timeline_x + timeline_w + 1, "]");
            }

            /* Annotation */
            if (span->annotation[0] && timeline_x + timeline_w + 3 + 20 < cols) {
                attron(COLOR_PAIR(COLOR_PAIR_INFO));
                mvprintw(row, timeline_x + timeline_w + 3, "%.40s", span->annotation);
                attroff(COLOR_PAIR(COLOR_PAIR_INFO));
            }
            row++;
        }

        /* Bottleneck callout */
        if (tr->status != 0 && tr->span_count > 0 && row <= max_row) {
            uint8_t bi = (uint8_t)tr->bottleneck_span_idx;
            if (bi < tr->span_count) {
                attron(COLOR_PAIR(COLOR_PAIR_WARNING));
                mvprintw(row++, 6, "  ★ bottleneck: span[%u] %s",
                         bi, tr->spans[bi].component);
                attroff(COLOR_PAIR(COLOR_PAIR_WARNING));
            }
        }

        if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
    }
}
