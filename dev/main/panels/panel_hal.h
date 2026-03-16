/**
 * @file    panel_hal.h
 * @brief   HAL panel - hardware device control (audio, camera, sensors, GPIO).
 */
#ifndef PANEL_HAL_H
#define PANEL_HAL_H

#include <ncurses.h>

int panel_hal_init(WINDOW *w);
void panel_hal_render(WINDOW *w);
int panel_hal_input(int ch);
void panel_hal_shutdown(void);

#endif
