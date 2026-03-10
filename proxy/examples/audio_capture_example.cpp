/**
 * @file    audio_capture_example.cpp
 * @brief   Example: capture 5 seconds of PCM audio via AudioProxy.
 *
 * Build:
 *   g++ -std=c++17 audio_capture_example.cpp \
 *       -I../../.. -lmiddleware_proxy -o audio_capture
 *
 * Usage:
 *   ./audio_capture [output.pcm]
 */

#include <middleware/proxy/audio/audio_proxy.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

using namespace middleware;

static std::atomic<bool> g_stop{false};

static void sighandler(int) { g_stop.store(true); }

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  sighandler);
    std::signal(SIGTERM, sighandler);

    const std::string out_path = (argc > 1) ? argv[1] : "capture.pcm";
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Cannot open " << out_path << "\n";
        return 1;
    }

    // 1. Build config — defaults connect to /tmp/servicemanager.sock
    ProxyConfig cfg;
    cfg.use_shared_memory = true;

    // 2. Create and connect the proxy.
    AudioProxy proxy(cfg);
    auto conn_r = proxy.connect();
    if (conn_r.isErr()) {
        std::cerr << "connect() failed: " << conn_r.message() << "\n";
        return 2;
    }
    std::cout << "Connected to audio daemon.\n";

    // 3. Register frame callback — called on proxy callback thread.
    std::atomic<size_t> frame_count{0};
    proxy.onFrameReady([&](AudioFrame frame) {
        out.write(reinterpret_cast<const char*>(frame.data),
                  static_cast<std::streamsize>(frame.data_bytes));
        ++frame_count;
    });

    // 4. Configure and start capture.
    proxy.setSampleRate(48000);
    proxy.setChannels(2);
    proxy.setBitDepth(16);

    auto r = proxy.startCapture();
    if (r.isErr()) {
        std::cerr << "startCapture() failed: " << r.message() << "\n";
        proxy.disconnect();
        return 3;
    }
    std::cout << "Capturing... (Ctrl+C to stop)\n";

    // 5. Wait for stop signal or 5-second default.
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(5);
    while (!g_stop.load() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 6. Graceful shutdown.
    proxy.stopCapture();
    proxy.disconnect();

    std::cout << "Captured " << frame_count.load()
              << " frames → " << out_path << "\n";
    return 0;
}
