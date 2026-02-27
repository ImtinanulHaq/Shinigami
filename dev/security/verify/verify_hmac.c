#define _POSIX_C_SOURCE 200809L
#include "verify_hmac.h"
#include "sm_crypto.h"
#include <string.h>

int hmac_sign(const uint8_t* key, size_t key_len,
              const uint8_t* data, size_t data_len,
              uint8_t* output)
{

    return sm_hmac_sha256(key, key_len, data, data_len, output);
}

int hmac_verify(const uint8_t* key, size_t key_len,
                const uint8_t* data, size_t data_len,
                const uint8_t* expected_hmac)
{

    return sm_hmac_verify(key, key_len, data, data_len, expected_hmac);
}

int hmac_compare(const uint8_t* hmac1, const uint8_t* hmac2)
{

    uint8_t diff = 0;
    for (int i = 0; i < HMAC_SHA256_SIZE; i++) {
        diff |= (hmac1[i] ^ hmac2[i]);
    }
    return diff == 0 ? 1 : 0;
}
