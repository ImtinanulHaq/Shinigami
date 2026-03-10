/**
 * @file    mock_service_daemon.h
 * @brief   In-process fake service daemon for unit tests.
 *
 * Listens on a Unix socket, echoes back well-formed empty-payload ACK
 * responses to any request.  Can be configured to inject errors / delays.
 */
#pragma once

#include "../../base/proxy_result.h"

extern "C" {
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
}

#include <atomic>
#include <thread>
#include <string>
#include <functional>
#include <cstdint>
#include <vector>
#include <mutex>
#include <optional>

namespace middleware::testing {

/** @brief Captured request for assertion in tests. */
struct CapturedRequest {
    uint16_t msg_type;
    std::vector<uint8_t> payload;
};

/**
 * @class  MockServiceDaemon
 * @brief  Minimal Unix socket server that speaks the sm_hdr_t framing protocol.
 *
 * Usage:
 * @code
 *   MockServiceDaemon daemon("/tmp/test_audio.sock");
 *   daemon.start();
 *   // ... run proxy under test ...
 *   daemon.stop();
 *   ASSERT_EQ(1u, daemon.requestCount());
 * @endcode
 */
class MockServiceDaemon {
public:
    using RequestHandler = std::function<
        std::optional<std::vector<uint8_t>>(uint16_t msg_type,
                                            const uint8_t* payload,
                                            size_t payload_len)>;

    explicit MockServiceDaemon(std::string socket_path);
    ~MockServiceDaemon();

    // ---- Lifecycle ----
    bool start();
    void stop();
    [[nodiscard]] bool isRunning() const noexcept;

    // ---- Configuration ----
    /** @brief Override default ACK handler with custom reply logic. */
    void setRequestHandler(RequestHandler h);

    /** @brief Make the daemon simulate a disconnect after @p n messages. */
    void injectDisconnectAfter(size_t n);

    /** @brief Inject an artificial reply delay. */
    void setReplyDelayMs(uint32_t ms) { reply_delay_ms_ = ms; }

    // ---- Inspection ----
    size_t requestCount() const noexcept;
    std::vector<CapturedRequest> capturedRequests() const;
    void clearCapture();

private:
    void acceptLoop();
    void clientLoop(int client_fd);
    bool sendAck(int fd, uint16_t msg_type, uint32_t nonce,
                 const uint8_t* payload, size_t payload_len);

    std::string           socket_path_;
    int                   listen_fd_{-1};
    std::atomic<bool>     running_{false};
    std::thread           accept_thread_;
    RequestHandler        handler_;
    std::atomic<size_t>   request_count_{0};
    std::optional<size_t> disconnect_after_;
    uint32_t              reply_delay_ms_{0};

    mutable std::mutex              capture_mutex_;
    std::vector<CapturedRequest>    captured_;
};

} // namespace middleware::testing
