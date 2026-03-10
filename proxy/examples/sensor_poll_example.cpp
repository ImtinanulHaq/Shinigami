/**
 * @file    sensor_poll_example.cpp
 * @brief   Example: print accelerometer + temperature readings for 10 seconds.
 */

#include <middleware/proxy/sensor/sensor_proxy.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

using namespace middleware;

static std::atomic<bool> g_stop{false};
static void sighandler(int) { g_stop.store(true); }

int main() {
    std::signal(SIGINT, sighandler);

    ProxyConfig cfg;
    SensorProxy proxy(cfg);

    auto r = proxy.connect();
    if (r.isErr()) { std::cerr << "connect: " << r.message() << "\n"; return 1; }

    proxy.onReadingReady([](const SensorReading& s) {
        std::cout << std::fixed << std::setprecision(4);
        switch (s.sensor_type) {
        case SensorReading::Type::Accelerometer:
            std::cout << "[ACCEL] x=" << s.x
                      << "  y=" << s.y
                      << "  z=" << s.z
                      << "  seq=" << s.sequence << "\n";
            break;
        case SensorReading::Type::Temperature:
            std::cout << "[TEMP ] " << s.scalar << " °C\n";
            break;
        default:
            break;
        }
    });

    proxy.startSampling(SensorReading::Type::Accelerometer, 50);
    proxy.startSampling(SensorReading::Type::Temperature, 1);

    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!g_stop.load() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    proxy.stopSampling(SensorReading::Type::Accelerometer);
    proxy.stopSampling(SensorReading::Type::Temperature);
    proxy.disconnect();
    return 0;
}
