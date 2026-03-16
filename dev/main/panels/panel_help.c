/**
 * @file    panel_help.c
 * @brief   Help panel implementation.
 */
#include "panel_help.h"
#include <stdio.h>

int panel_help_init(WINDOW *w)
{
    return 0;
}

void panel_help_render(WINDOW *w)
{
    if (!w) return;
    
    int h, wid;
    getmaxyx(w, h, wid);
    
    wclear(w);
    werase(w);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 0, 1, "KEYBOARD SHORTCUTS AND HELP");
    wattroff(w, A_BOLD);
    
    for (int x = 0; x < wid; x++) {
        mvwaddch(w, 1, x, ACS_HLINE);
    }
    
    int row = 3;
    mvwprintw(w, row++, 2, "Tab Navigation:");
    mvwprintw(w, row++, 4, "  1-8              Switch to specific tab (Dashboard/Services/Proxies/Security/HAL/Logs/Monitor/Help)");
    mvwprintw(w, row++, 4, "  Tab / Shift+Tab  Navigate between tabs or panels");
    
    row++;
    mvwprintw(w, row++, 2, "Command Palette:");
    mvwprintw(w, row++, 4, "  :                Enter command mode (type command and press Enter)");
    mvwprintw(w, row++, 4, "  :stop-all        Stop all services");
    mvwprintw(w, row++, 4, "  :start-all       Start all services");
    
    row++;
    mvwprintw(w, row++, 2, "Navigation:");
    mvwprintw(w, row++, 4, "  Arrow Keys       Move cursor / scroll in panels");
    mvwprintw(w, row++, 4, "  Page Up/Down     Scroll large areas");
    mvwprintw(w, row++, 4, "  Esc / q          Exit / Quit application");
    
    row++;
    mvwprintw(w, row++, 2, "More Info: See docs/SHINIGAMI_SPECIFICATION.md or type :help");
    
    if (h > 16) {
        row = h - 3;
        mvwprintw(w, row, 2, "Press : for command palette");
        mvwprintw(w, row + 1, 2, "Use arrow keys to navigate this help page");
    }
    
    wrefresh(w);
}

int panel_help_input(int ch)
{
    return 0;
}

void panel_help_shutdown(void)
{
}
