#ifndef VERIFY_HMAC_H
#define VERIFY_HMAC_H

#include <stdint.h>
#include <stddef.h>

#define HMAC_SHA256_SIZE 32

int hmac_sign(const uint8_t* key, size_t key_len,
              const uint8_t* data, size_t data_len,
              uint8_t* output);

int hmac_verify(const uint8_t* key, size_t key_len,
                const uint8_t* data, size_t data_len,
                const uint8_t* expected_hmac);

int hmac_compare(const uint8_t* hmac1, const uint8_t* hmac2);

#endif
