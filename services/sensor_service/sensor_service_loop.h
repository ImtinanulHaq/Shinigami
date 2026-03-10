/**
 * @file sensor_service_loop.h
 * @brief Sensor service epoll-based event loop declaration.
 */

#ifndef SENSOR_SERVICE_LOOP_H
#define SENSOR_SERVICE_LOOP_H

#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "sensor_service.h"

/**
 * @brief Run the sensor service event loop.
 *
 * Monitors the SM socket fd and the IIO device fd with epoll.
 * Returns when g_running == 0 or on fatal error.
 *
 * @param ctx  Sensor service context (HAL device must already be started).
 * @param ipc  Connected and registered IPC handle.
 * @param cfg  Loaded service configuration.
 * @return SVC_OK on clean shutdown, SVC_ERR_* on fatal error.
 */
int sensor_service_loop_run(sensor_service_ctx_t *ctx, svc_ipc_t *ipc,
                            service_config_t *cfg);

#endif /* SENSOR_SERVICE_LOOP_H */
