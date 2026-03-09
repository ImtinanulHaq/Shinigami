/**
 * @file gpio_service_hal.c
 * @brief GPIO HAL integration — create, open, set/get, interrupt, destroy.
 */

#include "gpio_service_hal.h"

#include <string.h>
#include <syslog.h>

/* GPIO HAL */
#include "../../dev/hal/layers/gpio/gpio_hal.h"

int gpio_service_hal_init(gpio_service_ctx_t *ctx)
{
    if (!ctx) return SVC_ERR_INVALID;

    gpio_config_t gcfg = gpio_hal_default_config(ctx->pin_number);
    gcfg.direction     = (gpio_direction_t)ctx->direction;
    gcfg.initial_value = (gpio_value_t)ctx->initial_value;
    gcfg.edge          = (gpio_edge_t)ctx->edge;

    hw_device_t *dev = gpio_hal_create(GPIO_SERVICE_NAME, &gcfg);
    if (!dev) {
        SVC_ERR("gpio_hal_create failed for pin %u", ctx->pin_number);
        return SVC_ERR_HAL;
    }

    if (dev->ops && dev->ops->open) {
        int rc = dev->ops->open(dev);
        if (rc != 0) {
            SVC_ERR("GPIO HAL open failed: %d", rc);
            gpio_hal_destroy(dev);
            return SVC_ERR_HAL;
        }
    }

    ctx->hal_device = dev;
    SVC_INFO("GPIO HAL initialised (pin=%u dir=%s)",
             ctx->pin_number,
             ctx->direction == 0 ? "input" : "output");
    return SVC_OK;
}

int gpio_service_hal_start(gpio_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->start) {
        int rc = dev->ops->start(dev);
        if (rc != 0) {
            SVC_ERR("GPIO HAL start failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("GPIO HAL started (pin=%u)", ctx->pin_number);
    return SVC_OK;
}

int gpio_service_hal_set_value(gpio_service_ctx_t *ctx, int value)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    int rc = gpio_hal_set_value((hw_device_t *)ctx->hal_device,
                                (gpio_value_t)value);
    return (rc == HAL_SUCCESS) ? SVC_OK : SVC_ERR_HAL;
}

int gpio_service_hal_get_value(gpio_service_ctx_t *ctx, int *value)
{
    if (!ctx || !ctx->hal_device || !value) return SVC_ERR_INVALID;

    gpio_value_t v;
    int rc = gpio_hal_get_value((hw_device_t *)ctx->hal_device, &v);
    if (rc != HAL_SUCCESS) return SVC_ERR_HAL;
    *value = (int)v;
    return SVC_OK;
}

int gpio_service_hal_wait_interrupt(gpio_service_ctx_t *ctx,
                                    uint32_t timeout_ms)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    int rc = gpio_hal_wait_interrupt((hw_device_t *)ctx->hal_device,
                                     timeout_ms);
    return (rc == HAL_SUCCESS) ? SVC_OK : SVC_ERR_HAL;
}

int gpio_service_hal_stop(gpio_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return SVC_ERR_INVALID;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->stop) {
        int rc = dev->ops->stop(dev);
        if (rc != 0) {
            SVC_WARN("GPIO HAL stop failed: %d", rc);
            return SVC_ERR_HAL;
        }
    }

    SVC_INFO("GPIO HAL stopped");
    return SVC_OK;
}

void gpio_service_hal_cleanup(gpio_service_ctx_t *ctx)
{
    if (!ctx || !ctx->hal_device) return;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    if (dev->ops && dev->ops->close)
        dev->ops->close(dev);

    gpio_hal_destroy(dev);
    ctx->hal_device = NULL;
    SVC_INFO("GPIO HAL cleaned up");
}
