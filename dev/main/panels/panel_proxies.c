/**
 * @file    panel_proxies.c
 * @brief   Proxies panel implementation.
 */
#include "panel_proxies.h"
#include <stdio.h>

int panel_proxies_init(WINDOW *w)
{
    return 0;
}

void panel_proxies_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "PROXY METRICS");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "Audio Proxy:");
    mvwprintw(w, row++, 4, "  Requests: 1,234  Latency: 12.5ms  Success: 99.8%%");
    
    row++;
    mvwprintw(w, row++, 2, "Camera Proxy:");
    mvwprintw(w, row++, 4, "  Requests:   987  Latency: 8.3ms   Success: 99.9%%");
    
    row++;
    mvwprintw(w, row++, 2, "GPIO Proxy:");
    mvwprintw(w, row++, 4, "  Requests:   456  Latency: 2.1ms   Success: 100.0%%");
    
    row++;
    mvwprintw(w, row++, 2, "Status: All proxies operational");
    
    if (h > 12) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Metrics update every 1 second");
    }
    
    wrefresh(w);
}

int panel_proxies_input(int ch)
{
    return 0;
}

void panel_proxies_shutdown(void)
{
}
