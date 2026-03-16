/**
 * @file    term_layout.c
 * @brief   Tab system and panel layout implementation.
 */
#include "term_layout.h"
#include "term_engine.h"
#include "term_colors.h"
#include "../main_config.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static const term_tab_t g_tabs[MAIN_PANEL_COUNT] = {
    {MAIN_PANEL_DASHBOARD, "DASHBOARD", "1"},
    {MAIN_PANEL_SERVICES,  "SERVICES",  "2"},
    {MAIN_PANEL_PROXIES,   "PROXIES",   "3"},
    {MAIN_PANEL_SECURITY,  "SECURITY",  "4"},
    {MAIN_PANEL_HAL,       "HAL",       "5"},
    {MAIN_PANEL_LOGS,      "LOGS",      "6"},
    {MAIN_PANEL_MONITOR,   "MONITOR",   "7"},
    {MAIN_PANEL_HELP,      "HELP",      "8"},
};

/**
 * Initialize layout: create windows and panels.
 */
int term_layout_init(term_layout_t *layout)
{
    if (!layout) return -1;

    memset(layout, 0, sizeof(*layout));
    term_engine_get_size(&layout->cols, &layout->rows);

    /* Validate minimum size */
    if (layout->cols < MAIN_MIN_COLS || layout->rows < MAIN_MIN_ROWS) {
        return -1;
    }

    /* Create windows: topbar (1), tabs (1), main (rows-4), bottombar (1) */
    layout->topbar_win = term_engine_newwin(1, layout->cols, 0, 0);
    layout->tab_win = term_engine_newwin(1, layout->cols, 1, 0);
    layout->main_win = term_engine_newwin(layout->rows - 3, layout->cols, 2, 0);
    layout->bottombar_win = term_engine_newwin(1, layout->cols, layout->rows - 1, 0);

    if (!layout->topbar_win || !layout->tab_win || 
        !layout->main_win || !layout->bottombar_win) {
        return -1;
    }

    /* Create panels for window stacking */
    layout->topbar_panel = new_panel(layout->topbar_win);
    layout->tab_panel = new_panel(layout->tab_win);
    layout->main_panel = new_panel(layout->main_win);
    layout->bottombar_panel = new_panel(layout->bottombar_win);

    if (!layout->topbar_panel || !layout->tab_panel ||
        !layout->main_panel || !layout->bottombar_panel) {
        return -1;
    }

    /* Set initial active tab */
    layout->active_tab = MAIN_PANEL_DASHBOARD;

    return 0;
}

/**
 * Shutdown layout and free windows.
 */
void term_layout_shutdown(term_layout_t *layout)
{
    if (!layout) return;

    term_engine_delwin(layout->topbar_win, layout->topbar_panel);
    term_engine_delwin(layout->tab_win, layout->tab_panel);
    term_engine_delwin(layout->main_win, layout->main_panel);
    term_engine_delwin(layout->bottombar_win, layout->bottombar_panel);

    memset(layout, 0, sizeof(*layout));
}

/**
 * Switch to a specific tab.
 */
int term_layout_switch_tab(term_layout_t *layout, int tab_id)
{
    if (!layout || tab_id < 0 || tab_id >= MAIN_PANEL_COUNT) {
        return -1;
    }

    layout->active_tab = tab_id;
    return 0;
}

/**
 * Switch to next tab (cycle).
 */
int term_layout_next_tab(term_layout_t *layout)
{
    if (!layout) return -1;
    int next = (layout->active_tab + 1) % MAIN_PANEL_COUNT;
    return term_layout_switch_tab(layout, next);
}

/**
 * Switch to previous tab.
 */
int term_layout_prev_tab(term_layout_t *layout)
{
    if (!layout) return -1;
    int prev = (layout->active_tab - 1 + MAIN_PANEL_COUNT) % MAIN_PANEL_COUNT;
    return term_layout_switch_tab(layout, prev);
}

/**
 * Render the tab bar with active/inactive styling.
 */
void term_layout_draw_tabs(term_layout_t *layout)
{
    if (!layout || !layout->tab_win) return;

    wclear(layout->tab_win);
    wmove(layout->tab_win, 0, 0);

    for (int i = 0; i < MAIN_PANEL_COUNT; i++) {
        if (i == layout->active_tab) {
            wattron(layout->tab_win, COLOR_PAIR(CLR_ACTIVE_TAB) | A_BOLD);
        } else {
            wattron(layout->tab_win, COLOR_PAIR(CLR_INACTIVE_TAB));
        }

        wprintw(layout->tab_win, " %s ", g_tabs[i].name);
        wattroff(layout->tab_win, COLOR_PAIR(CLR_ACTIVE_TAB) | A_BOLD | COLOR_PAIR(CLR_INACTIVE_TAB));

        if (i < MAIN_PANEL_COUNT - 1) {
            wprintw(layout->tab_win, " │ ");
        }
    }

    wrefresh(layout->tab_win);
}

/**
 * Render top status bar (title, version, time).
 */
void term_layout_draw_topbar(term_layout_t *layout)
{
    if (!layout || !layout->topbar_win) return;

    wclear(layout->topbar_win);
    wattron(layout->topbar_win, COLOR_PAIR(CLR_TOPBAR) | A_BOLD);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    /* Left: title and version */
    mvwprintw(layout->topbar_win, 0, 1, "%s v%s", MAIN_NAME, MAIN_VERSION);

    /* Right: time */
    int time_x = layout->cols - strlen(time_str) - 2;
    mvwprintw(layout->topbar_win, 0, time_x, "%s", time_str);

    wattroff(layout->topbar_win, COLOR_PAIR(CLR_TOPBAR) | A_BOLD);
    wrefresh(layout->topbar_win);
}

/**
 * Render bottom status bar.
 */
void term_layout_draw_bottombar(term_layout_t *layout)
{
    if (!layout || !layout->bottombar_win) return;

    wclear(layout->bottombar_win);
    wattron(layout->bottombar_win, COLOR_PAIR(CLR_BOTTOMBAR));

    /* Show current tab and keyboard shortcuts */
    const char *tab_name = g_tabs[layout->active_tab].name;
    mvwprintw(layout->bottombar_win, 0, 1, "[%s] 1-8:Tab Tab/Shift+Tab:Navigate :Cmd Esc:Quit",
              tab_name);

    wattroff(layout->bottombar_win, COLOR_PAIR(CLR_BOTTOMBAR));
    wrefresh(layout->bottombar_win);
}

/**
 * Handle terminal resize.
 */
int term_layout_on_resize(term_layout_t *layout)
{
    if (!layout) return -1;

    /* Shutdown and reinitialize */
    term_layout_shutdown(layout);
    return term_layout_init(layout);
}

/**
 * Refresh all layout windows.
 */
void term_layout_refresh(term_layout_t *layout)
{
    if (!layout) return;

    term_layout_draw_topbar(layout);
    term_layout_draw_tabs(layout);
    term_layout_draw_bottombar(layout);
    term_engine_refresh();
}

/**
 * Get the main content window.
 */
WINDOW *term_layout_get_main_win(term_layout_t *layout)
{
    return (layout) ? layout->main_win : NULL;
}

/**
 * Get current active tab.
 */
int term_layout_get_active_tab(term_layout_t *layout)
{
    return (layout) ? layout->active_tab : -1;
}

/**
 * Get tab name by id.
 */
const char *term_layout_get_tab_name(int tab_id)
{
    if (tab_id < 0 || tab_id >= MAIN_PANEL_COUNT) {
        return "UNKNOWN";
    }
    return g_tabs[tab_id].name;
}
