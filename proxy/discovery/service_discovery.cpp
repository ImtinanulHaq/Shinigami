/**
 * @file    service_discovery.cpp
 * @brief   ServiceDiscovery implementation.
 */

#include "service_discovery.h"

extern "C" {
#  include "core/service_manager/infrastructure/sm_protocol.h"
}

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

namespace middleware {

ServiceDiscovery::ServiceDiscovery(const ProxyConfig& config)
    : config_(config)
{}

// ── Internal helpers ──────────────────────────────────────────────────────

static int connectToSM(const std::string& path, int timeout_ms) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct timeval tv{};
    tv.tv_sec  =  timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// ── findService ───────────────────────────────────────────────────────────

Result<ServiceInfo> ServiceDiscovery::findService(
                        const std::string& service_name)
{
    const int timeout_ms = static_cast<int>(config_.connect_timeout.count());
    int fd = connectToSM(config_.sm_socket_path, timeout_ms);
    if (fd < 0) {
        return Result<ServiceInfo>::err(ProxyError::ServiceNotFound,
                                        "Cannot reach Service Manager: " +
                                        std::string(::strerror(errno)));
    }

    // Build lookup request.
    sm_hdr_t hdr{};
    hdr.magic   = SM_PROTOCOL_MAGIC;
    hdr.version = SM_PROTOCOL_VERSION;
    hdr.type    = SM_MSG_LOOKUP;
    hdr.length  = sizeof(sm_lookup_req_t);
    hdr.timestamp  = static_cast<uint32_t>(::time(nullptr));
    hdr.client_pid = static_cast<uint32_t>(::getpid());
    hdr.nonce   = 1;

    sm_lookup_req_t req{};
    std::strncpy(req.service_name, service_name.c_str(),
                 sizeof(req.service_name) - 1);

    // Send header.
    if (::send(fd, &hdr, sizeof(hdr), MSG_NOSIGNAL) != sizeof(hdr)) {
        ::close(fd);
        return Result<ServiceInfo>::err(ProxyError::SendFailed);
    }
    // Send payload.
    if (::send(fd, &req, sizeof(req), MSG_NOSIGNAL) != sizeof(req)) {
        ::close(fd);
        return Result<ServiceInfo>::err(ProxyError::SendFailed);
    }

    // Receive reply header.
    sm_hdr_t reply_hdr{};
    ssize_t got = ::recv(fd, &reply_hdr, sizeof(reply_hdr), MSG_WAITALL);
    if (got != static_cast<ssize_t>(sizeof(reply_hdr))) {
        ::close(fd);
        return Result<ServiceInfo>::err(ProxyError::RecvFailed);
    }

    // Receive reply payload.
    if (reply_hdr.length == 0) {
        ::close(fd);
        return Result<ServiceInfo>::err(ProxyError::ServiceNotFound,
                                        "SM returned empty payload");
    }

    sm_lookup_reply_t reply_payload{};
    if (reply_hdr.length >= sizeof(reply_payload)) {
        got = ::recv(fd, &reply_payload, sizeof(reply_payload), MSG_WAITALL);
        if (got != static_cast<ssize_t>(sizeof(reply_payload))) {
            ::close(fd);
            return Result<ServiceInfo>::err(ProxyError::RecvFailed);
        }
    } else {
        // Might be an sm_reply_t (error code).
        sm_reply_t err_reply{};
        got = ::recv(fd, &err_reply, sizeof(err_reply), MSG_WAITALL);
        ::close(fd);
        return Result<ServiceInfo>::err(ProxyError::ServiceNotFound,
                                        "Service not registered in SM");
    }

    ::close(fd);

    if (reply_payload.socket_path[0] == '\0') {
        return Result<ServiceInfo>::err(ProxyError::ServiceNotFound,
                                        "SM returned empty socket_path");
    }

    ServiceInfo info;
    info.socket_path  = reply_payload.socket_path;
    info.ring_name    = reply_payload.ring_name;
    info.service_pid  = static_cast<int>(reply_payload.service_pid);
    return Result<ServiceInfo>::ok(std::move(info));
}

// ── waitForService ────────────────────────────────────────────────────────

Result<ServiceInfo> ServiceDiscovery::waitForService(
    const std::string&        service_name,
    std::chrono::milliseconds timeout)
{
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + timeout;

    while (Clock::now() < deadline) {
        auto r = findService(service_name);
        if (r.isOk()) return r;

        // Only retry on ServiceNotFound — other errors are fatal.
        if (r.error() != ProxyError::ServiceNotFound &&
            r.error() != ProxyError::ConnectionRefused) {
            return r;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return Result<ServiceInfo>::err(ProxyError::Timeout,
                                    "Service '" + service_name +
                                    "' did not appear within timeout");
}

} // namespace middleware
