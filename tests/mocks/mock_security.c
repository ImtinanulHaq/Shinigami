/**
 * @file mock_security.c
 * @brief Security stubs implementation.
 */
#include "mock_security.h"
#include <string.h>

static mock_security_state_t g_state;

void mock_security_reset(mock_security_state_t *s)
{
    memset(s, 0, sizeof(*s));
    /* Default: all checks pass */
}

mock_security_state_t *mock_security_get_state(void)
{
    return &g_state;
}

int mock_apply_sandbox(void)
{
    g_state.apply_sandbox_calls++;
    return g_state.sandbox_ret;
}

int mock_check_capabilities(void)
{
    g_state.check_caps_calls++;
    return g_state.caps_ret;
}

int mock_apply_seccomp(void)
{
    g_state.apply_seccomp_calls++;
    return g_state.seccomp_ret;
}

int mock_verify_hmac(const void *msg, size_t len,
                     const uint8_t *key, size_t klen,
                     const uint8_t *mac)
{
    (void)msg; (void)len; (void)key; (void)klen; (void)mac;
    g_state.verify_hmac_calls++;
    return g_state.hmac_ret;
}
