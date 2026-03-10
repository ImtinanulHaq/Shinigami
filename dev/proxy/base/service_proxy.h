/**
 * @file    service_proxy.h
 * @brief   Abstract base class for all concrete proxy types.
 *
 * ServiceProxy owns:
 *  - A ProxyConnection (socket + HMAC framing)
 *  - A ProxyThreadPool (callback dispatcher thread)
 *  - A ServiceDiscovery helper (SM registry query)
 *  - An io thread that runs an epoll event loop
 *
 * Concrete classes (AudioProxy, CameraProxy, SensorProxy, GpioProxy)
 * inherit from ServiceProxy and implement onConnected() and
 * onDisconnected().  They send control messages via the protected
 * sendMessage() / sendRequestWaitReply() helpers.
 *
 * @note
 *   ServiceProxy's destructor calls disconnect().  Subclasses that hold
 *   additional resources (SHM readers, etc.) must release them in their
 *   own destructor BEFORE the base destructor runs, or in onDisconnected().
 *
 * @thread_safety
 *   connect() / disconnect()  — thread-safe (guarded internally).
 *   isConnected()             — thread-safe (atomic).
 *   onConnectionLost/Restored — must be called during setup, before connect().
 *   postCallback()            — thread-safe (MPSC queue).
 *   Protected send helpers    — thread-safe for send, io-thread only for recv.
 */

#pragma once

#include "proxy_result.h"
#include "proxy_config.h"
#include "proxy_connection.h"
#include "proxy_thread_pool.h"
#include "proxy_event_loop.h"
#include "../discovery/service_discovery.h"
#include "../security/proxy_auth.h"

#include <string>
#include <string_view>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>

namespace middleware {

// ── PROXY_API visibility macro ────────────────────────────────────────────

#if defined(__GNUC__) || defined(__clang__)
#  define PROXY_API __attribute__((visibility("default")))
#else
#  define PROXY_API
#endif

// ── ServiceProxy ──────────────────────────────────────────────────────────

/**
 * @brief Abstract base class for all middleware service proxies.
 *
 * Subclass example:
 * @code
 *   class AudioProxy : public ServiceProxy {
 *   public:
 *       explicit AudioProxy(ProxyConfig cfg = {})
 *           : ServiceProxy("audio_service", std::move(cfg)) {}
 *   protected:
 *       Result<void> onConnected()    override { ... }
 *       void         onDisconnected() override { ... }
 *   };
 * @endcode
 */
class PROXY_API ServiceProxy {
public:
    // ── Connection lifecycle ────────────────────────────────────────────

    /**
     * @brief Connect to the service daemon synchronously.
     *
     * If config.service_socket_path is empty, performs SM discovery first.
     * Blocks for up to config.connect_timeout.
     *
     * @return Result<void>::ok() once connected and authenticated.
     *
     * @thread_safety  Safe to call from any thread.
     */
    Result<void> connect();

    /**
     * @brief Connect asynchronously; invoke @p cb on completion or failure.
     *
     * Returns immediately.  @p cb is invoked on the callback thread.
     *
     * @thread_safety  Safe to call from any thread.
     */
    void connectAsync(std::function<void(Result<void>)> cb);

    /**
     * @brief Disconnect from the service daemon and stop background threads.
     *
     * Idempotent — safe to call when already disconnected.
     *
     * @thread_safety  Safe to call from any thread.
     */
    void disconnect();

    /**
     * @brief Query connection state.
     * @return true if the socket is connected and the auth handshake passed.
     *
     * @thread_safety  Thread-safe (atomic load).
     */
    [[nodiscard]] bool isConnected() const noexcept;

    // ── Event callbacks ─────────────────────────────────────────────────

    /**
     * @brief Set handler invoked when the daemon connection drops.
     *
     * Called on the callback thread.  The error argument describes the cause.
     * After this callback returns, the proxy will attempt to reconnect
     * automatically (if config.reconnect_max_attempts != 0).
     *
     * @param cb  Callback.  Pass nullptr to clear.
     *
     * @note  Must be set before connect() to avoid race conditions.
     */
    void onConnectionLost(std::function<void(ProxyError)> cb);

    /**
     * @brief Set handler invoked when the daemon connection is restored.
     *
     * Called on the callback thread after a successful reconnect.
     *
     * @param cb  Callback.  Pass nullptr to clear.
     */
    void onConnectionRestored(std::function<void()> cb);

    /**
     * @brief Set handler invoked on non-fatal protocol / queue errors.
     *
     * @param cb  Callback receiving error code + detail string.
     */
    void onError(std::function<void(ProxyError, std::string_view)> cb);

protected:
    /**
     * @brief Construct a service proxy for the named service.
     *
     * @param service_name  Registered name used for SM discovery
     *                      (e.g. "audio_service").
     * @param config        Configuration — copied into base.
     */
    explicit ServiceProxy(std::string_view service_name,
                          ProxyConfig      config = {});

    virtual ~ServiceProxy();

    // ── Subclass helpers ────────────────────────────────────────────────

    /**
     * @brief Send a fire-and-forget message to the daemon.
     *
     * @thread_safety  Thread-safe (serialised by send_mutex in ProxyConnection).
     */
    Result<void> sendMessage(uint16_t type,
                             const void* payload, uint32_t plen);

    /**
     * @brief Send a request and block until its reply arrives or timeout.
     *
     * Internally uses a condition_variable to wait for the matching reply
     * from the io thread.
     *
     * @thread_safety  NOT safe to call from the io thread (deadlock).
     */
    Result<ProxyMessage> sendRequestWaitReply(uint16_t type,
                                              const void* payload,
                                              uint32_t    plen);

    /**
     * @brief Post a callable to the callback dispatcher thread.
     *
     * @thread_safety  Thread-safe (MPSC queue).
     */
    void postCallback(std::function<void()> fn);

    // ── Subclass must implement ─────────────────────────────────────────

    /**
     * @brief Called after socket connection and HMAC handshake succeed.
     *
     * Subclasses perform service-specific setup here (open SHM, start
     * capture, etc.).  Called from the io thread.
     *
     * @return Result<void>::ok() to accept the connection; error will trigger
     *         disconnect + reconnect.
     */
    virtual Result<void> onConnected() = 0;

    /**
     * @brief Called when the connection is lost (error or explicit disconnect).
     *
     * Called from the io thread.  Subclasses should release service-specific
     * resources (e.g. close SHM reader) here.
     */
    virtual void onDisconnected() = 0;

    // ── Protected state available to subclasses ─────────────────────────

    ProxyConfig  config_;         ///< Copy of the configuration.
    std::string  service_name_;   ///< Service name used for SM discovery.

    /** @brief Release a ProxyMessage buffer back to the memory pool. */
    void releaseBuffer(ProxyMessage& msg);

    /** @brief Access the callback dispatcher thread pool. */
    ProxyThreadPool& threadPool() noexcept;

    /** @brief SHM / polling loop called on a dedicated shm_thread_.
     *  Default implementation does nothing; override in subclasses
     *  that need periodic SHM polling (e.g. CameraProxy, GpioProxy). */
    virtual void ioLoop() {}

private:
    // ── Background thread functions ──────────────────────────────────────
    void ioThreadFunc();
    void startReconnectLoop(std::chrono::milliseconds current_backoff);

    // ── Internal helpers ─────────────────────────────────────────────────
    Result<void> doConnect();
    void         doDisconnect(ProxyError reason);
    void         onSocketEvent(int fd, uint32_t epoll_ev);

    // ── State ─────────────────────────────────────────────────────────────
    std::unique_ptr<ProxyAuth>        auth_;
    std::unique_ptr<ProxyConnection>  connection_;
    std::unique_ptr<ProxyThreadPool>  callback_pool_;
    std::unique_ptr<ProxyEventLoop>   event_loop_;
    std::unique_ptr<ServiceDiscovery> discovery_;

    std::atomic<bool>  connected_{false};
    std::atomic<bool>  stopping_{false};
    std::atomic<bool>  reconnecting_{false};

    std::thread  io_thread_;
    std::thread  shm_thread_;    ///< Runs ioLoop() for SHM-based subclasses.
    std::mutex   connect_mutex_;

    // Pending synchronous request support.
    struct PendingReply {
        ProxyMessage       msg;
        bool               ready{false};
        std::mutex         mu;
        std::condition_variable cv;
    };
    PendingReply pending_reply_;

    // User-supplied lifecycle callbacks.
    std::function<void(ProxyError)>          on_lost_cb_;
    std::function<void()>                    on_restored_cb_;
    std::function<void(ProxyError, std::string_view)> on_error_cb_;
    std::mutex                               cb_mutex_;  ///< Protects above three
};

} // namespace middleware
