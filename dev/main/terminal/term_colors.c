/**
 * @file    term_colors.c
 * @brief   Color pair initialization for the RED theme.
 */
#include "term_colors.h"
#include <ncurses.h>

/**
 * Initialize all ncurses color pairs for the RED theme.
 */
int term_colors_init(void)
{
    if (!has_colors()) {
        return -1;
    }

    start_color();
    use_default_colors();

    /* Define all color pairs: foreground, background */
    
    init_pair(CLR_DEFAULT,      COLOR_WHITE,   COLOR_BLACK);
    init_pair(CLR_TITLE,        COLOR_WHITE,   COLOR_BLACK);    /* bold in attron */
    init_pair(CLR_BORDER,       COLOR_RED,     COLOR_BLACK);
    init_pair(CLR_ACTIVE_TAB,   COLOR_BLACK,   COLOR_RED);
    init_pair(CLR_INACTIVE_TAB, COLOR_WHITE,   COLOR_BLACK);
    init_pair(CLR_GOOD,         COLOR_GREEN,   COLOR_BLACK);
    init_pair(CLR_WARN,         COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CLR_CRIT,         COLOR_RED,     COLOR_BLACK);     /* plus A_BOLD */
    init_pair(CLR_DIM,          COLOR_WHITE,   COLOR_BLACK);     /* plus A_DIM */
    init_pair(CLR_CMD,          COLOR_RED,     COLOR_BLACK);     /* plus A_BOLD */
    init_pair(CLR_HIGHLIGHT,    COLOR_BLACK,   COLOR_RED);       /* reverse video */
    init_pair(CLR_BADGE_OK,     COLOR_BLACK,   COLOR_GREEN);
    init_pair(CLR_BADGE_WARN,   COLOR_BLACK,   COLOR_YELLOW);
    init_pair(CLR_BADGE_CRIT,   COLOR_WHITE,   COLOR_RED);
    init_pair(CLR_BADGE_INFO,   COLOR_WHITE,   COLOR_BLUE);
    init_pair(CLR_TOPBAR,       COLOR_RED,     COLOR_BLACK);     /* plus A_BOLD */
    init_pair(CLR_BOTTOMBAR,    COLOR_RED,     COLOR_BLACK);     /* plus A_DIM */
    init_pair(CLR_INPUT,        COLOR_WHITE,   COLOR_RED);       /* command input */
    init_pair(CLR_MONITOR_GOOD, COLOR_GREEN,   COLOR_BLACK);
    init_pair(CLR_MONITOR_GRAPH,COLOR_RED,     COLOR_BLACK);

    return 0;
}

/**
 * Apply a color pair with attron.
 */
void term_colors_apply(int pair)
{
    attron(COLOR_PAIR(pair));
}

/**
 * Clear color formatting with attroff.
 */
void term_colors_clear(void)
{
    attroff(COLOR_PAIR(0));
    attroff(A_BOLD);
    attroff(A_DIM);
    attroff(A_REVERSE);
}
