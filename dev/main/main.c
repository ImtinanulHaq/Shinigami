/**
 * @file    main.c
 * @brief   Shinigami Terminal entry point.
 */
#include "main_state.h"
#include "main_signals.h"
#include "main_loop.h"
#include "terminal/term_engine.h"
#include "terminal/term_layout.h"
#include "terminal/term_cmd.h"
#include "connectors/conn_sm.h"
#include "connectors/conn_monitor.h"
#include "main_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

/**
 * Main entry point.
 */
int main(int argc, char *argv[])
{
    /* Set UTF-8 locale for Unicode support */
    setlocale(LC_ALL, "en_US.UTF-8");

    /* Initialize global state */
    if (main_state_init() != 0) {
        fprintf(stderr, "Failed to initialize global state\n");
        return 1;
    }

    /* Initialize ncurses */
    if (term_engine_init() != 0) {
        fprintf(stderr, "Failed to initialize terminal engine\n");
        main_state_shutdown();
        return 1;
    }

    /* Initialize layout */
    if (term_layout_init(&g_state.layout) != 0) {
        fprintf(stderr, "Failed to initialize layout\n");
        term_engine_shutdown();
        main_state_shutdown();
        return 1;
    }

    /* Initialize command system */
    if (term_cmd_init() != 0) {
        fprintf(stderr, "Failed to initialize command system\n");
        term_layout_shutdown(&g_state.layout);
        term_engine_shutdown();
        main_state_shutdown();
        return 1;
    }

    /* Setup signal handlers */
    if (main_signals_init() != 0) {
        fprintf(stderr, "Failed to setup signals\n");
        term_cmd_shutdown();
        term_layout_shutdown(&g_state.layout);
        term_engine_shutdown();
        main_state_shutdown();
        return 1;
    }

    /* Initial render */
    term_layout_refresh(&g_state.layout);

    /* Run main event loop */
    int loop_result = main_loop_run();

    /* Cleanup */
    term_cmd_shutdown();
    conn_sm_disconnect();
    conn_monitor_disconnect();
    term_layout_shutdown(&g_state.layout);
    term_engine_shutdown();
    main_state_shutdown();

    if (loop_result != 0) {
        fprintf(stderr, "Main loop error\n");
        return 1;
    }

    printf("Shinigami Terminal exited cleanly.\n");
    return 0;
}
