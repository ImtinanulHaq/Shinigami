/**
 * @file    panel_logs.h
 * @brief   Logs panel - live log tail with inotify watching.
 */
#ifndef PANEL_LOGS_H
#define PANEL_LOGS_H

#include <ncurses.h>

int panel_logs_init(WINDOW *w);
void panel_logs_render(WINDOW *w);
int panel_logs_input(int ch);
void panel_logs_shutdown(void);

#endif
