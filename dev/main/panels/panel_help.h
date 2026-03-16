/**
 * @file    panel_help.h
 * @brief   Help panel - keyboard shortcuts, command reference, status info.
 */
#ifndef PANEL_HELP_H
#define PANEL_HELP_H

#include <ncurses.h>

int panel_help_init(WINDOW *w);
void panel_help_render(WINDOW *w);
int panel_help_input(int ch);
void panel_help_shutdown(void);

#endif
