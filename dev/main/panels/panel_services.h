/**
 * @file    panel_services.h
 * @brief   Services panel - start/stop/restart services.
 */
#ifndef PANEL_SERVICES_H
#define PANEL_SERVICES_H

#include <ncurses.h>

/**
 * Initialize services panel.
 */
int panel_services_init(WINDOW *w);

/**
 * Render services panel.
 */
void panel_services_render(WINDOW *w);

/**
 * Handle input for services panel.
 */
int panel_services_input(int ch);

/**
 * Shutdown services panel.
 */
void panel_services_shutdown(void);

#endif
