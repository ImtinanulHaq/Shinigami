/**
 * @file    proxy_thread_pool.cpp
 * @brief   ProxyThreadPool — lock-free MPSC callback dispatch implementation.
 */

#include "proxy_thread_pool.h"
#include <bit>       // std::bit_ceil (C++20 fallback below)
#include <chrono>
#include <cstddef>

namespace middleware {

// Portable next power-of-two (C++17 compatible).
static size_t nextPow2(size_t n) {
    if (n == 0) return 1;
    --n;
    n |= n >> 1;  n |= n >> 2;  n |= n >> 4;
    n |= n >> 8;  n |= n >> 16; n |= n >> 32;
    return n + 1;
}

// ── Construction / destruction ───────────────────────────────────────────

ProxyThreadPool::ProxyThreadPool(size_t queue_depth, int /*thread_count*/)
{
    // Enforce minimum depth and round to power-of-two.
    const size_t cap = nextPow2(queue_depth < 16 ? 16 : queue_depth);
    mask_  = cap - 1;
    slots_ = std::make_unique<Slot[]>(cap);
    for (size_t i = 0; i < cap; ++i) {
        slots_[i].ready.store(false, std::memory_order_relaxed);
    }
    running_.store(true, std::memory_order_release);
    consumer_thread_ = std::thread(&ProxyThreadPool::consumerLoop, this);
}

ProxyThreadPool::~ProxyThreadPool() {
    stop();
}

// ── Public interface ───────────────────────────────────────────────────

void ProxyThreadPool::setOverflowHandler(std::function<void(ProxyError)> h) {
    overflow_handler_ = std::move(h);
}

void ProxyThreadPool::post(std::function<void()> fn) {
    // Atomically claim the next head slot.
    uint64_t idx = head_.fetch_add(1, std::memory_order_acq_rel);
    Slot& slot   = slots_[idx & mask_];

    // If the slot is still occupied by an unprocessed callback (queue full),
    // spin-wait briefly then drop the oldest entry.
    // This keeps the post() call bounded-time rather than truly wait-free,
    // but in practice the consumer runs much faster than the producers.
    int spin = 0;
    while (slot.ready.load(std::memory_order_acquire)) {
        if (++spin > 64) {
            // Drop the slot's stale callback and fire overflow handler.
            slot.fn   = {};
            slot.ready.store(false, std::memory_order_release);
            if (overflow_handler_) {
                overflow_handler_(ProxyError::BufferFull);
            }
            break;
        }
        std::this_thread::yield();
    }

    slot.fn = std::move(fn);
    slot.ready.store(true, std::memory_order_release);
}

size_t ProxyThreadPool::pending() const noexcept {
    uint64_t h = head_.load(std::memory_order_acquire);
    uint64_t t = tail_.load(std::memory_order_acquire);
    return (h > t) ? static_cast<size_t>(h - t) : 0;
}

void ProxyThreadPool::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    // Post a sentinel no-op to unblock the consumer if it is sleeping.
    post([] {});

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
}

// ── Consumer loop ──────────────────────────────────────────────────────

void ProxyThreadPool::consumerLoop() {
    while (running_.load(std::memory_order_acquire) || pending() > 0) {
        uint64_t t    = tail_.load(std::memory_order_relaxed);
        Slot&    slot = slots_[t & mask_];

        if (!slot.ready.load(std::memory_order_acquire)) {
            // Nothing to consume — yield and retry.
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        // Invoke callback and advance tail.
        auto fn = std::move(slot.fn);
        slot.fn = {};
        slot.ready.store(false, std::memory_order_release);
        tail_.fetch_add(1, std::memory_order_release);

        if (fn) {
            try { fn(); } catch (...) { /* callbacks must not throw */ }
        }
    }
}

} // namespace middleware
