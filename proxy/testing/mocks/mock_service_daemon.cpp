/**
 * @file    mock_service_daemon.cpp
 * @brief   MockServiceDaemon implementation.
 */

#include "mock_service_daemon.h"

extern "C" {
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
}

#include <cstring>
#include <thread>
#include <chrono>
#include <cassert>
#include <stdexcept>

// We replicate only the parts of sm_hdr_t that we need here so that the mock
// does not need to depend on the production headers.
namespace {

#pragma pack(push, 1)
struct MockHdr {
    uint32_t magic;         // 0x534D4B47
    uint8_t  version;       // 2
    uint8_t  _r0[3];
    uint16_t msg_type;
    uint16_t flags;
    uint32_t payload_len;
    uint32_t nonce;
    uint64_t timestamp;
    uint8_t  hmac[32];      // Not validated in mock.
};
#pragma pack(pop)

static constexpr uint32_t MOCK_MAGIC   = 0x534D4B47u;
static constexpr uint8_t  MOCK_VERSION = 2;
static constexpr size_t   HDR_SIZE     = sizeof(MockHdr);

} // anonymous namespace

namespace middleware::testing {

// ── ctor / dtor ──────────────────────────────────────────────────────────────

MockServiceDaemon::MockServiceDaemon(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
    // Default handler: send empty-payload ACK.
    handler_ = [](uint16_t, const uint8_t*, size_t) ->
        std::optional<std::vector<uint8_t>> {
        return std::vector<uint8_t>{};
    };
}

MockServiceDaemon::~MockServiceDaemon() {
    stop();
}

// ── lifecycle ────────────────────────────────────────────────────────────────

bool MockServiceDaemon::start() {
    if (running_.load()) return true;

    ::unlink(socket_path_.c_str());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int flags = ::fcntl(listen_fd_, F_GETFL, 0);
    ::fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 8) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void MockServiceDaemon::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    ::unlink(socket_path_.c_str());
}

bool MockServiceDaemon::isRunning() const noexcept { return running_.load(); }

// ── configuration ────────────────────────────────────────────────────────────

void MockServiceDaemon::setRequestHandler(RequestHandler h) {
    handler_ = std::move(h);
}

void MockServiceDaemon::injectDisconnectAfter(size_t n) {
    disconnect_after_ = n;
}

// ── inspection ───────────────────────────────────────────────────────────────

size_t MockServiceDaemon::requestCount() const noexcept {
    return request_count_.load();
}

std::vector<CapturedRequest> MockServiceDaemon::capturedRequests() const {
    std::lock_guard<std::mutex> lk(capture_mutex_);
    return captured_;
}

void MockServiceDaemon::clearCapture() {
    std::lock_guard<std::mutex> lk(capture_mutex_);
    captured_.clear();
}

// ── private ──────────────────────────────────────────────────────────────────

void MockServiceDaemon::acceptLoop() {
    while (running_.load()) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        int rc = ::poll(&pfd, 1, 50 /*ms*/);
        if (rc <= 0) continue;

        int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) continue;

        std::thread([this, client] { clientLoop(client); }).detach();
    }
}

void MockServiceDaemon::clientLoop(int client_fd) {
    size_t local_req_count = 0;

    while (running_.load()) {
        // Read header.
        MockHdr hdr{};
        ssize_t n = ::recv(client_fd, &hdr, HDR_SIZE, MSG_WAITALL);
        if (n != static_cast<ssize_t>(HDR_SIZE)) break;
        if (hdr.magic != MOCK_MAGIC) break;

        // Read payload.
        std::vector<uint8_t> payload(hdr.payload_len);
        if (hdr.payload_len > 0) {
            ssize_t pn = ::recv(client_fd, payload.data(),
                                hdr.payload_len, MSG_WAITALL);
            if (pn != static_cast<ssize_t>(hdr.payload_len)) break;
        }

        ++request_count_;
        ++local_req_count;

        // Capture.
        {
            std::lock_guard<std::mutex> lk(capture_mutex_);
            captured_.push_back({hdr.msg_type, payload});
        }

        // Inject delay.
        if (reply_delay_ms_ > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(reply_delay_ms_));

        // Inject disconnect.
        if (disconnect_after_ && local_req_count >= *disconnect_after_) break;

        // Handler.
        std::optional<std::vector<uint8_t>> reply;
        if (handler_) {
            reply = handler_(hdr.msg_type,
                             payload.empty() ? nullptr : payload.data(),
                             payload.size());
        }

        if (!reply) break; // nullptr means "close connection"

        sendAck(client_fd, hdr.msg_type, hdr.nonce,
                reply->empty() ? nullptr : reply->data(),
                reply->size());
    }

    ::close(client_fd);
}

bool MockServiceDaemon::sendAck(int fd, uint16_t msg_type, uint32_t nonce,
                                const uint8_t* payload, size_t payload_len)
{
    MockHdr h{};
    h.magic       = MOCK_MAGIC;
    h.version     = MOCK_VERSION;
    h.msg_type    = msg_type;
    h.payload_len = static_cast<uint32_t>(payload_len);
    h.nonce       = nonce;

    iovec iov[2];
    iov[0].iov_base = &h;
    iov[0].iov_len  = HDR_SIZE;
    iov[1].iov_base = const_cast<uint8_t*>(payload);
    iov[1].iov_len  = payload_len;

    const int iov_cnt = (payload_len > 0) ? 2 : 1;
    ssize_t r = ::writev(fd, iov, iov_cnt);
    return r == static_cast<ssize_t>(HDR_SIZE + payload_len);
}

} // namespace middleware::testing
