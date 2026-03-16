/**
 * @file    main_signals.c
 * @brief   Signal handling implementation.
 */
#include "main_signals.h"
#include "main_state.h"
#include <signal.h>
#include <ncurses.h>

/**
 * Setup signal handlers.
 */
int main_signals_init(void)
{
    signal(SIGWINCH, main_signals_on_resize);
    signal(SIGINT, main_signals_on_interrupt);
    return 0;
}

/**
 * Handle terminal resize.
 */
void main_signals_on_resize(int sig)
{
    main_state_lock();
    g_state.resize_needed = 1;
    main_state_unlock();

    endwin();
    refresh();
}

/**
 * Handle interrupt (Ctrl-C).
 */
void main_signals_on_interrupt(int sig)
{
    main_state_lock();
    g_state.running = 0;
    main_state_unlock();
}
