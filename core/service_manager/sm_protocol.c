#include "sm_protocol.h"
#include "sm_logging.h"
#include <string.h>
#include <ctype.h>
#include <time.h>

// ── PROTOCOL VALIDATION ────────────────────────────────────────────────────────

int sm_validate_header(const sm_hdr_t* hdr, size_t received_size)
{
    if (!hdr) {
        sm_log(SM_LOG_ERROR, "validate_header: null header");
        return SM_ERR_INVALID;
    }

    // Check magic number
    if (hdr->magic != SM_PROTOCOL_MAGIC) {
        sm_log(SM_LOG_ERROR, "bad magic: 0x%08x (expected 0x%08x)",
               hdr->magic, SM_PROTOCOL_MAGIC);
        return SM_ERR_PROTOCOL;
    }

    // Check version
    if (hdr->version != SM_PROTOCOL_VERSION) {
        sm_log(SM_LOG_ERROR, "version mismatch: %d (expected %d)",
               hdr->version, SM_PROTOCOL_VERSION);
        return SM_ERR_PROTOCOL;
    }

    // Check message type
    if (hdr->type < SM_MSG_REGISTER || hdr->type > SM_MSG_UNREGISTER) {
        sm_log(SM_LOG_ERROR, "invalid message type: %u", hdr->type);
        return SM_ERR_INVALID;
    }

    // Check length bounds
    if (hdr->length == 0 || hdr->length > (SM_MAX_MESSAGE_SIZE - sizeof(sm_hdr_t))) {
        sm_log(SM_LOG_ERROR, "invalid payload length: %u", hdr->length);
        return SM_ERR_INVALID;
    }

    // Check received size matches header
    if (received_size != (sizeof(sm_hdr_t) + hdr->length)) {
        sm_log(SM_LOG_ERROR, "size mismatch: received %zu, expected %zu",
               received_size, sizeof(sm_hdr_t) + hdr->length);
        return SM_ERR_PROTOCOL;
    }

    // Check timestamp is reasonable (not in future)
    time_t now = time(NULL);
    if (hdr->timestamp > (uint32_t)now + 60) {
        sm_log(SM_LOG_WARN, "timestamp in future: %u vs %ld",
               hdr->timestamp, now);
        return SM_ERR_INVALID;
    }

    return SM_OK;
}

// ── NAME VALIDATION ───────────────────────────────────────────────────────────

int sm_validate_service_name(const char* name)
{
    if (!name || *name == 0) {
        return SM_ERR_INVALID;
    }

    size_t len = strnlen(name, SM_MAX_NAME);
    if (len >= SM_MAX_NAME) {
        sm_log(SM_LOG_ERROR, "service name too long: %zu", len);
        return SM_ERR_INVALID;
    }

    // Alphanumeric + underscore + hyphen only
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum(c) && c != '_' && c != '-') {
            sm_log(SM_LOG_ERROR, "invalid char in service name: '%c'", c);
            return SM_ERR_INVALID;
        }
    }

    return SM_OK;
}

// ── PATH VALIDATION ────────────────────────────────────────────────────────────

int sm_validate_socket_path(const char* path)
{
    if (!path || *path == 0) {
        return SM_ERR_INVALID;
    }

    size_t len = strnlen(path, SM_MAX_PATH);
    if (len >= SM_MAX_PATH) {
        sm_log(SM_LOG_ERROR, "socket path too long: %zu", len);
        return SM_ERR_INVALID;
    }

    // Must be absolute path or /tmp or /run
    if (path[0] != '/') {
        sm_log(SM_LOG_ERROR, "path not absolute: %s", path);
        return SM_ERR_INVALID;
    }

    // Check for suspicious patterns
    if (strstr(path, "..") || strstr(path, "//")) {
        sm_log(SM_LOG_ERROR, "suspicious path pattern: %s", path);
        return SM_ERR_INVALID;
    }

    return SM_OK;
}

// ── MESSAGE SIZE VALIDATION ────────────────────────────────────────────────────

int sm_validate_message_size(uint32_t payload_len, uint16_t msg_type)
{
    size_t expected = 0;

    switch (msg_type) {
        case SM_MSG_REGISTER:
            expected = sizeof(sm_register_req_t);
            break;
        case SM_MSG_LOOKUP:
            expected = sizeof(sm_lookup_req_t);
            break;
        case SM_MSG_HEARTBEAT:
            expected = sizeof(sm_heartbeat_req_t);
            break;
        case SM_MSG_UNREGISTER:
            expected = sizeof(sm_unregister_req_t);
            break;
        default:
            return SM_ERR_INVALID;
    }

    if (payload_len != expected) {
        sm_log(SM_LOG_ERROR, "payload size mismatch for type %u: %u != %zu",
               msg_type, payload_len, expected);
        return SM_ERR_INVALID;
    }

    return SM_OK;
}
