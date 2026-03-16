/**
 * @file    main_signals.h
 * @brief   Signal handling (SIGWINCH for resize, SIGINT for exit).
 */
#ifndef MAIN_SIGNALS_H
#define MAIN_SIGNALS_H

/**
 * Setup signal handlers.
 */
int main_signals_init(void);

/**
 * Handle SIGWINCH (terminal resize).
 */
void main_signals_on_resize(int sig);

/**
 * Handle SIGINT (Ctrl-C).
 */
void main_signals_on_interrupt(int sig);

#endif
