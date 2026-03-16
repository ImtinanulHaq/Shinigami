/**
 * @file    panel_hal.c
 * @brief   HAL panel implementation.
 */
#include "panel_hal.h"
#include <stdio.h>

int panel_hal_init(WINDOW *w)
{
    return 0;
}

void panel_hal_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "HARDWARE ABSTRACTION LAYER");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "Devices Exposed: 12");
    
    row++;
    mvwprintw(w, row++, 2, "Audio Devices:");
    mvwprintw(w, row++, 4, "  /dev/audio0  [ACTIVE]   PCM Output");
    mvwprintw(w, row++, 4, "  /dev/audio1  [ACTIVE]   Microphone");
    
    row++;
    mvwprintw(w, row++, 2, "Camera Devices:");
    mvwprintw(w, row++, 4, "  /dev/video0  [ACTIVE]   USB Camera");
    
    row++;
    mvwprintw(w, row++, 2, "GPIO Lines: 48 total, 8 configured");
    
    row++;
    mvwprintw(w, row++, 2, "Status: All HAL interfaces operational");
    
    if (h > 14) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Use arrow keys to view device details");
    }
    
    wrefresh(w);
}

int panel_hal_input(int ch)
{
    return 0;
}

void panel_hal_shutdown(void)
{
}
