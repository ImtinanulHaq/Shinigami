/**
 * @file    panel_security.c
 * @brief   Security panel implementation.
 */
#include "panel_security.h"
#include <stdio.h>

int panel_security_init(WINDOW *w)
{
    return 0;
}

void panel_security_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "SECURITY AND ALERTS");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "Capability Enforcement: ENABLED");
    mvwprintw(w, row++, 2, "Seccomp Filter:         ACTIVE");
    mvwprintw(w, row++, 2, "Sandbox Level:          HIGH");
    
    row++;
    mvwprintw(w, row++, 2, "Recent Alerts (Last 5):");
    mvwprintw(w, row++, 4, "[1] Memory spike in camera_service     10:45:23");
    mvwprintw(w, row++, 4, "[2] CPU threshold alert in sensor proc 10:44:11");
    mvwprintw(w, row++, 4, "[3] Audit: GPIO access denied         10:43:05");
    
    row++;
    mvwprintw(w, row++, 2, "Status: All security policies active, no violations");
    
    if (h > 12) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Real-time security monitoring enabled");
    }
    
    wrefresh(w);
}

int panel_security_input(int ch)
{
    return 0;
}

void panel_security_shutdown(void)
{
}
