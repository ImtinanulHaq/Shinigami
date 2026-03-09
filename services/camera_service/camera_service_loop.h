/**
 * @file camera_service_loop.h
 * @brief Camera service epoll-based event loop declaration.
 */

#ifndef CAMERA_SERVICE_LOOP_H
#define CAMERA_SERVICE_LOOP_H

#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "camera_service.h"

/**
 * @brief Run the camera service event loop.
 *
 * Monitors the SM socket fd and the HAL device fd with epoll.
 * Returns when g_running == 0 or on fatal error.
 *
 * @param ctx  Camera service context (HAL device must already be started).
 * @param ipc  Connected and registered IPC handle.
 * @param cfg  Loaded service configuration.
 * @return SVC_OK on clean shutdown, SVC_ERR_* on fatal error.
 */
int camera_service_loop_run(camera_service_ctx_t *ctx, svc_ipc_t *ipc,
                            service_config_t *cfg);

#endif /* CAMERA_SERVICE_LOOP_H */
