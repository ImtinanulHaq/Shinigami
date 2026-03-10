/**
 * @file    proxy_auth.cpp
 * @brief   ProxyAuth implementation — wraps verify.h with RAII & C++ types.
 */

#include "proxy_auth.h"

// Pull in the C security module headers.
extern "C" {
#  include "security/verify/verify.h"
}

#include <cstring>   // memset, explicit_bzero
#include <cstdlib>   // malloc, free

namespace middleware {

ProxyAuth::ProxyAuth()
    : ctx_(nullptr), ready_(false)
{}

ProxyAuth::~ProxyAuth() {
    if (ctx_) {
        verify_cleanup(ctx_);
        // Wipe the context before freeing — key material must not linger.
        ::explicit_bzero(ctx_, sizeof(verify_context_t));
        ::free(ctx_);
        ctx_ = nullptr;
    }
    ready_ = false;
}

ProxyAuth::ProxyAuth(ProxyAuth&& o) noexcept
    : ctx_(o.ctx_), ready_(o.ready_)
{
    o.ctx_   = nullptr;
    o.ready_ = false;
}

ProxyAuth& ProxyAuth::operator=(ProxyAuth&& o) noexcept {
    if (this != &o) {
        if (ctx_) {
            verify_cleanup(ctx_);
            ::explicit_bzero(ctx_, sizeof(verify_context_t));
            ::free(ctx_);
        }
        ctx_   = o.ctx_;
        ready_ = o.ready_;
        o.ctx_   = nullptr;
        o.ready_ = false;
    }
    return *this;
}

Result<void> ProxyAuth::init(std::string_view key_file) {
    // Allocate context on heap so we fully control its lifetime & zeroing.
    ctx_ = static_cast<verify_context_t*>(
                ::malloc(sizeof(verify_context_t)));
    if (!ctx_) {
        return Result<void>::err(ProxyError::InternalError,
                                 "Failed to allocate verify_context_t");
    }
    ::memset(ctx_, 0, sizeof(verify_context_t));

    int rc;
    if (key_file.empty()) {
        // No HMAC key supplied — dev / test mode.
        // Initialise with a dummy zero key so the context is valid.
        const uint8_t zero_key[32] = {};
        rc = verify_init(ctx_, zero_key, sizeof(zero_key));
    } else {
        // key_file path must be NUL-terminated — make a copy.
        std::string path(key_file);
        rc = verify_init_from_file(ctx_, path.c_str());
        // Wipe the path string so it does not linger on the stack.
        ::explicit_bzero(path.data(), path.size());
    }

    if (rc != 0) {
        ::explicit_bzero(ctx_, sizeof(verify_context_t));
        ::free(ctx_);
        ctx_ = nullptr;
        return Result<void>::err(ProxyError::AuthFailed,
                                 "verify_init failed (bad key file?)");
    }

    ready_ = true;
    return Result<void>::ok();
}

Result<void> ProxyAuth::sign(const void* data, size_t len,
                              message_auth_t* auth_out)
{
    if (!ready_ || !ctx_) {
        // No key loaded — no-op; fill auth_out with zeros.
        if (auth_out) ::memset(auth_out, 0, sizeof(*auth_out));
        return Result<void>::ok();
    }

    int rc = verify_sign_message(ctx_, data, len, auth_out);
    if (rc != 0) {
        return Result<void>::err(ProxyError::AuthFailed,
                                 "verify_sign_message failed");
    }
    return Result<void>::ok();
}

bool ProxyAuth::check(const void* data, size_t len,
                      const message_auth_t* auth)
{
    if (!ready_ || !ctx_) {
        // Dev/test mode — permit everything.
        return true;
    }
    return verify_check_message(ctx_, data, len, auth) == 0;
}

bool ProxyAuth::isReady() const noexcept {
    return ready_;
}

} // namespace middleware
