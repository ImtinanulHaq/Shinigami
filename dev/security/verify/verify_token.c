#define _POSIX_C_SOURCE 200809L
#include "verify_token.h"
#include "verify_hmac.h"
#include "verify_replay.h"
#include "verify.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>

static time_t get_monotonic_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        syslog(LOG_ERR, "[token] clock_gettime(MONOTONIC) failed: %s", strerror(errno));
        return 0;
    }
    return ts.tv_sec;
}

static void secure_zero(void* ptr, size_t len)
{
    if (ptr) {
        volatile unsigned char* p = ptr;
        while (len--) {
            *p++ = 0;
        }
    }
}

int token_issue(verify_context_t* ctx,
                const char* service_name,
                uint32_t permissions,
                uint32_t validity_seconds,
                service_token_t* token)
{
    if (!ctx || !service_name || !token) {
        syslog(LOG_ERR, "[token] token_issue: invalid parameters");
        return -1;
    }

    if (strlen(service_name) == 0 || strlen(service_name) >= sizeof(token->service_name)) {
        syslog(LOG_ERR, "[token] invalid service_name length: %zu", strlen(service_name));
        return -1;
    }

    memset(token, 0, sizeof(service_token_t));
    strncpy(token->service_name, service_name, sizeof(token->service_name) - 1);
    token->service_name[sizeof(token->service_name) - 1] = '\0';

    token->issued_at = get_monotonic_time();
    token->expires_at = token->issued_at + validity_seconds;
    token->permissions = permissions;

    size_t name_len = strlen(token->service_name);
    size_t data_len = name_len + sizeof(token->issued_at) +
                      sizeof(token->expires_at) + sizeof(token->permissions);

    uint8_t* data = malloc(data_len);
    if (!data) {
        syslog(LOG_ERR, "[token] malloc failed: %s", strerror(errno));
        return -1;
    }

    uint8_t* p = data;
    memcpy(p, token->service_name, name_len);
    p += name_len;
    memcpy(p, &token->issued_at, sizeof(token->issued_at));
    p += sizeof(token->issued_at);
    memcpy(p, &token->expires_at, sizeof(token->expires_at));
    p += sizeof(token->expires_at);
    memcpy(p, &token->permissions, sizeof(token->permissions));

    int result = hmac_sign(ctx->master_key, ctx->master_key_len,
                          data, data_len, token->token);

    secure_zero(data, data_len);
    free(data);

    if (result == 0) {
        syslog(LOG_INFO, "[token] Issued token for '%s' valid until %ld (permissions: 0x%x)",
               service_name, token->expires_at, permissions);
    } else {
        syslog(LOG_ERR, "[token] Failed to issue token for '%s'", service_name);
    }

    return result;
}

int token_verify(verify_context_t* ctx, const service_token_t* token)
{
    if (!ctx || !token) {
        syslog(LOG_ERR, "[token] token_verify: invalid parameters");
        return -1;
    }

    time_t now = get_monotonic_time();
    if (now == 0 || now >= token->expires_at) {
        syslog(LOG_WARNING, "[token] Token expired for '%s' (expired at %ld, now is %ld)",
               token->service_name, token->expires_at, now);
        return 0;
    }

    size_t name_len = strlen(token->service_name);
    size_t data_len = name_len + sizeof(token->issued_at) +
                      sizeof(token->expires_at) + sizeof(token->permissions);

    uint8_t* data = malloc(data_len);
    if (!data) {
        syslog(LOG_ERR, "[token] malloc failed: %s", strerror(errno));
        return -1;
    }

    uint8_t* p = data;
    memcpy(p, token->service_name, name_len);
    p += name_len;
    memcpy(p, &token->issued_at, sizeof(token->issued_at));
    p += sizeof(token->issued_at);
    memcpy(p, &token->expires_at, sizeof(token->expires_at));
    p += sizeof(token->expires_at);
    memcpy(p, &token->permissions, sizeof(token->permissions));

    int result = hmac_verify(ctx->master_key, ctx->master_key_len,
                            data, data_len, token->token);

    secure_zero(data, data_len);
    free(data);

    if (result == 1) {
        syslog(LOG_DEBUG, "[token] Valid token for '%s' (expires in %ld seconds)",
               token->service_name, token->expires_at - now);
    } else {
        syslog(LOG_WARNING, "[token] Invalid token signature for '%s'",
               token->service_name);
    }

    return result;
}

int token_has_permission(const service_token_t* token, uint32_t permission)
{
    if (!token) {
        return 0;
    }

    return (token->permissions & permission) != 0;
}

int32_t token_time_remaining(const service_token_t* token)
{
    if (!token) {
        return -1;
    }

    time_t now = get_monotonic_time();

    if (now == 0 || now >= token->expires_at) {
        return 0;
    }

    return (int32_t)(token->expires_at - now);
}

void token_secure_clear(service_token_t* token)
{
    if (!token) {
        return;
    }

    secure_zero(token, sizeof(service_token_t));
}
