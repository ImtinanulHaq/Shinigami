/**
 * @file mock_security.h / mock_security.c
 * @brief Controllable security module stubs.
 *
 * Replaces capabilities/seccomp/sandbox checks with no-op stubs so
 * service integration tests run without requiring root or specific
 * kernel capabilities.
 */
#ifndef MOCK_SECURITY_H
#define MOCK_SECURITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  apply_sandbox_calls;
    int  check_caps_calls;
    int  apply_seccomp_calls;
    int  verify_hmac_calls;

    /* What to return from each stub */
    int  sandbox_ret;
    int  caps_ret;
    int  seccomp_ret;
    int  hmac_ret;
} mock_security_state_t;

void mock_security_reset(mock_security_state_t *s);
mock_security_state_t *mock_security_get_state(void);

/* Drop-in stubs matching real security function signatures */
int mock_apply_sandbox(void);
int mock_check_capabilities(void);
int mock_apply_seccomp(void);
int mock_verify_hmac(const void *msg, size_t len,
                     const uint8_t *key, size_t klen,
                     const uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_SECURITY_H */
