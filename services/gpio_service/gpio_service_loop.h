/**
 * @file gpio_service_loop.h
 * @brief GPIO service epoll-based event loop declaration.
 */

#ifndef GPIO_SERVICE_LOOP_H
#define GPIO_SERVICE_LOOP_H

#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "gpio_service.h"

/**
 * @brief Run the GPIO service event loop.
 *
 * Monitors the SM socket fd and the GPIO device fd with epoll.
 * Returns when g_running == 0 or on fatal error.
 *
 * @param ctx  GPIO service context (HAL device must already be started).
 * @param ipc  Connected and registered IPC handle.
 * @param cfg  Loaded service configuration.
 * @return SVC_OK on clean shutdown, SVC_ERR_* on fatal error.
 */
int gpio_service_loop_run(gpio_service_ctx_t *ctx, svc_ipc_t *ipc,
                          service_config_t *cfg);

#endif /* GPIO_SERVICE_LOOP_H */
