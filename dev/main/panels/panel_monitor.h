/**
 * @file    panel_monitor.h
 * @brief   Monitor panel - embedded mw_tui-style real-time monitoring dashboard.
 */
#ifndef PANEL_MONITOR_H
#define PANEL_MONITOR_H

#include <ncurses.h>

int panel_monitor_init(WINDOW *w);
void panel_monitor_render(WINDOW *w);
int panel_monitor_input(int ch);
void panel_monitor_shutdown(void);

#endif
