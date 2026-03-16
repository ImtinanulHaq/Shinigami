/**
 * @file    panel_hal.c
 * @brief   HAL panel implementation.
 */
#include "panel_hal.h"

int panel_hal_init(WINDOW *w) { return 0; }
void panel_hal_render(WINDOW *w) { if (w) { wclear(w); mvwprintw(w, 0, 0, "HAL PANEL - TODO"); wrefresh(w); } }
int panel_hal_input(int ch) { return 0; }
void panel_hal_shutdown(void) { }
