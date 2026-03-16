/**
 * Main event loop - single screen with BANKAI display and command input
 */
#ifndef MAIN_LOOP_H
#define MAIN_LOOP_H

/* Initialize terminal and connections */
int main_loop_init(void);

/* Run the main event loop */
int main_loop_run(void);

/* Shutdown and cleanup */
void main_loop_shutdown(void);

#endif

