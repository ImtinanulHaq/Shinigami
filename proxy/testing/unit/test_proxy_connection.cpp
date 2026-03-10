/**
 * @file    test_proxy_connection.cpp
 * @brief   Unit tests for ProxyConnection against MockServiceDaemon.
 */

#include "../../base/proxy_connection.h"
#include "../../base/proxy_config.h"
#include "../mocks/mock_service_daemon.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace middleware;
using namespace middleware::testing;

static constexpr const char* SOCK = "/tmp/test_proxy_conn.sock";

class ProxyConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        daemon_ = std::make_unique<MockServiceDaemon>(SOCK);
        ASSERT_TRUE(daemon_->start());
        // Give accept loop a moment to spin up.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    void TearDown() override {
        daemon_->stop();
    }
    std::unique_ptr<MockServiceDaemon> daemon_;
};

TEST_F(ProxyConnectionTest, ConnectSucceeds) {
    ProxyConfig cfg;
    cfg.service_socket_path = SOCK;
    cfg.connect_timeout     = std::chrono::milliseconds(500);

    ProxyConnection conn;
    auto r = conn.connect(SOCK, cfg.connect_timeout);
    EXPECT_TRUE(r.isOk()) << r.message();
    conn.disconnect();
}

TEST_F(ProxyConnectionTest, SendAndReceive) {
    ProxyConfig cfg;
    cfg.service_socket_path = SOCK;
    cfg.connect_timeout = std::chrono::milliseconds(500);

    ProxyConnection conn;
    ASSERT_TRUE(conn.connect(SOCK, cfg.connect_timeout).isOk());

    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    auto sr = conn.sendMessage(0x0301, 0xABCD1234, payload, sizeof(payload));
    EXPECT_TRUE(sr.isOk()) << sr.message();

    // Daemon should have received 1 request.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(1u, daemon_->requestCount());

    conn.disconnect();
}

TEST_F(ProxyConnectionTest, BadSocketPathFails) {
    ProxyConnection conn;
    auto r = conn.connect("/tmp/nonexistent_XYZZY.sock",
                          std::chrono::milliseconds(100));
    EXPECT_TRUE(r.isErr());
}

TEST_F(ProxyConnectionTest, CapturedPayloadMatchesSent) {
    ProxyConfig cfg;
    cfg.service_socket_path = SOCK;

    ProxyConnection conn;
    ASSERT_TRUE(conn.connect(SOCK, std::chrono::milliseconds(500)).isOk());

    const uint8_t payload[] = {1, 2, 3};
    conn.sendMessage(0x0401, 1, payload, sizeof(payload));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto reqs = daemon_->capturedRequests();
    ASSERT_FALSE(reqs.empty());
    EXPECT_EQ(std::vector<uint8_t>({1, 2, 3}), reqs.back().payload);

    conn.disconnect();
}
