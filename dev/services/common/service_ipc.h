/**
 * @file service_ipc.h
 * @brief Service Manager IPC: connect, register (with auth token), heartbeat,
 *        ping, and graceful disconnect.
 *
 * On registration the SM returns an authentication token stored in
 * svc_ipc_t.auth_token.  All outgoing messages are signed via
 * verify_sign_message(); all incoming messages are checked via
 * verify_check_message().
 *
 * All SM message types recognised by the service layer (including extended
 * types for health-check and config-reload) are defined here so _loop.c
 * files have a single header dependency.
 */

#ifndef SERVICE_IPC_H
#define SERVICE_IPC_H

#include "service_base.h"
#include <stdint.h>
#include <time.h>

/* ── SM socket defaults ───────────────────────────────────────────────── */

#define SM_SOCKET_PATH          "/tmp/servicemanager.sock"
#define SM_CONNECT_RETRIES      3
#define SM_CONNECT_RETRY_DELAY  2   /* seconds between retries */
#define SM_REPLY_TIMEOUT_SEC    5
#define SM_HEARTBEAT_INTERVAL   5   /* seconds */
#define SM_PING_INTERVAL        5   /* epoll timeout-based ping period (s) */

/* ── extended SM message types (service-layer use) ────────────────────── */

/* Standard wire types from sm_protocol.h */
#define SVC_MSG_REGISTER        1
#define SVC_MSG_HEARTBEAT       3
#define SVC_MSG_UNREGISTER      4
/* Extended types for service→SM and SM→service commands */
#define SVC_MSG_HEALTH_CHECK    5   /* SM asks for service health            */
#define SVC_MSG_HEALTH_OK       6   /* service responds with status payload  */
#define SVC_MSG_SHUTDOWN        7   /* SM commands service to stop           */
#define SVC_MSG_RELOAD_CONFIG   8   /* SM commands service to reload config  */
#define SVC_MSG_PING            9   /* service pings SM to check liveness    */
#define SVC_MSG_PONG            10  /* SM responds to ping                   */

/* ── auth token ───────────────────────────────────────────────────────── */

#define SVC_AUTH_TOKEN_SIZE  64

typedef struct {
    uint8_t  data[SVC_AUTH_TOKEN_SIZE];
    size_t   len;
    uint32_t permissions;
    uint64_t expires_at;   /* UNIX seconds; 0 = never                       */
} svc_auth_token_t;

/* ── health status payload (for MSG_HEALTH_OK) ────────────────────────── */

typedef struct {
    uint32_t uptime_sec;
    uint32_t error_count;
    uint8_t  hal_state;    /* hw_device state byte                           */
    uint8_t  svc_state;    /* svc_state_t                                    */
    uint16_t _pad;
} svc_health_status_t;

/* ── IPC context ──────────────────────────────────────────────────────── */

typedef struct {
    int               fd;                         /**< Unix socket fd, -1 closed */
    char              service_name[SERVICE_MAX_NAME];
    char              socket_path[SERVICE_MAX_PATH]; /**< SM socket path       */
    char              verify_key_path[SERVICE_MAX_PATH];
    svc_auth_token_t  auth_token;                 /**< Returned at registration */
    uint32_t          last_nonce;
    time_t            last_heartbeat;
    int               reconnect_backoff_sec;      /**< Current backoff value    */
    int               reconnect_backoff_max;      /**< Config ceiling (s)       */
    /* verify_context_t is embedded opaquely — allocated in service_ipc_init */
    void             *verify_ctx;                 /**< verify_context_t *       */
} svc_ipc_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Initialise IPC context. Does not connect.
 *
 * @param ipc             Context to initialise.
 * @param service_name    This service's name (must match SM_MAX_NAME limit).
 * @param verify_key_path Path to HMAC key file, or NULL to skip auth.
 * @return SVC_OK on success.
 */
int service_ipc_init(svc_ipc_t *ipc, const char *service_name,
                     const char *verify_key_path);

/**
 * @brief Connect to the SM Unix socket.
 *
 * Single attempt.  The caller (main) wraps this in a retry loop of
 * SM_CONNECT_RETRIES attempts spaced SM_CONNECT_RETRY_DELAY seconds apart.
 *
 * @return SVC_OK or SVC_ERR_IPC.
 */
int service_ipc_connect(svc_ipc_t *ipc);

/**
 * @brief Register with the SM.
 *
 * Sends service name, exe path, version string, and PID; signs the message
 * with verify_sign_message().  On success the SM reply carries the auth
 * token which is stored in ipc->auth_token.
 *
 * @param ipc      Connected context.
 * @param exe_path Absolute path to this service's binary.
 * @param version  Version string (e.g. "2.0.0").
 * @param pid      This process's PID.
 * @return SVC_OK or SVC_ERR_IPC.
 */
int service_ipc_register(svc_ipc_t *ipc, const char *exe_path,
                         const char *version, pid_t pid);

/**
 * @brief Send a heartbeat to the SM (SVC_MSG_HEARTBEAT).
 */
int service_ipc_heartbeat(svc_ipc_t *ipc);

/**
 * @brief Ping the SM and wait for SVC_MSG_PONG.
 *
 * Used by the epoll timeout handler.  If the SM does not reply within
 * SM_REPLY_TIMEOUT_SEC the SM is considered dead and the caller should
 * try to reconnect.
 *
 * @return SVC_OK if alive, SVC_ERR_IPC or SVC_ERR_TIMEOUT if unreachable.
 */
int service_ipc_ping(svc_ipc_t *ipc);

/**
 * @brief Unregister from the SM (SVC_MSG_UNREGISTER).
 */
int service_ipc_unregister(svc_ipc_t *ipc);

/**
 * @brief Close the socket and free the verify context.
 */
void service_ipc_disconnect(svc_ipc_t *ipc);

/**
 * @brief Send a MSG_HEALTH_OK reply to the SM with current service status.
 */
int service_ipc_send_health(svc_ipc_t *ipc, const svc_health_status_t *st);

/**
 * @brief Return 1 if the socket fd is valid and connected.
 */
int service_ipc_is_connected(const svc_ipc_t *ipc);

#endif /* SERVICE_IPC_H */
