/**
 * @file mock_sm.h
 * @brief Minimal Service Manager stub for service-level unit tests.
 *
 * Simulates the SM Unix socket so service IPC code can be tested
 * without a real SM process.  The mock SM accepts connections,
 * parses register/heartbeat/lookup messages, and returns configured
 * responses.
 */
#ifndef MOCK_SM_H
#define MOCK_SM_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_SM_MAX_SERVICES 16

typedef struct {
    /* Configuration */
    int     register_response;   /**< SM_OK or error code to return on REGISTER */
    int     lookup_response;     /**< SM_OK or error on LOOKUP */
    int     heartbeat_response;  /**< SM_OK or error on HEARTBEAT */
    int     shutdown_on_connect; /**< If non-zero, close conn immediately */

    /* Statistics */
    int     register_count;
    int     lookup_count;
    int     heartbeat_count;
    int     connect_count;

    /* Internal */
    int     listen_fd;
    char    socket_path[256];
    pthread_t thread;
    volatile int running;
    char    last_registered_name[64];
} mock_sm_t;

/**
 * Start the mock SM on a temp socket path.
 * @p out_path receives the socket path (caller must provide 256-byte buf).
 */
int  mock_sm_start(mock_sm_t *sm, char *out_path);

/** Stop the mock SM and wait for its thread to exit. */
void mock_sm_stop(mock_sm_t *sm);

/** Send a SHUTDOWN message to all connected services (test helper). */
int  mock_sm_broadcast_shutdown(mock_sm_t *sm);

/** Reset statistics counters. */
void mock_sm_reset_stats(mock_sm_t *sm);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_SM_H */
