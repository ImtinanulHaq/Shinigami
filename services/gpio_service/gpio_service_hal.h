/**
 * @file gpio_service_hal.h
 * @brief GPIO HAL integration interface.
 */

#ifndef GPIO_SERVICE_HAL_H
#define GPIO_SERVICE_HAL_H

#include "gpio_service.h"

int  gpio_service_hal_init(gpio_service_ctx_t *ctx);
int  gpio_service_hal_start(gpio_service_ctx_t *ctx);
int  gpio_service_hal_set_value(gpio_service_ctx_t *ctx, int value);
int  gpio_service_hal_get_value(gpio_service_ctx_t *ctx, int *value);
int  gpio_service_hal_wait_interrupt(gpio_service_ctx_t *ctx,
                                     uint32_t timeout_ms);
int  gpio_service_hal_stop(gpio_service_ctx_t *ctx);
void gpio_service_hal_cleanup(gpio_service_ctx_t *ctx);

#endif /* GPIO_SERVICE_HAL_H */
