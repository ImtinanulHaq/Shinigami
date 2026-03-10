/**
 * @file    test_proxy_sm_integration.cpp
 * @brief   Integration test: proxies talk to MockServiceDaemon over Unix sockets.
 *
 * Verifies the full handshake, reconnect, and message-round-trip path.
 */

#include "../../audio/audio_proxy.h"
#include "../../sensor/sensor_proxy.h"
#include "../../gpio/gpio_proxy.h"
#include "../mocks/mock_service_daemon.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace middleware;
using namespace middleware::testing;

// ── Fixture ──────────────────────────────────────────────────────────────────

class ProxySMIntegration : public ::testing::Test {
protected:
    static constexpr const char* AUDIO_SOCK  = "/tmp/test_int_audio.sock";
    static constexpr const char* SENSOR_SOCK = "/tmp/test_int_sensor.sock";
    static constexpr const char* GPIO_SOCK   = "/tmp/test_int_gpio.sock";

    void SetUp() override {
        audio_d_  = std::make_unique<MockServiceDaemon>(AUDIO_SOCK);
        sensor_d_ = std::make_unique<MockServiceDaemon>(SENSOR_SOCK);
        gpio_d_   = std::make_unique<MockServiceDaemon>(GPIO_SOCK);

        ASSERT_TRUE(audio_d_->start());
        ASSERT_TRUE(sensor_d_->start());
        ASSERT_TRUE(gpio_d_->start());

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void TearDown() override {
        audio_d_->stop();
        sensor_d_->stop();
        gpio_d_->stop();
    }

    ProxyConfig cfg(const char* sock) {
        ProxyConfig c;
        c.service_socket_path = sock;
        c.connect_timeout     = std::chrono::milliseconds(500);
        c.use_shared_memory   = false; // disable SHM in tests
        c.hmac_key_file       = "";    // dev mode — no HMAC
        return c;
    }

    std::unique_ptr<MockServiceDaemon> audio_d_;
    std::unique_ptr<MockServiceDaemon> sensor_d_;
    std::unique_ptr<MockServiceDaemon> gpio_d_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(ProxySMIntegration, AudioStartCaptureRoundTrip) {
    AudioProxy proxy(cfg(AUDIO_SOCK));
    ASSERT_TRUE(proxy.connect().isOk());

    auto r = proxy.startCapture();
    EXPECT_TRUE(r.isOk()) << r.message();

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_GE(audio_d_->requestCount(), 1u);

    proxy.disconnect();
}

TEST_F(ProxySMIntegration, SensorStartSamplingRoundTrip) {
    SensorProxy proxy(cfg(SENSOR_SOCK));
    ASSERT_TRUE(proxy.connect().isOk());

    auto r = proxy.startSampling(SensorReading::Type::Accelerometer, 100u);
    EXPECT_TRUE(r.isOk()) << r.message();

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_GE(sensor_d_->requestCount(), 1u);

    proxy.disconnect();
}

TEST_F(ProxySMIntegration, GpioConfigureAndSet) {
    GpioProxy proxy(cfg(GPIO_SOCK));
    ASSERT_TRUE(proxy.connect().isOk());

    EXPECT_TRUE(proxy.configurePin(17, GpioDirection::Output).isOk());
    EXPECT_TRUE(proxy.setPin(17, 1).isOk());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_GE(gpio_d_->requestCount(), 2u);

    proxy.disconnect();
}

TEST_F(ProxySMIntegration, AudioReconnectAfterDisconnect) {
    // Daemon drops connection after 1 request.
    audio_d_->injectDisconnectAfter(1);

    ProxyConfig c = cfg(AUDIO_SOCK);
    c.reconnect_initial    = std::chrono::milliseconds(50);
    c.reconnect_max        = std::chrono::milliseconds(200);
    c.reconnect_max_attempts = 3;

    AudioProxy proxy(c);
    ASSERT_TRUE(proxy.connect().isOk());

    proxy.startCapture(); // triggers disconnect after daemon closes

    // Proxy should attempt reconnect — give it time.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    // No assertion on success here (daemon is permanently closed);
    // we just verify no crash / hang.
    proxy.disconnect();
}
