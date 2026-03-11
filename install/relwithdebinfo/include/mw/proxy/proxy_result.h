/**
 * @file    proxy_result.h
 * @brief   Result<T, ProxyError> monad — error handling without exceptions.
 *
 * Every public proxy method that can fail returns Result<T, ProxyError>.
 * No raw integers, no thrown exceptions from normal I/O paths.
 *
 * Usage:
 * @code
 *   auto r = audio.startCapture();
 *   if (r.isErr()) { log(r.detail()); return; }
 *
 *   // Monadic chaining:
 *   auto volume = audio.getVolume()
 *       .andThen([](float v) { return Result<float>::ok(v * 2.0f); });
 * @endcode
 *
 * @thread_safety  Result<T> objects are immutable after construction.
 *                 Their methods are safe to call from any thread.
 */

#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cassert>

namespace middleware {

// ─── Error catalog ─────────────────────────────────────────────────────────

/**
 * @brief Enumeration of all possible proxy error conditions.
 */
enum class ProxyError {
    None = 0,
    NotConnected,       ///< Operation called before connect()
    AlreadyConnected,   ///< connect() called when already connected
    ConnectionRefused,  ///< Daemon not running or socket not found
    ConnectionTimeout,  ///< connect() timed out
    AuthFailed,         ///< HMAC token validation failed
    ServiceNotFound,    ///< SM registry does not have the requested service
    ShmCreateFailed,    ///< shm_open / mmap failed
    ShmSizeMismatch,    ///< Daemon and proxy disagree on SHM layout / magic
    SendFailed,         ///< write() to socket failed
    RecvFailed,         ///< read() from socket failed
    InvalidResponse,    ///< Response message failed protocol validation
    Timeout,            ///< Operation timed out waiting for response
    DaemonCrashed,      ///< Socket closed unexpectedly
    BufferFull,         ///< Proxy-side callback queue is full
    InvalidArgument,    ///< Caller passed a bad parameter
    PermissionDenied,   ///< Process lacks permission for this operation
    InternalError,      ///< Should never happen — log immediately if seen
};

/** @brief Return a human-readable name for a ProxyError value. */
inline std::string_view proxyErrorName(ProxyError e) noexcept {
    switch (e) {
    case ProxyError::None:              return "None";
    case ProxyError::NotConnected:      return "NotConnected";
    case ProxyError::AlreadyConnected:  return "AlreadyConnected";
    case ProxyError::ConnectionRefused: return "ConnectionRefused";
    case ProxyError::ConnectionTimeout: return "ConnectionTimeout";
    case ProxyError::AuthFailed:        return "AuthFailed";
    case ProxyError::ServiceNotFound:   return "ServiceNotFound";
    case ProxyError::ShmCreateFailed:   return "ShmCreateFailed";
    case ProxyError::ShmSizeMismatch:   return "ShmSizeMismatch";
    case ProxyError::SendFailed:        return "SendFailed";
    case ProxyError::RecvFailed:        return "RecvFailed";
    case ProxyError::InvalidResponse:   return "InvalidResponse";
    case ProxyError::Timeout:           return "Timeout";
    case ProxyError::DaemonCrashed:     return "DaemonCrashed";
    case ProxyError::BufferFull:        return "BufferFull";
    case ProxyError::InvalidArgument:   return "InvalidArgument";
    case ProxyError::PermissionDenied:  return "PermissionDenied";
    case ProxyError::InternalError:     return "InternalError";
    }
    return "Unknown";
}

// ─── Primary template ───────────────────────────────────────────────────────

/**
 * @brief Discriminated union holding either a value T or a ProxyError.
 *
 * @tparam T  The success value type.  Must be default-constructible or
 *            move-constructible.  Use Result<void> for operations that
 *            succeed or fail with no return value.
 *
 * @note  Value access via value() asserts isOk() in debug builds (NDEBUG not
 *        defined).  In release builds the assertion is removed; accessing the
 *        value of an error Result is undefined behaviour — guard with isOk().
 */
template<typename T>
class Result {
public:
    // ── Construction helpers ──────────────────────────────────────────────

    /**
     * @brief Construct a successful Result holding @p val.
     * @param val  Value to wrap (moved in).
     */
    static Result<T> ok(T val) {
        Result<T> r;
        r.ok_    = true;
        r.value_ = std::move(val);
        return r;
    }

    /**
     * @brief Construct a failed Result.
     * @param err     The error code.
     * @param detail  Optional human-readable description.
     */
    static Result<T> err(ProxyError err,
                         std::string_view detail = "") {
        Result<T> r;
        r.ok_     = false;
        r.error_  = err;
        r.detail_ = std::string(detail);
        return r;
    }

    // ── Observers ────────────────────────────────────────────────────────

    /** @return true if this Result holds a success value. */
    [[nodiscard]] bool isOk()  const noexcept { return  ok_; }

    /** @return true if this Result holds an error. */
    [[nodiscard]] bool isErr() const noexcept { return !ok_; }

    /**
     * @brief Access the success value.
     * @pre   isOk() must be true (asserted in debug builds).
     * @return Reference to the wrapped value.
     */
    T& value() {
        assert(ok_ && "Result::value() called on error Result");
        return value_;
    }
    const T& value() const {
        assert(ok_ && "Result::value() called on error Result");
        return value_;
    }

    /** @return The error code (ProxyError::None if isOk()). */
    [[nodiscard]] ProxyError error() const noexcept { return error_; }

    /** @return Human-readable detail string (may be empty). */
    [[nodiscard]] std::string detail() const noexcept { return detail_; }

    // ── Monadic combinators ───────────────────────────────────────────────

    /**
     * @brief If ok, apply @p fn to the value and return its Result.
     *        If err, propagate the error unchanged.
     *
     * @tparam F  Callable with signature Result<U>(T).
     */
    template<typename F>
    auto andThen(F&& fn) -> decltype(fn(std::declval<T>())) {
        if (ok_) return std::forward<F>(fn)(value_);
        using U = typename decltype(fn(std::declval<T>()))::value_type;
        return Result<U>::err(error_, detail_);
    }

    /**
     * @brief If err, apply @p fn to this Result and return its result.
     *        If ok, return *this unchanged.
     *
     * @tparam F  Callable with signature Result<T>(ProxyError, std::string).
     */
    template<typename F>
    Result<T> orElse(F&& fn) {
        if (!ok_) return std::forward<F>(fn)(error_, detail_);
        return *this;
    }

    // Expose T for template meta-programming (used by andThen).
    using value_type = T;

private:
    Result() : ok_(false), error_(ProxyError::None) {}

    bool        ok_;
    T           value_{};
    ProxyError  error_;
    std::string detail_;
};

// ─── Void specialisation ───────────────────────────────────────────────────

/**
 * @brief Specialisation of Result for operations that return no value.
 *
 * Usage:
 * @code
 *   Result<void> r = Result<void>::ok();
 *   Result<void> e = Result<void>::err(ProxyError::Timeout, "timed out");
 * @endcode
 */
template<>
class Result<void> {
public:
    static Result<void> ok() {
        Result<void> r;
        r.ok_ = true;
        return r;
    }

    static Result<void> err(ProxyError e,
                             std::string_view detail = "") {
        Result<void> r;
        r.ok_     = false;
        r.error_  = e;
        r.detail_ = std::string(detail);
        return r;
    }

    [[nodiscard]] bool         isOk()   const noexcept { return  ok_; }
    [[nodiscard]] bool         isErr()  const noexcept { return !ok_; }
    [[nodiscard]] ProxyError   error()  const noexcept { return error_; }
    [[nodiscard]] std::string  detail() const noexcept { return detail_; }

    template<typename F>
    auto andThen(F&& fn) -> decltype(fn()) {
        if (ok_) return std::forward<F>(fn)();
        using U = typename decltype(fn())::value_type;
        return Result<U>::err(error_, detail_);
    }

    template<typename F>
    Result<void> orElse(F&& fn) {
        if (!ok_) return std::forward<F>(fn)(error_, detail_);
        return *this;
    }

    using value_type = void;

private:
    Result() : ok_(false), error_(ProxyError::None) {}
    bool        ok_;
    ProxyError  error_;
    std::string detail_;
};

// ─── Convenience macro ─────────────────────────────────────────────────────

/**
 * @brief Early-return helper.  Inside a function returning Result<T>:
 * @code
 *   PROXY_TRY(someFunction());   // returns on error
 * @endcode
 */
#define PROXY_TRY(expr)                                  \
    do {                                                 \
        auto _r = (expr);                                \
        if (_r.isErr())                                  \
            return ::middleware::Result<void>::err(      \
                _r.error(), _r.detail());                \
    } while (0)

} // namespace middleware
