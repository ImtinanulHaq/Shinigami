/**
 * @file    panel_security.h
 * @brief   Security panel - violations, audit trail, capability matrix.
 */
#ifndef PANEL_SECURITY_H
#define PANEL_SECURITY_H

#include <ncurses.h>

int panel_security_init(WINDOW *w);
void panel_security_render(WINDOW *w);
int panel_security_input(int ch);
void panel_security_shutdown(void);

#endif
