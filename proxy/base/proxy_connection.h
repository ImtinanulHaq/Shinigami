/**
 * @file    proxy_connection.h
 * @brief   Internal Unix-socket + HMAC connection manager.
 *
 * ProxyConnection owns the raw socket fd, the ProxyAuth context, a send
 * mutex (so the caller never has to serialise writes itself), and a
 * memory-pool-backed receive buffer.  It is used exclusively by
 * ServiceProxy — application code never touches it directly.
 *
 * Protocol framing:
 *   [sm_hdr_t | payload bytes]
 * Every message is protected by an HMAC-SHA256 over the header bytes 0..23
 * plus the payload, using the key loaded at construction time.
 *
 * @thread_safety
 *   send()  – protected by internal send_mutex_.  Safe from any thread.
 *   recv()  – NOT thread-safe.  Must be called from the io thread only.
 *   connect / disconnect – NOT thread-safe.  Call from the io thread only.
 */

#pragma once

#include "proxy_result.h"
#include "proxy_config.h"

#include <cstdint>
#include <string>
#include <mutex>
#include <functional>

// Forward declarations — avoids exposing C headers in this C++ header.
struct memory_pool;
struct sm_hdr;

namespace middleware {

class ProxyAuth;

/**
 * @brief A single parsed message received from a daemon.
 *
 * The payload points into an internally-managed memory pool block.
 * It remains valid until the caller calls releaseBuffer() or until
 * ProxyConnection is destroyed — whichever comes first.
 */
struct ProxyMessage {
    uint16_t        type;           ///< SM_MSG_* constant from sm_protocol.h
    uint32_t        nonce;          ///< Per-message anti-replay nonce
    const uint8_t*  payload;        ///< Pointer into memory pool (may be null)
    uint32_t        payload_len;    ///< Payload size in bytes
    void*           pool_block;     ///< Opaque: pass to releaseBuffer()
};

/**
 * @brief Internal socket + HMAC + framing manager.
 *
 * Typical use (by ServiceProxy):
 * @code
 *   ProxyConnection conn(config);
 *   conn.setAuth(&auth);                   // optional — dev mode if null
 *   auto r = conn.connect("/tmp/svc.sock");
 *   if (r.isErr()) { ... }
 *
 *   auto sr = conn.sendRequest(req_hdr, payload, plen);
 *   if (sr.isErr()) { ... }
 *
 *   ProxyMessage reply{};
 *   auto rr = conn.recvMessage(reply);
 *   if (rr.isErr()) { ... }
 *   // ... use reply ...
 *   conn.releaseBuffer(reply);
 * @endcode
 */
class ProxyConnection {
public:
    /**
     * @brief Construct a connection manager.
     * @param config   Proxy configuration (timeouts, pool depth, verbose).
     */
    explicit ProxyConnection(const ProxyConfig& config);
    ~ProxyConnection();

    // Non-copyable, non-movable (owns fd and pool).
    ProxyConnection(const ProxyConnection&)            = delete;
    ProxyConnection& operator=(const ProxyConnection&) = delete;

    /**
     * @brief Attach the authentication helper (optional).
     *
     * Must be called before connect() if HMAC signing is desired.
     * If auth is null, messages are sent unsigned (dev/test only).
     *
     * @param auth  Initialised ProxyAuth; ownership stays with the caller.
     */
    void setAuth(ProxyAuth* auth) noexcept;

    /**
     * @brief Connect to a Unix domain socket and perform protocol handshake.
     *
     * Steps:
     *  1. socket(AF_UNIX, SOCK_STREAM, 0)
     *  2. setsockopt SO_RCVTIMEO / SO_SNDTIMEO from config.connect_timeout
     *  3. connect() to @p socket_path
     *  4. Set request-phase timeouts (config.request_timeout)
     *  5. Send PROXY_HELLO
     *  6. Receive and validate PROXY_WELCOME
     *
     * @param socket_path  Absolute path to the daemon's Unix socket.
     * @return             Result<void>::ok() on successful handshake.
     *
     * @thread_safety  Not safe to call concurrently with send/recv.
     */
    Result<void> connect(std::string_view socket_path);

    /**
     * @brief Gracefully close the socket.
     *
     * After disconnect() returns, isConnected() == false and no further
     * send() or recv() calls should be made until connect() is called again.
     *
     * @thread_safety  Not safe to call concurrently with send/recv.
     */
    void disconnect() noexcept;

    /** @return true if the socket fd is open and handshaked. */
    [[nodiscard]] bool isConnected() const noexcept;

    /**
     * @brief Send a message to the daemon.
     *
     * Builds a sm_hdr_t with @p type, fills timestamp/nonce/HMAC, then
     * uses writev() to send header + payload atomically.
     *
     * @param type     SM_MSG_* constant.
     * @param payload  Payload bytes (may be null if payload_len == 0).
     * @param plen     Number of payload bytes.
     * @return         Result<void>::ok() on success.
     *
     * @thread_safety  Protected by internal send_mutex_.  Safe from any thread.
     */
    Result<void> send(uint16_t type,
                      const void* payload, uint32_t plen);

    /**
     * @brief Receive one complete message from the daemon.
     *
     * Reads the fixed-size header, validates magic/version/HMAC, reads the
     * variable-length payload into a memory-pool block, and fills @p msg.
     *
     * If HMAC validation fails, the connection is closed immediately and
     * ProxyError::AuthFailed is returned (caller should trigger reconnect).
     *
     * @param msg   Output: filled on success.  Call releaseBuffer(msg)
     *              when finished to return the pool block.
     *
     * @return  Result<void>::ok() on success.
     *
     * @thread_safety  Not thread-safe.  Must be called from the io thread only.
     */
    Result<void> recvMessage(ProxyMessage& msg);

    /**
     * @brief Return the memory pool block owned by a received ProxyMessage.
     *
     * Must be called exactly once for each successful recvMessage() call.
     * After this call, msg.payload is invalid.
     *
     * @param msg  Message whose pool_block should be freed.
     */
    void releaseBuffer(ProxyMessage& msg) noexcept;

    /**
     * @brief Return the raw socket file descriptor.
     *
     * Used by the event loop to register the fd with io_uring/epoll.
     * Returns -1 if not connected.
     */
    [[nodiscard]] int fd() const noexcept;

private:
    // ── Helpers ─────────────────────────────────────────────────────────
    Result<void> setSocketTimeouts(int timeout_ms);
    Result<void> readExact(void* buf, size_t n);
    Result<void> writeExact(const void* buf, size_t n);
    Result<void> sendHandshakeHello();
    Result<void> recvHandshakeWelcome();

    // ── State ────────────────────────────────────────────────────────────
    const ProxyConfig&  config_;
    ProxyAuth*          auth_;          ///< Borrowed pointer, may be null.
    int                 fd_;            ///< Socket fd; -1 when disconnected.
    bool                connected_;
    uint32_t            seq_;           ///< Monotonic sequence counter.
    std::mutex          send_mutex_;    ///< Serialises all socket writes.
    struct memory_pool* recv_pool_;     ///< Pool for inbound message payloads.
};

} // namespace middleware
