/**
 * @file    gpio_proxy.h
 * @brief   GpioProxy — digital I/O pin control via the GPIO HAL daemon.
 *
 * Messages: 0x0400–0x04FF
 */
#pragma once

#include "../base/service_proxy.h"
#include <functional>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace middleware {

/** @brief Message type constants for the GPIO protocol. */
enum GpioMessageType : uint16_t {
    GPIO_MSG_CONFIGURE    = 0x0401,
    GPIO_MSG_SET          = 0x0402,
    GPIO_MSG_GET          = 0x0403,
    GPIO_MSG_EDGE_EVENT   = 0x040A,
    GPIO_MSG_WATCH        = 0x0404,
    GPIO_MSG_UNWATCH      = 0x0405,
};

/** @brief Pin direction. */
enum class GpioDirection : uint8_t { Input = 0, Output = 1 };

/** @brief Edge detection mode. */
enum class GpioEdge : uint8_t { None = 0, Rising = 1, Falling = 2, Both = 3 };

/** @brief Describes a GPIO edge event delivered to the callback. */
struct GpioEvent {
    uint32_t    pin;          ///< Pin number that fired.
    uint8_t     value;        ///< Logical level after the edge.
    GpioEdge    edge;         ///< Which edge was detected.
    uint64_t    timestamp_us; ///< Kernel monotonic timestamp in microseconds.
};

/** @brief Type of per-pin and global edge callbacks. */
using GpioEdgeCallback = std::function<void(const GpioEvent&)>;

/**
 * @class  GpioProxy
 * @brief  Control GPIO pins and subscribe to edge events.
 *
 * Thread-safe: all public methods may be called from any thread.
 */
class PROXY_API GpioProxy final : public ServiceProxy {
public:
    explicit GpioProxy(ProxyConfig cfg = {});
    ~GpioProxy() override;

    // ---- Pin configuration ----
    Result<void> configurePin(uint32_t pin,
                               GpioDirection direction,
                               GpioEdge edge = GpioEdge::None);

    // ---- Output control ----
    Result<void> setPin(uint32_t pin, uint8_t value);

    // ---- Input read ----
    Result<uint8_t> getPin(uint32_t pin);

    // ---- Async edge subscription ----
    /** @brief Watch for edges on @p pin.  Installs a per-pin watch at the daemon. */
    Result<void> watchPin(uint32_t pin, GpioEdge edge);
    Result<void> unwatchPin(uint32_t pin);

    /** @brief Per-pin callback — invoked on the callback thread. */
    void onPinEvent(uint32_t pin, GpioEdgeCallback cb);

    /** @brief Global callback for ALL watched pins — invoked on the callback thread. */
    void onEdgeEvent(GpioEdgeCallback cb);

private:
    Result<void> onConnected()    override;
    void         onDisconnected() override;
    void         ioLoop()         override;

    void dispatchEdgeEvent(const ProxyMessage& msg);

    GpioEdgeCallback                                    global_cb_;
    std::unordered_map<uint32_t, GpioEdgeCallback>      per_pin_cb_;
    mutable std::mutex                                  cb_mutex_;
};

} // namespace middleware
