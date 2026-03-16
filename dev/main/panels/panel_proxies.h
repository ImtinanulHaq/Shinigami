/**
 * @file    panel_proxies.h
 * @brief   Proxies panel - audio, camera, sensor, gpio proxy metrics.
 */
#ifndef PANEL_PROXIES_H
#define PANEL_PROXIES_H

#include <ncurses.h>

int panel_proxies_init(WINDOW *w);
void panel_proxies_render(WINDOW *w);
int panel_proxies_input(int ch);
void panel_proxies_shutdown(void);

#endif
