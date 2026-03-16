/**
 * @file    panel_dashboard.c
 * @brief   Dashboard panel implementation.
 */
#include "panel_dashboard.h"

int panel_dashboard_init(WINDOW *w)
{
    return 0;
}

void panel_dashboard_render(WINDOW *w)
{
    if (!w) return;
    wclear(w);
    mvwprintw(w, 0, 0, "DASHBOARD PANEL - TODO");
    wrefresh(w);
}

int panel_dashboard_input(int ch)
{
    return 0;
}

void panel_dashboard_shutdown(void)
{
}
