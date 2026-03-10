/**
 * @file    proxy_auth.h
 * @brief   RAII wrapper around verify.h for the C++ proxy layer.
 *
 * ProxyAuth owns a verify_context_t, loads the HMAC key from disk, signs
 * outgoing messages, and validates incoming ones.  Key material is never
 * written to logs and is wiped with explicit_bzero on destruction.
 *
 * @thread_safety
 *   sign() and verify() are NOT thread-safe.  The caller (ProxyConnection)
 *   must serialise calls with its send/recv mutexes.
 */

#pragma once

#include "../base/proxy_result.h"
#include <cstdint>
#include <string>
#include <string_view>

// Include C verify types (message_auth_t is an anonymous struct typedef
// so it cannot be forward-declared).
#include "security/verify/verify.h"

namespace middleware {

/**
 * @brief RAII handle for HMAC-SHA256 message authentication.
 *
 * Typical lifecycle:
 * @code
 *   ProxyAuth auth;
 *   auto r = auth.init("/etc/middleware/proxy.key");
 *   if (r.isErr()) { ... }
 *
 *   message_auth_t sig;
 *   auth.sign(buf, len, &sig);
 *   // send buf + sig ...
 *   if (!auth.check(buf, len, &received_sig)) { disconnect(); }
 * @endcode
 */
class ProxyAuth {
public:
    ProxyAuth();
    ~ProxyAuth();

    // Non-copyable, movable.
    ProxyAuth(const ProxyAuth&)            = delete;
    ProxyAuth& operator=(const ProxyAuth&) = delete;
    ProxyAuth(ProxyAuth&& o) noexcept;
    ProxyAuth& operator=(ProxyAuth&& o) noexcept;

    /**
     * @brief Load HMAC key from file and initialise the verify context.
     *
     * @param key_file  Path to raw binary key file (16–64 bytes).
     *                  Pass an empty string to initialise without key
     *                  (HMAC is disabled; every sign() is a no-op and
     *                  check() always returns true — dev/test mode only).
     *
     * @return  Result<void>::ok() on success, or an error.
     *
     * @note  key_file contents are NEVER logged.
     */
    Result<void> init(std::string_view key_file);

    /**
     * @brief Sign a message buffer.
     *
     * Fills @p auth_out with the HMAC-SHA256 signature, current timestamp,
     * and a fresh nonce.
     *
     * @param data      Message bytes to sign.
     * @param len       Number of bytes to cover.
     * @param auth_out  Output: filled by verify_sign_message().
     * @return  Result<void>::ok() on success.
     *
     * @thread_safety  Not safe to call concurrently from multiple threads.
     */
    Result<void> sign(const void* data, size_t len,
                      message_auth_t* auth_out);

    /**
     * @brief Check a received message signature.
     *
     * @param data  Message bytes that were signed.
     * @param len   Number of signed bytes.
     * @param auth  Auth block received with the message.
     * @return  true if valid, false if HMAC mismatch / replay / expired.
     *
     * @thread_safety  Not safe to call concurrently from multiple threads.
     */
    bool check(const void* data, size_t len,
               const message_auth_t* auth);

    /** @return true if a key has been successfully loaded. */
    [[nodiscard]] bool isReady() const noexcept;

private:
    struct verify_context* ctx_; ///< Opaque C context (heap-allocated).
    bool                   ready_;
};

} // namespace middleware
