#define _POSIX_C_SOURCE 200809L
#include "verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/random.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

// ══════════════════════════════════════════════════════════════════════════════
// INTERNAL HELPERS
// ══════════════════════════════════════════════════════════════════════════════

// Secure memory zeroing (compiler cannot optimize away)
static void secure_zero(void* ptr, size_t len)
{
    volatile uint8_t* p = ptr;
    while (len--) *p++ = 0;
}

// Read key from file securely
static int read_key_from_file(const char* path, uint8_t* key, size_t* key_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("[verify] open key file");
        return -1;
    }

    ssize_t bytes_read = read(fd, key, MAX_KEY_SIZE);
    close(fd);

    if (bytes_read < 0) {
        perror("[verify] read key file");
        return -1;
    }

    if (bytes_read == 0) {
        fprintf(stderr, "[verify] key file is empty\n");
        return -1;
    }

    *key_len = (size_t)bytes_read;
    return 0;
}

// Write key to file securely (restricted permissions)
static int write_key_to_file(const char* path, const uint8_t* key, size_t key_len)
{
    // Create file with restrictive permissions (0600 = owner read/write only)
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("[verify] create key file");
        return -1;
    }

    ssize_t bytes_written = write(fd, key, key_len);
    close(fd);

    if (bytes_written != (ssize_t)key_len) {
        fprintf(stderr, "[verify] incomplete key write\n");
        unlink(path);
        return -1;
    }

    printf("[verify] key saved to %s (permissions: 0600)\n", path);
    return 0;
}

// Generate cryptographically secure random bytes
static int generate_random_bytes(uint8_t* buffer, size_t len)
{
    // Use getrandom() - cryptographically secure random number generator
    ssize_t result = getrandom(buffer, len, 0);
    if (result != (ssize_t)len) {
        perror("[verify] getrandom");
        return -1;
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// INITIALIZATION FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

int verify_init(verify_context_t* ctx, const uint8_t* key, size_t key_len)
{
    if (!ctx || !key || key_len == 0) {
        fprintf(stderr, "[verify] invalid arguments to verify_init\n");
        return -1;
    }

    if (key_len > MAX_KEY_SIZE) {
        fprintf(stderr, "[verify] key too large (max %d bytes)\n", MAX_KEY_SIZE);
        return -1;
    }

    // Zero context first
    memset(ctx, 0, sizeof(verify_context_t));

    // Copy key
    memcpy(ctx->master_key, key, key_len);
    ctx->master_key_len = key_len;

    // Initialize state
    ctx->last_timestamp = 0;
    ctx->last_sequence = 0;
    ctx->strict_ordering = 1;              // Enabled by default
    ctx->enable_timestamp_check = 1;       // Enabled by default

    printf("[verify] context initialized with %zu-byte key\n", key_len);
    return 0;
}

int verify_init_from_file(verify_context_t* ctx, const char* key_file)
{
    if (!ctx || !key_file) return -1;

    uint8_t key[MAX_KEY_SIZE];
    size_t key_len;

    if (read_key_from_file(key_file, key, &key_len) < 0) {
        return -1;
    }

    int result = verify_init(ctx, key, key_len);
    secure_zero(key, sizeof(key));  // Erase key from stack
    return result;
}

int verify_init_from_env(verify_context_t* ctx, const char* env_var)
{
    if (!ctx || !env_var) return -1;

    const char* key_hex = getenv(env_var);
    if (!key_hex) {
        fprintf(stderr, "[verify] environment variable '%s' not set\n", env_var);
        return -1;
    }

    // Convert hex string to bytes
    size_t hex_len = strlen(key_hex);
    if (hex_len % 2 != 0) {
        fprintf(stderr, "[verify] key must be hex string with even length\n");
        return -1;
    }

    size_t key_len = hex_len / 2;
    if (key_len > MAX_KEY_SIZE) {
        fprintf(stderr, "[verify] key too large\n");
        return -1;
    }

    uint8_t key[MAX_KEY_SIZE];
    for (size_t i = 0; i < key_len; i++) {
        if (sscanf(key_hex + i * 2, "%2hhx", &key[i]) != 1) {
            fprintf(stderr, "[verify] invalid hex character in key\n");
            return -1;
        }
    }

    int result = verify_init(ctx, key, key_len);
    secure_zero(key, sizeof(key));
    return result;
}

int verify_generate_key(const char* key_file)
{
    if (!key_file) return -1;

    // Generate 32-byte (256-bit) random key
    uint8_t key[32];
    if (generate_random_bytes(key, sizeof(key)) < 0) {
        return -1;
    }

    int result = write_key_to_file(key_file, key, sizeof(key));
    secure_zero(key, sizeof(key));

    if (result == 0) {
        printf("[verify] generated 256-bit key\n");
    }
    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
// LOW-LEVEL HMAC FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

int verify_hmac_sha256(const uint8_t* key,
                       size_t key_len,
                       const uint8_t* data,
                       size_t data_len,
                       uint8_t* output)
{
    if (!key || !data || !output) return -1;

    unsigned int hmac_len = HMAC_SHA256_SIZE;

    // Use OpenSSL HMAC (production-grade cryptography)
    if (HMAC(EVP_sha256(), key, (int)key_len, data, data_len, output, &hmac_len) == NULL) {
        fprintf(stderr, "[verify] HMAC computation failed\n");
        return -1;
    }

    if (hmac_len != HMAC_SHA256_SIZE) {
        fprintf(stderr, "[verify] unexpected HMAC length: %u\n", hmac_len);
        return -1;
    }

    return 0;
}

int verify_hmac_compare(const uint8_t* hmac1, const uint8_t* hmac2)
{
    if (!hmac1 || !hmac2) return 0;

    // Constant-time comparison (timing attack resistant)
    // This prevents attackers from guessing HMAC byte-by-byte via timing
    volatile uint8_t result = 0;
    for (size_t i = 0; i < HMAC_SHA256_SIZE; i++) {
        result |= hmac1[i] ^ hmac2[i];
    }

    return result == 0 ? 1 : 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// MESSAGE AUTHENTICATION FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

int verify_sign_message(verify_context_t* ctx,
                        const void* data,
                        size_t data_len,
                        message_auth_t* auth)
{
    if (!ctx || !data || !auth) return -1;

    // Fill authentication metadata
    auth->timestamp = verify_get_timestamp();
    auth->nonce = verify_generate_nonce();
    auth->sequence = ++ctx->last_sequence;  // Increment sequence

    // Build data to sign: data || timestamp || nonce || sequence
    size_t total_len = data_len + sizeof(auth->timestamp) + 
                       sizeof(auth->nonce) + sizeof(auth->sequence);
    uint8_t* to_sign = malloc(total_len);
    if (!to_sign) {
        perror("[verify] malloc");
        return -1;
    }

    // Concatenate all fields
    uint8_t* p = to_sign;
    memcpy(p, data, data_len);
    p += data_len;
    memcpy(p, &auth->timestamp, sizeof(auth->timestamp));
    p += sizeof(auth->timestamp);
    memcpy(p, &auth->nonce, sizeof(auth->nonce));
    p += sizeof(auth->nonce);
    memcpy(p, &auth->sequence, sizeof(auth->sequence));

    // Compute HMAC
    int result = verify_hmac_sha256(ctx->master_key, ctx->master_key_len,
                                    to_sign, total_len, auth->hmac);

    secure_zero(to_sign, total_len);
    free(to_sign);

    return result;
}

int verify_check_message(verify_context_t* ctx,
                         const void* data,
                         size_t data_len,
                         const message_auth_t* auth)
{
    if (!ctx || !data || !auth) return -1;

    // Check 1: Timestamp validation (replay attack prevention)
    if (ctx->enable_timestamp_check) {
        uint64_t now = verify_get_timestamp();
        
        // Message too old
        if (now > auth->timestamp && (now - auth->timestamp) > TIMESTAMP_WINDOW) {
            fprintf(stderr, "[verify] message too old (timestamp diff: %lu seconds)\n",
                    now - auth->timestamp);
            return 0;
        }
        
        // Message from future (clock skew or attack)
        if (auth->timestamp > now && (auth->timestamp - now) > TIMESTAMP_WINDOW) {
            fprintf(stderr, "[verify] message from future (clock skew: %lu seconds)\n",
                    auth->timestamp - now);
            return 0;
        }
    }

    // Check 2: Sequence number validation
    if (ctx->strict_ordering) {
        if (auth->sequence <= ctx->last_sequence) {
            fprintf(stderr, "[verify] replay detected (sequence %u <= last %u)\n",
                    auth->sequence, ctx->last_sequence);
            return 0;
        }
    }

    // Check 3: Recompute HMAC and compare
    size_t total_len = data_len + sizeof(auth->timestamp) + 
                       sizeof(auth->nonce) + sizeof(auth->sequence);
    uint8_t* to_verify = malloc(total_len);
    if (!to_verify) {
        perror("[verify] malloc");
        return -1;
    }

    // Rebuild signed data
    uint8_t* p = to_verify;
    memcpy(p, data, data_len);
    p += data_len;
    memcpy(p, &auth->timestamp, sizeof(auth->timestamp));
    p += sizeof(auth->timestamp);
    memcpy(p, &auth->nonce, sizeof(auth->nonce));
    p += sizeof(auth->nonce);
    memcpy(p, &auth->sequence, sizeof(auth->sequence));

    // Compute expected HMAC
    uint8_t expected_hmac[HMAC_SHA256_SIZE];
    int hmac_result = verify_hmac_sha256(ctx->master_key, ctx->master_key_len,
                                         to_verify, total_len, expected_hmac);

    secure_zero(to_verify, total_len);
    free(to_verify);

    if (hmac_result < 0) return -1;

    // Compare HMACs (constant-time)
    int valid = verify_hmac_compare(auth->hmac, expected_hmac);
    secure_zero(expected_hmac, sizeof(expected_hmac));

    if (valid) {
        // Update state on successful verification
        ctx->last_timestamp = auth->timestamp;
        ctx->last_sequence = auth->sequence;
        return 1;
    }

    fprintf(stderr, "[verify] HMAC mismatch - message authentication failed\n");
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// SERVICE AUTHENTICATION FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

int verify_issue_token(verify_context_t* ctx,
                       const char* service_name,
                       uint32_t permissions,
                       uint32_t validity_seconds,
                       service_token_t* token)
{
    if (!ctx || !service_name || !token) return -1;

    // Fill token metadata
    memset(token, 0, sizeof(service_token_t));
    strncpy(token->service_name, service_name, sizeof(token->service_name) - 1);
    token->issued_at = time(NULL);
    token->expires_at = token->issued_at + validity_seconds;
    token->permissions = permissions;

    // Build data to sign: service_name || issued_at || expires_at || permissions
    size_t data_len = strlen(token->service_name) + sizeof(token->issued_at) +
                      sizeof(token->expires_at) + sizeof(token->permissions);
    uint8_t* data = malloc(data_len);
    if (!data) {
        perror("[verify] malloc");
        return -1;
    }

    uint8_t* p = data;
    size_t name_len = strlen(token->service_name);
    memcpy(p, token->service_name, name_len);
    p += name_len;
    memcpy(p, &token->issued_at, sizeof(token->issued_at));
    p += sizeof(token->issued_at);
    memcpy(p, &token->expires_at, sizeof(token->expires_at));
    p += sizeof(token->expires_at);
    memcpy(p, &token->permissions, sizeof(token->permissions));

    // Generate token signature
    int result = verify_hmac_sha256(ctx->master_key, ctx->master_key_len,
                                    data, data_len, token->token);

    secure_zero(data, data_len);
    free(data);

    if (result == 0) {
        printf("[verify] issued token for '%s' valid until %ld (permissions: 0x%x)\n",
               service_name, token->expires_at, permissions);
    }

    return result;
}

int verify_check_token(verify_context_t* ctx, const service_token_t* token)
{
    if (!ctx || !token) return -1;

    // Check 1: Expiration
    time_t now = time(NULL);
    if (now >= token->expires_at) {
        fprintf(stderr, "[verify] token expired (expired at %ld, now is %ld)\n",
                token->expires_at, now);
        return 0;
    }

    // Check 2: Issue time sanity
    if (token->issued_at > now) {
        fprintf(stderr, "[verify] token issued in the future (clock skew)\n");
        return 0;
    }

    // Check 3: Recompute signature and compare
    size_t data_len = strlen(token->service_name) + sizeof(token->issued_at) +
                      sizeof(token->expires_at) + sizeof(token->permissions);
    uint8_t* data = malloc(data_len);
    if (!data) {
        perror("[verify] malloc");
        return -1;
    }

    uint8_t* p = data;
    size_t name_len = strlen(token->service_name);
    memcpy(p, token->service_name, name_len);
    p += name_len;
    memcpy(p, &token->issued_at, sizeof(token->issued_at));
    p += sizeof(token->issued_at);
    memcpy(p, &token->expires_at, sizeof(token->expires_at));
    p += sizeof(token->expires_at);
    memcpy(p, &token->permissions, sizeof(token->permissions));

    uint8_t expected_token[HMAC_SHA256_SIZE];
    int hmac_result = verify_hmac_sha256(ctx->master_key, ctx->master_key_len,
                                         data, data_len, expected_token);

    secure_zero(data, data_len);
    free(data);

    if (hmac_result < 0) return -1;

    int valid = verify_hmac_compare(token->token, expected_token);
    secure_zero(expected_token, sizeof(expected_token));

    if (!valid) {
        fprintf(stderr, "[verify] token signature invalid for '%s'\n", token->service_name);
    }

    return valid;
}

int verify_token_has_permission(const service_token_t* token, service_permissions_t permission)
{
    if (!token) return 0;
    return (token->permissions & permission) != 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

uint64_t verify_get_timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec;
}

uint32_t verify_generate_nonce(void)
{
    uint32_t nonce;
    if (generate_random_bytes((uint8_t*)&nonce, sizeof(nonce)) < 0) {
        // Fallback to less secure random if getrandom fails
        return (uint32_t)time(NULL) ^ (uint32_t)getpid();
    }
    return nonce;
}

void verify_set_strict_ordering(verify_context_t* ctx, int enable)
{
    if (ctx) {
        ctx->strict_ordering = enable;
        printf("[verify] strict ordering %s\n", enable ? "enabled" : "disabled");
    }
}

void verify_set_timestamp_check(verify_context_t* ctx, int enable)
{
    if (ctx) {
        ctx->enable_timestamp_check = enable;
        printf("[verify] timestamp check %s\n", enable ? "enabled" : "disabled");
    }
}

void verify_cleanup(verify_context_t* ctx)
{
    if (ctx) {
        secure_zero(ctx, sizeof(verify_context_t));
        printf("[verify] context cleaned up\n");
    }
}