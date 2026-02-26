#define _POSIX_C_SOURCE 200809L
#include "verify.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

static void secure_zero(void* ptr, size_t len)
{
    if (ptr) {
        volatile unsigned char* p = ptr;
        while (len--) {
            *p++ = 0;
        }
    }
}

static int generate_random_bytes(uint8_t* buffer, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("[verify] open /dev/urandom");
        return -1;
    }

    ssize_t ret = read(fd, buffer, len);
    close(fd);

    if (ret != (ssize_t)len) {
        fprintf(stderr, "[verify] failed to read %zu random bytes\n", len);
        return -1;
    }

    return 0;
}

int verify_init(verify_context_t* ctx, const uint8_t* key, size_t key_len)
{
    if (!ctx || !key || key_len == 0 || key_len > MAX_KEY_SIZE) {
        syslog(LOG_ERR, "[verify] verify_init: invalid parameters");
        return -1;
    }

    memset(ctx, 0, sizeof(verify_context_t));

    memcpy(ctx->master_key, key, key_len);
    ctx->master_key_len = key_len;

    if (replay_context_init(&ctx->replay_ctx, TIMESTAMP_WINDOW) != 0) {
        syslog(LOG_ERR, "[verify] Failed to initialize replay context");
        return -1;
    }

    ctx->strict_ordering = 1;
    ctx->enable_timestamp_check = 1;

    syslog(LOG_INFO, "[verify] Initialized with %zu-byte key", key_len);
    return 0;
}

int verify_init_from_file(verify_context_t* ctx, const char* key_file)
{
    if (!ctx || !key_file) {
        syslog(LOG_ERR, "[verify] verify_init_from_file: invalid parameters");
        return -1;
    }

    FILE* f = fopen(key_file, "rb");
    if (!f) {
        syslog(LOG_ERR, "[verify] Failed to open key file '%s': %s",
               key_file, strerror(errno));
        return -1;
    }

    uint8_t key[MAX_KEY_SIZE];
    size_t key_len = fread(key, 1, sizeof(key), f);
    fclose(f);

    if (key_len == 0) {
        syslog(LOG_ERR, "[verify] Empty key file '%s'", key_file);
        return -1;
    }

    int ret = verify_init(ctx, key, key_len);
    secure_zero(key, sizeof(key));

    return ret;
}

int verify_init_from_env(verify_context_t* ctx, const char* env_var)
{
    if (!ctx || !env_var) {
        syslog(LOG_ERR, "[verify] verify_init_from_env: invalid parameters");
        return -1;
    }

    const char* key_hex = getenv(env_var);
    if (!key_hex) {
        syslog(LOG_ERR, "[verify] Environment variable '%s' not set", env_var);
        return -1;
    }

    size_t hex_len = strlen(key_hex);
    if (hex_len % 2 != 0 || hex_len == 0) {
        syslog(LOG_ERR, "[verify] Invalid hex key length: %zu", hex_len);
        return -1;
    }

    size_t key_len = hex_len / 2;
    if (key_len > MAX_KEY_SIZE) {
        syslog(LOG_ERR, "[verify] Key too large: %zu bytes", key_len);
        return -1;
    }

    uint8_t key[MAX_KEY_SIZE];
    for (size_t i = 0; i < key_len; i++) {
        sscanf(&key_hex[i * 2], "%2hhx", &key[i]);
    }

    int ret = verify_init(ctx, key, key_len);
    secure_zero(key, sizeof(key));

    return ret;
}

int verify_generate_key(const char* key_file)
{
    if (!key_file) {
        syslog(LOG_ERR, "[verify] verify_generate_key: null key_file");
        return -1;
    }

    uint8_t key[32];
    if (generate_random_bytes(key, sizeof(key)) < 0) {
        return -1;
    }

    FILE* f = fopen(key_file, "wb");
    if (!f) {
        syslog(LOG_ERR, "[verify] Failed to create key file '%s': %s",
               key_file, strerror(errno));
        secure_zero(key, sizeof(key));
        return -1;
    }

    fwrite(key, 1, sizeof(key), f);
    fclose(f);

    secure_zero(key, sizeof(key));

    printf("[verify] Generated 256-bit key in '%s'\n", key_file);
    return 0;
}

static int verify_check_rate_limit(verify_context_t* ctx)
{
    if (ctx->rate_limit_per_sec == 0) return 1;

    uint64_t now = replay_get_monotonic_time();
    if (now != ctx->rate_window_start) {
        ctx->rate_window_start = now;
        ctx->rate_count = 0;
    }

    if (ctx->rate_count >= ctx->rate_limit_per_sec) {
        syslog(LOG_WARNING, "[verify] rate limit exceeded (%u/sec)",
               ctx->rate_limit_per_sec);
        return 0;
    }

    ctx->rate_count++;
    return 1;
}

int verify_sign_message(verify_context_t* ctx,
                        const void* data,
                        size_t data_len,
                        message_auth_t* auth)
{
    if (!ctx || !data || data_len == 0 || !auth) {
        syslog(LOG_ERR, "[verify] verify_sign_message: invalid parameters");
        return -1;
    }

    if (!verify_check_rate_limit(ctx)) return -1;

    memset(auth, 0, sizeof(*auth));

    auth->timestamp = replay_get_monotonic_time();
    auth->nonce = verify_generate_nonce();
    auth->sequence = ctx->replay_ctx.last_sequence + 1;

    size_t total_len = sizeof(auth->timestamp) + sizeof(auth->nonce) +
                       sizeof(auth->sequence) + data_len;
    uint8_t* sign_data = malloc(total_len);
    if (!sign_data) {
        syslog(LOG_ERR, "[verify] malloc failed: %s", strerror(errno));
        return -1;
    }

    uint8_t* p = sign_data;
    memcpy(p, &auth->timestamp, sizeof(auth->timestamp));
    p += sizeof(auth->timestamp);
    memcpy(p, &auth->nonce, sizeof(auth->nonce));
    p += sizeof(auth->nonce);
    memcpy(p, &auth->sequence, sizeof(auth->sequence));
    p += sizeof(auth->sequence);
    memcpy(p, data, data_len);

    int result = hmac_sign(ctx->master_key, ctx->master_key_len,
                          sign_data, total_len, auth->hmac);

    secure_zero(sign_data, total_len);
    free(sign_data);

    return result;
}

int verify_check_message(verify_context_t* ctx,
                         const void* data,
                         size_t data_len,
                         const message_auth_t* auth)
{
    if (!ctx || !data || data_len == 0 || !auth) {
        syslog(LOG_ERR, "[verify] verify_check_message: invalid parameters");
        return -1;
    }

    if (ctx->enable_timestamp_check || ctx->strict_ordering) {
        int replay_result = replay_check(&ctx->replay_ctx,
                                         auth->timestamp,
                                         auth->sequence);
        if (replay_result != 1) {
            syslog(LOG_WARNING, "[verify] Message failed replay check");
            return 0;
        }
    }

    size_t total_len = sizeof(auth->timestamp) + sizeof(auth->nonce) +
                       sizeof(auth->sequence) + data_len;
    uint8_t* sign_data = malloc(total_len);
    if (!sign_data) {
        syslog(LOG_ERR, "[verify] malloc failed: %s", strerror(errno));
        return -1;
    }

    uint8_t* p = sign_data;
    memcpy(p, &auth->timestamp, sizeof(auth->timestamp));
    p += sizeof(auth->timestamp);
    memcpy(p, &auth->nonce, sizeof(auth->nonce));
    p += sizeof(auth->nonce);
    memcpy(p, &auth->sequence, sizeof(auth->sequence));
    p += sizeof(auth->sequence);
    memcpy(p, data, data_len);

    int result = hmac_verify(ctx->master_key, ctx->master_key_len,
                            sign_data, total_len, auth->hmac);

    secure_zero(sign_data, total_len);
    free(sign_data);

    if (result == 1) {

        replay_update(&ctx->replay_ctx, auth->timestamp, auth->sequence);
    }

    return result;
}

int verify_issue_token(verify_context_t* ctx,
                       const char* service_name,
                       uint32_t permissions,
                       uint32_t validity_seconds,
                       service_token_t* token)
{

    return token_issue(ctx, service_name, permissions, validity_seconds, token);
}

int verify_check_token(verify_context_t* ctx, const service_token_t* token)
{

    return token_verify(ctx, token);
}

int verify_token_has_permission(const service_token_t* token, service_permissions_t permission)
{

    return token_has_permission(token, (uint32_t)permission);
}

int verify_hmac_sha256(const uint8_t* key,
                       size_t key_len,
                       const uint8_t* data,
                       size_t data_len,
                       uint8_t* output)
{

    return hmac_sign(key, key_len, data, data_len, output);
}

int verify_hmac_compare(const uint8_t* hmac1, const uint8_t* hmac2)
{

    return hmac_compare(hmac1, hmac2);
}

uint64_t verify_get_timestamp(void)
{

    return replay_get_monotonic_time();
}

uint32_t verify_generate_nonce(void)
{
    uint32_t nonce;
    if (generate_random_bytes((uint8_t*)&nonce, sizeof(nonce)) < 0) {

        syslog(LOG_WARNING, "[verify] Using fallback nonce generation");
        nonce = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    }
    return nonce;
}

void verify_set_strict_ordering(verify_context_t* ctx, int enable)
{
    if (!ctx) return;

    ctx->strict_ordering = enable;
    replay_set_strict_ordering(&ctx->replay_ctx, enable);
}

void verify_set_timestamp_check(verify_context_t* ctx, int enable)
{
    if (!ctx) return;

    ctx->enable_timestamp_check = enable;
    replay_set_timestamp_check(&ctx->replay_ctx, enable);
}

int verify_rotate_key(verify_context_t* ctx, const uint8_t* new_key, size_t new_key_len)
{
    if (!ctx || !new_key || new_key_len == 0 || new_key_len > MAX_KEY_SIZE) {
        syslog(LOG_ERR, "[verify] verify_rotate_key: invalid parameters");
        return -1;
    }

    memcpy(ctx->prev_key, ctx->master_key, ctx->master_key_len);
    ctx->prev_key_len = ctx->master_key_len;

    secure_zero(ctx->master_key, sizeof(ctx->master_key));
    memcpy(ctx->master_key, new_key, new_key_len);
    ctx->master_key_len = new_key_len;
    ctx->key_generation++;

    syslog(LOG_INFO, "[verify] Key rotated to generation %u", ctx->key_generation);
    return 0;
}

void verify_set_rate_limit(verify_context_t* ctx, uint32_t max_per_second)
{
    if (!ctx) return;

    ctx->rate_limit_per_sec = max_per_second;
    ctx->rate_count = 0;
    ctx->rate_window_start = 0;

    syslog(LOG_INFO, "[verify] Rate limit set to %u msg/sec", max_per_second);
}

void verify_cleanup(verify_context_t* ctx)
{
    if (!ctx) return;

    replay_context_cleanup(&ctx->replay_ctx);

    secure_zero(ctx->master_key, sizeof(ctx->master_key));
    secure_zero(ctx->prev_key, sizeof(ctx->prev_key));
    memset(ctx, 0, sizeof(*ctx));
}
