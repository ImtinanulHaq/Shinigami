/**
 * @file    panel_dashboard.c
 * @brief   Dashboard panel implementation.
 */
#include "panel_dashboard.h"
#include "../main_config.h"
#include <time.h>
#include <stdio.h>

int panel_dashboard_init(WINDOW *w)
{
    return 0;
}

void panel_dashboard_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "SYSTEM OVERVIEW");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "CPU Usage:     -- %% (initializing...)");
    mvwprintw(w, row++, 2, "Memory Usage:  -- %% (initializing...)");
    mvwprintw(w, row++, 2, "Services:      -- running");
    mvwprintw(w, row++, 2, "Uptime:        -- hours");
    
    row++;
    mvwprintw(w, row++, 2, "Status: Connecting to monitor socket...");
    
    if (h > 10) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Press 1-8 to switch tabs or Tab to navigate");
    }
    
    wrefresh(w);
}

int panel_dashboard_input(int ch)
{
    return 0;
}

void panel_dashboard_shutdown(void)
{
}
