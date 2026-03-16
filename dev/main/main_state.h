/**
 * @file    main_state.h
 * @brief   Global application state structure.
 */
#ifndef MAIN_STATE_H
#define MAIN_STATE_H

#include <time.h>
#include <pthread.h>

/**
 * Global application state.
 */
typedef struct {
    /* UI state */
    int active_tab;
    int running;
    int resize_needed;

    /* Connection state */
    int sm_socket;              /* Servicemanager socket fd (-1 if closed) */
    int monitor_socket;         /* Monitor broadcast socket fd */
    time_t sm_last_connect_attempt;
    time_t monitor_last_connect_attempt;

    /* Command palette state */
    char cmd_input[512];         /* Current command being typed */
    int cmd_input_len;
    int cmd_palette_visible;

    /* Log buffers (inotify watching) */
    char log_audio[5000];        /* Most recent 5000 lines from audio service log */
    char log_camera[5000];
    char log_sensor[5000];
    char log_gpio[5000];
    char log_services[5000];
    char log_monitord[5000];
    char log_sm[5000];

    /* Last error message */
    char last_error[256];

    /* Refresh timing */
    time_t last_refresh;
    int refresh_ms;

    /* Panel-specific data pointers (set by panel implementations) */
    void *panel_data[8];        /* For each MAIN_PANEL_* to store panel state */

    /* Synchronization */
    pthread_mutex_t state_lock;

} main_state_t;

/**
 * Global state instance (defined in main.c).
 */
extern main_state_t g_state;

/**
 * Initialize global state.
 */
int main_state_init(void);

/**
 * Shutdown global state.
 */
void main_state_shutdown(void);

/**
 * Lock state for safe multi-threaded access.
 */
void main_state_lock(void);

/**
 * Unlock state.
 */
void main_state_unlock(void);

/**
 * Set last error message.
 */
void main_state_set_error(const char *fmt, ...);

/**
 * Get last error message.
 */
const char *main_state_get_error(void);

#endif
