/**
 * @file    main_loop.h
 * @brief   Main event loop for terminal UI.
 */
#ifndef MAIN_LOOP_H
#define MAIN_LOOP_H

/**
 * Run the main event loop.
 * Returns 0 on clean exit, -1 on error.
 */
int main_loop_run(void);

/**
 * One iteration of the main loop (for testing/stepping).
 */
int main_loop_iterate(void);

/**
 * Stop the main loop.
 */
void main_loop_stop(void);

#endif
