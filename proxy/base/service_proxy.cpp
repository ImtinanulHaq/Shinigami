/**
 * @file    service_proxy.cpp
 * @brief   ServiceProxy implementation — connection, reconnect, io thread.
 */

#include "service_proxy.h"

#include <sys/epoll.h>
#include <syslog.h>
#include <cerrno>
#include <cstring>

namespace middleware {

// ── Internal log helper ───────────────────────────────────────────────────

static void log(const ProxyConfig& cfg,
                const char* tag, const char* msg) {
    if (cfg.log_callback) {
        std::string line = std::string("[") + tag + "] " + msg;
        cfg.log_callback(line);
    } else {
        syslog(LOG_INFO, "[%s] %s", tag, msg);
    }
}

// ── Construction / destruction ───────────────────────────────────────────

ServiceProxy::ServiceProxy(std::string_view service_name,
                           ProxyConfig      config)
    : config_(std::move(config))
    , service_name_(service_name)
{
    auth_         = std::make_unique<ProxyAuth>();
    connection_   = std::make_unique<ProxyConnection>(config_);
    callback_pool_= std::make_unique<ProxyThreadPool>(
                        config_.callback_queue_depth,
                        config_.callback_thread_count);
    event_loop_   = std::make_unique<ProxyEventLoop>();
    discovery_    = std::make_unique<ServiceDiscovery>(config_);

    // Wire overflow handler → user error callback.
    callback_pool_->setOverflowHandler([this](ProxyError e) {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (on_error_cb_) on_error_cb_(e, "callback queue full — drop");
    });
}

ServiceProxy::~ServiceProxy() {
    disconnect();
    callback_pool_.reset();  // Join consumer thread.
}

// ── Lifecycle callbacks registration ─────────────────────────────────────

void ServiceProxy::onConnectionLost(std::function<void(ProxyError)> cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    on_lost_cb_ = std::move(cb);
}

void ServiceProxy::onConnectionRestored(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    on_restored_cb_ = std::move(cb);
}

void ServiceProxy::onError(
    std::function<void(ProxyError, std::string_view)> cb)
{
    std::lock_guard<std::mutex> lk(cb_mutex_);
    on_error_cb_ = std::move(cb);
}

// ── connect ───────────────────────────────────────────────────────────────

Result<void> ServiceProxy::connect() {
    std::lock_guard<std::mutex> lk(connect_mutex_);

    if (connected_.load()) {
        return Result<void>::err(ProxyError::AlreadyConnected);
    }

    auto r = doConnect();
    if (r.isErr()) return r;

    // Start io thread and SHM poll thread.
    stopping_.store(false);
    io_thread_  = std::thread(&ServiceProxy::ioThreadFunc, this);
    shm_thread_ = std::thread([this] {
        while (!stopping_.load(std::memory_order_acquire))
            ioLoop();
    });
    return Result<void>::ok();
}

void ServiceProxy::connectAsync(std::function<void(Result<void>)> cb) {
    std::thread([this, cb = std::move(cb)]() mutable {
        auto r = connect();
        postCallback([cb = std::move(cb), r]() mutable { cb(std::move(r)); });
    }).detach();
}

// ── Internal connect logic ────────────────────────────────────────────────

Result<void> ServiceProxy::doConnect() {
    // 1. Initialise authentication.
    if (!auth_->isReady()) {
        auto ar = auth_->init(config_.hmac_key_file);
        if (ar.isErr()) return ar;
        connection_->setAuth(auth_.get());
    }

    // 2. Resolve socket path.
    std::string socket_path = config_.service_socket_path;
    if (socket_path.empty()) {
        auto si = discovery_->findService(service_name_);
        if (si.isErr()) return Result<void>::err(si.error(), si.detail());
        socket_path = si.value().socket_path;
    }

    // 3. Connect socket.
    auto r = connection_->connect(socket_path);
    if (r.isErr()) return r;

    // 4. Notify subclass.
    r = onConnected();
    if (r.isErr()) {
        connection_->disconnect();
        return r;
    }

    connected_.store(true, std::memory_order_release);
    log(config_, service_name_.c_str(), "connected");
    return Result<void>::ok();
}

// ── disconnect ────────────────────────────────────────────────────────────

void ServiceProxy::disconnect() {
    stopping_.store(true, std::memory_order_release);
    connected_.store(false, std::memory_order_release);

    // Stop event loop (unblocks io thread).
    if (event_loop_) event_loop_->stop();

    // Join io thread.
    if (io_thread_.joinable())  io_thread_.join();
    // Join SHM poll thread.
    if (shm_thread_.joinable()) shm_thread_.join();

    // Close socket.
    if (connection_) connection_->disconnect();

    // Notify subclass.
    onDisconnected();
}

bool ServiceProxy::isConnected() const noexcept {
    return connected_.load(std::memory_order_acquire);
}

// ── Protected send helpers ────────────────────────────────────────────────

Result<void> ServiceProxy::sendMessage(uint16_t type,
                                       const void* payload, uint32_t plen) {
    if (!connected_.load()) {
        return Result<void>::err(ProxyError::NotConnected);
    }
    return connection_->send(type, payload, plen);
}

Result<ProxyMessage> ServiceProxy::sendRequestWaitReply(uint16_t type,
                                                        const void* payload,
                                                        uint32_t    plen)
{
    if (!connected_.load()) {
        return Result<ProxyMessage>::err(ProxyError::NotConnected);
    }

    {
        std::lock_guard<std::mutex> lk(pending_reply_.mu);
        pending_reply_.ready = false;
    }

    auto sr = connection_->send(type, payload, plen);
    if (sr.isErr()) {
        return Result<ProxyMessage>::err(sr.error(), sr.detail());
    }

    // Wait for io thread to deliver reply via PendingReply.
    std::unique_lock<std::mutex> lk(pending_reply_.mu);
    bool got = pending_reply_.cv.wait_for(
        lk,
        config_.request_timeout,
        [this] { return pending_reply_.ready; });

    if (!got) {
        return Result<ProxyMessage>::err(ProxyError::Timeout);
    }

    ProxyMessage msg = std::move(pending_reply_.msg);
    pending_reply_.ready = false;
    return Result<ProxyMessage>::ok(std::move(msg));
}

void ServiceProxy::postCallback(std::function<void()> fn) {
    if (callback_pool_) callback_pool_->post(std::move(fn));
}

ProxyThreadPool& ServiceProxy::threadPool() noexcept {
    return *callback_pool_;
}

void ServiceProxy::releaseBuffer(ProxyMessage& msg) {
    if (connection_) connection_->releaseBuffer(msg);
}

// ── IO thread ────────────────────────────────────────────────────────────

void ServiceProxy::ioThreadFunc() {
    // Register service socket with the event loop.
    int fd = connection_->fd();
    if (fd >= 0) {
        event_loop_->addFd(fd,
            static_cast<uint32_t>(EventType::Read) |
            static_cast<uint32_t>(EventType::HangUp) |
            static_cast<uint32_t>(EventType::Error),
            [this](int f, uint32_t ev) { onSocketEvent(f, ev); });
    }

    event_loop_->run(
        static_cast<int>(config_.health_ping_interval.count()));
}

void ServiceProxy::onSocketEvent(int /*fd*/, uint32_t epoll_ev) {
    // HangUp / Error — daemon closed the connection.
    if (epoll_ev & (EPOLLHUP | EPOLLERR)) {
        doDisconnect(ProxyError::DaemonCrashed);
        return;
    }

    // Readable — receive one message.
    ProxyMessage msg{};
    auto r = connection_->recvMessage(msg);
    if (r.isErr()) {
        if (r.error() == ProxyError::DaemonCrashed ||
            r.error() == ProxyError::RecvFailed) {
            doDisconnect(r.error());
        }
        return;
    }

    // Route: if there's a pending synchronous request, satisfy it.
    {
        std::lock_guard<std::mutex> lk(pending_reply_.mu);
        if (!pending_reply_.ready) {
            pending_reply_.msg   = std::move(msg);
            pending_reply_.ready = true;
            pending_reply_.cv.notify_one();
            return;
        }
    }

    // Otherwise release the buffer (unhandled push notification).
    connection_->releaseBuffer(msg);
}

// ── Reconnect logic ───────────────────────────────────────────────────────

void ServiceProxy::doDisconnect(ProxyError reason) {
    bool was_connected = connected_.exchange(false);
    connection_->disconnect();
    onDisconnected();

    // Fire user callback on the callback thread.
    if (was_connected) {
        postCallback([this, reason] {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            if (on_lost_cb_) on_lost_cb_(reason);
        });
    }

    // Start reconnect loop if not stopping.
    if (!stopping_.load() && config_.reconnect_max_attempts != 0) {
        startReconnectLoop(config_.reconnect_initial);
    }
}

void ServiceProxy::startReconnectLoop(
    std::chrono::milliseconds current_backoff)
{
    if (reconnecting_.exchange(true)) return;  // Already reconnecting.

    std::thread([this, backoff = current_backoff]() mutable {
        int attempts = 0;
        const int max = config_.reconnect_max_attempts;

        while (!stopping_.load()) {
            if (max > 0 && attempts >= max) break;

            std::this_thread::sleep_for(backoff);
            ++attempts;

            auto r = doConnect();
            if (r.isOk()) {
                reconnecting_.store(false);
                postCallback([this] {
                    std::lock_guard<std::mutex> lk(cb_mutex_);
                    if (on_restored_cb_) on_restored_cb_();
                });
                return;
            }

            // Exponential back-off.
            backoff = std::min(backoff * 2, config_.reconnect_max);
        }

        reconnecting_.store(false);
    }).detach();
}

} // namespace middleware
