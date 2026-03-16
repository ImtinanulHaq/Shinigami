/**
 * @file    term_layout.h
 * @brief   Tab system, panel layout, and navigation logic.
 */
#ifndef TERM_LAYOUT_H
#define TERM_LAYOUT_H

#include <ncurses.h>
#include <panel.h>

/**
 * Tab structure for navigation.
 */
typedef struct {
    int id;                     /* Panel index (0-7) */
    const char *name;           /* Tab name (DASHBOARD, SERVICES, etc.) */
    const char *shortcut;       /* Keyboard shortcut (1-8, Q, W, E, R, T, Y, U, I) */
} term_tab_t;

/**
 * Layout state (tracks current active tab, window refs).
 */
typedef struct {
    int active_tab;             /* Current active panel id (0-7) */
    int cols, rows;             /* Current terminal dimensions */
    WINDOW *root_win;           /* Root ncurses window */
    WINDOW *topbar_win;         /* Top status bar */
    WINDOW *tab_win;            /* Tab bar window */
    WINDOW *main_win;           /* Main content area (all panels below) */
    WINDOW *bottombar_win;      /* Bottom status bar (help text + mode) */
    PANEL *topbar_panel;        /* Panels for stacking */
    PANEL *tab_panel;
    PANEL *main_panel;
    PANEL *bottombar_panel;
} term_layout_t;

/**
 * Initialize layout and create all windows.
 */
int term_layout_init(term_layout_t *layout);

/**
 * Shutdown layout and clean up windows.
 */
void term_layout_shutdown(term_layout_t *layout);

/**
 * Switch to a specific tab by id (0-7).
 */
int term_layout_switch_tab(term_layout_t *layout, int tab_id);

/**
 * Switch to next tab (cycling DASHBOARD -> SERVICES -> ... -> HELP -> DASHBOARD).
 */
int term_layout_next_tab(term_layout_t *layout);

/**
 * Switch to previous tab.
 */
int term_layout_prev_tab(term_layout_t *layout);

/**
 * Render the tab bar with active/inactive styling.
 */
void term_layout_draw_tabs(term_layout_t *layout);

/**
 * Render the top status bar (title, version, time).
 */
void term_layout_draw_topbar(term_layout_t *layout);

/**
 * Render the bottom status bar (shortcuts, current mode).
 */
void term_layout_draw_bottombar(term_layout_t *layout);

/**
 * Handle terminal resize: store new dimensions and rebuild layout.
 */
int term_layout_on_resize(term_layout_t *layout);

/**
 * Refresh all layout windows.
 */
void term_layout_refresh(term_layout_t *layout);

/**
 * Display command input overlay at bottom of screen.
 */
void term_layout_show_command_input(WINDOW *win, const char *cmd_buffer, int cmd_len);

/**
 * Get the window for the main content area (where panels draw).
 */
WINDOW *term_layout_get_main_win(term_layout_t *layout);

/**
 * Get current active tab id.
 */
int term_layout_get_active_tab(term_layout_t *layout);

/**
 * Get tab name by id.
 */
const char *term_layout_get_tab_name(int tab_id);

#endif
