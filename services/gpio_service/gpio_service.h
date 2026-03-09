/**
 * @file gpio_service.h
 * @brief Public types, structs, and API for the GPIO Service daemon.
 */

#ifndef GPIO_SERVICE_H
#define GPIO_SERVICE_H

#include "../common/service_base.h"
#include "../common/service_config.h"
#include "../common/service_ipc.h"

#include <stdint.h>

/* ── defaults ─────────────────────────────────────────────────────────── */

#define GPIO_SERVICE_NAME             "gpio_service"
#define GPIO_SERVICE_CONF             "/etc/gpio_service/gpio_service.conf"
#define GPIO_SERVICE_DEFAULT_PIN      4
#define GPIO_SERVICE_DEFAULT_DIR      1   /* GPIO_DIR_OUTPUT */
#define GPIO_SERVICE_DEFAULT_EDGE     0   /* GPIO_EDGE_NONE  */

/* ── context ──────────────────────────────────────────────────────────── */

typedef struct {
    svc_context_t   base;
    svc_ipc_t       ipc;
    config_t        config;
    void           *hal_device;    /**< hw_device_t* from gpio HAL          */
    int             security_applied;

    /* GPIO-specific config */
    uint32_t        pin_number;
    int             direction;     /**< gpio_direction_t enum value         */
    int             initial_value; /**< gpio_value_t enum value             */
    int             edge;          /**< gpio_edge_t enum value              */
    int             interrupt_timeout_ms;
} gpio_service_ctx_t;

/* ── lifecycle API ────────────────────────────────────────────────────── */

int  gpio_service_init(gpio_service_ctx_t *ctx, int argc, char **argv);
int  gpio_service_run(gpio_service_ctx_t *ctx);
void gpio_service_shutdown(gpio_service_ctx_t *ctx);

#endif /* GPIO_SERVICE_H */
