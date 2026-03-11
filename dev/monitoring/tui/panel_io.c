/**
 * @file    panel_io.c
 * @brief   I/O panel — io_uring rings, ring buffers, IPC channels.
 */
#include "panel_io.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static void sec_hdr(int *row, int max_row, int cols, const char *title)
{
    if (*row > max_row) return;
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(*row, 2, "-- %s ", title);
    int tlen = 4 + (int)strlen(title) + 1;
    mvhline(*row, 2 + tlen, ACS_HLINE, cols - tlen - 4);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    (*row)++;
}

static const char *ipc_state_str(uint8_t st)
{
    switch (st) {
    case 0: return "DOWN ";
    case 1: return "OK   ";
    case 2: return "WARN ";
    case 3: return "ERR  ";
    default: return "?    ";
    }
}

static int ipc_state_col(uint8_t st)
{
    switch (st) {
    case 1: return COLOR_PAIR_GOOD;
    case 2: return COLOR_PAIR_WARNING;
    case 3: return COLOR_PAIR_CRITICAL;
    default: return COLOR_PAIR_DEFAULT;
    }
}

void panel_io_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;
    (void)scroll;

    /* ── io_uring rings ──────────────────────────────────────── */
    sec_hdr(&row, max_row, cols, "io_uring Rings");

    if (s->uring_count == 0) {
        if (row <= max_row) mvprintw(row++, 4, "(no io_uring rings registered)");
    } else {
        if (row <= max_row) {
            attron(A_BOLD);
            mvprintw(row++, 4, "%-16s %6s %6s %6s %6s %10s %12s %8s %8s %8s",
                     "Name", "SQdep", "CQdep", "SQfil%", "CQfil%",
                     "Submit/s", "Complt/s", "p50us", "p95us", "p99us");
            attroff(A_BOLD);
            if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
        }
        for (uint32_t i = 0; i < s->uring_count && i < URING_MAX; i++) {
            const uring_metrics_t *u = &s->urings[i];
            if (!u->name[0] || row > max_row) continue;
            int col = (u->cq_fill_pct > 80) ? COLOR_PAIR_CRITICAL
                    : (u->cq_fill_pct > 60) ? COLOR_PAIR_WARNING
                    :                          COLOR_PAIR_GOOD;
            attron(COLOR_PAIR(col));
            mvprintw(row++, 4, "%-16s %6u %6u %5.1f%% %5.1f%% %10.1f %12.1f %8u %8u %8u",
                     u->name, u->sq_depth, u->cq_depth,
                     (double)u->sq_fill_pct, (double)u->cq_fill_pct,
                     u->submits_per_s, u->completions_per_s,
                     u->lat_p50_us, u->lat_p95_us, u->lat_p99_us);
            attroff(COLOR_PAIR(col));
        }
    }
    row++;

    /* ── Ring buffers ────────────────────────────────────────── */
    if (row <= max_row) sec_hdr(&row, max_row, cols, "Ring Buffers");

    if (s->ringbuf_count == 0) {
        if (row <= max_row) mvprintw(row++, 4, "(no ring buffers registered)");
    } else {
        if (row <= max_row) {
            attron(A_BOLD);
            mvprintw(row++, 4, "%-16s %8s %8s %7s %10s %10s %8s %8s",
                     "Name", "Capacity", "Used", "Fill%", "Writes/s", "Reads/s", "Drops", "Wraps");
            attroff(A_BOLD);
            if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
        }
        for (uint32_t i = 0; i < s->ringbuf_count && i < RINGBUF_MAX; i++) {
            const ringbuf_metrics_t *rb = &s->ring_buffers[i];
            if (!rb->name[0] || row > max_row) continue;
            int col = (rb->fill_pct > 85) ? COLOR_PAIR_CRITICAL
                    : (rb->fill_pct > 60) ? COLOR_PAIR_WARNING
                    :                        COLOR_PAIR_GOOD;
            attron(COLOR_PAIR(col));
            mvprintw(row++, 4, "%-16s %8u %8u %6.1f%% %10.1f %10.1f %8llu %8llu",
                     rb->name, rb->capacity, rb->used,
                     (double)rb->fill_pct,
                     rb->writes_per_s, rb->reads_per_s,
                     (unsigned long long)rb->drop_count,
                     (unsigned long long)rb->wrap_count);
            attroff(COLOR_PAIR(col));
        }
    }
    row++;

    /* ── IPC channels ────────────────────────────────────────── */
    if (row <= max_row) sec_hdr(&row, max_row, cols, "IPC Channels");

    if (s->ipc_count == 0) {
        if (row <= max_row) mvprintw(row++, 4, "(no IPC channels registered)");
    } else {
        if (row <= max_row) {
            attron(A_BOLD);
            mvprintw(row++, 4, "%-24s %-5s %7s %7s %8s %7s %8s",
                     "Channel", "State", "SendQ", "RecvQ", "Msgs/s", "Lat ms", "Drops");
            attroff(A_BOLD);
            if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
        }
        for (uint32_t i = 0; i < s->ipc_count && i < IPC_CHAN_MAX; i++) {
            const ipc_metrics_t *c = &s->ipc_channels[i];
            if (!c->name[0] || row > max_row) continue;
            int col = ipc_state_col(c->state);
            attron(COLOR_PAIR(col));
            mvprintw(row++, 4, "%-24s %-5s %7u %7u %8.1f %7.2f %8llu",
                     c->name, ipc_state_str(c->state),
                     c->send_q_depth, c->recv_q_depth,
                     c->msgs_per_s, (double)c->lat_ms,
                     (unsigned long long)c->drop_count);
            attroff(COLOR_PAIR(col));
        }
    }
}
