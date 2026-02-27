#ifndef VERIFY_TOKEN_H
#define VERIFY_TOKEN_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef struct verify_context verify_context_t;

#define HMAC_SHA256_SIZE 32

typedef struct {
    char     service_name[64];
    uint8_t  token[HMAC_SHA256_SIZE];
    time_t   issued_at;
    time_t   expires_at;
    uint32_t permissions;
} service_token_t;

typedef enum {
    PERM_NONE           = 0,
    PERM_REGISTER       = 1 << 0,
    PERM_LOOKUP         = 1 << 1,
    PERM_SEND_MSG       = 1 << 2,
    PERM_RECV_MSG       = 1 << 3,
    PERM_CREATE_BUFFER  = 1 << 4,
    PERM_ADMIN          = 1 << 5,
} service_permissions_t;

int token_issue(verify_context_t* ctx,
                const char* service_name,
                uint32_t permissions,
                uint32_t validity_seconds,
                service_token_t* token);

int token_verify(verify_context_t* ctx, const service_token_t* token);

int token_has_permission(const service_token_t* token, uint32_t permission);

int32_t token_time_remaining(const service_token_t* token);

void token_secure_clear(service_token_t* token);

#endif
