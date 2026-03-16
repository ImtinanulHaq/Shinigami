/**
 * @file    panel_proxies.c
 * @brief   Proxies panel implementation.
 */
#include "panel_proxies.h"

int panel_proxies_init(WINDOW *w) { return 0; }
void panel_proxies_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "PROXIES PANEL - TODO"); wrefresh(w); } }
int panel_proxies_input(int ch) { return 0; }
void panel_proxies_shutdown(void) { }
