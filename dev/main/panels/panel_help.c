/**
 * @file    panel_help.c
 * @brief   Help panel implementation.
 */
#include "panel_help.h"

int panel_help_init(WINDOW *w) { return 0; }
void panel_help_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "HELP PANEL - TODO"); wrefresh(w); } }
int panel_help_input(int ch) { return 0; }
void panel_help_shutdown(void) { }
