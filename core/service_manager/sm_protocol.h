#ifndef SM_PROTOCOL_H
#define SM_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// ── PROTOCOL MAGIC & VERSION ──────────────────────────────────────────────────
// Protects against:
// - Malformed clients
// - Version mismatches
// - Buffer confusion attacks

#define SM_PROTOCOL_MAGIC   0x534D4B47  // "SMKG" in hex
#define SM_PROTOCOL_VERSION 1

// ── MESSAGE TYPES ─────────────────────────────────────────────────────────────

#define SM_MSG_REGISTER     1
#define SM_MSG_LOOKUP       2
#define SM_MSG_HEARTBEAT    3
#define SM_MSG_UNREGISTER   4

// ── RESPONSE CODES ────────────────────────────────────────────────────────────

#define SM_OK               0
#define SM_ERR_NOT_FOUND   -1
#define SM_ERR_FULL        -2
#define SM_ERR_EXISTS      -3
#define SM_ERR_INVALID     -4
#define SM_ERR_PROTOCOL    -5
#define SM_ERR_PERMISSION  -6
#define SM_ERR_RATELIMIT   -7

// ── PROTOCOL HEADER ───────────────────────────────────────────────────────────
// Must be first field in every message
// Prevents protocol confusion attacks

typedef struct {
    uint32_t magic;          // SM_PROTOCOL_MAGIC
    uint16_t version;        // SM_PROTOCOL_VERSION
    uint16_t type;           // SM_MSG_*
    uint32_t length;         // payload length (excluding header)
    uint32_t timestamp;      // client timestamp (seconds)
    uint32_t client_pid;     // client process id
} sm_hdr_t;

// ── CONSTRAINTS ────────────────────────────────────────────────────────────────

#define SM_MAX_SERVICES     32
#define SM_MAX_NAME         64
#define SM_MAX_PATH         256
#define SM_HEARTBEAT_TIMEOUT 10
#define SM_MAX_MESSAGE_SIZE (sizeof(sm_hdr_t) + 1024)

// ── SERVICE PAYLOAD (sent after header) ───────────────────────────────────────

typedef struct {
    char service_name[SM_MAX_NAME];
    char socket_path[SM_MAX_PATH];
    char ring_name[SM_MAX_PATH];
    int  pid;
} sm_register_req_t;

typedef struct {
    char service_name[SM_MAX_NAME];
} sm_lookup_req_t;

typedef struct {
    char socket_path[SM_MAX_PATH];
    char ring_name[SM_MAX_PATH];
    int  service_pid;
} sm_lookup_reply_t;

typedef struct {
    char service_name[SM_MAX_NAME];
} sm_heartbeat_req_t;

typedef struct {
    char service_name[SM_MAX_NAME];
} sm_unregister_req_t;

// ── GENERIC REPLY ──────────────────────────────────────────────────────────────

typedef struct {
    int response_code;
} sm_reply_t;

// ── MESSAGE VALIDATION ─────────────────────────────────────────────────────────

int  sm_validate_header(const sm_hdr_t* hdr, size_t received_size);
int  sm_validate_service_name(const char* name);
int  sm_validate_socket_path(const char* path);
int  sm_validate_message_size(uint32_t payload_len, uint16_t msg_type);

#endif // SM_PROTOCOL_H
