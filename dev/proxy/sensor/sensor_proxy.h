/**
 * @file    sensor_proxy.h
 * @brief   SensorProxy — subscribe to sensor readings from the HAL daemon.
 *
 * Messages: 0x0300–0x03FF
 * Sensor readings are small and always travel over the Unix socket (no SHM).
 */
#pragma once

#include "../base/service_proxy.h"
#include "sensor_reading.h"
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>

namespace middleware {

/** @brief Message type constants for the sensor protocol. */
enum SensorMessageType : uint16_t {
    SENSOR_MSG_START_SAMPLING  = 0x0301,
    SENSOR_MSG_STOP_SAMPLING   = 0x0302,
    SENSOR_MSG_SET_RATE        = 0x0303,
    SENSOR_MSG_GET_LATEST      = 0x0304,
    SENSOR_MSG_READING         = 0x030A,
    SENSOR_MSG_BATCH_READY     = 0x030B,
    SENSOR_MSG_GET_DEVICE_INFO = 0x0307,
};

using SensorReadingCallback      = std::function<void(const SensorReading&)>;
using SensorBatchCallback        = std::function<void(const std::vector<SensorReading>&)>;

/**
 * @class  SensorProxy
 * @brief  Subscribes to periodic sensor samples from the sensor HAL daemon.
 *
 * There is NO shared memory path for sensors; readings are small enough that
 * socket delivery (via sm_hdr_t framing) is sufficient even at 1 kHz.
 */
class PROXY_API SensorProxy final : public ServiceProxy {
public:
    explicit SensorProxy(ProxyConfig cfg = {});
    ~SensorProxy() override;

    // ---- Sampling control ----
    Result<void> startSampling(SensorReading::Type type, uint32_t rate_hz = 100);
    Result<void> stopSampling(SensorReading::Type type);

    // ---- Synchronous latest value (single-shot, blocks until reply) ----
    Result<SensorReading> getLatestReading(SensorReading::Type type);

    // ---- Async callbacks ----
    /** @brief Called on callback thread for every incoming sample. */
    void onReadingReady(SensorReadingCallback cb);

    /** @brief Called on callback thread when at least @p min_count samples arrived. */
    void onBatchReady(size_t min_count, SensorBatchCallback cb);

private:
    Result<void> onConnected()    override;
    void         onDisconnected() override;
    void         ioLoop()         override;

    void dispatchReading(const ProxyMessage& msg);
    void dispatchBatch(const ProxyMessage& msg);

    // Latest reading per sensor type — lock-free atomic copy-out pattern.
    // Using mutex here because SensorReading is 52 bytes (not atomically copyable).
    SensorReading          latest_reading_;
    mutable std::mutex     latest_mutex_;

    SensorReadingCallback  reading_cb_;
    SensorBatchCallback    batch_cb_;
    size_t                 batch_min_count_{8};
    mutable std::mutex     cb_mutex_;

    std::vector<SensorReading>  batch_buf_;
    std::mutex                  batch_buf_mutex_;
};

} // namespace middleware
