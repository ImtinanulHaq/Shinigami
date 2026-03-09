/**
 * @file mock_sm.h
 * @brief Mock Service Manager for unit and integration testing.
 *
 * Stands up a real Unix-domain socket server that speaks the sm_protocol.h
 * wire format.  A dedicated POSIX thread handles connection accept and
 * dispatches messages.
 *
 * Features:
 *   - Accepts one client connection per test (re-arms automatically).
 *   - Responds to REGISTER, HEARTBEAT, and UNREGISTER with configurable codes.
 *   - Can proactively push MSG_HEALTH_CHECK, MSG_SHUTDOWN, and
 *     MSG_RELOAD_CONFIG to the connected client.
 *   - Records all received messages for post-test assertion.
 *   - Thread-safe: all fields guarded by an internal mutex.
 *   - mock_sm_reset() for clean per-test isolation.
 *
 * Usage:
 * @code
 *   mock_sm_t sm;
 *   mock_sm_start(&sm, "/tmp/test_sm.sock");   // spawn thread, listen
 *   // ... start service under test ...
 *   mock_sm_wait_request(&sm, 2000);            // wait for REGISTER
 *   assert(mock_sm_is_registered(&sm, "audio_service"));
 *   mock_sm_send_health_check(&sm);             // ask for health
 *   mock_sm_wait_request(&sm, 2000);            // wait for HEALTH_OK
 *   mock_sm_stop(&sm);
 * @endcode
 */

#ifndef MOCK_SM_H
#define MOCK_SM_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/types.h>

#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── limits ───────────────────────────────────────────────────────────── */

#define MOCK_SM_MAX_SERVICES  16   /**< Maximum simultaneously registered services. */
#define MOCK_SM_MAX_MESSAGES  64   /**< Circular log of received message types.     */

/* ── registered-service entry ─────────────────────────────────────────── */

/** @brief Record of one registered service. */
typedef struct {
    char    service_name[SM_MAX_NAME]; /**< As supplied in REGISTER payload.  */
    char    socket_path[SM_MAX_PATH];  /**< Socket path from REGISTER payload. */
    pid_t   pid;                       /**< PID from REGISTER payload.         */
    int     registered;                /**< 1 while registered, 0 after UNREG. */
} mock_sm_entry_t;

/* ── mock SM state ────────────────────────────────────────────────────── */

/**
 * @brief Opaque mock SM state.  Initialised by mock_sm_start().
 */
typedef struct {
    /* ── socket ── */
    int  listen_fd;                      /**< Bound/listening socket fd.       */
    int  client_fd;                      /**< Active client connection, or -1. */
    char socket_path[SM_MAX_PATH];       /**< Path passed to mock_sm_start().  */

    /* ── directory ── */
    mock_sm_entry_t entries[MOCK_SM_MAX_SERVICES];
    int             entry_count;

    /* ── message log ── */
    uint16_t msg_log[MOCK_SM_MAX_MESSAGES]; /**< Circular log of hdr.type values. */
    int      msg_log_head;
    int      msg_log_count;

    /* ── per-message counters ── */
    int register_count;
    int unregister_count;
    int heartbeat_count;
    int health_ok_count;   /**< SVC_MSG_HEALTH_OK responses received.         */
    int ping_count;        /**< SVC_MSG_PING requests received.               */

    /* ── injected reply codes (SM_OK = 0 = accept) ── */
    int32_t register_reply;
    int32_t heartbeat_reply;
    int32_t unregister_reply;

    /* ── auth token to return on REGISTER ── */
    uint8_t auth_token[64];
    size_t  auth_token_len;

    /* ── synchronisation ── */
    pthread_mutex_t lock;   /**< Guards all fields above.                     */
    pthread_cond_t  cond;   /**< Signalled when a request is processed.       */

    /* ── worker thread ── */
    pthread_t  thread;
    atomic_int running;     /**< 1 while thread should keep accepting msgs.   */
} mock_sm_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Create the Unix socket, bind, listen, and spawn the worker thread.
 *
 * Any existing socket file at @p path is unlinked first.
 *
 * @param sm    State struct to initialise (must outlive mock_sm_stop()).
 * @param path  Unix socket path (e.g. "/tmp/mock_sm.sock").
 * @return 0 on success, -1 on failure.
 */
int mock_sm_start(mock_sm_t *sm, const char *path);

/**
 * @brief Stop the worker thread, close the socket, and free resources.
 *
 * Safe to call even if mock_sm_start() partially failed.
 *
 * @param sm  State struct passed to mock_sm_start().
 */
void mock_sm_stop(mock_sm_t *sm);

/**
 * @brief Block until at least one message has been processed (or timeout).
 *
 * @param sm          Mock SM state.
 * @param timeout_ms  Maximum wait in milliseconds (0 = check once, no wait).
 * @return 0 if a message was processed within @p timeout_ms, -1 on timeout.
 */
int mock_sm_wait_request(mock_sm_t *sm, int timeout_ms);

/**
 * @brief Return 1 if a service with @p service_name is currently registered.
 *
 * @param sm            Mock SM state.
 * @param service_name  Name to look up.
 * @return 1 if registered, 0 otherwise.
 */
int mock_sm_is_registered(const mock_sm_t *sm, const char *service_name);

/**
 * @brief Push a MSG_HEALTH_CHECK to the currently connected client.
 *
 * @param sm  Mock SM state.
 * @return 0 on success, -1 if no client is connected or send fails.
 */
int mock_sm_send_health_check(mock_sm_t *sm);

/**
 * @brief Push a MSG_SHUTDOWN command to the currently connected client.
 *
 * @param sm  Mock SM state.
 * @return 0 on success, -1 on failure.
 */
int mock_sm_send_shutdown(mock_sm_t *sm);

/**
 * @brief Push a MSG_RELOAD_CONFIG command to the currently connected client.
 *
 * @param sm  Mock SM state.
 * @return 0 on success, -1 on failure.
 */
int mock_sm_send_reload(mock_sm_t *sm);

/**
 * @brief Reset all counters and registered entries for test isolation.
 *
 * Does NOT close the socket or stop the worker thread.
 *
 * @param sm  Mock SM state.
 */
void mock_sm_reset(mock_sm_t *sm);

/**
 * @brief Return the number of messages of the given type received so far.
 *
 * @param sm    Mock SM state.
 * @param type  sm_protocol.h message type (e.g. SM_MSG_REGISTER).
 * @return Count of received messages of that type.
 */
int mock_sm_count_msg(const mock_sm_t *sm, uint16_t type);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_SM_H */
