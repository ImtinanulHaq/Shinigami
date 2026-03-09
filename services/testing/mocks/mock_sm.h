/**
 * @file mock_sm.h
 * @brief Mock Service Manager socket responder for integration testing.
 *
 * Creates a listening Unix domain socket that mimics the real SM's
 * wire protocol.  Test code can inspect which messages arrived and
 * inject reply codes.
 */

#ifndef MOCK_SM_H
#define MOCK_SM_H

#include <stdint.h>
#include <sys/types.h>

#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"

/* ── mock SM state ─────────────────────────────────────────────────────── */

#define MOCK_SM_MAX_SERVICES 16
#define MOCK_SM_BACKLOG       8

typedef struct {
    char  service_name[SM_MAX_NAME];
    char  socket_path[SM_MAX_PATH];
    pid_t pid;
    int   registered;
} mock_sm_entry_t;

typedef struct {
    int               listen_fd;
    char              socket_path[SM_MAX_PATH];
    mock_sm_entry_t   entries[MOCK_SM_MAX_SERVICES];
    int               entry_count;

    /* Message counters */
    int   register_count;
    int   unregister_count;
    int   heartbeat_count;

    /* Injected reply codes (0 = SM_OK = accept) */
    int32_t register_reply;
    int32_t heartbeat_reply;
    int32_t unregister_reply;

    /* Worker thread */
    int   running;
    void *thread;    /* pthread_t stored opaquely */
} mock_sm_t;

/* ── public API ─────────────────────────────────────────────────────────── */

/**
 * @brief Create and start a mock SM on the given Unix socket path.
 *
 * Spawns a background thread that accepts one client connection per
 * test case.  The socket is removed and recreated on each call.
 *
 * @param path   Unix socket path (e.g. "/tmp/mock_sm.sock").
 * @return 0 on success, -1 on failure.
 */
int mock_sm_start(mock_sm_t *sm, const char *path);

/**
 * @brief Stop the mock SM and release all resources.
 */
void mock_sm_stop(mock_sm_t *sm);

/**
 * @brief Wait for exactly one client request to be processed (blocking).
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return 0 if a request was received within the timeout, -1 otherwise.
 */
int mock_sm_wait_request(mock_sm_t *sm, int timeout_ms);

/**
 * @brief Check whether a service is currently registered with the mock SM.
 */
int mock_sm_is_registered(const mock_sm_t *sm, const char *service_name);

/**
 * @brief Reset all counters and registered entries.
 */
void mock_sm_reset(mock_sm_t *sm);

#endif /* MOCK_SM_H */
