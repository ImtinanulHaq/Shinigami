/**
 * @file service_ipc.c
 * @brief Service Manager IPC implementation.
 *
 * Wraps the sm_protocol.h wire format with verify_sign_message() /
 * verify_check_message() from the Verify module so every outgoing frame
 * is signed and every incoming frame is authenticated before processing.
 *
 * Registration payload extends sm_register_req_t with exe-path and version
 * so the SM has full provenance information.
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

/* ── extended registration payload ───────────────────────────────────── */

typedef struct {
    char     service_name[SM_MAX_NAME];
    char     socket_path[SM_MAX_PATH];
    char     exe_path[SM_MAX_PATH];
    char     version[SERVICE_MAX_VERSION];
    int32_t  pid;
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
 * Sign the header+payload using the Verify module.
 * The HMAC covers bytes 0..SM_HDR_HMAC_OFFSET-1 of the header + full payload.
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
    memcpy(blob,                    hdr,     SM_HDR_HMAC_OFFSET);
    if (payload && payload_len > 0)
        memcpy(blob + SM_HDR_HMAC_OFFSET, payload, payload_len);

    message_auth_t auth;
    int rc = verify_sign_message(vctx, blob, blob_len, &auth);
    free(blob);
    if (rc != 0) return -1;

    memcpy(hdr->hmac, auth.hmac, 32);
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
 * Read a full SM reply (header + sm_reply_t payload).
 * Optionally verify HMAC and check response_code.
 */
static int recv_reply(svc_ipc_t *ipc, int32_t *code_out)
{
    sm_hdr_t hdr;
    if (recv_exact(ipc->fd, &hdr, sizeof(hdr)) < 0) return SVC_ERR_IPC;

    if (hdr.magic != SM_PROTOCOL_MAGIC) {
        LOG_ERR("SM reply: bad magic 0x%08X", hdr.magic);
        return SVC_ERR_IPC;
    }

    /* Read payload if present */
    uint8_t payload[SM_MAX_PAYLOAD_SIZE] = {0};
    if (hdr.length > 0 && hdr.length <= SM_MAX_PAYLOAD_SIZE) {
        if (recv_exact(ipc->fd, payload, hdr.length) < 0)
            return SVC_ERR_IPC;
    }

    /* Verify signature if we have a key */
    if (ipc->verify_ctx && hdr.length > 0) {
        verify_context_t *vctx = (verify_context_t *)ipc->verify_ctx;
        size_t blob_len = SM_HDR_HMAC_OFFSET + hdr.length;
        uint8_t *blob = malloc(blob_len);
        if (blob) {
            memcpy(blob, &hdr, SM_HDR_HMAC_OFFSET);
            memcpy(blob + SM_HDR_HMAC_OFFSET, payload, hdr.length);
            message_auth_t auth;
            memcpy(auth.hmac, hdr.hmac, 32);
            auth.timestamp = hdr.timestamp;
            auth.nonce     = hdr.nonce;
            if (verify_check_message(vctx, blob, blob_len, &auth) != 0) {
                LOG_WARN("SM reply: HMAC verification failed");
                free(blob);
                return SVC_ERR_IPC;
            }
            free(blob);
        }
    }

    if (code_out && hdr.length >= sizeof(sm_reply_t)) {
        sm_reply_t *rep = (sm_reply_t *)payload;
        *code_out = rep->response_code;
    }
    return SVC_OK;
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
    if (ipc->fd >= 0) return SVC_OK; /* already connected */

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        LOG_ERR("socket: %s", strerror(errno));
        return SVC_ERR_IPC;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("connect(%s): %s", ipc->socket_path, strerror(errno));
        close(s);
        return SVC_ERR_IPC;
    }

    ipc->fd = s;
    ipc->reconnect_backoff_sec = 1; /* reset on successful connect */
    return SVC_OK;
}

int service_ipc_register(svc_ipc_t *ipc, const char *exe_path,
                         const char *version, pid_t pid)
{
    if (!ipc || ipc->fd < 0) return SVC_ERR_IPC;

    svc_register_payload_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);
    snprintf(req.socket_path, SM_MAX_PATH, "/run/%s.sock", ipc->service_name);
    if (exe_path)  strncpy(req.exe_path, exe_path, SM_MAX_PATH - 1);
    if (version)   strncpy(req.version, version, SERVICE_MAX_VERSION - 1);
    req.pid = (int32_t)pid;

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_REGISTER, (uint32_t)sizeof(req), pid,
                &ipc->last_nonce);
    if (sign_message(ipc, &hdr, &req, sizeof(req)) != 0) {
        LOG_ERR("register: sign_message failed");
        return SVC_ERR_IPC;
    }

    if (send_frame(ipc->fd, &hdr, &req, sizeof(req)) != 0) {
        LOG_ERR("register: send_frame failed: %s", strerror(errno));
        return SVC_ERR_IPC;
    }

    int32_t code = 0;
    int rc = recv_reply(ipc, &code);
    if (rc != SVC_OK) return rc;

    if (code != SM_OK) {
        LOG_ERR("register: SM rejected with code %d", code);
        return SVC_ERR_IPC;
    }

    LOG_INFO("registered with SM (exe=%s, ver=%s, pid=%d)",
             exe_path ? exe_path : "(unknown)",
             version  ? version  : "(unknown)",
             (int)pid);
    return SVC_OK;
}

int service_ipc_heartbeat(svc_ipc_t *ipc)
{
    if (!ipc || ipc->fd < 0) return SVC_ERR_IPC;

    sm_heartbeat_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_HEARTBEAT, (uint32_t)sizeof(req),
                getpid(), &ipc->last_nonce);
    sign_message(ipc, &hdr, &req, sizeof(req));

    if (send_frame(ipc->fd, &hdr, &req, sizeof(req)) != 0)
        return SVC_ERR_IPC;

    int32_t code = 0;
    int rc = recv_reply(ipc, &code);
    if (rc == SVC_OK && code != SM_OK) return SVC_ERR_IPC;

    ipc->last_heartbeat = time(NULL);
    return rc;
}

int service_ipc_ping(svc_ipc_t *ipc)
{
    if (!ipc || ipc->fd < 0) return SVC_ERR_IPC;

    /* A ping is a lightweight heartbeat; re-use the heartbeat path */
    sm_heartbeat_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_PING, (uint32_t)sizeof(req),
                getpid(), &ipc->last_nonce);
    sign_message(ipc, &hdr, &req, sizeof(req));

    if (send_frame(ipc->fd, &hdr, &req, sizeof(req)) != 0)
        return SVC_ERR_TIMEOUT;

    int32_t code = 0;
    int rc = recv_reply(ipc, &code);
    return (rc == SVC_OK && code == SM_OK) ? SVC_OK : SVC_ERR_TIMEOUT;
}

int service_ipc_unregister(svc_ipc_t *ipc)
{
    if (!ipc || ipc->fd < 0) return SVC_OK;

    sm_unregister_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.service_name, ipc->service_name, SM_MAX_NAME - 1);

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_UNREGISTER, (uint32_t)sizeof(req),
                getpid(), &ipc->last_nonce);
    sign_message(ipc, &hdr, &req, sizeof(req));

    send_frame(ipc->fd, &hdr, &req, sizeof(req));
    /* Best-effort: don't check reply — we're shutting down */
    recv_reply(ipc, NULL);
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
    if (!ipc || ipc->fd < 0 || !st) return SVC_ERR_INVALID;

    sm_hdr_t hdr;
    fill_header(&hdr, SVC_MSG_HEALTH_OK, (uint32_t)sizeof(*st),
                getpid(), &ipc->last_nonce);
    sign_message(ipc, &hdr, st, sizeof(*st));
    return send_frame(ipc->fd, &hdr, st, sizeof(*st)) == 0
           ? SVC_OK : SVC_ERR_IPC;
}

int service_ipc_is_connected(const svc_ipc_t *ipc)
{
    return ipc && ipc->fd >= 0;
}
