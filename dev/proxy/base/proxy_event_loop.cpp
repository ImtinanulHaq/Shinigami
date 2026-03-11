/**
 * @file    proxy_event_loop.cpp
 * @brief   ProxyEventLoop implementation using epoll.
 */

#include "proxy_event_loop.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <cstdint>

namespace middleware {

// Map our EventType bitmask to epoll bitmask.
static uint32_t toEpollEvents(uint32_t ev) {
    uint32_t out = 0;
    if (ev & static_cast<uint32_t>(EventType::Read))   out |= EPOLLIN;
    if (ev & static_cast<uint32_t>(EventType::Write))  out |= EPOLLOUT;
    if (ev & static_cast<uint32_t>(EventType::HangUp)) out |= EPOLLHUP;
    if (ev & static_cast<uint32_t>(EventType::Error))  out |= EPOLLERR;
    return out;
}

// ── Construction / destruction ───────────────────────────────────────────

ProxyEventLoop::ProxyEventLoop()
    : epoll_fd_(-1), wake_fd_(-1), running_(false)
{
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll_create1 failed: " +
                                  std::string(::strerror(errno)));
    }

    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
        ::close(epoll_fd_); epoll_fd_ = -1;
        throw std::runtime_error("eventfd failed: " +
                                  std::string(::strerror(errno)));
    }

    // Register the wake fd so stop() can unblock epoll_wait().
    struct epoll_event ev{};
    ev.events   = EPOLLIN;
    ev.data.fd  = wake_fd_;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_fd_, &ev);
}

ProxyEventLoop::~ProxyEventLoop() {
    stop();
    if (wake_fd_  >= 0) { ::close(wake_fd_);  wake_fd_  = -1; }
    if (epoll_fd_ >= 0) { ::close(epoll_fd_); epoll_fd_ = -1; }
}

// ── addFd / removeFd ──────────────────────────────────────────────────────

Result<void> ProxyEventLoop::addFd(int fd, uint32_t events,
                                   ProxyEventHandler handler) {
    struct epoll_event ev{};
    ev.events   = toEpollEvents(events) | EPOLLET;
    ev.data.fd  = fd;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        // Try MOD in case it was already registered.
        if (errno == EEXIST) {
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
        } else {
            return Result<void>::err(ProxyError::InternalError,
                                     "epoll_ctl ADD failed");
        }
    }
    handlers_[fd] = std::move(handler);
    return Result<void>::ok();
}

void ProxyEventLoop::removeFd(int fd) noexcept {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    handlers_.erase(fd);
}

// ── run ───────────────────────────────────────────────────────────────────

void ProxyEventLoop::run(int timeout_ms) {
    running_.store(true, std::memory_order_release);

    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (running_.load(std::memory_order_acquire)) {
        int n = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);

        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;

            if (fd == wake_fd_) {
                // Drain the eventfd to re-arm it.
                uint64_t val;
                // GCC warns on unchecked read/write even with (void), use local var
                [[maybe_unused]] ssize_t rd = ::read(wake_fd_, &val, sizeof(val));  // best-effort drain
                running_.store(false, std::memory_order_release);
                break;
            }

            auto it = handlers_.find(fd);
            if (it != handlers_.end() && it->second) {
                it->second(fd, events[i].events);
            }
        }
    }

    running_.store(false, std::memory_order_release);
}

// ── stop ─────────────────────────────────────────────────────────────────

void ProxyEventLoop::stop() noexcept {
    if (wake_fd_ >= 0) {
        uint64_t val = 1;
        [[maybe_unused]] ssize_t wr = ::write(wake_fd_, &val, sizeof(val));  // best-effort wakeup
    }
    running_.store(false, std::memory_order_release);
}

bool ProxyEventLoop::isRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

} // namespace middleware
