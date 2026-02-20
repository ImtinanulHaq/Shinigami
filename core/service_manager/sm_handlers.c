#define _POSIX_C_SOURCE 200809L

#include "sm_handlers.h"
#include "sm_logging.h"
#include "sm_protocol.h"
#include "sm_rate_limit.h"
#include "sm_security.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <time.h>
#include <stdint.h>

#pragma GCC diagnostic ignored "-Wstringop-truncation"

// ── REPLY HELPERS ──────────────────────────────────────────────────────────────

static int send_reply(int fd, int response_code)
{
    sm_reply_t reply = {
        .response_code = response_code,
    };
    if (send(fd, &reply, sizeof(reply), MSG_NOSIGNAL) < 0) {
        sm_log(SM_LOG_ERROR, "send reply failed: %m");
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
    reply.service_pid = entry->pid;

    if (send(fd, &reply, sizeof(reply), MSG_NOSIGNAL) < 0) {
        sm_log(SM_LOG_ERROR, "send lookup reply failed: %m");
        return -1;
    }
    return 0;
}

// ── ASYNC REPLY HELPERS (for non-blocking I/O) ─────────────────────────────────

static int buf_reply(void* buf, int buf_size, int response_code)
{
    if (buf_size < (int)sizeof(sm_reply_t)) {
        return -1;
    }
    sm_reply_t* reply = (sm_reply_t*)buf;
    reply->response_code = response_code;
    return sizeof(sm_reply_t);
}

static int buf_reply_lookup(void* buf, int buf_size, const service_entry_t* entry)
{
    if (buf_size < (int)sizeof(sm_lookup_reply_t)) {
        return -1;
    }
    sm_lookup_reply_t* reply = (sm_lookup_reply_t*)buf;
    strncpy(reply->socket_path, entry->socket_path, SM_MAX_PATH - 1);
    reply->socket_path[SM_MAX_PATH - 1] = '\0';
    strncpy(reply->ring_name, entry->ring_name, SM_MAX_PATH - 1);
    reply->ring_name[SM_MAX_PATH - 1] = '\0';
    reply->service_pid = entry->pid;
    return sizeof(sm_lookup_reply_t);
}

// ── REQUEST VALIDATION ─────────────────────────────────────────────────────────

static int validate_request(const sm_hdr_t* hdr, const void* body __attribute__((unused)), size_t body_size)
{
    // Check header
    if (sm_validate_header(hdr, sizeof(*hdr) + body_size) != SM_OK) {
        return SM_ERR_PROTOCOL;
    }

    // Check payload size matches type
    if (sm_validate_message_size(hdr->length, hdr->type) != SM_OK) {
        return SM_ERR_INVALID;
    }

    return SM_OK;
}

// ── HANDLER: REGISTER ──────────────────────────────────────────────────────────

int sm_handle_register(int fd, const sm_hdr_t* hdr, const sm_register_req_t* req)
{
    sm_log(SM_LOG_DEBUG, "registering service '%s' from PID %d (uid=%d)",
           req->service_name, hdr->client_pid, sm_get_peer_uid(fd));

    // Validate input
    if (sm_validate_service_name(req->service_name) != SM_OK) {
        sm_log(SM_LOG_ERROR, "invalid service name: '%s'", req->service_name);
        return send_reply(fd, SM_ERR_INVALID);
    }

    if (sm_validate_socket_path(req->socket_path) != SM_OK) {
        sm_log(SM_LOG_ERROR, "invalid socket path: '%s'", req->socket_path);
        return send_reply(fd, SM_ERR_INVALID);
    }

    if (sm_validate_socket_path(req->ring_name) != SM_OK) {
        sm_log(SM_LOG_ERROR, "invalid ring_name: '%s'", req->ring_name);
        return send_reply(fd, SM_ERR_INVALID);
    }

    // Build entry
    service_entry_t entry = {0};
    strncpy(entry.name, req->service_name, SM_MAX_NAME - 1);
    entry.name[SM_MAX_NAME - 1] = '\0';
    strncpy(entry.socket_path, req->socket_path, SM_MAX_PATH - 1);
    entry.socket_path[SM_MAX_PATH - 1] = '\0';
    strncpy(entry.ring_name, req->ring_name, SM_MAX_PATH - 1);
    entry.ring_name[SM_MAX_PATH - 1] = '\0';
    entry.pid = req->pid;
    entry.uid = sm_get_peer_uid(fd);
    entry.gid = sm_get_peer_gid(fd);
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);

    // Add to registry
    int rc = sm_registry_add(&entry);
    return send_reply(fd, rc);
}

// ── HANDLER: LOOKUP ────────────────────────────────────────────────────────────

int sm_handle_lookup(int fd, const sm_hdr_t* hdr, const sm_lookup_req_t* req)
{
    sm_log(SM_LOG_DEBUG, "lookup '%s' from PID %d",
           req->service_name, hdr->client_pid);

    // Validate input
    if (sm_validate_service_name(req->service_name) != SM_OK) {
        return send_reply(fd, SM_ERR_INVALID);
    }

    // Find service
    service_entry_t* entry = sm_registry_find(req->service_name);
    if (!entry) {
        sm_log(SM_LOG_WARN, "service '%s' not found", req->service_name);
        return send_reply(fd, SM_ERR_NOT_FOUND);
    }

    // Check service health
    if (entry->status != SERVICE_RUNNING) {
        sm_log(SM_LOG_WARN, "service '%s' not running (status=%d)",
               req->service_name, entry->status);
        return send_reply(fd, SM_ERR_NOT_FOUND);
    }

    // Return service info
    return send_reply_lookup(fd, entry);
}

// ── HANDLER: HEARTBEAT ────────────────────────────────────────────────────────

int sm_handle_heartbeat(int fd, const sm_hdr_t* hdr, const sm_heartbeat_req_t* req)
{
    sm_log(SM_LOG_DEBUG, "heartbeat from '%s' (PID %d)",
           req->service_name, hdr->client_pid);

    // Validate input
    if (sm_validate_service_name(req->service_name) != SM_OK) {
        return send_reply(fd, SM_ERR_INVALID);
    }

    // Update heartbeat
    int rc = sm_registry_update_heartbeat(req->service_name);
    if (rc != SM_OK) {
        sm_log(SM_LOG_WARN, "heartbeat for unknown service '%s'",
               req->service_name);
    }

    return send_reply(fd, rc);
}

// ── HANDLER: UNREGISTER ───────────────────────────────────────────────────────

int sm_handle_unregister(int fd, const sm_hdr_t* hdr, const sm_unregister_req_t* req)
{
    sm_log(SM_LOG_DEBUG, "unregister '%s' from PID %d",
           req->service_name, hdr->client_pid);

    // Validate input
    if (sm_validate_service_name(req->service_name) != SM_OK) {
        return send_reply(fd, SM_ERR_INVALID);
    }

    // Verify this PID owns the service
    service_entry_t* entry = sm_registry_find(req->service_name);
    if (entry && entry->pid != (pid_t)hdr->client_pid) {
        sm_log(SM_LOG_ERROR, "unregister: PID %u trying to unregister service of PID %d",
               hdr->client_pid, entry->pid);
        return send_reply(fd, SM_ERR_PERMISSION);
    }

    // Remove from registry
    int rc = sm_registry_remove(req->service_name);
    return send_reply(fd, rc);
}

// ── MAIN CLIENT HANDLER ────────────────────────────────────────────────────────

int sm_handle_client(int client_fd)
{
    pid_t peer_pid = sm_get_peer_pid(client_fd);

    // Check rate limit
    if (sm_rate_limit_check(peer_pid) != SM_OK) {
        sm_log(SM_LOG_WARN, "rate limit exceeded for PID %d", peer_pid);
        return -1;
    }

    // Receive header
    sm_hdr_t hdr = {0};
    if (recv(client_fd, &hdr, sizeof(hdr), MSG_WAITALL) != sizeof(hdr)) {
        sm_log(SM_LOG_ERROR, "failed to receive header");
        return -1;
    }

    // Receive payload
    uint8_t payload[SM_MAX_MESSAGE_SIZE];
    if (recv(client_fd, payload, hdr.length, MSG_WAITALL) != (int)hdr.length) {
        sm_log(SM_LOG_ERROR, "failed to receive payload");
        return -1;
    }

    // Validate entire message
    if (validate_request(&hdr, payload, hdr.length) != SM_OK) {
        sm_log(SM_LOG_ERROR, "invalid message from PID %d type=%d",
               peer_pid, hdr.type);
        send_reply(client_fd, SM_ERR_PROTOCOL);
        return -1;
    }

    // Dispatch to handler
    int rc = 0;
    switch (hdr.type) {
        case SM_MSG_REGISTER: {
            sm_register_req_t* req = (sm_register_req_t*)payload;
            rc = sm_handle_register(client_fd, &hdr, req);
            break;
        }
        case SM_MSG_LOOKUP: {
            sm_lookup_req_t* req = (sm_lookup_req_t*)payload;
            rc = sm_handle_lookup(client_fd, &hdr, req);
            break;
        }
        case SM_MSG_HEARTBEAT: {
            sm_heartbeat_req_t* req = (sm_heartbeat_req_t*)payload;
            rc = sm_handle_heartbeat(client_fd, &hdr, req);
            break;
        }
        case SM_MSG_UNREGISTER: {
            sm_unregister_req_t* req = (sm_unregister_req_t*)payload;
            rc = sm_handle_unregister(client_fd, &hdr, req);
            break;
        }
        default:
            sm_log(SM_LOG_ERROR, "unknown message type: %d", hdr.type);
            send_reply(client_fd, SM_ERR_INVALID);
            rc = -1;
    }

    return rc;
}

// ── ASYNCHRONOUS CLIENT HANDLER ────────────────────────────────────────────────
// For non-blocking I/O: builds response in buffer instead of sending directly

int sm_handle_client_async(int client_fd, const sm_hdr_t* hdr, const void* payload, 
                           void* reply_buf, int reply_buf_size, int* out_size)
{
    if (!hdr || !reply_buf || !out_size) {
        return -1;
    }

    pid_t peer_pid = sm_get_peer_pid(client_fd);

    // Check rate limit
    if (sm_rate_limit_check(peer_pid) != SM_OK) {
        sm_log(SM_LOG_WARN, "rate limit exceeded for PID %d", peer_pid);
        *out_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_RATELIMIT);
        return *out_size;
    }

    // Validate entire message
    if (validate_request(hdr, payload, hdr->length) != SM_OK) {
        sm_log(SM_LOG_ERROR, "invalid message from PID %d type=%d",
               peer_pid, hdr->type);
        *out_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_PROTOCOL);
        return *out_size;
    }

    // Dispatch to handler (handlers fill reply buffer)
    int reply_size = 0;
    switch (hdr->type) {
        case SM_MSG_REGISTER: {
            sm_register_req_t* req = (sm_register_req_t*)payload;
            sm_log(SM_LOG_DEBUG, "registering service '%s' from PID %d (uid=%d)",
                   req->service_name, hdr->client_pid, sm_get_peer_uid(client_fd));

            // Validate input
            if (sm_validate_service_name(req->service_name) != SM_OK) {
                sm_log(SM_LOG_ERROR, "invalid service name: '%s'", req->service_name);
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
                break;
            }

            if (sm_validate_socket_path(req->socket_path) != SM_OK) {
                sm_log(SM_LOG_ERROR, "invalid socket path: '%s'", req->socket_path);
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
                break;
            }

            if (sm_validate_socket_path(req->ring_name) != SM_OK) {
                sm_log(SM_LOG_ERROR, "invalid ring_name: '%s'", req->ring_name);
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
                break;
            }

            // Build service entry
            service_entry_t entry = {0};
            strncpy(entry.name, req->service_name, SM_MAX_NAME - 1);
            entry.name[SM_MAX_NAME - 1] = '\0';
            strncpy(entry.socket_path, req->socket_path, SM_MAX_PATH - 1);
            entry.socket_path[SM_MAX_PATH - 1] = '\0';
            strncpy(entry.ring_name, req->ring_name, SM_MAX_PATH - 1);
            entry.ring_name[SM_MAX_PATH - 1] = '\0';
            entry.pid = req->pid;
            entry.uid = sm_get_peer_uid(client_fd);
            entry.gid = sm_get_peer_gid(client_fd);
            entry.status = SERVICE_RUNNING;
            entry.last_heartbeat = time(NULL);

            int rc = sm_registry_add(&entry);
            reply_size = buf_reply(reply_buf, reply_buf_size, rc);
            break;
        }
        case SM_MSG_LOOKUP: {
            sm_lookup_req_t* req = (sm_lookup_req_t*)payload;
            sm_log(SM_LOG_DEBUG, "lookup '%s' from PID %d",
                   req->service_name, hdr->client_pid);

            if (sm_validate_service_name(req->service_name) != SM_OK) {
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
                break;
            }

            service_entry_t* entry = sm_registry_find(req->service_name);
            if (!entry) {
                sm_log(SM_LOG_WARN, "service '%s' not found", req->service_name);
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_NOT_FOUND);
                break;
            }

            if (entry->status != SERVICE_RUNNING) {
                sm_log(SM_LOG_WARN, "service '%s' not running (status=%d)",
                       req->service_name, entry->status);
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_NOT_FOUND);
                break;
            }

            reply_size = buf_reply_lookup(reply_buf, reply_buf_size, entry);
            break;
        }
        case SM_MSG_HEARTBEAT: {
            sm_heartbeat_req_t* req = (sm_heartbeat_req_t*)payload;
            sm_log(SM_LOG_DEBUG, "heartbeat '%s' from PID %d",
                   req->service_name, hdr->client_pid);

            if (sm_validate_service_name(req->service_name) != SM_OK) {
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
                break;
            }

            int rc = sm_registry_update_heartbeat(req->service_name);
            if (rc != SM_OK) {
                sm_log(SM_LOG_WARN, "heartbeat for unknown service '%s'",
                       req->service_name);
            }
            reply_size = buf_reply(reply_buf, reply_buf_size, rc);
            break;
        }
        case SM_MSG_UNREGISTER: {
            sm_unregister_req_t* req = (sm_unregister_req_t*)payload;
            sm_log(SM_LOG_DEBUG, "unregister '%s' from PID %d",
                   req->service_name, hdr->client_pid);

            if (sm_validate_service_name(req->service_name) != SM_OK) {
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
                break;
            }

            // Verify this PID owns the service
            service_entry_t* entry = sm_registry_find(req->service_name);
            if (entry && entry->pid != (pid_t)hdr->client_pid) {
                sm_log(SM_LOG_ERROR, "unregister: PID %u trying to unregister service of PID %d",
                       hdr->client_pid, entry->pid);
                reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_PERMISSION);
                break;
            }

            int rc = sm_registry_remove(req->service_name);
            reply_size = buf_reply(reply_buf, reply_buf_size, rc);
            break;
        }
        default:
            sm_log(SM_LOG_ERROR, "unknown message type: %d", hdr->type);
            reply_size = buf_reply(reply_buf, reply_buf_size, SM_ERR_INVALID);
    }

    *out_size = reply_size;
    return reply_size >= 0 ? 0 : -1;
}
