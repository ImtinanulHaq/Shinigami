/**
 * @file    proxy_event_loop.h
 * @brief   epoll-based event loop for the proxy io thread.
 *
 * ProxyEventLoop wraps epoll to monitor multiple file descriptors (the
 * service daemon socket, eventfds from SHM readers, health-check timers).
 * It is used internally by ServiceProxy::ioThreadFunc() and is not part of
 * the public API.
 *
 * Design:
 *   - A single epoll fd monitors all registered fds.
 *   - Each fd has an associated ProxyEventHandler callback.
 *   - The loop runs until stop() is called.
 *   - Events are dispatched synchronously inside run() — the caller is
 *     responsible for posting actual work to ProxyThreadPool.
 *
 * @thread_safety
 *   run()  — must be called from one thread only (the io thread).
 *   stop() — safe from any thread.
 *   addFd() / removeFd() — must be called before run() or from the io thread.
 */

#pragma once

#include "proxy_result.h"
#include <functional>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace middleware {

/** @brief Bitmask of event types mirroring epoll(7) flags. */
enum class EventType : uint32_t {
    Read    = 0x001,  ///< EPOLLIN  — data available to read
    Write   = 0x004,  ///< EPOLLOUT — ready to write
    HangUp  = 0x010,  ///< EPOLLHUP — hang-up on fd
    Error   = 0x008,  ///< EPOLLERR — error condition
};

/** @brief Callback type for epoll events. */
using ProxyEventHandler = std::function<void(int fd, uint32_t events)>;

/**
 * @brief epoll-based event loop used by the proxy io thread.
 */
class ProxyEventLoop {
public:
    /**
     * @brief Construct the event loop (creates the epoll fd).
     * @throws std::runtime_error if epoll_create1 fails.
     */
    ProxyEventLoop();
    ~ProxyEventLoop();

    ProxyEventLoop(const ProxyEventLoop&)            = delete;
    ProxyEventLoop& operator=(const ProxyEventLoop&) = delete;

    /**
     * @brief Register a file descriptor with an event handler.
     *
     * @param fd       File descriptor to monitor.
     * @param events   Bitmask of EventType values OR'd together.
     * @param handler  Callback invoked when events fire on @p fd.
     * @return         Result<void>::ok() on success.
     */
    Result<void> addFd(int fd, uint32_t events, ProxyEventHandler handler);

    /**
     * @brief Deregister a file descriptor.
     *
     * Safe to call even if @p fd was never added (no-op).
     */
    void removeFd(int fd) noexcept;

    /**
     * @brief Run the event loop until stop() is called.
     *
     * Blocks the calling thread.  Events are dispatched inline inside this
     * call.  Each handler must return quickly — any heavy work should be
     * posted to ProxyThreadPool.
     *
     * @param timeout_ms  epoll_wait timeout in milliseconds.
     *                    -1 = block indefinitely until event or stop().
     */
    void run(int timeout_ms = -1);

    /**
     * @brief Signal the event loop to stop after the current dispatch round.
     *
     * Thread-safe.  The run() call will return within one timeout cycle.
     */
    void stop() noexcept;

    /** @return true if the loop is currently running. */
    [[nodiscard]] bool isRunning() const noexcept;

private:
    int                                          epoll_fd_;
    int                                          wake_fd_;    ///< eventfd for stop()
    std::atomic<bool>                            running_;
    std::unordered_map<int, ProxyEventHandler>   handlers_;
};

} // namespace middleware
