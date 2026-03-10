/**
 * @file    proxy_thread_pool.h
 * @brief   Lock-free MPSC callback dispatcher thread pool.
 *
 * The io thread(s) produce callbacks via post(); the single consumer thread
 * drains them in FIFO order and calls each function.
 *
 * Design guarantees:
 *  - post() never blocks (wait-free producer).
 *  - Callbacks of the same type are always dispatched in submission order.
 *  - If the queue is full, the oldest entry is silently dropped and an error
 *    callback is fired via the overflow_handler set by setOverflowHandler().
 *  - callback_thread_count == 1 (default) guarantees total ordering.
 *
 * @thread_safety
 *   post() — safe from any thread (multiple producers allowed).
 *   All other methods — call from the owning thread only (setup/teardown).
 */

#pragma once

#include "proxy_result.h"
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>

namespace middleware {

/**
 * @brief Lock-free MPSC (multiple-producer single-consumer) callback dispatcher.
 *
 * Internally uses a power-of-two ring of std::function<void()> slots guarded
 * by a ticket-based head/tail pair.  Producers claim a slot with an atomic
 * CAS; the consumer advances the tail when the slot is marked ready.
 */
class ProxyThreadPool {
public:
    /**
     * @brief Construct and start the dispatcher thread.
     *
     * @param queue_depth   Size of the circular callback queue (rounded up to
     *                      next power of two, minimum 16).
     * @param thread_count  Number of consumer threads (currently only 1 is
     *                      fully supported for strict in-order delivery).
     */
    explicit ProxyThreadPool(size_t queue_depth  = 256,
                             int    thread_count  = 1);
    ~ProxyThreadPool();

    // Non-copyable, non-movable — owns background thread.
    ProxyThreadPool(const ProxyThreadPool&)            = delete;
    ProxyThreadPool& operator=(const ProxyThreadPool&) = delete;

    /**
     * @brief Enqueue a callback for dispatch on the consumer thread.
     *
     * This method is WAIT-FREE for the producer.  If the queue is full,
     * the oldest pending callback is dropped and overflow_handler_ is called
     * (if set) with ProxyError::BufferFull.
     *
     * @param fn  Callable to dispatch.  Moved into the queue slot.
     *
     * @thread_safety  Safe from any thread.
     */
    void post(std::function<void()> fn);

    /**
     * @brief Set the handler invoked when a callback is dropped (queue full).
     *
     * @param h  Callable receiving the overflow error code.
     */
    void setOverflowHandler(std::function<void(ProxyError)> h);

    /**
     * @brief Gracefully stop the consumer thread.
     *
     * Drains the queue before returning.  Blocks until consumer exits.
     * Safe to call multiple times (no-op after first call).
     */
    void stop();

    /** @return Current number of pending callbacks in the queue. */
    [[nodiscard]] size_t pending() const noexcept;

private:
    void consumerLoop();

    // ── Queue slot ───────────────────────────────────────────────────────
    struct Slot {
        std::function<void()>   fn;
        std::atomic<bool>       ready{false};
    };

    // ── State ────────────────────────────────────────────────────────────
    std::unique_ptr<Slot[]>          slots_;
    size_t                           mask_;       ///< capacity - 1 (bitmask)
    std::atomic<uint64_t>            head_{0};    ///< Producer claim index
    std::atomic<uint64_t>            tail_{0};    ///< Consumer take index
    std::atomic<bool>                running_{false};
    std::function<void(ProxyError)>  overflow_handler_;
    std::thread                      consumer_thread_;
};

} // namespace middleware
