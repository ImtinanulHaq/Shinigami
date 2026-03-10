/**
 * @file    gpio_proxy.cpp
 * @brief   GpioProxy implementation.
 */

#include "gpio_proxy.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

namespace middleware {

#pragma pack(push, 1)
struct GpioCfgReq   { uint32_t pin; uint8_t direction; uint8_t edge; };
struct GpioSetReq   { uint32_t pin; uint8_t value; };
struct GpioGetReq   { uint32_t pin; };
struct GpioWatchReq { uint32_t pin; uint8_t edge; };
#pragma pack(pop)

// ── ctor / dtor ──────────────────────────────────────────────────────────────

GpioProxy::GpioProxy(ProxyConfig cfg)
    : ServiceProxy("gpio_service", std::move(cfg)) {}

GpioProxy::~GpioProxy() {
    disconnect();
}

// ── public API ───────────────────────────────────────────────────────────────

Result<void> GpioProxy::configurePin(uint32_t pin,
                                     GpioDirection direction,
                                     GpioEdge edge)
{
    GpioCfgReq req{pin,
                   static_cast<uint8_t>(direction),
                   static_cast<uint8_t>(edge)};
    auto r = sendRequestWaitReply(GPIO_MSG_CONFIGURE, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<void> GpioProxy::setPin(uint32_t pin, uint8_t value) {
    GpioSetReq req{pin, value};
    auto r = sendRequestWaitReply(GPIO_MSG_SET, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<uint8_t> GpioProxy::getPin(uint32_t pin) {
    GpioGetReq req{pin};
    auto r = sendRequestWaitReply(GPIO_MSG_GET, &req, sizeof(req));
    if (r.isErr()) return Result<uint8_t>::err(r.error());

    const auto& msg = r.value();
    if (msg.payload_len < 1)
        return Result<uint8_t>::err(ProxyError::InvalidResponse,
                                    "empty GPIO_MSG_GET reply");
    uint8_t level = 0;
    std::memcpy(&level, msg.payload, sizeof(level));
    return Result<uint8_t>::ok(level);
}

Result<void> GpioProxy::watchPin(uint32_t pin, GpioEdge edge) {
    GpioWatchReq req{pin, static_cast<uint8_t>(edge)};
    auto r = sendRequestWaitReply(GPIO_MSG_WATCH, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

Result<void> GpioProxy::unwatchPin(uint32_t pin) {
    GpioWatchReq req{pin, static_cast<uint8_t>(GpioEdge::None)};
    auto r = sendRequestWaitReply(GPIO_MSG_UNWATCH, &req, sizeof(req));
    if (r.isErr()) return Result<void>::err(r.error(), r.detail());
    releaseBuffer(r.value());
    return Result<void>::ok();
}

void GpioProxy::onPinEvent(uint32_t pin, GpioEdgeCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    if (cb) per_pin_cb_[pin] = std::move(cb);
    else    per_pin_cb_.erase(pin);
}

void GpioProxy::onEdgeEvent(GpioEdgeCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    global_cb_ = std::move(cb);
}

// ── private: ServiceProxy virtuals ──────────────────────────────────────────

Result<void> GpioProxy::onConnected()    { return Result<void>::ok(); }
void GpioProxy::onDisconnected() {}

void GpioProxy::ioLoop() {
    // Event loop in ServiceProxy handles incoming GPIO_MSG_EDGE_EVENT frames.
    // ioLoop is a yielder; real dispatch lives in dispatchEdgeEvent.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void GpioProxy::dispatchEdgeEvent(const ProxyMessage& msg) {
    if (msg.payload_len < sizeof(GpioEvent)) return;

    GpioEvent evt{};
    std::memcpy(&evt, msg.payload, sizeof(GpioEvent));

    threadPool().post([this, evt]() {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        // Fire per-pin first.
        auto it = per_pin_cb_.find(evt.pin);
        if (it != per_pin_cb_.end() && it->second) it->second(evt);
        // Then global.
        if (global_cb_) global_cb_(evt);
    });
}

} // namespace middleware
