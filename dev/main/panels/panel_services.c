/**
 * @file    panel_services.c
 * @brief   Services panel implementation.
 */
#include "panel_services.h"
#include <stdio.h>

int panel_services_init(WINDOW *w)
{
    return 0;
}

void panel_services_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "MANAGED SERVICES");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "[*] Audio Service     [RUNNING] pid=1234");
    mvwprintw(w, row++, 2, "[*] Camera Service    [RUNNING] pid=1235");
    mvwprintw(w, row++, 2, "[*] GPIO Service      [RUNNING] pid=1236");
    mvwprintw(w, row++, 2, "[*] Sensor Service    [RUNNING] pid=1237");
    
    row++;
    mvwprintw(w, row++, 2, "Status: 4 services running, 0 failed");
    
    if (h > 10) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Use arrow keys to select a service");
    }
    
    wrefresh(w);
}

int panel_services_input(int ch)
{
    return 0;
}

void panel_services_shutdown(void)
{
}
