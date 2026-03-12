/**
 * @file    panel_memory.c
 * @brief   Memory panel — system RAM/swap + memory pool details.
 */
#include "panel_memory.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static void draw_pct_bar(int y, int x, int width, float pct,
                         int ok_col, int warn_col, int crit_col)
{
    if (width <= 2) return;
    int inner = width - 2;
    int filled = (int)(pct / 100.0f * inner + 0.5f);
    if (filled < 0)     filled = 0;
    if (filled > inner) filled = inner;

    int col = (pct >= 90.0f) ? crit_col
            : (pct >= 70.0f) ? warn_col
            :                   ok_col;

    /* Opening bracket */
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvaddch(y, x, '[');
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

    /* Filled: inverted solid block — works on every terminal */
    for (int i = 0; i < inner; i++) {
        if (i < filled) {
            attron(COLOR_PAIR(col) | A_REVERSE | A_BOLD);
            mvaddch(y, x + 1 + i, ' ');
            attroff(A_BOLD | A_REVERSE | COLOR_PAIR(col));
        } else {
            attron(COLOR_PAIR(COLOR_PAIR_BORDER));
            mvaddch(y, x + 1 + i, '-');
            attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
        }
    }

    /* Closing bracket */
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvaddch(y, x + 1 + inner, ']');
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
}

void panel_memory_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;

#define CHK if (row > max_row) return

    /* ── System memory section header ─────────────────────────── */
    attron(COLOR_PAIR(COLOR_PAIR_BORDER) | A_BOLD);
    mvprintw(row, 0, "|");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_BORDER));
    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvprintw(row, 2, "-- SYSTEM MEMORY ");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvhline(row, 19, '-', cols - 21);
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    row++;

    uint64_t ram_total = s->sysinfo.ram_total_bytes;
    uint64_t ram_used  = s->sysinfo.ram_used_bytes;
    uint64_t swap_tot  = s->sysinfo.swap_total_bytes;
    uint64_t swap_used = s->sysinfo.swap_used_bytes;

    float ram_pct  = ram_total  ? (float)ram_used  * 100.0f / (float)ram_total  : 0.0f;
    float swap_pct = swap_tot   ? (float)swap_used  * 100.0f / (float)swap_tot   : 0.0f;

    CHK;
    attron(COLOR_PAIR(COLOR_PAIR_DIM) | A_BOLD);
    mvprintw(row, 4, "RAM  ");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_DIM));
    draw_pct_bar(row, 9, 44, ram_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
    attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
    mvprintw(row, 55, " %lu / %lu MB",
             (unsigned long)(ram_used / (1024*1024)),
             (unsigned long)(ram_total / (1024*1024)));
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));
    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    printw("  (%.1f%%)", (double)ram_pct);
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    row++;

    CHK;
    attron(COLOR_PAIR(COLOR_PAIR_DIM) | A_BOLD);
    mvprintw(row, 4, "Swap ");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_DIM));
    draw_pct_bar(row, 9, 44, swap_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
    attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
    mvprintw(row, 55, " %lu / %lu MB",
             (unsigned long)(swap_used / (1024*1024)),
             (unsigned long)(swap_tot / (1024*1024)));
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));
    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    printw("  (%.1f%%)", (double)swap_pct);
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    row++;

    if (s->sysinfo.fd_used || s->sysinfo.fd_max) {
        CHK;
        float fd_pct = s->sysinfo.fd_max ? (float)s->sysinfo.fd_used * 100.0f / s->sysinfo.fd_max : 0;
        attron(COLOR_PAIR(COLOR_PAIR_DIM) | A_BOLD);
        mvprintw(row, 4, "FDs  ");
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_DIM));
        draw_pct_bar(row, 9, 44, fd_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
        attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
        mvprintw(row, 55, " %u / %u",
                 s->sysinfo.fd_used, s->sysinfo.fd_max);
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));
        attron(COLOR_PAIR(COLOR_PAIR_DIM));
        printw("  (%.1f%%)", (double)fd_pct);
        attroff(COLOR_PAIR(COLOR_PAIR_DIM));
        row++;
    }
    row++;

    /* ── Memory pools section header ───────────────────────── */
    CHK;
    attron(COLOR_PAIR(COLOR_PAIR_BORDER) | A_BOLD);
    mvprintw(row, 0, "|");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_BORDER));
    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvprintw(row, 2, "-- MEMORY POOLS (%u) ", s->pool_count);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvhline(row, 22, '-', cols - 24);
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    row++;

    if (s->pool_count == 0) {
        CHK;
        mvprintw(row++, 4, "(no pools registered)");
        return;
    }

    /* Column header */
    CHK;
    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvprintw(row++, 4, "%-20s %8s %8s %8s %8s %8s %8s %10s",
             "Pool", "BlkSz", "Total", "Used", "Free", "Peak", "Usage%", "Alloc/s");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    mvhline(row++, 4, '-', cols - 6);
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

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
        attron(COLOR_PAIR(COLOR_PAIR_DIM));
        mvprintw(row, 6, "Usage ");
        attroff(COLOR_PAIR(COLOR_PAIR_DIM));
        draw_pct_bar(row, 12, 36, p->usage_pct, COLOR_PAIR_GOOD, COLOR_PAIR_WARNING, COLOR_PAIR_CRITICAL);
        attron(COLOR_PAIR(COLOR_PAIR_DIM));
        mvprintw(row, 50, "  fail:%llu  frag:%.1f%%",
                 (unsigned long long)p->fail_count, (double)p->fragmentation_pct);
        attroff(COLOR_PAIR(COLOR_PAIR_DIM));
        row++;

        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        mvhline(row++, 6, '-', cols - 8);
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    }
#undef CHK
}
