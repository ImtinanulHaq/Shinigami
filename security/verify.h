#ifndef VERIFY_H
#define VERIFY_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// HMAC-SHA256 output is always 32 bytes (256 bits)
#define HMAC_SHA256_SIZE  32

// Maximum key size supported (in bytes)
#define MAX_KEY_SIZE      64

// Replay attack prevention window (seconds)
#define TIMESTAMP_WINDOW  30

// Message authentication code structure
typedef struct {
    uint8_t  hmac[HMAC_SHA256_SIZE];  // HMAC-SHA256 signature
    uint64_t timestamp;                // Unix timestamp (replay prevention)
    uint32_t nonce;                    // Random nonce (uniqueness)
    uint32_t sequence;                 // Sequence number (ordering)
} message_auth_t;

// Service authentication token
typedef struct {
    char     service_name[64];         // Service identifier
    uint8_t  token[HMAC_SHA256_SIZE];  // Authentication token
    time_t   issued_at;                // Token issue time
    time_t   expires_at;               // Token expiration time
    uint32_t permissions;              // Permission flags
} service_token_t;

// Permission flags for services
typedef enum {
    PERM_NONE           = 0,
    PERM_REGISTER       = 1 << 0,  // Can register with service manager
    PERM_LOOKUP         = 1 << 1,  // Can lookup other services
    PERM_SEND_MSG       = 1 << 2,  // Can send messages via ring buffer
    PERM_RECV_MSG       = 1 << 3,  // Can receive messages
    PERM_CREATE_BUFFER  = 1 << 4,  // Can create ring buffers
    PERM_ADMIN          = 1 << 5,  // Administrative operations
} service_permissions_t;

// Verification context (holds keys and state)
typedef struct {
    uint8_t  master_key[MAX_KEY_SIZE];     // Master key for HMAC
    size_t   master_key_len;               // Actual key length
    uint64_t last_timestamp;               // Last seen timestamp (replay detection)
    uint32_t last_sequence;                // Last seen sequence (replay detection)
    int      strict_ordering;              // Enforce sequence ordering
    int      enable_timestamp_check;       // Enable replay attack prevention
} verify_context_t;

// ══════════════════════════════════════════════════════════════════════════════
// INITIALIZATION FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Initialize verification context with a key
// key: symmetric key for HMAC (shared secret)
// key_len: length of key in bytes
// Returns: 0 on success, -1 on failure
int verify_init(verify_context_t* ctx, const uint8_t* key, size_t key_len);

// Initialize with key from file
int verify_init_from_file(verify_context_t* ctx, const char* key_file);

// Initialize with key from environment variable
int verify_init_from_env(verify_context_t* ctx, const char* env_var);

// Generate random key and save to file
int verify_generate_key(const char* key_file);

// ══════════════════════════════════════════════════════════════════════════════
// MESSAGE AUTHENTICATION FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Sign a message (generate HMAC + metadata)
// ctx: verification context
// data: message data to sign
// data_len: length of data
// auth: output authentication structure
// Returns: 0 on success, -1 on failure
int verify_sign_message(verify_context_t* ctx,
                        const void* data,
                        size_t data_len,
                        message_auth_t* auth);

// Verify a signed message
// ctx: verification context
// data: message data to verify
// data_len: length of data
// auth: authentication structure from sender
// Returns: 1 if valid, 0 if invalid, -1 on error
int verify_check_message(verify_context_t* ctx,
                         const void* data,
                         size_t data_len,
                         const message_auth_t* auth);

// ══════════════════════════════════════════════════════════════════════════════
// SERVICE AUTHENTICATION FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Issue authentication token for a service
// ctx: verification context
// service_name: name of service to authenticate
// permissions: permission flags for this service
// validity_seconds: how long token is valid
// token: output token structure
// Returns: 0 on success, -1 on failure
int verify_issue_token(verify_context_t* ctx,
                       const char* service_name,
                       uint32_t permissions,
                       uint32_t validity_seconds,
                       service_token_t* token);

// Verify a service authentication token
// ctx: verification context
// token: token to verify
// Returns: 1 if valid, 0 if expired/invalid, -1 on error
int verify_check_token(verify_context_t* ctx, const service_token_t* token);

// Check if token has specific permission
// token: token to check
// permission: permission flag to test
// Returns: 1 if has permission, 0 otherwise
int verify_token_has_permission(const service_token_t* token, service_permissions_t permission);

// ══════════════════════════════════════════════════════════════════════════════
// LOW-LEVEL HMAC FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Compute HMAC-SHA256
// key: HMAC key
// key_len: length of key
// data: data to authenticate
// data_len: length of data
// output: buffer for HMAC output (must be HMAC_SHA256_SIZE bytes)
// Returns: 0 on success, -1 on failure
int verify_hmac_sha256(const uint8_t* key,
                       size_t key_len,
                       const uint8_t* data,
                       size_t data_len,
                       uint8_t* output);

// Compare two HMACs in constant time (timing attack resistant)
// hmac1: first HMAC
// hmac2: second HMAC
// Returns: 1 if equal, 0 if different
int verify_hmac_compare(const uint8_t* hmac1, const uint8_t* hmac2);

// ══════════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Get current timestamp (seconds since epoch)
uint64_t verify_get_timestamp(void);

// Generate random nonce
uint32_t verify_generate_nonce(void);

// Enable/disable strict sequence ordering
void verify_set_strict_ordering(verify_context_t* ctx, int enable);

// Enable/disable timestamp checking
void verify_set_timestamp_check(verify_context_t* ctx, int enable);

// Clean up context (zeros sensitive data)
void verify_cleanup(verify_context_t* ctx);

#endif // VERIFY_H