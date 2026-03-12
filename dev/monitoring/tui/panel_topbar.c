/**
 * @file    panel_topbar.c
 * @brief   Top bar — « SHINIGAMI MONITOR », centered clock, LIVE/DISC indicator.
 *
 * Layout (full terminal width, row 0):
 *   Left:   ├─ SHINIGAMI MONITOR  (green, bold)
 *   Centre: HH:MM:SS              (white)
 *   Right: \u25cf LIVE / \u2717 DISC        (green+blink / red)
 */
#include "panel_topbar.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_connected = 1;  /* default: assume connected */

void panel_topbar_set_connected(int connected)
{
    g_connected = connected;
}

void panel_topbar_render(const mon_snapshot_t *snapshot, int y, int cols)
{
    (void)snapshot;

    /* \u2500\u2500 Fill row with border color \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500 */
    move(y, 0);
    attron(COLOR_PAIR(COLOR_PAIR_BORDER));
    for (int i = 0; i < cols; i++) addch(' ');
    attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

    /* \u2500\u2500 Left: title \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500 */
    attron(COLOR_PAIR(COLOR_PAIR_BORDER) | A_BOLD);
    mvprintw(y, 1, "\xe2\x94\xbc\xe2\x94\x80 SHINIGAMI MONITOR");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_BORDER));

    /* \u2500\u2500 Centre: HH:MM:SS \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500 */
    time_t     now  = time(NULL);
    struct tm *tms  = localtime(&now);
    char       tbuf[12];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tms);
    int clen = (int)strlen(tbuf);
    int cx   = (cols - clen) / 2;
    if (cx > 0) {
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        mvprintw(y, cx, "%s", tbuf);
        attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));
    }

    /* \u2500\u2500 Right: \u25cf LIVE / \u2717 DISC \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500 */
    if (cols > 12) {
        if (g_connected) {
            attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD | A_BLINK);
            mvprintw(y, cols - 9, "\xe2\x97\x8f LIVE  ");
            attroff(A_BLINK | A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));
        } else {
            attron(COLOR_PAIR(COLOR_PAIR_CRITICAL) | A_BOLD);
            mvprintw(y, cols - 9, "\xe2\x9c\x97 DISC  ");
            attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_CRITICAL));
        }
    }
}

