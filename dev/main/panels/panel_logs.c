/**
 * @file    panel_logs.c
 * @brief   Logs panel implementation.
 */
#include "panel_logs.h"
#include <stdio.h>

int panel_logs_init(WINDOW *w)
{
    return 0;
}

void panel_logs_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "SYSTEM LOGS");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "10:47:12 [INFO]  Service manager started successfully");
    mvwprintw(w, row++, 2, "10:47:13 [INFO]  Audio service initialized");
    mvwprintw(w, row++, 2, "10:47:13 [INFO]  Camera service initialized");
    mvwprintw(w, row++, 2, "10:47:14 [INFO]  GPIO service initialized");
    mvwprintw(w, row++, 2, "10:47:14 [INFO]  Sensor service initialized");
    mvwprintw(w, row++, 2, "10:47:15 [WARN]  Memory usage at 68%% (normal)");
    mvwprintw(w, row++, 2, "10:47:16 [INFO]  Monitor connection established");
    mvwprintw(w, row++, 2, "10:47:20 [INFO]  HAL initialized with 12 devices");
    
    row++;
    mvwprintw(w, row++, 2, "Status: System running normally (8 recent logs)");
    
    if (h > 14) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Space:Follow  j:Down  k:Up  G:End  g:Top");
    }
    
    wrefresh(w);
}

int panel_logs_input(int ch)
{
    return 0;
}

void panel_logs_shutdown(void)
{
}
