/**
 * @file sensor_service.h
 * @brief Public types, structs, and API for the Sensor Service daemon.
 */

#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "../common/service_base.h"
#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "hal_interface.h"
#include "../../dev/core/memory_pool.h"

#include <stdint.h>

/* ── defaults ─────────────────────────────────────────────────────────── */

#define SENSOR_SERVICE_NAME             "sensor_service"
#define SENSOR_SERVICE_CONF             "/etc/sensor_service/sensor_service.conf"
#define SENSOR_SERVICE_DEFAULT_IIO_DEV  "iio:device0"
#define SENSOR_SERVICE_DEFAULT_RATE     100   /* Hz */
#define SENSOR_SERVICE_DEFAULT_TYPE     1     /* SENSOR_TYPE_ACCEL */
#define SENSOR_SERVICE_DEFAULT_BUFFER   0     /* sysfs mode */

/* ── context ──────────────────────────────────────────────────────────── */

typedef struct {
    svc_context_t   base;
    svc_ipc_t           ipc;
    service_config_t    config;
    void           *hal_device;    /**< hw_device_t* from sensor HAL        */
    int             security_applied;

    /* Sensor-specific config */
    char            iio_device[64];
    uint32_t        sampling_rate_hz;
    int             sensor_type;   /**< sensor_type_t enum value            */
    int             enable_buffer; /**< Non-zero for IIO buffer mode        */

    /* Memory pools */
    memory_pool_t  *reading_pool;  /**< sensor_reading_t structs (32 B × 32). */
    memory_pool_t  *ipc_pool;      /**< IPC message buffers (512 B × 64).     */
} sensor_service_ctx_t;

/* ── lifecycle API ────────────────────────────────────────────────────── */

int  sensor_service_init(sensor_service_ctx_t *ctx, int argc, char **argv);
int  sensor_service_run(sensor_service_ctx_t *ctx);
void sensor_service_shutdown(sensor_service_ctx_t *ctx);

#endif /* SENSOR_SERVICE_H */
