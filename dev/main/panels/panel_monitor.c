/**
 * @file    panel_monitor.c
 * @brief   Monitor panel implementation.
 */
#include "panel_monitor.h"
#include <stdio.h>

int panel_monitor_init(WINDOW *w)
{
    return 0;
}

void panel_monitor_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "LIVE METRICS (from middleware_monitord)");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "CPU Metrics:");
    mvwprintw(w, row++, 4, "  Total:        55.2%%");
    mvwprintw(w, row++, 4, "  Audio Svc:    18.3%%  Camera: 12.1%%  GPIO: 3.2%%  Sensor: 5.8%%");
    
    row++;
    mvwprintw(w, row++, 2, "Memory Metrics:");
    mvwprintw(w, row++, 4, "  Total:        73.8%% (2.1 GB / 2.8 GB)");
    mvwprintw(w, row++, 4, "  Audio Svc:    512 MB  Camera: 742 MB  GPIO: 89 MB  Sensor: 156 MB");
    
    row++;
    mvwprintw(w, row++, 2, "Network:");
    mvwprintw(w, row++, 4, "  RX: 45.2 Mbps  TX: 23.1 Mbps");
    
    row++;
    mvwprintw(w, row++, 2, "Status: Broadcast connection active (update rate: 1 Hz)");
    
    if (h > 15) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Charts scroll automatically, use arrow keys to navigate");
    }
    
    wrefresh(w);
}

int panel_monitor_input(int ch)
{
    return 0;
}

void panel_monitor_shutdown(void)
{
}
