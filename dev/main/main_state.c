/**
 * @file    main_state.c
 * @brief   Global application state implementation.
 */
#include "main_state.h"
#include "main_config.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Global state instance */
main_state_t g_state;

/**
 * Initialize global state.
 */
int main_state_init(void)
{
    memset(&g_state, 0, sizeof(g_state));

    /* Initialize mutex */
    if (pthread_mutex_init(&g_state.state_lock, NULL) != 0) {
        return -1;
    }

    /* Initialize sockets to -1 (closed) */
    g_state.sm_socket = -1;
    g_state.monitor_socket = -1;

    /* Set defaults */
    g_state.active_tab = MAIN_PANEL_DASHBOARD;
    g_state.running = 1;
    g_state.refresh_ms = MAIN_REFRESH_MS;
    g_state.last_refresh = time(NULL);

    return 0;
}

/**
 * Shutdown global state.
 */
void main_state_shutdown(void)
{
    pthread_mutex_destroy(&g_state.state_lock);
    memset(&g_state, 0, sizeof(g_state));
}

/**
 * Lock state.
 */
void main_state_lock(void)
{
    pthread_mutex_lock(&g_state.state_lock);
}

/**
 * Unlock state.
 */
void main_state_unlock(void)
{
    pthread_mutex_unlock(&g_state.state_lock);
}

/**
 * Set error message.
 */
void main_state_set_error(const char *fmt, ...)
{
    if (!fmt) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_state.last_error, sizeof(g_state.last_error), fmt, args);
    va_end(args);
}

/**
 * Get last error.
 */
const char *main_state_get_error(void)
{
    return g_state.last_error[0] ? g_state.last_error : "No error";
}
