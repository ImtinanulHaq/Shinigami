/**
 * @file audio_service_hal.c
 * @brief Audio HAL integration — create, open, start, read, write, stop, close.
 *
 * Translates audio_service_ctx_t configuration into audio_hal API calls,
 * using the hw_device_t* vtable (ops->open, ops->start, etc.).
 */

#include "audio_service_hal.h"

#include <string.h>
#include <syslog.h>

/* HAL headers */
#include "../../dev/hal/layers/audio/audio_hal.h"

/* ── public API ───────────────────────────────────────────────────────── */

int audio_service_hal_init(audio_service_ctx_t *ctx)
{
    if (!ctx)
        return SVC_ERR_INVALID;

    /* Build HAL config from service-level settings */
    audio_config_t acfg = audio_hal_default_config(
        (audio_direction_t)ctx->direction);
    acfg.sample_rate  = ctx->sample_rate;
    acfg.channels     = ctx->channels;
    acfg.format       = (audio_format_t)ctx->format;
    acfg.period_size  = ctx->period_size;
    acfg.buffer_size  = ctx->buffer_size;

    hw_device_t *dev = audio_hal_create(AUDIO_SERVICE_NAME,
                                        ctx->alsa_device, &acfg);
    if (!dev) {
        SVC_ERR("audio_hal_create failed for %s", ctx->alsa_device);
        return SVC_ERR_HAL;
    }

    /* Open the ALSA device */
    if (dev->ops && dev->ops->open) {
        int rc = dev->ops->open(dev);
        if (rc != 0) {
            SVC_ERR("audio HAL open failed: %d", rc);
            audio_hal_destroy(dev);
            return SVC_ERR_HAL;
        }
    }

    ctx->hal_device = dev;
    SVC_INFO("audio HAL initialised (device=%s rate=%u ch=%u)",
             ctx->alsa_device, ctx->sample_rate, ctx->channels);
    return SVC_OK;
}

int audio_service_hal_start(audio_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device)
        return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    if (dev->ops && dev->ops->start) {
        int rc = dev->ops->start(dev);
        if (rc != 0) {
            SVC_ERR("audio HAL start failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("audio HAL started (streaming)");
    return SVC_OK;
}

ssize_t audio_service_hal_read(audio_service_ctx_t *ctx, void *buf,
                               size_t size)
{
    if (!ctx || !ctx->hal_device || !buf)
        return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    if (!dev->ops || !dev->ops->read)
        return SVC_ERR_HAL;

    ssize_t ret = dev->ops->read(dev, buf, size);
    if (ret < 0) {
        SVC_WARN("audio HAL read error: %zd", ret);
        return SVC_ERR_HAL;
    }
    return ret;
}

ssize_t audio_service_hal_write(audio_service_ctx_t *ctx, const void *buf,
                                size_t size)
{
    if (!ctx || !ctx->hal_device || !buf)
        return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    if (!dev->ops || !dev->ops->write)
        return SVC_ERR_HAL;

    ssize_t ret = dev->ops->write(dev, buf, size);
    if (ret < 0) {
        SVC_WARN("audio HAL write error: %zd", ret);
        return SVC_ERR_HAL;
    }
    return ret;
}

int audio_service_hal_stop(audio_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device)
        return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    if (dev->ops && dev->ops->stop) {
        int rc = dev->ops->stop(dev);
        if (rc != 0) {
            SVC_WARN("audio HAL stop failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("audio HAL stopped");
    return SVC_OK;
}

void audio_service_hal_cleanup(audio_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device)
        return;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    /* Stop streaming then close before destroy */
    if (dev->ops && dev->ops->stop)
        dev->ops->stop(dev);

    if (dev->ops && dev->ops->close)
        dev->ops->close(dev);

    audio_hal_destroy(dev);
    ctx->hal_device = NULL;
    SVC_INFO("audio HAL cleaned up");
}
