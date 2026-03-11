/**
 * @file    proxy_c_api.h
 * @brief   Pure-C opaque-handle API for the middleware proxy layer.
 *
 * Usable from plain C and other FFI languages (Python ctypes, Rust FFI, etc.).
 * Each handle wraps a heap-allocated C++ proxy object.
 *
 * Error codes map 1-to-1 to ProxyError.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handle types ─────────────────────────────────────────────────── */
typedef struct ProxyHandle_AudioProxy_   *AudioProxyHandle;
typedef struct ProxyHandle_CameraProxy_  *CameraProxyHandle;
typedef struct ProxyHandle_SensorProxy_  *SensorProxyHandle;
typedef struct ProxyHandle_GpioProxy_    *GpioProxyHandle;

/* ── Error codes (mirror ProxyError enum) ───────────────────────────────── */
typedef enum {
    PROXY_OK                  =  0,
    PROXY_ERR_UNKNOWN         =  1,
    PROXY_ERR_NOT_CONNECTED   =  2,
    PROXY_ERR_TIMEOUT         =  3,
    PROXY_ERR_AUTH_FAILED     =  4,
    PROXY_ERR_PROTOCOL_ERROR  =  5,
    PROXY_ERR_SERVICE_BUSY    =  6,
    PROXY_ERR_BAD_ARGUMENT    =  7,
    PROXY_ERR_BUFFER_FULL     =  8,
    PROXY_ERR_SHM_MISMATCH    =  9,
    PROXY_ERR_IO_ERROR        = 10,
    PROXY_ERR_INTERNAL        = 99,
} ProxyErrCode;

/* ── Configuration ───────────────────────────────────────────────────────── */
typedef struct {
    const char *sm_socket_path;     /**< Service manager socket path; NULL = default. */
    const char *hmac_key_file;      /**< HMAC key file path; NULL = dev/no-auth mode. */
    int         connect_timeout_ms; /**< 0 = default (3000 ms). */
    int         use_shared_memory;  /**< 0 = disable SHM, 1 = enable (default). */
    const char *shm_name_prefix;    /**< NULL = default "/middleware_proxy_". */
} ProxyCConfig;

/* ── Sensor type codes (mirror SensorReading::Type) ─────────────────────── */
typedef enum {
    PROXY_SENSOR_UNKNOWN       = 0,
    PROXY_SENSOR_ACCELEROMETER = 1,
    PROXY_SENSOR_GYROSCOPE     = 2,
    PROXY_SENSOR_MAGNETOMETER  = 3,
    PROXY_SENSOR_TEMPERATURE   = 4,
    PROXY_SENSOR_PRESSURE      = 5,
    PROXY_SENSOR_HUMIDITY      = 6,
    PROXY_SENSOR_LIGHT         = 7,
    PROXY_SENSOR_PROXIMITY     = 8,
    PROXY_SENSOR_BAROMETER     = 9,
} ProxySensorType;

/* ── GPIO types ──────────────────────────────────────────────────────────── */
typedef enum { PROXY_GPIO_INPUT = 0, PROXY_GPIO_OUTPUT = 1 } ProxyGpioDir;
typedef enum {
    PROXY_GPIO_EDGE_NONE    = 0,
    PROXY_GPIO_EDGE_RISING  = 1,
    PROXY_GPIO_EDGE_FALLING = 2,
    PROXY_GPIO_EDGE_BOTH    = 3,
} ProxyGpioEdge;

typedef struct {
    uint32_t pin;
    uint8_t  value;
    uint8_t  edge;
    uint64_t timestamp_us;
} ProxyGpioEvent;

/* ─────────────────────────────────────────────────────────────────────────── */
/* AudioProxy C API                                                            */
/* ─────────────────────────────────────────────────────────────────────────── */

/** @brief Allocate an AudioProxy (not yet connected). */
AudioProxyHandle audio_proxy_create(const ProxyCConfig *cfg);

/** @brief Release all resources (calls disconnect first). */
void audio_proxy_destroy(AudioProxyHandle h);

/** @brief Connect to the audio HAL daemon. */
ProxyErrCode audio_proxy_connect(AudioProxyHandle h);

/** @brief Disconnect. */
void audio_proxy_disconnect(AudioProxyHandle h);

/** @brief Begin audio capture. */
ProxyErrCode audio_proxy_start_capture(AudioProxyHandle h);

/** @brief Stop audio capture. */
ProxyErrCode audio_proxy_stop_capture(AudioProxyHandle h);

/**
 * @brief Register a frame-ready callback.
 * @param cb  Called on the proxy callback thread.
 *            data points to a COPY of the frame; valid only during the call.
 */
typedef void (*AudioFrameCb)(const uint8_t *data, size_t bytes,
                             uint32_t channels, uint32_t sample_rate,
                             void *userdata);

void audio_proxy_set_frame_callback(AudioProxyHandle h,
                                    AudioFrameCb cb, void *userdata);

/* ─────────────────────────────────────────────────────────────────────────── */
/* CameraProxy C API                                                           */
/* ─────────────────────────────────────────────────────────────────────────── */

CameraProxyHandle camera_proxy_create(const ProxyCConfig *cfg);
void              camera_proxy_destroy(CameraProxyHandle h);
ProxyErrCode      camera_proxy_connect(CameraProxyHandle h);
void              camera_proxy_disconnect(CameraProxyHandle h);
ProxyErrCode      camera_proxy_start_stream(CameraProxyHandle h);
ProxyErrCode      camera_proxy_stop_stream(CameraProxyHandle h);
ProxyErrCode      camera_proxy_set_resolution(CameraProxyHandle h,
                                              uint32_t width, uint32_t height);

typedef void (*CameraFrameCb)(const uint8_t *data, size_t bytes,
                              uint32_t width, uint32_t height,
                              uint8_t format, void *userdata);

void camera_proxy_set_frame_callback(CameraProxyHandle h,
                                     CameraFrameCb cb, void *userdata);

/* ─────────────────────────────────────────────────────────────────────────── */
/* SensorProxy C API                                                           */
/* ─────────────────────────────────────────────────────────────────────────── */

SensorProxyHandle sensor_proxy_create(const ProxyCConfig *cfg);
void              sensor_proxy_destroy(SensorProxyHandle h);
ProxyErrCode      sensor_proxy_connect(SensorProxyHandle h);
void              sensor_proxy_disconnect(SensorProxyHandle h);
ProxyErrCode      sensor_proxy_start(SensorProxyHandle h,
                                     ProxySensorType type, uint32_t rate_hz);
ProxyErrCode      sensor_proxy_stop(SensorProxyHandle h, ProxySensorType type);

typedef struct {
    uint8_t  type;
    uint64_t timestamp_us;
    float    x, y, z, scalar;
    uint32_t sequence;
} ProxySensorReading;

typedef void (*SensorReadingCb)(const ProxySensorReading *r, void *userdata);

void sensor_proxy_set_callback(SensorProxyHandle h,
                               SensorReadingCb cb, void *userdata);

/* ─────────────────────────────────────────────────────────────────────────── */
/* GpioProxy C API                                                             */
/* ─────────────────────────────────────────────────────────────────────────── */

GpioProxyHandle gpio_proxy_create(const ProxyCConfig *cfg);
void            gpio_proxy_destroy(GpioProxyHandle h);
ProxyErrCode    gpio_proxy_connect(GpioProxyHandle h);
void            gpio_proxy_disconnect(GpioProxyHandle h);
ProxyErrCode    gpio_proxy_configure(GpioProxyHandle h, uint32_t pin,
                                     ProxyGpioDir dir, ProxyGpioEdge edge);
ProxyErrCode    gpio_proxy_set(GpioProxyHandle h, uint32_t pin, uint8_t value);
ProxyErrCode    gpio_proxy_get(GpioProxyHandle h, uint32_t pin, uint8_t *out_value);
ProxyErrCode    gpio_proxy_watch(GpioProxyHandle h, uint32_t pin, ProxyGpioEdge edge);
ProxyErrCode    gpio_proxy_unwatch(GpioProxyHandle h, uint32_t pin);

typedef void (*GpioEdgeCb)(const ProxyGpioEvent *evt, void *userdata);

void gpio_proxy_set_edge_callback(GpioProxyHandle h,
                                  GpioEdgeCb cb, void *userdata);

/* ── Utility ────────────────────────────────────────────────────────────── */
/** @brief Return a human-readable string for an error code. */
const char *proxy_err_str(ProxyErrCode code);

#ifdef __cplusplus
} // extern "C"
#endif
