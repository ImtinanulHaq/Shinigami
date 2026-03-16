/**
 * Shinigami Terminal - Simple single-screen middleware control
 */
#include "main_loop.h"
#include <stdlib.h>
#include <stdio.h>

int main(void)
{
    if (main_loop_init() != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize terminal\n");
        return 1;
    }

    if (main_loop_run() != 0) {
        fprintf(stderr, "[ERROR] Main loop failed\n");
        main_loop_shutdown();
        return 1;
    }

    main_loop_shutdown();
    return 0;
}
