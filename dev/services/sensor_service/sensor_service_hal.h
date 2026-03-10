/**
 * @file sensor_service_hal.h
 * @brief Sensor HAL integration interface.
 */

#ifndef SENSOR_SERVICE_HAL_H
#define SENSOR_SERVICE_HAL_H

#include "sensor_service.h"

/* Forward declare from sensor_hal.h */
typedef struct { float x; float y; float z; uint64_t timestamp; }
    svc_sensor_3axis_t;

int  sensor_service_hal_init(sensor_service_ctx_t *ctx);
int  sensor_service_hal_start(sensor_service_ctx_t *ctx);
int  sensor_service_hal_read_3axis(sensor_service_ctx_t *ctx,
                                   svc_sensor_3axis_t *out);
int  sensor_service_hal_read_scalar(sensor_service_ctx_t *ctx, float *value,
                                    uint64_t *timestamp);
int  sensor_service_hal_stop(sensor_service_ctx_t *ctx);
void sensor_service_hal_cleanup(sensor_service_ctx_t *ctx);

#endif /* SENSOR_SERVICE_HAL_H */
