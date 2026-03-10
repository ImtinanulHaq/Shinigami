/**
 * @file sensor_service_hal.c
 * @brief Sensor HAL integration — create, open, start, read, stop, close.
 */

#include "sensor_service_hal.h"

#include <string.h>
#include <syslog.h>

/* Sensor HAL */
#include "../../dev/hal/layers/sensors/sensor_hal.h"

int sensor_service_hal_init(sensor_service_ctx_t *ctx)
{
    if (!ctx) return SVC_ERR_INVALID;

    sensor_config_t scfg = sensor_hal_default_config(
        (sensor_type_t)ctx->sensor_type);
    scfg.sampling_rate_hz = ctx->sampling_rate_hz;
    scfg.enable_buffer    = ctx->enable_buffer;

    hw_device_t *dev = sensor_hal_create(SENSOR_SERVICE_NAME,
                                         ctx->iio_device, &scfg);
    if (!dev) {
        SVC_ERR("sensor_hal_create failed for %s", ctx->iio_device);
        return SVC_ERR_HAL;
    }

    if (dev->ops && dev->ops->open) {
        int rc = dev->ops->open(dev);
        if (rc != 0) {
            SVC_ERR("sensor HAL open failed: %d", rc);
            sensor_hal_destroy(dev);
            return SVC_ERR_HAL;
        }
    }

    ctx->hal_device = dev;
    SVC_INFO("sensor HAL initialised (iio=%s type=%s rate=%uHz)",
             ctx->iio_device,
             sensor_hal_type_string((sensor_type_t)ctx->sensor_type),
             ctx->sampling_rate_hz);
    return SVC_OK;
}

int sensor_service_hal_start(sensor_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->start) {
        int rc = dev->ops->start(dev);
        if (rc != 0) {
            SVC_ERR("sensor HAL start failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("sensor HAL started");
    return SVC_OK;
}

int sensor_service_hal_read_3axis(sensor_service_ctx_t *ctx,
                                  svc_sensor_3axis_t *out)
{
    if (!ctx || !ctx->hal_device || !out) return SVC_ERR_INVALID;

    sensor_data_3axis_t data;
    int rc = sensor_hal_read_3axis((hw_device_t *)ctx->hal_device, &data);
    if (rc != HAL_SUCCESS) {
        SVC_DBG("sensor_hal_read_3axis: %d", rc);
        return SVC_ERR_HAL;
    }

    out->x = data.x;
    out->y = data.y;
    out->z = data.z;
    out->timestamp = data.timestamp;
    return SVC_OK;
}

int sensor_service_hal_read_scalar(sensor_service_ctx_t *ctx, float *value,
                                   uint64_t *timestamp)
{
    if (!ctx || !ctx->hal_device || !value) return SVC_ERR_INVALID;

    sensor_data_1axis_t data;
    int rc = sensor_hal_read_1axis((hw_device_t *)ctx->hal_device, &data);
    if (rc != HAL_SUCCESS) {
        SVC_DBG("sensor_hal_read_1axis: %d", rc);
        return SVC_ERR_HAL;
    }

    *value = data.value;
    if (timestamp) *timestamp = data.timestamp;
    return SVC_OK;
}

int sensor_service_hal_stop(sensor_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->stop) {
        int rc = dev->ops->stop(dev);
        if (rc != 0) {
            SVC_WARN("sensor HAL stop failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("sensor HAL stopped");
    return SVC_OK;
}

void sensor_service_hal_cleanup(sensor_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->stop)
        dev->ops->stop(dev);

    if (dev->ops && dev->ops->close)
        dev->ops->close(dev);

    sensor_hal_destroy(dev);
    ctx->hal_device = NULL;
    SVC_INFO("sensor HAL cleaned up");
}
