/**
 * @file    proxy_c_api.cpp
 * @brief   C API shim implementations.
 *
 * Each function converts between the C opaque handle / struct world and the
 * C++ proxy objects, mapping ProxyError → ProxyErrCode.
 */

#include "proxy_c_api.h"
#include "../../audio/audio_proxy.h"
#include "../../camera/camera_proxy.h"
#include "../../sensor/sensor_proxy.h"
#include "../../gpio/gpio_proxy.h"
#include <cstring>

// ── Helper: build ProxyConfig from ProxyCConfig ──────────────────────────────

static middleware::ProxyConfig convert_cfg(const ProxyCConfig* c) {
    middleware::ProxyConfig cfg{};
    if (c) {
        if (c->sm_socket_path)  cfg.sm_socket_path  = c->sm_socket_path;
        if (c->hmac_key_file)   cfg.hmac_key_file   = c->hmac_key_file;
        if (c->shm_name_prefix) cfg.shm_name_prefix = c->shm_name_prefix;
        if (c->connect_timeout_ms > 0)
            cfg.connect_timeout = std::chrono::milliseconds(c->connect_timeout_ms);
        cfg.use_shared_memory = (c->use_shared_memory != 0);
    }
    return cfg;
}

// ── Helper: map ProxyError → ProxyErrCode ────────────────────────────────────
static ProxyErrCode to_c_err(middleware::ProxyError e) {
    using E = middleware::ProxyError;
    switch (e) {
    case E::None:            return PROXY_OK;
    case E::NotConnected:    return PROXY_ERR_NOT_CONNECTED;
    case E::Timeout:         return PROXY_ERR_TIMEOUT;
    case E::AuthFailed:      return PROXY_ERR_AUTH_FAILED;
    case E::InvalidResponse:  return PROXY_ERR_PROTOCOL_ERROR;
    case E::InvalidArgument:  return PROXY_ERR_BAD_ARGUMENT;
    case E::BufferFull:       return PROXY_ERR_BUFFER_FULL;
    case E::ShmSizeMismatch:  return PROXY_ERR_SHM_MISMATCH;
    case E::SendFailed:       return PROXY_ERR_IO_ERROR;
    case E::RecvFailed:       return PROXY_ERR_IO_ERROR;
    case E::InternalError:   return PROXY_ERR_INTERNAL;
    default:                 return PROXY_ERR_UNKNOWN;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// AudioProxy
// ═══════════════════════════════════════════════════════════════════════════

struct ProxyHandle_AudioProxy_ {
    middleware::AudioProxy proxy;
    AudioFrameCb           user_cb   = nullptr;
    void*                  userdata  = nullptr;
};

AudioProxyHandle audio_proxy_create(const ProxyCConfig* cfg) {
    return new ProxyHandle_AudioProxy_{middleware::AudioProxy(convert_cfg(cfg))};
}

void audio_proxy_destroy(AudioProxyHandle h) { delete h; }

ProxyErrCode audio_proxy_connect(AudioProxyHandle h) {
    return to_c_err(h->proxy.connect().isOk()
                    ? middleware::ProxyError::None
                    : h->proxy.connect().error());
}

void audio_proxy_disconnect(AudioProxyHandle h) { h->proxy.disconnect(); }

ProxyErrCode audio_proxy_start_capture(AudioProxyHandle h) {
    auto r = h->proxy.startCapture();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}

ProxyErrCode audio_proxy_stop_capture(AudioProxyHandle h) {
    auto r = h->proxy.stopCapture();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}

void audio_proxy_set_frame_callback(AudioProxyHandle h,
                                    AudioFrameCb cb, void* userdata) {
    h->user_cb  = cb;
    h->userdata = userdata;
    h->proxy.onFrameReady([h](middleware::AudioFrame frame) {
        if (h->user_cb) {
            h->user_cb(reinterpret_cast<const uint8_t*>(frame.data),
                       frame.data_bytes,
                       frame.channels,
                       frame.sample_rate,
                       h->userdata);
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// CameraProxy
// ═══════════════════════════════════════════════════════════════════════════

struct ProxyHandle_CameraProxy_ {
    middleware::CameraProxy proxy;
    CameraFrameCb           user_cb   = nullptr;
    void*                   userdata  = nullptr;
};

CameraProxyHandle camera_proxy_create(const ProxyCConfig* cfg) {
    return new ProxyHandle_CameraProxy_{middleware::CameraProxy(convert_cfg(cfg))};
}
void         camera_proxy_destroy(CameraProxyHandle h) { delete h; }
ProxyErrCode camera_proxy_connect(CameraProxyHandle h) {
    auto r = h->proxy.connect();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
void         camera_proxy_disconnect(CameraProxyHandle h) { h->proxy.disconnect(); }
ProxyErrCode camera_proxy_start_stream(CameraProxyHandle h) {
    auto r = h->proxy.startStream();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
ProxyErrCode camera_proxy_stop_stream(CameraProxyHandle h) {
    auto r = h->proxy.stopStream();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
ProxyErrCode camera_proxy_set_resolution(CameraProxyHandle h,
                                         uint32_t w, uint32_t ht) {
    auto r = h->proxy.setResolution(w, ht);
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
void camera_proxy_set_frame_callback(CameraProxyHandle h,
                                     CameraFrameCb cb, void* userdata) {
    h->user_cb  = cb;
    h->userdata = userdata;
    h->proxy.onFrameReady([h](middleware::CameraFrame frame) {
        if (h->user_cb) {
            h->user_cb(static_cast<const uint8_t*>(frame.data),
                       frame.data_bytes,
                       frame.width, frame.height,
                       static_cast<uint8_t>(frame.format),
                       h->userdata);
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// SensorProxy
// ═══════════════════════════════════════════════════════════════════════════

struct ProxyHandle_SensorProxy_ {
    middleware::SensorProxy proxy;
    SensorReadingCb         user_cb   = nullptr;
    void*                   userdata  = nullptr;
};

SensorProxyHandle sensor_proxy_create(const ProxyCConfig* cfg) {
    return new ProxyHandle_SensorProxy_{middleware::SensorProxy(convert_cfg(cfg))};
}
void         sensor_proxy_destroy(SensorProxyHandle h)      { delete h; }
ProxyErrCode sensor_proxy_connect(SensorProxyHandle h) {
    auto r = h->proxy.connect();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
void sensor_proxy_disconnect(SensorProxyHandle h) { h->proxy.disconnect(); }
ProxyErrCode sensor_proxy_start(SensorProxyHandle h,
                                ProxySensorType type, uint32_t rate_hz) {
    auto r = h->proxy.startSampling(
        static_cast<middleware::SensorReading::Type>(type), rate_hz);
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
ProxyErrCode sensor_proxy_stop(SensorProxyHandle h, ProxySensorType type) {
    auto r = h->proxy.stopSampling(
        static_cast<middleware::SensorReading::Type>(type));
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
void sensor_proxy_set_callback(SensorProxyHandle h,
                               SensorReadingCb cb, void* userdata) {
    h->user_cb  = cb;
    h->userdata = userdata;
    h->proxy.onReadingReady([h](const middleware::SensorReading& sr) {
        if (!h->user_cb) return;
        ProxySensorReading p{};
        p.type         = static_cast<uint8_t>(sr.sensor_type);
        p.timestamp_us = sr.timestamp_us;
        p.x = sr.x; p.y = sr.y; p.z = sr.z;
        p.scalar       = sr.scalar;
        p.sequence     = sr.sequence;
        h->user_cb(&p, h->userdata);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// GpioProxy
// ═══════════════════════════════════════════════════════════════════════════

struct ProxyHandle_GpioProxy_ {
    middleware::GpioProxy proxy;
    GpioEdgeCb            user_cb   = nullptr;
    void*                 userdata  = nullptr;
};

GpioProxyHandle gpio_proxy_create(const ProxyCConfig* cfg) {
    return new ProxyHandle_GpioProxy_{middleware::GpioProxy(convert_cfg(cfg))};
}
void         gpio_proxy_destroy(GpioProxyHandle h)     { delete h; }
ProxyErrCode gpio_proxy_connect(GpioProxyHandle h) {
    auto r = h->proxy.connect();
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
void gpio_proxy_disconnect(GpioProxyHandle h) { h->proxy.disconnect(); }
ProxyErrCode gpio_proxy_configure(GpioProxyHandle h, uint32_t pin,
                                  ProxyGpioDir dir, ProxyGpioEdge edge) {
    auto r = h->proxy.configurePin(pin,
        static_cast<middleware::GpioDirection>(dir),
        static_cast<middleware::GpioEdge>(edge));
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
ProxyErrCode gpio_proxy_set(GpioProxyHandle h, uint32_t pin, uint8_t value) {
    auto r = h->proxy.setPin(pin, value);
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
ProxyErrCode gpio_proxy_get(GpioProxyHandle h, uint32_t pin,
                            uint8_t* out_value) {
    auto r = h->proxy.getPin(pin);
    if (r.isErr()) return to_c_err(r.error());
    if (out_value) *out_value = r.value();
    return PROXY_OK;
}
ProxyErrCode gpio_proxy_watch(GpioProxyHandle h, uint32_t pin,
                              ProxyGpioEdge edge) {
    auto r = h->proxy.watchPin(pin, static_cast<middleware::GpioEdge>(edge));
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
ProxyErrCode gpio_proxy_unwatch(GpioProxyHandle h, uint32_t pin) {
    auto r = h->proxy.unwatchPin(pin);
    return to_c_err(r.isErr() ? r.error() : middleware::ProxyError::None);
}
void gpio_proxy_set_edge_callback(GpioProxyHandle h,
                                  GpioEdgeCb cb, void* userdata) {
    h->user_cb  = cb;
    h->userdata = userdata;
    h->proxy.onEdgeEvent([h](const middleware::GpioEvent& evt) {
        if (!h->user_cb) return;
        ProxyGpioEvent e{evt.pin, evt.value,
                         static_cast<uint8_t>(evt.edge), evt.timestamp_us};
        h->user_cb(&e, h->userdata);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════════════════════

const char* proxy_err_str(ProxyErrCode code) {
    switch (code) {
    case PROXY_OK:                  return "OK";
    case PROXY_ERR_NOT_CONNECTED:   return "not connected";
    case PROXY_ERR_TIMEOUT:         return "timeout";
    case PROXY_ERR_AUTH_FAILED:     return "authentication failed";
    case PROXY_ERR_PROTOCOL_ERROR:  return "protocol error";
    case PROXY_ERR_SERVICE_BUSY:    return "service busy";
    case PROXY_ERR_BAD_ARGUMENT:    return "bad argument";
    case PROXY_ERR_BUFFER_FULL:     return "buffer full";
    case PROXY_ERR_SHM_MISMATCH:    return "SHM size mismatch";
    case PROXY_ERR_IO_ERROR:        return "I/O error";
    case PROXY_ERR_INTERNAL:        return "internal error";
    default:                        return "unknown error";
    }
}
