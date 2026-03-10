/**
 * @file    camera_stream_example.cpp
 * @brief   Example: stream camera frames and save the first 100 as raw files.
 */

#include <middleware/proxy/camera/camera_proxy.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sstream>
#include <iomanip>

using namespace middleware;

static std::atomic<bool>   g_stop{false};
static std::atomic<size_t> g_saved{0};
static constexpr size_t    MAX_FRAMES = 100;

static void sighandler(int) { g_stop.store(true); }

int main() {
    std::signal(SIGINT, sighandler);

    ProxyConfig cfg;
    CameraProxy proxy(cfg);

    auto r = proxy.connect();
    if (r.isErr()) {
        std::cerr << "connect() failed: " << r.message() << "\n";
        return 1;
    }

    proxy.setResolution(1280, 720);
    proxy.setFps(30);
    proxy.setFormat(CameraFrame::Format::YUYV);

    proxy.onFrameReady([](CameraFrame frame) {
        const size_t n = g_saved.fetch_add(1);
        if (n >= MAX_FRAMES) { g_stop.store(true); return; }

        std::ostringstream fname;
        fname << "frame_" << std::setw(4) << std::setfill('0') << n << ".yuv";
        std::ofstream f(fname.str(), std::ios::binary);
        f.write(reinterpret_cast<const char*>(frame.data),
                static_cast<std::streamsize>(frame.data_bytes));
        std::cout << "\r  saved " << (n + 1) << "/" << MAX_FRAMES << std::flush;
    });

    PROXY_TRY(proxy.startStream());

    while (!g_stop.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    proxy.stopStream();
    proxy.disconnect();
    std::cout << "\nDone. " << g_saved.load() << " frames saved.\n";
    return 0;
}
