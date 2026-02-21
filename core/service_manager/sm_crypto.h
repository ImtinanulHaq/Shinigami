#ifndef SM_CRYPTO_H
#define SM_CRYPTO_H

/*
 * sm_crypto.h - Message authentication via HMAC-SHA256.
 *
 * Provides integrity and replay protection for every IPC message.
 * The shared key is stored in SM_KEY_FILE (mode 0640), readable only by
 * the servicemanager group.  SHA-256 and HMAC are implemented without any
 * external crypto library.
 *
 * Initialization order:
 *   sm_crypto_init()       -- BEFORE sm_drop_privileges() and seccomp
 *   (use key at runtime)
 *   sm_crypto_cleanup()    -- at shutdown
 */

#include <stdint.h>
#include <stddef.h>

/* HMAC-SHA256 digest size */
#define SM_HMAC_SIZE       32

/* Key length in bytes (sourced from /dev/urandom on first run) */
#define SM_HMAC_KEY_SIZE   32

/* Key file path and permissions */
#define SM_KEY_FILE        "/run/servicemanager.key"
#define SM_KEY_FILE_MODE   0640

/*
 * sm_crypto_init() - Load or generate the HMAC key.
 *
 * If SM_KEY_FILE exists and contains exactly SM_HMAC_KEY_SIZE bytes, it is
 * loaded.  Otherwise a new key is generated from /dev/urandom and written to
 * SM_KEY_FILE with SM_KEY_FILE_MODE permissions.
 *
 * Must be called once at startup, before privilege drop and seccomp.
 * Returns 0 on success, -1 on error.
 */
int sm_crypto_init(void);

/*
 * sm_hmac_sha256() - Compute HMAC-SHA256.
 *
 * Computes the MAC of 'data_len' bytes at 'data' using 'key' and writes
 * exactly SM_HMAC_SIZE bytes into 'out'.
 * Returns 0 on success, -1 if any pointer argument is NULL.
 */
int sm_hmac_sha256(const uint8_t* key,  size_t key_len,
                   const uint8_t* data, size_t data_len,
                   uint8_t        out[SM_HMAC_SIZE]);

/*
 * sm_hmac_verify() - Verify HMAC using constant-time comparison.
 *
 * Computes the MAC of 'data' and compares it to 'expected' without
 * short-circuiting.  This prevents timing side-channel attacks.
 * Returns 0 if the digest matches, -1 otherwise.
 */
int sm_hmac_verify(const uint8_t* key,      size_t key_len,
                   const uint8_t* data,     size_t data_len,
                   const uint8_t  expected[SM_HMAC_SIZE]);

/*
 * sm_crypto_get_key() - Return a pointer to the loaded key.
 *
 * Returns the global key buffer (SM_HMAC_KEY_SIZE bytes) if sm_crypto_init()
 * succeeded, or NULL if the key has not been loaded.
 * The returned buffer must not be freed or modified by the caller.
 */
const uint8_t* sm_crypto_get_key(void);

/*
 * sm_crypto_cleanup() - Zeroize key material from memory.
 *
 * Should be called during shutdown to reduce the window in which the key
 * exists in process memory.
 */
void sm_crypto_cleanup(void);

#endif /* SM_CRYPTO_H */