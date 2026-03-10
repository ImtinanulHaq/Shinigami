#ifndef VERIFY_H
#define VERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "verify_hmac.h"
#include "verify_replay.h"
#include "verify_token.h"

#define HMAC_SHA256_SIZE 32
#define MAX_KEY_SIZE 64
#define TIMESTAMP_WINDOW 30

typedef struct {
  uint8_t hmac[HMAC_SHA256_SIZE];
  uint64_t timestamp;
  uint32_t nonce;
  uint32_t sequence;
} message_auth_t;

typedef struct verify_context {
  uint8_t master_key[MAX_KEY_SIZE];
  uint8_t prev_key[MAX_KEY_SIZE];
  size_t master_key_len;
  size_t prev_key_len;
  uint32_t key_generation;
  replay_context_t replay_ctx;
  int strict_ordering;
  int enable_timestamp_check;
  uint32_t rate_limit_per_sec;
  uint32_t rate_count;
  uint64_t rate_window_start;
} verify_context_t;

int verify_init(verify_context_t *ctx, const uint8_t *key, size_t key_len);

int verify_init_from_file(verify_context_t *ctx, const char *key_file);

int verify_init_from_env(verify_context_t *ctx, const char *env_var);

int verify_generate_key(const char *key_file);

int verify_sign_message(verify_context_t *ctx, const void *data,
                        size_t data_len, message_auth_t *auth);

int verify_check_message(verify_context_t *ctx, const void *data,
                         size_t data_len, const message_auth_t *auth);

int verify_issue_token(verify_context_t *ctx, const char *service_name,
                       uint32_t permissions, uint32_t validity_seconds,
                       service_token_t *token);

int verify_check_token(verify_context_t *ctx, const service_token_t *token);

int verify_token_has_permission(const service_token_t *token,
                                service_permissions_t permission);

int verify_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data,
                       size_t data_len, uint8_t *output);

int verify_hmac_compare(const uint8_t *hmac1, const uint8_t *hmac2);

uint64_t verify_get_timestamp(void);

uint32_t verify_generate_nonce(void);

void verify_set_strict_ordering(verify_context_t *ctx, int enable);

void verify_set_timestamp_check(verify_context_t *ctx, int enable);

int verify_rotate_key(verify_context_t *ctx, const uint8_t *new_key,
                      size_t new_key_len);

void verify_set_rate_limit(verify_context_t *ctx, uint32_t max_per_second);

void verify_cleanup(verify_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
