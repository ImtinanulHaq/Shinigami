/**
 * @file    ui_layout.c
 * @brief   Main layout implementation.
 */
#include "ui_layout.h"
#include "ui_colors.h"
#include "ui_engine.h"
#include "panel_topbar.h"
#include "panel_overview.h"
#include "panel_services.h"
#include "panel_hal.h"
#include "panel_memory.h"
#include "panel_io.h"
#include "panel_security.h"
#include "panel_alerts.h"
#include "panel_logs.h"
#include "panel_traces.h"
#include "panel_help.h"
#include <ncurses.h>
#include <string.h>

static int g_current_tab  = 0;
static int g_scroll_offset = 0;

static const char *TAB_NAMES[] = {
    "Overview",
    "Services",
    "HAL",
    "Memory",
    "I/O",
    "Security",
    "Alerts",
    "Logs",
    "Traces",
    "Help",
};
static const int TAB_COUNT = 10;

void ui_layout_render(const mon_snapshot_t *snapshot)
{
    clear();

    int rows, cols;
    ui_engine_get_size(&rows, &cols);

    /* Render top bar (always) */
    panel_topbar_render(snapshot, 0, cols);

    /* Render tab bar */
    move(1, 0);
    attron(COLOR_PAIR(COLOR_PAIR_HEADER));
    for (int i = 0; i < cols; i++) addch(' ');
    move(1, 2);
    for (int i = 0; i < TAB_COUNT; i++) {
        if (i == g_current_tab) {
            attron(A_BOLD | COLOR_PAIR(COLOR_PAIR_SELECTED));
        } else {
            attroff(A_BOLD);
            attron(COLOR_PAIR(COLOR_PAIR_HEADER));
        }
        printw(" %s ", TAB_NAMES[i]);
    }
    attroff(A_BOLD);
    attroff(COLOR_PAIR(COLOR_PAIR_HEADER));
    attroff(COLOR_PAIR(COLOR_PAIR_SELECTED));

    /* Render active panel */
    int panel_y = 3;
    int panel_h = rows - panel_y - 1;

    switch (g_current_tab) {
    case 0:  panel_overview_render (snapshot, panel_y, panel_h, cols);                   break;
    case 1:  panel_services_render (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 2:  panel_hal_render      (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 3:  panel_memory_render   (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 4:  panel_io_render       (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 5:  panel_security_render (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 6:  panel_alerts_render   (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 7:  panel_logs_render     (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 8:  panel_traces_render   (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    case 9:  panel_help_render     (snapshot, panel_y, panel_h, cols, g_scroll_offset);  break;
    }

    /* Render bottom status bar */
    move(rows - 1, 0);
    attron(COLOR_PAIR(COLOR_PAIR_HEADER));
    for (int i = 0; i < cols; i++) addch(' ');
    mvprintw(rows - 1, 2, "Middleware Monitor | q=quit | Tab=panels | r=refresh");
    attroff(COLOR_PAIR(COLOR_PAIR_HEADER));
}

void ui_layout_set_tab(int tab_index)
{
    if (tab_index >= 0 && tab_index < TAB_COUNT) {
        g_current_tab   = tab_index;
        g_scroll_offset = 0;          /* reset scroll when switching panels */
    }
}

int ui_layout_get_tab(void)
{
    return g_current_tab;
}

/* direction > 0 = scroll down, < 0 = scroll up, large magnitude = page */
void ui_layout_scroll(int direction)
{
    g_scroll_offset += direction;
    if (g_scroll_offset < 0)
        g_scroll_offset = 0;
}
