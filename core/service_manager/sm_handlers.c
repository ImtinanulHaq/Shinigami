#define _POSIX_C_SOURCE 200809L

/*
 * sm_handlers.c - Message dispatch and per-type request handlers.
 *
 * Fixes applied:
 *   - Payload length validated BEFORE recv() to prevent stack overflow.
 *   - recv() return compared as ssize_t (not truncated to int).
 *   - SO_RCVTIMEO / SO_SNDTIMEO set to prevent Slow Loris blocking.
 *   - Unregister uses sm_registry_remove_if_owner(): PID check and removal
 *     happen under the same write lock - no TOCTOU window.
 *   - Lookup uses sm_registry_find_copy(): entry is copied while holding the
 *     read lock - no dangling pointer after lock release.
 *   - hdr->client_pid used only for logging, never for access control.
 */

#include "sm_handlers.h"
#include "sm_logging.h"
#include "sm_protocol.h"
#include "sm_crypto.h"
#include "sm_rate_limit.h"
#include "sm_advanced_ratelimit.h"
#include "sm_security.h"
#include "sm_metrics.h"
#include "sm_audit.h"
#include "sm_structured_log.h"
#include "sm_request_id.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>

#pragma GCC diagnostic ignored "-Wstringop-truncation"

/* Timeout applied to every client socket - prevents Slow Loris attacks */
#define CLIENT_IO_TIMEOUT_SEC  5

/* ── HELPERS ────────────────────────────────────────────────────────────────── */

/*
 * Apply recv/send timeouts.  Must be called immediately after accept().
 *
 * The client socket must be blocking (no O_NONBLOCK) for SO_RCVTIMEO to
 * work as expected with MSG_WAITALL.  accept4() is called WITHOUT SOCK_NONBLOCK
 * so client fds are blocking; the listening server fd remains O_NONBLOCK for
 * the epoll loop.
 */
static int set_socket_timeout(int fd)
{
    struct timeval tv = { .tv_sec = CLIENT_IO_TIMEOUT_SEC, .tv_usec = 0 };

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        sm_log(SM_LOG_ERROR, "handlers: setsockopt timeout failed: %m");
        return -1;
    }
    return 0;
}

static int send_reply(int fd, int response_code)
{
    sm_reply_t reply = { .response_code = (int32_t)response_code };

    if (send(fd, &reply, sizeof(reply), MSG_NOSIGNAL) < 0) {
        sm_log(SM_LOG_ERROR, "handlers: send reply failed: %m");
        return -1;
    }
    return 0;
}

static int send_reply_lookup(int fd, const service_entry_t* entry)
{
    sm_lookup_reply_t reply = {0};

    strncpy(reply.socket_path, entry->socket_path, SM_MAX_PATH - 1);
    reply.socket_path[SM_MAX_PATH - 1] = '\0';
    strncpy(reply.ring_name, entry->ring_name, SM_MAX_PATH - 1);
    reply.ring_name[SM_MAX_PATH - 1] = '\0';
    reply.service_pid = (int32_t)entry->pid;

    if (send(fd, &reply, sizeof(reply), MSG_NOSIGNAL) < 0) {
        sm_log(SM_LOG_ERROR, "handlers: send lookup reply failed: %m");
        return -1;
    }
    return 0;
}

/* ── REGISTER ───────────────────────────────────────────────────────────────── */

int sm_handle_register(int fd, const sm_hdr_t* hdr, const sm_register_req_t* req)
{
    service_entry_t  entry     = {0};
    uid_t            peer_uid  = sm_get_peer_uid(fd);
    gid_t            peer_gid  = sm_get_peer_gid(fd);
    pid_t            peer_pid  = sm_get_peer_pid(fd);

    (void)hdr;

    /* Check rate limit for this operation */
    if (sm_ratelimit_check_extended(peer_pid, req->service_name, SM_MSG_REGISTER) < 0) {
        sm_log(SM_LOG_WARN, "handlers: register rate limit exceeded for '%s' from pid=%d",
               req->service_name, (int)peer_pid);
        sm_audit_log(AUDIT_REGISTER, req->service_name, 0, peer_pid, peer_uid, SM_ERR_RATELIMIT, "rate_limit_exceeded");
        return send_reply(fd, SM_ERR_RATELIMIT);
    }

    sm_log(SM_LOG_DEBUG, "handlers: register '%s' from pid=%d uid=%d",
           req->service_name, (int)peer_pid, (int)peer_uid);

    if (sm_validate_service_name(req->service_name) != SM_OK) {
        sm_log(SM_LOG_WARN, "handlers: register invalid name from pid=%d", (int)peer_pid);
        return send_reply(fd, SM_ERR_INVALID);
    }

    if (sm_validate_socket_path(req->socket_path) != SM_OK) {
        sm_log(SM_LOG_WARN, "handlers: register invalid socket_path from pid=%d", (int)peer_pid);
        return send_reply(fd, SM_ERR_INVALID);
    }

    if (sm_validate_socket_path(req->ring_name) != SM_OK) {
        sm_log(SM_LOG_WARN, "handlers: register invalid ring_name from pid=%d", (int)peer_pid);
        return send_reply(fd, SM_ERR_INVALID);
    }

    strncpy(entry.name,        req->service_name, SM_MAX_NAME - 1);
    strncpy(entry.socket_path, req->socket_path,  SM_MAX_PATH - 1);
    strncpy(entry.ring_name,   req->ring_name,     SM_MAX_PATH - 1);
    entry.name[SM_MAX_NAME - 1]        = '\0';
    entry.socket_path[SM_MAX_PATH - 1] = '\0';
    entry.ring_name[SM_MAX_PATH - 1]   = '\0';

    /* Use kernel-verified credentials - never trust client-supplied values */
    entry.pid    = peer_pid;
    entry.uid    = peer_uid;
    entry.gid    = peer_gid;
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);

    return send_reply(fd, sm_registry_add(&entry));
}

/* ── LOOKUP ─────────────────────────────────────────────────────────────────── */

int sm_handle_lookup(int fd, const sm_hdr_t* hdr, const sm_lookup_req_t* req)
{
    /*
     * Use sm_registry_find_copy() instead of sm_registry_find().
     *
     * sm_registry_find() releases the lock then returns a raw pointer.
     * Between that unlock and the dereference below, sm_registry_remove()
     * in another thread could shift the array, making the pointer dangle.
     *
     * sm_registry_find_copy() copies the entry while the read lock is held,
     * so the data we inspect is always consistent.
     */
    service_entry_t entry_copy = {0};
    int             rc;
    pid_t           peer_pid   = sm_get_peer_pid(fd);
    uid_t           peer_uid   = sm_get_peer_uid(fd);

    (void)hdr;

    /* Check rate limit for this operation */
    if (sm_ratelimit_check_extended(peer_pid, req->service_name, SM_MSG_LOOKUP) < 0) {
        sm_log(SM_LOG_WARN, "handlers: lookup rate limit exceeded for '%s' from pid=%d",
               req->service_name, (int)peer_pid);
        sm_audit_log(AUDIT_LOOKUP, req->service_name, 0, peer_pid, peer_uid, SM_ERR_RATELIMIT, "rate_limit_exceeded");
        return send_reply(fd, SM_ERR_RATELIMIT);
    }

    if (sm_validate_service_name(req->service_name) != SM_OK)
        return send_reply(fd, SM_ERR_INVALID);

    rc = sm_registry_find_copy(req->service_name, &entry_copy);
    if (rc != SM_OK) {
        sm_log(SM_LOG_WARN, "handlers: lookup '%s' not found", req->service_name);
        return send_reply(fd, SM_ERR_NOT_FOUND);
    }

    if (entry_copy.status != SERVICE_RUNNING) {
        sm_log(SM_LOG_WARN, "handlers: lookup '%s' status=%d (not running)",
               req->service_name, entry_copy.status);
        return send_reply(fd, SM_ERR_NOT_FOUND);
    }

    return send_reply_lookup(fd, &entry_copy);
}

/* ── HEARTBEAT ──────────────────────────────────────────────────────────────── */

int sm_handle_heartbeat(int fd, const sm_hdr_t* hdr, const sm_heartbeat_req_t* req)
{
    int   rc;
    pid_t peer_pid = sm_get_peer_pid(fd);
    uid_t peer_uid = sm_get_peer_uid(fd);

    (void)hdr;

    /* Check rate limit for this operation */
    if (sm_ratelimit_check_extended(peer_pid, req->service_name, SM_MSG_HEARTBEAT) < 0) {
        sm_log(SM_LOG_WARN, "handlers: heartbeat rate limit exceeded for '%s' from pid=%d",
               req->service_name, (int)peer_pid);
        sm_audit_log(AUDIT_HEARTBEAT, req->service_name, 0, peer_pid, peer_uid, SM_ERR_RATELIMIT, "rate_limit_exceeded");
        return send_reply(fd, SM_ERR_RATELIMIT);
    }

    if (sm_validate_service_name(req->service_name) != SM_OK)
        return send_reply(fd, SM_ERR_INVALID);

    rc = sm_registry_update_heartbeat(req->service_name);
    if (rc != SM_OK) {
        sm_log(SM_LOG_WARN, "handlers: heartbeat for unknown service '%s'",
               req->service_name);
    }

    return send_reply(fd, rc);
}

/* ── UNREGISTER ─────────────────────────────────────────────────────────────── */

int sm_handle_unregister(int fd, const sm_hdr_t* hdr, const sm_unregister_req_t* req)
{
    pid_t peer_pid = sm_get_peer_pid(fd);   /* kernel-verified via SO_PEERCRED */
    uid_t peer_uid = sm_get_peer_uid(fd);

    (void)hdr;

    /* Check rate limit for this operation */
    if (sm_ratelimit_check_extended(peer_pid, req->service_name, SM_MSG_UNREGISTER) < 0) {
        sm_log(SM_LOG_WARN, "handlers: unregister rate limit exceeded for '%s' from pid=%d",
               req->service_name, (int)peer_pid);
        sm_audit_log(AUDIT_UNREGISTER, req->service_name, 0, peer_pid, peer_uid, SM_ERR_RATELIMIT, "rate_limit_exceeded");
        return send_reply(fd, SM_ERR_RATELIMIT);
    }

    if (sm_validate_service_name(req->service_name) != SM_OK)
        return send_reply(fd, SM_ERR_INVALID);

    /*
     * sm_registry_remove_if_owner() performs the PID authorization check AND
     * the removal under the same write lock.
     *
     * The previous pattern was:
     *   entry = sm_registry_find(name);   // lock released after this
     *   if (entry->pid != peer_pid) ...   // TOCTOU: entry could be stale
     *   sm_registry_remove(name);         // second TOCTOU window
     *
     * Now both steps happen atomically: no window for another thread to
     * interleave and invalidate the authorization decision.
     *
     * peer_pid comes from SO_PEERCRED and cannot be spoofed by the client.
     * hdr->client_pid is NOT used here.
     */
    return send_reply(fd, sm_registry_remove_if_owner(req->service_name, peer_pid));
}

/* ── MAIN CLIENT HANDLER ────────────────────────────────────────────────────── */

int sm_handle_client(int client_fd)
{
    sm_hdr_t        hdr     = {0};
    uint8_t         payload[SM_MAX_PAYLOAD_SIZE];
    ssize_t         n;
    pid_t           peer_pid;
    const uint8_t*  key;
    int             rc      = 0;
    request_id_t    req_id;

    /* Generate and set request ID for this request (for distributed tracing) */
    req_id = sm_request_id_generate();
    sm_request_id_set(req_id);

    /* Apply I/O timeout immediately - before any blocking recv() call */
    if (set_socket_timeout(client_fd) < 0) {
        sm_request_id_clear();
        return -1;
    }

    peer_pid = sm_get_peer_pid(client_fd);

    /* Rate limit check using kernel-verified PID */
    if (sm_rate_limit_check(peer_pid) != SM_OK) {
        sm_log(SM_LOG_WARN, "handlers: rate limit hit for pid=%d [req=%s]",
               (int)peer_pid, sm_request_id_str(req_id));
        sm_request_id_clear();
        return -1;
    }

    /* Receive header */
    n = recv(client_fd, &hdr, sizeof(hdr), MSG_WAITALL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            sm_log(SM_LOG_WARN, "handlers: header recv timeout pid=%d", (int)peer_pid);
        else
            sm_log(SM_LOG_ERROR, "handlers: header recv failed: %m");
        return -1;
    }
    if ((size_t)n != sizeof(hdr)) {
        sm_log(SM_LOG_ERROR, "handlers: short header recv=%zd expected=%zu",
               n, sizeof(hdr));
        return -1;
    }

    /*
     * Validate length BEFORE issuing the payload recv().
     * If we recv() first and validate second, a large hdr.length can overflow
     * the fixed-size payload[] buffer on the stack.
     */
    if (hdr.length == 0 || hdr.length > SM_MAX_PAYLOAD_SIZE) {
        sm_log(SM_LOG_ERROR, "handlers: bad payload length %u from pid=%d",
               hdr.length, (int)peer_pid);
        send_reply(client_fd, SM_ERR_PROTOCOL);
        return -1;
    }

    /* Receive payload */
    n = recv(client_fd, payload, hdr.length, MSG_WAITALL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            sm_log(SM_LOG_WARN, "handlers: payload recv timeout pid=%d", (int)peer_pid);
        else
            sm_log(SM_LOG_ERROR, "handlers: payload recv failed: %m");
        return -1;
    }
    if ((size_t)n != (size_t)hdr.length) {
        sm_log(SM_LOG_ERROR, "handlers: short payload recv=%zd expected=%u",
               n, hdr.length);
        return -1;
    }

    /* Structural header validation (magic, version, type, size, timestamp) */
    if (sm_validate_header(&hdr, sizeof(hdr) + hdr.length) != SM_OK) {
        sm_log(SM_LOG_ERROR, "handlers: invalid header from pid=%d", (int)peer_pid);
        send_reply(client_fd, SM_ERR_PROTOCOL);
        return -1;
    }

    /* Exact payload size for this message type */
    if (sm_validate_message_size(hdr.length, hdr.type) != SM_OK) {
        sm_log(SM_LOG_ERROR, "handlers: size mismatch type=%u from pid=%d",
               hdr.type, (int)peer_pid);
        send_reply(client_fd, SM_ERR_INVALID);
        return -1;
    }

    /* HMAC integrity and authentication check */
    key = sm_crypto_get_key();
    if (key) {
        if (sm_validate_header_hmac(&hdr, payload, key, SM_HMAC_KEY_SIZE) != SM_OK) {
            sm_log(SM_LOG_WARN, "handlers: HMAC failed from pid=%d", (int)peer_pid);
            send_reply(client_fd, SM_ERR_AUTH);
            return -1;
        }
    }

    /* Dispatch to the appropriate handler */
    switch (hdr.type) {
        case SM_MSG_REGISTER:
            rc = sm_handle_register(client_fd, &hdr, (const sm_register_req_t*)payload);
            break;
        case SM_MSG_LOOKUP:
            rc = sm_handle_lookup(client_fd, &hdr, (const sm_lookup_req_t*)payload);
            break;
        case SM_MSG_HEARTBEAT:
            rc = sm_handle_heartbeat(client_fd, &hdr, (const sm_heartbeat_req_t*)payload);
            break;
        case SM_MSG_UNREGISTER:
            rc = sm_handle_unregister(client_fd, &hdr, (const sm_unregister_req_t*)payload);
            break;
        default:
            sm_log(SM_LOG_ERROR, "handlers: unknown type %u from pid=%d",
                   hdr.type, (int)peer_pid);
            send_reply(client_fd, SM_ERR_INVALID);
            rc = -1;
            break;
    }

    return rc;
}