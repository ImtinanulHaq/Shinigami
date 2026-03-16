/**
 * @file    panel_services.c
 * @brief   Services panel implementation.
 */
#include "panel_services.h"

int panel_services_init(WINDOW *w) { return 0; }
void panel_services_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "SERVICES PANEL - TODO"); wrefresh(w); } }
int panel_services_input(int ch) { return 0; }
void panel_services_shutdown(void) { }
