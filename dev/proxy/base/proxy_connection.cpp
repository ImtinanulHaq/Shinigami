/**
 * @file    proxy_connection.cpp
 * @brief   ProxyConnection — Unix socket + HMAC framing implementation.
 */

#include "proxy_connection.h"
#include "../security/proxy_auth.h"

extern "C" {
#  include "core/service_manager/infrastructure/sm_protocol.h"
#  include "core/memory_pool.h"
}

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>     // writev
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <syslog.h>

namespace middleware {

// Internal helper: log via config callback or syslog.
static void proxyLog(const ProxyConfig& cfg,
                     bool verbose, const char* msg) {
    if (verbose && !cfg.verbose_logging) return;
    if (cfg.log_callback) {
        cfg.log_callback(msg);
    } else {
        syslog(LOG_DEBUG, "[proxy_conn] %s", msg);
    }
}

// ── Construction / destruction ───────────────────────────────────────────

ProxyConnection::ProxyConnection(const ProxyConfig& config)
    : config_(config)
    , auth_(nullptr)
    , fd_(-1)
    , connected_(false)
    , seq_(0)
    , recv_pool_(nullptr)
{
    // Pre-allocate a small receive buffer pool.
    // Block size = one full max-size message; count = callback_queue_depth*2.
    memory_pool_config_t pcfg{};
    pcfg.block_size  = SM_MAX_MESSAGE_SIZE;
    pcfg.block_count = static_cast<size_t>(config_.callback_queue_depth) * 2;
    pcfg.thread_safe = 0;  // pool is accessed only from the io thread.
    pcfg.name        = "proxy_recv_pool";

    recv_pool_ = memory_pool_create(&pcfg);
    // If this fails, recvMessage will fail with InternalError — acceptable.
}

ProxyConnection::~ProxyConnection() {
    disconnect();
    if (recv_pool_) {
        memory_pool_destroy(recv_pool_);
        recv_pool_ = nullptr;
    }
}

void ProxyConnection::setAuth(ProxyAuth* auth) noexcept {
    auth_ = auth;
}

// ── connect ───────────────────────────────────────────────────────────────

Result<void> ProxyConnection::connect(std::string_view socket_path) {
    if (connected_) {
        return Result<void>::err(ProxyError::AlreadyConnected);
    }

    // 1. Create socket.
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        return Result<void>::err(ProxyError::InternalError,
                                 "socket() failed");
    }

    // 2. Set connect-phase timeouts.
    auto r = setSocketTimeouts(
        static_cast<int>(config_.connect_timeout.count()));
    if (r.isErr()) {
        ::close(fd_); fd_ = -1;
        return r;
    }

    // 3. connect()
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        ::close(fd_); fd_ = -1;
        return Result<void>::err(ProxyError::InvalidArgument,
                                 "socket path too long");
    }
    std::memcpy(addr.sun_path, socket_path.data(), socket_path.size());

    int rc = ::connect(fd_,
                       reinterpret_cast<struct sockaddr*>(&addr),
                       sizeof(addr));
    if (rc < 0) {
        // Map errno to ProxyError.
        ProxyError pe = (errno == ENOENT || errno == ECONNREFUSED)
                        ? ProxyError::ConnectionRefused
                        : ProxyError::ConnectionTimeout;
        ::close(fd_); fd_ = -1;
        return Result<void>::err(pe, ::strerror(errno));
    }

    // 4. Tighten to request-phase timeouts for normal operation.
    r = setSocketTimeouts(
        static_cast<int>(config_.request_timeout.count()));
    if (r.isErr()) {
        ::close(fd_); fd_ = -1;
        return r;
    }

    connected_ = true;
    seq_       = 0;

    // 5. Protocol handshake.
    r = sendHandshakeHello();
    if (r.isErr()) { disconnect(); return r; }

    r = recvHandshakeWelcome();
    if (r.isErr()) { disconnect(); return r; }

    proxyLog(config_, true, "socket connected and handshaked");
    return Result<void>::ok();
}

Result<void> ProxyConnection::setSocketTimeouts(int timeout_ms) {
    struct timeval tv{};
    tv.tv_sec  =  timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return Result<void>::err(ProxyError::InternalError,
                                 "setsockopt timeout failed");
    }
    return Result<void>::ok();
}

// ── disconnect ────────────────────────────────────────────────────────────

void ProxyConnection::disconnect() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

bool ProxyConnection::isConnected() const noexcept {
    return connected_ && fd_ >= 0;
}

int ProxyConnection::fd() const noexcept {
    return fd_;
}

// ── send ─────────────────────────────────────────────────────────────────

Result<void> ProxyConnection::send(uint16_t type,
                                   const void* payload, uint32_t plen)
{
    std::lock_guard<std::mutex> lk(send_mutex_);

    if (!connected_ || fd_ < 0) {
        return Result<void>::err(ProxyError::NotConnected);
    }

    // Build header.
    sm_hdr_t hdr{};
    hdr.magic      = SM_PROTOCOL_MAGIC;
    hdr.version    = SM_PROTOCOL_VERSION;
    hdr.type       = type;
    hdr.length     = plen;
    hdr.timestamp  = static_cast<uint32_t>(::time(nullptr));
    hdr.client_pid = static_cast<uint32_t>(::getpid());
    hdr.nonce      = ++seq_;

    // HMAC: covers bytes 0..SM_HDR_HMAC_OFFSET-1 of header + payload.
    if (auth_ && auth_->isReady()) {
        // We need to sign (header_prefix || payload).
        // Build a contiguous buffer for signing.
        const size_t sign_hdr_len = SM_HDR_HMAC_OFFSET;
        const size_t sign_total   = sign_hdr_len + plen;
        // Use stack for small messages.
        uint8_t  stack_buf[512];
        uint8_t* sign_buf = sign_total <= sizeof(stack_buf)
                            ? stack_buf
                            : static_cast<uint8_t*>(::malloc(sign_total));
        if (!sign_buf) {
            return Result<void>::err(ProxyError::InternalError,
                                     "sign buffer allocation failed");
        }
        std::memcpy(sign_buf,                  &hdr,    sign_hdr_len);
        if (plen > 0 && payload) {
            std::memcpy(sign_buf + sign_hdr_len, payload, plen);
        }

        message_auth_t mauth{};
        auto sr = auth_->sign(sign_buf, sign_total, &mauth);
        if (sign_buf != stack_buf) { ::free(sign_buf); }
        if (sr.isErr()) return sr;

        // Copy HMAC into header.
        static_assert(sizeof(mauth.hmac) == sizeof(hdr.hmac),
                      "HMAC size mismatch between verify_t and sm_hdr_t");
        std::memcpy(hdr.hmac, mauth.hmac, sizeof(hdr.hmac));
    }

    // Atomic header + payload send via writev().
    struct iovec iov[2];
    iov[0].iov_base = &hdr;
    iov[0].iov_len  = sizeof(hdr);
    iov[1].iov_base = const_cast<void*>(payload);
    iov[1].iov_len  = plen;

    int iovcnt = (plen > 0 && payload) ? 2 : 1;
    ssize_t sent = ::writev(fd_, iov, iovcnt);
    if (sent < 0) {
        ProxyError pe = (errno == EPIPE || errno == ECONNRESET)
                        ? ProxyError::DaemonCrashed
                        : ProxyError::SendFailed;
        connected_ = false;
        return Result<void>::err(pe, ::strerror(errno));
    }

    return Result<void>::ok();
}

// ── recv ──────────────────────────────────────────────────────────────────

Result<void> ProxyConnection::readExact(void* buf, size_t n) {
    uint8_t* p   = static_cast<uint8_t*>(buf);
    size_t  got  = 0;
    while (got < n) {
        ssize_t r = ::recv(fd_, p + got, n - got, MSG_WAITALL);
        if (r == 0) {
            connected_ = false;
            return Result<void>::err(ProxyError::DaemonCrashed,
                                     "connection closed by peer");
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            ProxyError pe = (errno == EAGAIN || errno == EWOULDBLOCK)
                            ? ProxyError::Timeout
                            : ProxyError::RecvFailed;
            connected_ = false;
            return Result<void>::err(pe, ::strerror(errno));
        }
        got += static_cast<size_t>(r);
    }
    return Result<void>::ok();
}

Result<void> ProxyConnection::recvMessage(ProxyMessage& msg) {
    if (!connected_ || fd_ < 0) {
        return Result<void>::err(ProxyError::NotConnected);
    }
    if (!recv_pool_) {
        return Result<void>::err(ProxyError::InternalError,
                                 "recv pool not initialised");
    }

    // Step 1: Read fixed header.
    sm_hdr_t hdr{};
    auto r = readExact(&hdr, sizeof(hdr));
    if (r.isErr()) return r;

    // Step 2: Basic header validation (magic, version, size bounds).
    int vr = sm_validate_header(&hdr, sizeof(hdr) + hdr.length);
    if (vr != 0) {
        // Protocol violation — close immediately, do not attempt recovery.
        connected_ = false;
        ::close(fd_); fd_ = -1;
        return Result<void>::err(ProxyError::InvalidResponse,
                                 "sm_validate_header failed");
    }

    // Step 3: Read payload.
    uint8_t* block    = nullptr;
    void*    pool_blk = nullptr;

    if (hdr.length > 0) {
        pool_blk = memory_pool_alloc(recv_pool_);
        if (!pool_blk) {
            return Result<void>::err(ProxyError::BufferFull,
                                     "recv pool exhausted");
        }
        block = static_cast<uint8_t*>(pool_blk);
        r = readExact(block, hdr.length);
        if (r.isErr()) {
            memory_pool_free(recv_pool_, pool_blk);
            return r;
        }
    }

    // Step 4: HMAC validation.
    if (auth_ && auth_->isReady()) {
        const size_t sign_hdr_len = SM_HDR_HMAC_OFFSET;
        const size_t sign_total   = sign_hdr_len + hdr.length;
        uint8_t  stack_buf[512];
        uint8_t* sign_buf = sign_total <= sizeof(stack_buf)
                            ? stack_buf
                            : static_cast<uint8_t*>(::malloc(sign_total));
        if (sign_buf) {
            std::memcpy(sign_buf, &hdr, sign_hdr_len);
            if (hdr.length > 0) {
                std::memcpy(sign_buf + sign_hdr_len, block, hdr.length);
            }
            message_auth_t ma{};
            std::memcpy(ma.hmac, hdr.hmac, sizeof(ma.hmac));
            ma.timestamp = hdr.timestamp;
            ma.nonce     = hdr.nonce;

            bool ok = auth_->check(sign_buf, sign_total, &ma);
            if (sign_buf != stack_buf) { ::free(sign_buf); }

            if (!ok) {
                if (pool_blk) memory_pool_free(recv_pool_, pool_blk);
                connected_ = false;
                ::close(fd_); fd_ = -1;
                return Result<void>::err(ProxyError::AuthFailed,
                                         "HMAC validation failed — disconnecting");
            }
        }
    }

    // Step 5: Fill output struct.
    msg.type        = hdr.type;
    msg.nonce       = hdr.nonce;
    msg.payload     = block;
    msg.payload_len = hdr.length;
    msg.pool_block  = pool_blk;

    return Result<void>::ok();
}

void ProxyConnection::releaseBuffer(ProxyMessage& msg) noexcept {
    if (msg.pool_block && recv_pool_) {
        memory_pool_free(recv_pool_, msg.pool_block);
    }
    msg.payload_len = 0;
    msg.payload     = nullptr;
    msg.pool_block  = nullptr;
}

// ── Handshake ─────────────────────────────────────────────────────────────

Result<void> ProxyConnection::sendHandshakeHello() {
    // Send a lightweight HEARTBEAT as the hello (reuses the heartbeat slot).
    // The daemon responds with another HEARTBEAT → WELCOME semantics.
    sm_heartbeat_req_t hb{};
    std::strncpy(hb.service_name, "proxy", sizeof(hb.service_name) - 1);
    return send(SM_MSG_HEARTBEAT,
                &hb, static_cast<uint32_t>(sizeof(hb)));
}

Result<void> ProxyConnection::recvHandshakeWelcome() {
    // We do not strictly require a response — many daemons just start sending
    // data after the first message.  Make recv optional / best-effort.
    // Set a short timeout, then ignore TimeoutError.
    struct timeval tv_short;
    tv_short.tv_sec  = 1;
    tv_short.tv_usec = 0;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv_short, sizeof(tv_short));

    ProxyMessage welcome{};
    auto r = recvMessage(welcome);
    if (r.isOk()) {
        releaseBuffer(welcome);
    }
    // Reset to normal request timeout.
    setSocketTimeouts(static_cast<int>(config_.request_timeout.count()));
    return Result<void>::ok();  // Non-fatal if daemon sent nothing.
}

} // namespace middleware
