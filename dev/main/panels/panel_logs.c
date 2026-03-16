/**
 * @file    panel_logs.c
 * @brief   Logs panel implementation.
 */
#include "panel_logs.h"

int panel_logs_init(WINDOW *w) { return 0; }
void panel_logs_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "LOGS PANEL - TODO"); wrefresh(w); } }
int panel_logs_input(int ch) { return 0; }
void panel_logs_shutdown(void) { }
