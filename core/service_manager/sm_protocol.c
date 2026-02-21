#define _POSIX_C_SOURCE 200809L

/*
 * sm_protocol.c - Protocol validation
 *
 * Fixes applied:
 *   - Timestamp checked in BOTH directions (reject future AND stale messages)
 *   - Socket path validated against an allowed-directory whitelist
 *   - HMAC verification added
 */

#include "sm_protocol.h"
#include "sm_crypto.h"
#include "sm_logging.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>

/* ── HEADER VALIDATION ──────────────────────────────────────────────────────── */

int sm_validate_header(const sm_hdr_t* hdr, size_t received_size)
{
    time_t   now;
    uint32_t now32;

    if (!hdr) {
        sm_log(SM_LOG_ERROR, "validate_header: null pointer");
        return SM_ERR_INVALID;
    }

    /* Magic number identifies this protocol */
    if (hdr->magic != SM_PROTOCOL_MAGIC) {
        sm_log(SM_LOG_ERROR, "validate_header: bad magic 0x%08x", hdr->magic);
        return SM_ERR_PROTOCOL;
    }

    /* Version must match exactly */
    if (hdr->version != SM_PROTOCOL_VERSION) {
        sm_log(SM_LOG_ERROR, "validate_header: version %u (expected %u)",
               hdr->version, SM_PROTOCOL_VERSION);
        return SM_ERR_PROTOCOL;
    }

    /* Message type must be in known range */
    if (hdr->type < SM_MSG_REGISTER || hdr->type > SM_MSG_UNREGISTER) {
        sm_log(SM_LOG_ERROR, "validate_header: unknown type %u", hdr->type);
        return SM_ERR_INVALID;
    }

    /* Payload length must be non-zero and within hard limit */
    if (hdr->length == 0 || hdr->length > SM_MAX_PAYLOAD_SIZE) {
        sm_log(SM_LOG_ERROR, "validate_header: bad length %u", hdr->length);
        return SM_ERR_INVALID;
    }

    /* received_size must equal header + declared payload */
    if (received_size != sizeof(sm_hdr_t) + (size_t)hdr->length) {
        sm_log(SM_LOG_ERROR, "validate_header: size mismatch recv=%zu expected=%zu",
               received_size, sizeof(sm_hdr_t) + (size_t)hdr->length);
        return SM_ERR_PROTOCOL;
    }

    /* Timestamp window check - both directions */
    now   = time(NULL);
    now32 = (uint32_t)now;

    if (hdr->timestamp > now32 + SM_TIMESTAMP_MAX_SKEW) {
        sm_log(SM_LOG_WARN, "validate_header: timestamp in future ts=%u now=%u",
               hdr->timestamp, now32);
        return SM_ERR_INVALID;
    }

    /* Reject stale messages to prevent replay attacks */
    if ((time_t)hdr->timestamp < now - SM_TIMESTAMP_MAX_AGE) {
        sm_log(SM_LOG_WARN, "validate_header: stale message ts=%u now=%u age=%ld",
               hdr->timestamp, now32, (long)(now - (time_t)hdr->timestamp));
        return SM_ERR_INVALID;
    }

    return SM_OK;
}

/*
 * Verify HMAC over the header (bytes before the hmac field) concatenated with
 * the payload.  The hmac field itself is NOT included in the MAC input.
 *
 * MAC input layout:
 *   header bytes [0 .. SM_HDR_HMAC_OFFSET-1]  (24 bytes)
 *   payload bytes [0 .. hdr->length-1]
 */
int sm_validate_header_hmac(const sm_hdr_t* hdr, const uint8_t* payload,
                             const uint8_t* key,  size_t key_len)
{
    /* Build a contiguous buffer for the MAC input */
    uint8_t  mac_input[SM_HDR_HMAC_OFFSET + SM_MAX_PAYLOAD_SIZE];
    size_t   mac_len;

    if (!hdr || !payload || !key) return SM_ERR_INVALID;

    mac_len = SM_HDR_HMAC_OFFSET + hdr->length;
    if (mac_len > sizeof(mac_input)) return SM_ERR_INVALID;

    memcpy(mac_input,                   hdr,     SM_HDR_HMAC_OFFSET);
    memcpy(mac_input + SM_HDR_HMAC_OFFSET, payload, hdr->length);

    if (sm_hmac_verify(key, key_len, mac_input, mac_len, hdr->hmac) != 0) {
        sm_log(SM_LOG_WARN, "validate_hmac: HMAC mismatch - rejecting message");
        return SM_ERR_AUTH;
    }

    return SM_OK;
}

/* ── NAME VALIDATION ────────────────────────────────────────────────────────── */

int sm_validate_service_name(const char* name)
{
    size_t len;
    size_t i;

    if (!name || *name == '\0') return SM_ERR_INVALID;

    len = strnlen(name, SM_MAX_NAME);
    if (len >= SM_MAX_NAME) {
        sm_log(SM_LOG_ERROR, "validate_name: too long (%zu)", len);
        return SM_ERR_INVALID;
    }

    /* Allow only alphanumeric, hyphen, underscore */
    for (i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            sm_log(SM_LOG_ERROR, "validate_name: illegal char 0x%02x at pos %zu",
                   (unsigned char)c, i);
            return SM_ERR_INVALID;
        }
    }

    return SM_OK;
}

/* ── PATH VALIDATION ────────────────────────────────────────────────────────── */

int sm_validate_socket_path(const char* path)
{
    size_t len;
    int    allowed;

    if (!path || *path == '\0') return SM_ERR_INVALID;

    len = strnlen(path, SM_MAX_PATH);
    if (len >= SM_MAX_PATH) {
        sm_log(SM_LOG_ERROR, "validate_path: too long (%zu)", len);
        return SM_ERR_INVALID;
    }

    /* Must be absolute */
    if (path[0] != '/') {
        sm_log(SM_LOG_ERROR, "validate_path: not absolute: %.64s", path);
        return SM_ERR_INVALID;
    }

    /* Reject directory traversal and double-slash */
    if (strstr(path, "..") || strstr(path, "//")) {
        sm_log(SM_LOG_ERROR, "validate_path: suspicious pattern: %.64s", path);
        return SM_ERR_INVALID;
    }

    /* Whitelist: path must start with an allowed directory prefix */
    allowed = (strncmp(path, SM_ALLOWED_PATH_1, strlen(SM_ALLOWED_PATH_1)) == 0 ||
               strncmp(path, SM_ALLOWED_PATH_2, strlen(SM_ALLOWED_PATH_2)) == 0);
    if (!allowed) {
        sm_log(SM_LOG_ERROR, "validate_path: not in allowed prefix: %.64s", path);
        return SM_ERR_INVALID;
    }

    return SM_OK;
}

/* ── MESSAGE SIZE VALIDATION ────────────────────────────────────────────────── */

int sm_validate_message_size(uint32_t payload_len, uint16_t msg_type)
{
    size_t expected;

    switch (msg_type) {
        case SM_MSG_REGISTER:   expected = sizeof(sm_register_req_t);   break;
        case SM_MSG_LOOKUP:     expected = sizeof(sm_lookup_req_t);      break;
        case SM_MSG_HEARTBEAT:  expected = sizeof(sm_heartbeat_req_t);   break;
        case SM_MSG_UNREGISTER: expected = sizeof(sm_unregister_req_t);  break;
        default:
            sm_log(SM_LOG_ERROR, "validate_size: unknown type %u", msg_type);
            return SM_ERR_INVALID;
    }

    if ((size_t)payload_len != expected) {
        sm_log(SM_LOG_ERROR, "validate_size: type %u got %u expected %zu",
               msg_type, payload_len, expected);
        return SM_ERR_INVALID;
    }

    return SM_OK;
}