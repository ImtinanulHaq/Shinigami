/**
 * @file camera_service_hal.c
 * @brief Camera HAL integration — create, open, start, capture, stop, close.
 *
 * Uses camera_hal_create() and the hw_device_t ops vtable plus the
 * camera_hal_capture_frame() / camera_hal_return_frame() helpers.
 */

#include "camera_service_hal.h"

#include <string.h>
#include <syslog.h>

/* Camera HAL */
#include "../../dev/hal/layers/camera/camera_hal.h"

int camera_service_hal_init(camera_service_ctx_t *ctx)
{
    if (!ctx) return SVC_ERR_INVALID;

    camera_config_t ccfg;
    ccfg.width        = ctx->width;
    ccfg.height       = ctx->height;
    ccfg.format       = (camera_format_t)ctx->format;
    ccfg.fps          = ctx->fps;
    ccfg.buffer_count = ctx->buffer_count;

    /* Validate the V4L2 path before opening */
    if (camera_hal_validate_device_path(ctx->v4l2_device) != HAL_SUCCESS) {
        SVC_ERR("invalid V4L2 device path: %s", ctx->v4l2_device);
        return SVC_ERR_HAL;
    }

    hw_device_t *dev = camera_hal_create(CAMERA_SERVICE_NAME,
                                         ctx->v4l2_device, &ccfg);
    if (!dev) {
        SVC_ERR("camera_hal_create failed for %s", ctx->v4l2_device);
        return SVC_ERR_HAL;
    }

    if (dev->ops && dev->ops->open) {
        int rc = dev->ops->open(dev);
        if (rc != 0) {
            SVC_ERR("camera HAL open failed: %d", rc);
            camera_hal_destroy(dev);
            return SVC_ERR_HAL;
        }
    }

    ctx->hal_device = dev;
    SVC_INFO("camera HAL initialised (%s %ux%u @%ufps)",
             ctx->v4l2_device, ctx->width, ctx->height, ctx->fps);
    return SVC_OK;
}

int camera_service_hal_start(camera_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->start) {
        int rc = dev->ops->start(dev);
        if (rc != 0) {
            SVC_ERR("camera HAL start failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("camera HAL streaming started");
    return SVC_OK;
}

int camera_service_hal_capture(camera_service_ctx_t *ctx,
                               void **frame_data, size_t *frame_size,
                               uint32_t *buffer_index)
{
    if (!ctx || !ctx->hal_device || !frame_data || !frame_size)
        return SVC_ERR_INVALID;

    camera_frame_t frame;
    int rc = camera_hal_capture_frame((hw_device_t *)ctx->hal_device,
                                      &frame, 1000 /* 1 s timeout */);
    if (rc != HAL_SUCCESS) {
        SVC_DBG("camera capture returned %d", rc);
        return SVC_ERR_HAL;
    }

    *frame_data   = frame.data;
    *frame_size   = frame.size;
    if (buffer_index)
        *buffer_index = frame._buffer_index;

    return SVC_OK;
}

int camera_service_hal_return(camera_service_ctx_t *ctx,
                              uint32_t buffer_index)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;

    camera_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame._buffer_index = buffer_index;

    camera_hal_return_frame((hw_device_t *)ctx->hal_device, &frame);
    return SVC_OK;
}

int camera_service_hal_stop(camera_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->stop) {
        int rc = dev->ops->stop(dev);
        if (rc != 0) {
            SVC_WARN("camera HAL stop failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("camera HAL streaming stopped");
    return SVC_OK;
}

void camera_service_hal_cleanup(camera_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->close)
        dev->ops->close(dev);

    camera_hal_destroy(dev);
    ctx->hal_device = NULL;
    SVC_INFO("camera HAL cleaned up");
}
