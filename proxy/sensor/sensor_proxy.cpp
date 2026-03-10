/**
 * @file    sensor_proxy.cpp
 * @brief   SensorProxy implementation.
 */

#include "sensor_proxy.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>

namespace middleware {

#pragma pack(push, 1)
struct SensorStartReq { uint8_t type; uint32_t rate_hz; };
struct SensorStopReq  { uint8_t type; };
struct SensorGetReq   { uint8_t type; };
#pragma pack(pop)

// ── ctor / dtor ──────────────────────────────────────────────────────────────

SensorProxy::SensorProxy(ProxyConfig cfg)
    : ServiceProxy("sensor_service", std::move(cfg)) {}

SensorProxy::~SensorProxy() {
    disconnect();
}

// ── public API ───────────────────────────────────────────────────────────────

Result<void> SensorProxy::startSampling(SensorReading::Type type, uint32_t rate_hz) {
    SensorStartReq req{static_cast<uint8_t>(type), rate_hz};
    auto r = sendRequestWaitReply(SENSOR_MSG_START_SAMPLING, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<void> SensorProxy::stopSampling(SensorReading::Type type) {
    SensorStopReq req{static_cast<uint8_t>(type)};
    auto r = sendRequestWaitReply(SENSOR_MSG_STOP_SAMPLING, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<SensorReading> SensorProxy::getLatestReading(SensorReading::Type type) {
    SensorGetReq req{static_cast<uint8_t>(type)};
    auto r = sendRequestWaitReply(SENSOR_MSG_GET_LATEST, &req, sizeof(req));
    if (r.isErr()) return Result<SensorReading>::err(r.error());

    auto& msg = r.value();
    SensorReading reading{};
    const size_t copy_sz = std::min(static_cast<size_t>(msg.payload_len),
                                    sizeof(SensorReading));
    std::memcpy(&reading, msg.payload, copy_sz);
    releaseBuffer(msg);
    return Result<SensorReading>::ok(reading);
}

void SensorProxy::onReadingReady(SensorReadingCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    reading_cb_ = std::move(cb);
}

void SensorProxy::onBatchReady(size_t min_count, SensorBatchCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    batch_min_count_ = min_count;
    batch_cb_        = std::move(cb);
}

// ── private: ServiceProxy virtuals ──────────────────────────────────────────

Result<void> SensorProxy::onConnected() {
    // Nothing special needed — sensor daemon pushes readings unsolicited.
    return Result<void>::ok();
}

void SensorProxy::onDisconnected() {
    std::lock_guard<std::mutex> lk(batch_buf_mutex_);
    batch_buf_.clear();
}

void SensorProxy::ioLoop() {
    // The base class io thread calls ioLoop() in a tight loop while connected.
    // We rely on the epoll/socket path in ServiceProxy::run() for incoming
    // SENSOR_MSG_READING frames; ioLoop is a no-op yielder here because
    // frame dispatch happens via onMessage() (registered with the event loop).
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void SensorProxy::dispatchReading(const ProxyMessage& msg) {
    if (msg.payload_len < sizeof(SensorReading)) return;

    SensorReading reading{};
    std::memcpy(&reading, msg.payload, sizeof(SensorReading));

    // Update latest (mutex-protected because 52-byte struct is not lock-free).
    {
        std::lock_guard<std::mutex> lk(latest_mutex_);
        latest_reading_ = reading;
    }

    // Post reading callback to callback thread.
    threadPool().post([this, reading]() {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (reading_cb_) reading_cb_(reading);
    });

    // Batch accumulation.
    {
        std::lock_guard<std::mutex> lk(batch_buf_mutex_);
        batch_buf_.push_back(reading);

        if (batch_buf_.size() >= batch_min_count_) {
            std::vector<SensorReading> batch = std::move(batch_buf_);
            batch_buf_.reserve(batch_min_count_);

            threadPool().post([this, batch = std::move(batch)]() mutable {
                std::lock_guard<std::mutex> lk2(cb_mutex_);
                if (batch_cb_) batch_cb_(batch);
            });
        }
    }
}

void SensorProxy::dispatchBatch(const ProxyMessage& msg) {
    const size_t n = msg.payload_len / sizeof(SensorReading);
    if (n == 0) return;

    std::vector<SensorReading> batch(n);
    std::memcpy(batch.data(), msg.payload, n * sizeof(SensorReading));

    // Update latest with last reading.
    {
        std::lock_guard<std::mutex> lk(latest_mutex_);
        latest_reading_ = batch.back();
    }

    threadPool().post([this, batch = std::move(batch)]() mutable {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (batch_cb_) batch_cb_(batch);
    });
}

} // namespace middleware
