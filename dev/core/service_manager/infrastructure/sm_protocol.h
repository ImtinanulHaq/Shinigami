#ifndef SM_PROTOCOL_H
#define SM_PROTOCOL_H

/*
 * sm_protocol.h - Wire protocol definitions for the service manager IPC.
 *
 * Every message consists of:
 *   [sm_hdr_t header] [payload bytes]
 *
 * The HMAC field in the header covers all other header bytes plus the payload.
 * This provides message integrity and replay protection in combination with
 * the timestamp and nonce fields.
 */

#include <stdint.h>
#include <stddef.h>

/* Protocol identification */
#define SM_PROTOCOL_MAGIC    0x534D4B47   /* "SMKG" */
#define SM_PROTOCOL_VERSION  2            /* bumped for HMAC header addition */

/* Message type codes */
#define SM_MSG_REGISTER      1
#define SM_MSG_LOOKUP        2
#define SM_MSG_HEARTBEAT     3
#define SM_MSG_UNREGISTER    4

/* Response codes returned in sm_reply_t */
#define SM_OK                0
#define SM_ERR_NOT_FOUND    -1
#define SM_ERR_FULL         -2
#define SM_ERR_EXISTS       -3
#define SM_ERR_INVALID      -4
#define SM_ERR_PROTOCOL     -5
#define SM_ERR_PERMISSION   -6
#define SM_ERR_RATELIMIT    -7
#define SM_ERR_AUTH         -8    /* HMAC verification failed */

/*
 * Protocol header - must be the first field in every message sent on the wire.
 *
 * Layout (little-endian fields, packed):
 *   Offset  0: magic      (4 bytes)
 *   Offset  4: version    (2 bytes)
 *   Offset  6: type       (2 bytes)
 *   Offset  8: length     (4 bytes) - payload size in bytes, NOT including header
 *   Offset 12: timestamp  (4 bytes) - Unix seconds (uint32)
 *   Offset 16: client_pid (4 bytes) - informational only; authoritative PID from SO_PEERCRED
 *   Offset 20: nonce      (4 bytes) - random per-message value, anti-replay
 *   Offset 24: hmac[32]   (32 bytes)- HMAC-SHA256 over bytes 0..23 of header + payload
 *   Total:     56 bytes
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;       /* payload length, excluding this header */
    uint32_t timestamp;    /* seconds since epoch */
    uint32_t client_pid;   /* informational only - not used for auth decisions */
    uint32_t nonce;        /* random per-request value */
    uint8_t  hmac[32];     /* HMAC-SHA256(key, header[0..23] || payload) */
} sm_hdr_t;

/* Offset of the HMAC field within the header (everything before it is covered) */
#define SM_HDR_HMAC_OFFSET  offsetof(sm_hdr_t, hmac)

/* Size constraints */
#define SM_MAX_SERVICES      32
#define SM_MAX_NAME          64
#define SM_MAX_PATH          256
#define SM_HEARTBEAT_TIMEOUT 10            /* seconds without heartbeat = crashed */
#define SM_MAX_PAYLOAD_SIZE  1024          /* maximum payload, excluding header */
#define SM_MAX_MESSAGE_SIZE  (sizeof(sm_hdr_t) + SM_MAX_PAYLOAD_SIZE)

/* Timestamp window: reject messages older than this many seconds */
#define SM_TIMESTAMP_MAX_AGE 120
/* Reject messages with timestamps more than this many seconds in the future */
#define SM_TIMESTAMP_MAX_SKEW 60

/* Allowed socket path prefixes for registered services */
#define SM_ALLOWED_PATH_1   "/run/"
#define SM_ALLOWED_PATH_2   "/tmp/"
#define SM_ALLOWED_PATH_3   "/dev/shm/"

/* ── PAYLOAD STRUCTURES ─────────────────────────────────────────────────────── */

typedef struct {
    char     service_name[SM_MAX_NAME];
    char     socket_path[SM_MAX_PATH];
    char     ring_name[SM_MAX_PATH];
    int32_t  pid;
} sm_register_req_t;

typedef struct {
    char service_name[SM_MAX_NAME];
} sm_lookup_req_t;

typedef struct {
    char     socket_path[SM_MAX_PATH];
    char     ring_name[SM_MAX_PATH];
    int32_t  service_pid;
} sm_lookup_reply_t;

typedef struct {
    char service_name[SM_MAX_NAME];
} sm_heartbeat_req_t;

typedef struct {
    char service_name[SM_MAX_NAME];
} sm_unregister_req_t;

typedef struct {
    int32_t response_code;
} sm_reply_t;

/* ── VALIDATION API ─────────────────────────────────────────────────────────── */

/*
 * Validate header fields, received_size, timestamp window, and HMAC.
 * 'key' is the HMAC key (SM_HMAC_KEY_SIZE bytes).
 * 'payload' is the payload buffer (hdr->length bytes).
 */
int  sm_validate_header(const sm_hdr_t* hdr,     size_t received_size);
int  sm_validate_header_hmac(const sm_hdr_t* hdr, const uint8_t* payload,
                              const uint8_t* key,  size_t key_len);
int  sm_validate_service_name(const char* name);
int  sm_validate_socket_path(const char* path);
int  sm_validate_message_size(uint32_t payload_len, uint16_t msg_type);

#endif /* SM_PROTOCOL_H */