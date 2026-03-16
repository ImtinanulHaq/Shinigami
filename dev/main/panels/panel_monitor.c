/**
 * @file    panel_monitor.c
 * @brief   Monitor panel implementation.
 */
#include "panel_monitor.h"

int panel_monitor_init(WINDOW *w) { return 0; }
void panel_monitor_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "MONITOR PANEL - TODO"); wrefresh(w); } }
int panel_monitor_input(int ch) { return 0; }
void panel_monitor_shutdown(void) { }
