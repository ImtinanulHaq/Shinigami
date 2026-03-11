/**
 * @file    panel_memory.c
 * @brief   Memory panel — system RAM/swap + memory pool details.
 */
#include "panel_memory.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static void draw_pct_bar(int y, int x, int width, float pct, int ok_col, int warn_col, int crit_col)
{
    if (width <= 2) return;
    int inner = width - 2;
    int filled = (int)(pct / 100.0f * inner);
    if (filled < 0) filled = 0;
    if (filled > inner) filled = inner;

    int col = (pct >= 90.0f) ? crit_col : (pct >= 70.0f) ? warn_col : ok_col;

    mvaddch(y, x, '[');
    attron(COLOR_PAIR(col));
    for (int i = 0; i < inner; i++)
        mvaddch(y, x + 1 + i, i < filled ? ACS_BLOCK : '.');
    attroff(COLOR_PAIR(col));
    mvaddch(y, x + 1 + inner, ']');
}

void panel_memory_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;

#define CHK if (row > max_row) return

    /* ── System memory ─────────────────────────────────────────── */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "── System Memory ");
    mvhline(row, 19, ACS_HLINE, cols - 21);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    uint64_t ram_total = s->sysinfo.ram_total_bytes;
    uint64_t ram_used  = s->sysinfo.ram_used_bytes;
    uint64_t swap_tot  = s->sysinfo.swap_total_bytes;
    uint64_t swap_used = s->sysinfo.swap_used_bytes;

    float ram_pct  = ram_total  ? (float)ram_used  * 100.0f / (float)ram_total  : 0.0f;
    float swap_pct = swap_tot   ? (float)swap_used  * 100.0f / (float)swap_tot   : 0.0f;

    CHK;
    mvprintw(row, 4, "RAM  ");
    draw_pct_bar(row, 9, 40, ram_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
    mvprintw(row, 51, " %6lu / %6lu MB  (%.1f%%)",
             ram_used / (1024*1024), ram_total / (1024*1024), (double)ram_pct);
    row++;

    CHK;
    mvprintw(row, 4, "Swap ");
    draw_pct_bar(row, 9, 40, swap_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
    mvprintw(row, 51, " %6lu / %6lu MB  (%.1f%%)",
             swap_used / (1024*1024), swap_tot / (1024*1024), (double)swap_pct);
    row++;

    if (s->sysinfo.fd_used || s->sysinfo.fd_max) {
        CHK;
        float fd_pct = s->sysinfo.fd_max ? (float)s->sysinfo.fd_used * 100.0f / s->sysinfo.fd_max : 0;
        mvprintw(row, 4, "FDs  ");
        draw_pct_bar(row, 9, 40, fd_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
        mvprintw(row, 51, " %6u / %6u       (%.1f%%)",
                 s->sysinfo.fd_used, s->sysinfo.fd_max, (double)fd_pct);
        row++;
    }
    row++;

    /* ── Memory pools ──────────────────────────────────────────── */
    CHK;
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "── Memory Pools (%u) ", s->pool_count);
    mvhline(row, 22, ACS_HLINE, cols - 24);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    if (s->pool_count == 0) {
        CHK;
        mvprintw(row++, 4, "(no pools registered)");
        return;
    }

    /* Column header */
    CHK;
    attron(A_BOLD);
    mvprintw(row++, 4, "%-20s %8s %8s %8s %8s %8s %8s %10s",
             "Pool", "BlkSz", "Total", "Used", "Free", "Peak", "Usage%", "Alloc/s");
    attroff(A_BOLD);
    mvhline(row++, 4, ACS_HLINE, cols - 6);

    int shown = 0;
    for (uint32_t i = 0; i < POOL_MAX; i++) {
        const pool_metrics_t *p = &s->pools[i];
        if (!p->name[0]) continue;
        if (shown++ < scroll) continue;
        if (row + 3 > max_row) break;

        int pc = (p->usage_pct >= 90) ? COLOR_PAIR_CRITICAL
               : (p->usage_pct >= 70) ? COLOR_PAIR_WARNING
               :                        COLOR_PAIR_GOOD;

        attron(COLOR_PAIR(pc));
        mvprintw(row, 4, "%-20s %8u %8u %8u %8u %8u %7.1f%%  %8.1f",
                 p->name, p->block_size, p->total_blocks, p->used_blocks,
                 p->free_blocks, p->peak_used,
                 (double)p->usage_pct, p->alloc_per_s);
        attroff(COLOR_PAIR(pc));
        row++;

        /* Usage bar */
        CHK;
        mvprintw(row, 6, "Usage ");
        draw_pct_bar(row, 12, 32, p->usage_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
        mvprintw(row, 46, "  fail:%llu  frag:%.1f%%",
                 (unsigned long long)p->fail_count, (double)p->fragmentation_pct);
        row++;

        mvhline(row++, 6, ACS_HLINE, cols - 8);
    }
#undef CHK
}
