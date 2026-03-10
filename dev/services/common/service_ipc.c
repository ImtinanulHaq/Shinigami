/**
 * @file service_ipc.c
 * @brief Service Manager IPC implementation.
 *
 * SM uses a per-request connection model: it accepts one message, sends
 * a reply, then closes the client FD.  Each call to service_ipc_register()
 * or service_ipc_heartbeat() therefore opens a fresh connection, sends the
 * message, reads the reply from SM, and closes the socket before returning.
 *
 * ipc->fd is set to the SM socket FD only during an active request; it is
 * always -1 between requests.  The event loop must NOT add ipc->fd to an
 * io_uring/epoll watch for persistent SM data — SM never sends unsolicited
 * data; all communication is client-initiated.
 */

#define _GNU_SOURCE
#include "service_ipc.h"
#include "service_base.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

/* Pull in SM wire protocol */
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
/* Pull in Verify module */
#include "../../dev/security/verify/verify.h"

/* ── registration payload — must match sm_register_req_t (580 bytes) ─── */

typedef struct {
    char     service_name[SM_MAX_NAME];   /* 64 bytes */
    char     socket_path[SM_MAX_PATH];    /* 256 bytes */
    char     ring_name[SM_MAX_PATH];      /* 256 bytes — shared-memory ring name */
    int32_t  pid;                         /* 4 bytes   = 580 total */
} svc_register_payload_t;

/* ── helpers ──────────────────────────────────────────────────────────── */

static uint32_t _nonce(void)
{
    return verify_generate_nonce();
}

static uint64_t _now_sec(void)
{
    return (uint64_t)time(NULL);
}

/**
 * Fill a sm_hdr_t (without HMAC — caller signs afterwards).
 */
static void fill_header(sm_hdr_t *hdr, uint16_t type, uint32_t payload_len,
                        pid_t pid, uint32_t *nonce_out)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic      = SM_PROTOCOL_MAGIC;
    hdr->version    = SM_PROTOCOL_VERSION;
    hdr->type       = type;
    hdr->length     = payload_len;
    hdr->timestamp  = (uint32_t)_now_sec();
    hdr->client_pid = (uint32_t)pid;
    hdr->nonce      = _nonce();
    if (nonce_out) *nonce_out = hdr->nonce;
}

/**
 * Sign the header+payload using HMAC-SHA256.
 * The HMAC covers exactly bytes 0..SM_HDR_HMAC_OFFSET-1 of the header
 * followed by the full payload — matching sm_validate_header_hmac() in SM.
 * We call verify_hmac_sha256() directly so the input is identical to what
 * SM verifies (no timestamp/nonce/sequence prepend from verify_sign_message).
 */
static int sign_message(svc_ipc_t *ipc, sm_hdr_t *hdr,
                        const void *payload, uint32_t payload_len)
{
    if (!ipc->verify_ctx) return 0; /* no key loaded — skip signing */

    verify_context_t *vctx = (verify_context_t *)ipc->verify_ctx;

    /* Build the blob to sign: header bytes before HMAC + payload */
    size_t blob_len = SM_HDR_HMAC_OFFSET + payload_len;
    uint8_t *blob = malloc(blob_len);
    if (!blob) return -1;
    memcpy(blob,                       hdr,     SM_HDR_HMAC_OFFSET);
    if (payload && payload_len > 0)
        memcpy(blob + SM_HDR_HMAC_OFFSET, payload, payload_len);

    uint8_t hmac_out[32];
    int rc = verify_hmac_sha256(vctx->master_key, vctx->master_key_len,
                                blob, blob_len, hmac_out);
    free(blob);
    if (rc != 0) return -1;

    memcpy(hdr->hmac, hmac_out, 32);
    return 0;
}

/**
 * Send header + optional payload atomically.
 */
static int send_frame(int fd, sm_hdr_t *hdr,
                      const void *payload, uint32_t payload_len)
{
    struct iovec iov[2];
    int  iovcnt = 1;
    iov[0].iov_base = hdr;
    iov[0].iov_len  = sizeof(sm_hdr_t);
    if (payload && payload_len > 0) {
        iov[1].iov_base = (void *)payload;
        iov[1].iov_len  = payload_len;
        iovcnt = 2;
    }
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov    = iov;
    msg.msg_iovlen = (size_t)iovcnt;

    ssize_t sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
    return (sent > 0) ? 0 : -1;
}

/**
 * Receive exactly @len bytes, blocking for up to SM_REPLY_TIMEOUT_SEC.
 */
static int recv_exact(int fd, void *buf, size_t len)
{
    /* Set socket receive timeout */
    struct timeval tv = { .tv_sec = SM_REPLY_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    size_t got = 0;
    uint8_t *p = (uint8_t *)buf;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/**
 * Read a raw SM reply.
 * SM sends ONLY sm_reply_t (4 bytes = int32_t response_code) without any
 * sm_hdr_t wrapper.  No HMAC is applied to replies.
 */
static int recv_reply_fd(int fd, int32_t *code_out)
{
    sm_reply_t reply;
    if (recv_exact(fd, &reply, sizeof(reply)) < 0) return SVC_ERR_IPC;
    if (code_out) *code_out = reply.response_code;
    return SVC_OK;
}

/**
 * Open a fresh connection to SM and return the fd (or -1 on error).
 * SM uses per-request connections: each request gets its own FD.
 */
static int open_sm_connection(svc_ipc_t *ipc)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        LOG_ERR("open_sm_connection: socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("open_sm_connection: connect(%s): %s",
                ipc->socket_path, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

/* ── public API ───────────────────────────────────────────────────────── */

int service_ipc_init(svc_ipc_t *ipc, const char *service_name,
                     const char *verify_key_path)
{
    if (!ipc || !service_name) return SVC_ERR_INVALID;

    memset(ipc, 0, sizeof(*ipc));
    ipc->fd = -1;
    strncpy(ipc->service_name, service_name, SERVICE_MAX_NAME - 1);
    strncpy(ipc->socket_path,  SM_SOCKET_PATH, SERVICE_MAX_PATH - 1);
    ipc->reconnect_backoff_sec = 1;
    ipc->reconnect_backoff_max = 30;

    if (verify_key_path && verify_key_path[0] != '\0') {
        verify_context_t *vctx = calloc(1, sizeof(verify_context_t));
        if (!vctx) return SVC_ERR_NOMEM;
        if (verify_init_from_file(vctx, verify_key_path) != 0) {
            LOG_WARN("service_ipc_init: cannot load verify key from %s "
                     "— messages will be unsigned", verify_key_path);
            free(vctx);
        } else {
            ipc->verify_ctx = vctx;
            strncpy(ipc->verify_key_path, verify_key_path,
                    SERVICE_MAX_PATH - 1);
        }
    }

    return SVC_OK;
}

int service_ipc_connect(svc_ipc_t *ipc)
{
    if (!ipc) return SVC_ERR_INVALID;
    /* In the per-request model, ipc->fd is only valid during a request.
     * service_ipc_connect() is kept for API compat but is a no-op here;
     * each API call opens its own connection. */
    return SVC_OK;
}

int service_ipc_register(svc_ipc_t *ipc, const char *exe_path,
                         const char *version, pid_t pid)
{
    (void)exe_path; (void)version; /* unused in per-request model */

    if (!ipc) return SVC_ERR_IPC;

    int fd = open_sm_connection(ipc);
    if (fd < 0) return SVC_ERR_IPC;

    svc_register_payload_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);
    snprintf(req.socket_path, SM_MAX_PATH, "/run/%s.sock", ipc->service_name);
    snprintf(req.ring_name, SM_MAX_PATH, "/dev/shm/%s_ring", ipc->service_name);
    req.pid = (int32_t)pid;

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_REGISTER, (uint32_t)sizeof(req), pid,
                &ipc->last_nonce);
    /* Temporarily set ipc->fd for sign_message */
    ipc->fd = fd;
    if (sign_message(ipc, &hdr, &req, sizeof(req)) != 0) {
        LOG_ERR("register: sign_message failed");
        close(fd); ipc->fd = -1;
        return SVC_ERR_IPC;
    }

    if (send_frame(fd, &hdr, &req, sizeof(req)) != 0) {
        LOG_ERR("register: send_frame failed: %s", strerror(errno));
        close(fd); ipc->fd = -1;
        return SVC_ERR_IPC;
    }

    int32_t code = 0;
    int rc = recv_reply_fd(fd, &code);

    /* Close after receiving the reply — SM closes its end too */
    close(fd);
    ipc->fd = -1;

    if (rc != SVC_OK) return rc;

    if (code != SM_OK) {
        LOG_ERR("register: SM rejected with code %d", code);
        return SVC_ERR_IPC;
    }

    LOG_INFO("registered with SM (ring=/dev/shm/%s_ring, pid=%d)",
             ipc->service_name, (int)pid);
    ipc->registered = 1;
    return SVC_OK;
}

int service_ipc_heartbeat(svc_ipc_t *ipc)
{
    if (!ipc) return SVC_ERR_IPC;

    int fd = open_sm_connection(ipc);
    if (fd < 0) return SVC_ERR_IPC;

    sm_heartbeat_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_HEARTBEAT, (uint32_t)sizeof(req),
                getpid(), &ipc->last_nonce);
    ipc->fd = fd;   /* for sign_message */
    sign_message(ipc, &hdr, &req, sizeof(req));

    int rc = SVC_OK;
    if (send_frame(fd, &hdr, &req, sizeof(req)) != 0) {
        rc = SVC_ERR_IPC;
    } else {
        int32_t code = 0;
        rc = recv_reply_fd(fd, &code);
        if (rc == SVC_OK && code != SM_OK) {
            LOG_WARN("heartbeat: SM returned %d", code);
            rc = SVC_ERR_IPC;
        }
    }

    close(fd);
    ipc->fd = -1;

    if (rc == SVC_OK)
        ipc->last_heartbeat = time(NULL);
    return rc;
}

int service_ipc_ping(svc_ipc_t *ipc)
{
    /* SM doesn't know SM_MSG_PING (type=9).  Use heartbeat instead. */
    return service_ipc_heartbeat(ipc);
}

int service_ipc_unregister(svc_ipc_t *ipc)
{
    if (!ipc) return SVC_OK;

    int fd = open_sm_connection(ipc);
    if (fd < 0) return SVC_OK; /* best-effort */

    sm_unregister_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_UNREGISTER, (uint32_t)sizeof(req),
                getpid(), &ipc->last_nonce);
    ipc->fd = fd;
    sign_message(ipc, &hdr, &req, sizeof(req));
    send_frame(fd, &hdr, &req, sizeof(req));
    /* Best-effort: don't check reply — we're shutting down */
    int32_t code = 0;
    recv_reply_fd(fd, &code);
    close(fd);
    ipc->fd   = -1;
    ipc->registered = 0;
    return SVC_OK;
}

void service_ipc_disconnect(svc_ipc_t *ipc)
{
    if (!ipc) return;
    if (ipc->fd >= 0) {
        close(ipc->fd);
        ipc->fd = -1;
    }
    if (ipc->verify_ctx) {
        verify_cleanup((verify_context_t *)ipc->verify_ctx);
        free(ipc->verify_ctx);
        ipc->verify_ctx = NULL;
    }
}

int service_ipc_send_health(svc_ipc_t *ipc, const svc_health_status_t *st)
{
    /* SM is per-request; it does not send SVC_MSG_HEALTH_CHECK probes.
     * This function is kept for API compatibility but does nothing. */
    (void)ipc; (void)st;
    return SVC_OK;
}

int service_ipc_is_connected(const svc_ipc_t *ipc)
{
    /* In per-request model, "connected" means "registered" */
    return ipc && ipc->registered;
}

