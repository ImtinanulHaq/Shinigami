/**
 * @file    panel_dashboard.h
 * @brief   Dashboard panel - system overview and quick stats.
 */
#ifndef PANEL_DASHBOARD_H
#define PANEL_DASHBOARD_H

#include <ncurses.h>

/**
 * Initialize dashboard panel.
 */
int panel_dashboard_init(WINDOW *w);

/**
 * Render dashboard panel.
 */
void panel_dashboard_render(WINDOW *w);

/**
 * Handle input for dashboard panel.
 */
int panel_dashboard_input(int ch);

/**
 * Shutdown dashboard panel.
 */
void panel_dashboard_shutdown(void);

#endif
