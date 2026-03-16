/**
 * @file    panel_security.c
 * @brief   Security panel implementation.
 */
#include "panel_security.h"

int panel_security_init(WINDOW *w) { return 0; }
void panel_security_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "SECURITY PANEL - TODO"); wrefresh(w); } }
int panel_security_input(int ch) { return 0; }
void panel_security_shutdown(void) { }
