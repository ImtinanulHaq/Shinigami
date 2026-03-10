/**
 * @file    ui_layout.c
 * @brief   Main layout implementation.
 */
#include "ui_layout.h"
#include "ui_colors.h"
#include "ui_engine.h"
#include "panel_topbar.h"
#include "panel_overview.h"
#include <ncurses.h>
#include <string.h>

static int g_current_tab = 0;

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
    case 0:  /* Overview */
        panel_overview_render(snapshot, panel_y, panel_h, cols);
        break;
    case 1:  /* Services */
        /* TODO: panel_services_render() */
        mvprintw(panel_y, 2, "[Services panel - TODO]");
        break;
    case 2:  /* HAL */
        mvprintw(panel_y, 2, "[HAL panel - TODO]");
        break;
    case 3:  /* Memory */
        mvprintw(panel_y, 2, "[Memory panel - TODO]");
        break;
    case 4:  /* I/O */
        mvprintw(panel_y, 2, "[I/O panel - TODO]");
        break;
    case 5:  /* Security */
        mvprintw(panel_y, 2, "[Security panel - TODO]");
        break;
    case 6:  /* Alerts */
        mvprintw(panel_y, 2, "[Alerts panel - TODO]");
        break;
    case 7:  /* Logs */
        mvprintw(panel_y, 2, "[Logs panel - TODO]");
        break;
    case 8:  /* Traces */
        mvprintw(panel_y, 2, "[Traces panel - TODO]");
        break;
    case 9:  /* Help */
        mvprintw(panel_y, 2, "[Help panel - TODO]");
        mvprintw(panel_y + 2, 2, "Keys: q=quit, Tab=next panel, Arrows=navigate");
        break;
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
    if (tab_index >= 0 && tab_index < TAB_COUNT)
        g_current_tab = tab_index;
}

int ui_layout_get_tab(void)
{
    return g_current_tab;
}
