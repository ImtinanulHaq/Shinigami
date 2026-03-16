/**
 * @file    term_engine.c
 * @brief   ncurses terminal engine implementation.
 */
#include "term_engine.h"
#include "term_colors.h"
#include "../main_config.h"
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

static WINDOW *g_root_win = NULL;

/**
 * Initialize ncurses and verify terminal capabilities.
 */
int term_engine_init(void)
{
    /* Initialize ncurses with terminal type detection */
    g_root_win = initscr();
    if (!g_root_win) {
        fprintf(stderr, "ERROR: initscr() failed - ncurses initialization\n");
        fprintf(stderr, "Ensure TERM environment variable is set\n");
        return -1;
    }

    /* Configure ncurses for all terminal types */
    cbreak();                   /* Line buffering disabled */
    noecho();                   /* Don't echo input */
    keypad(stdscr, TRUE);       /* Enable function keys */
    nodelay(stdscr, TRUE);      /* Non-blocking getch() */
    notimeout(stdscr, TRUE);    /* Don't timeout on function keys */
    curs_set(0);                /* Hide cursor */
    
    /* Set default terminal attributes */
    term_clear_attrib();

    /* Check and initialize colors */
    if (term_colors_init() != 0) {
        endwin();
        fprintf(stderr, "ERROR: Color initialization failed\n");
        fprintf(stderr, "Continuing with monochrome mode...\n");
    }

    /* Verify terminal size */
    if (term_engine_check_size() != 0) {
        int cols = 0, rows = 0;
        getmaxyx(stdscr, rows, cols);
        endwin();
        fprintf(stderr, "ERROR: Terminal too small: %dx%d (need 100x28)\n", cols, rows);
        return -1;
    }

    return 0;
}

/**
 * Shutdown ncurses cleanly.
 */
void term_engine_shutdown(void)
{
    if (g_root_win) {
        curs_set(1);            /* Show cursor */
        endwin();               /* Exit ncurses mode */
        g_root_win = NULL;
    }
}

/**
 * Check if terminal dimensions meet minimum requirements.
 */
int term_engine_check_size(void)
{
    int cols = 0, rows = 0;
    getmaxyx(stdscr, rows, cols);

    if (cols < MAIN_MIN_COLS || rows < MAIN_MIN_ROWS) {
        return -1;
    }

    return 0;
}

/**
 * Get current terminal dimensions.
 */
void term_engine_get_size(int *cols, int *rows)
{
    getmaxyx(stdscr, *rows, *cols);
}

/**
 * Refresh all panels and update display (optimized).
 */
void term_engine_refresh(void)
{
    /* Update panels and redraw only changed areas */
    update_panels();
    doupdate();  /* Use optimized update instead of wrefresh() */
}

/**
 * Clear the entire screen.
 */
void term_engine_clear(void)
{
    clear();
    refresh();
}

/**
 * Handle terminal resize event (SIGWINCH).
 */
void term_engine_on_resize(void)
{
    /* resize_term() called by signal handler in main_signals.c */
    /* Here we just validate and warn if size is too small */
    if (term_engine_check_size() != 0) {
        /* Too small; caller should handle */
    }
}

/**
 * Draw a box with given color pair using box-drawing characters.
 */
void term_engine_draw_box(WINDOW *w, int color_pair)
{
    int h, wid;
    getmaxyx(w, h, wid);

    attron(COLOR_PAIR(color_pair));

    /* Corners and edges */
    mvwaddch(w, 0, 0, ACS_ULCORNER);
    mvwaddch(w, 0, wid - 1, ACS_URCORNER);
    mvwaddch(w, h - 1, 0, ACS_LLCORNER);
    mvwaddch(w, h - 1, wid - 1, ACS_LRCORNER);

    /* Horizontal lines */
    for (int x = 1; x < wid - 1; x++) {
        mvwaddch(w, 0, x, ACS_HLINE);
        mvwaddch(w, h - 1, x, ACS_HLINE);
    }

    /* Vertical lines */
    for (int y = 1; y < h - 1; y++) {
        mvwaddch(w, y, 0, ACS_VLINE);
        mvwaddch(w, y, wid - 1, ACS_VLINE);
    }

    attroff(COLOR_PAIR(color_pair));
}

/**
 * Draw a horizontal line at row y.
 */
void term_engine_draw_hline(WINDOW *w, int y, int color_pair)
{
    int h, wid;
    getmaxyx(w, h, wid);

    if (y < 0 || y >= h) return;

    attron(COLOR_PAIR(color_pair));
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, y, x, ACS_HLINE);
    }
    attroff(COLOR_PAIR(color_pair));
}

/**
 * Create a new window with initialization.
 */
WINDOW *term_engine_newwin(int h, int w, int y, int x)
{
    return newwin(h, w, y, x);
}

/**
 * Delete a window and associated panel.
 */
void term_engine_delwin(WINDOW *w, PANEL *p)
{
    if (p) {
        del_panel(p);
    }
    if (w) {
        delwin(w);
    }
}

/**
 * Clear all text attributes and renditions.
 */
void term_clear_attrib(void)
{
    attroff(A_BOLD);
    attroff(A_DIM);
    attroff(A_STANDOUT);
    attroff(A_UNDERLINE);
    attroff(A_BLINK);
    attroff(A_REVERSE);
}
