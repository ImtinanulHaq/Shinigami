/**
 * @file audio_service_loop.h
 * @brief Audio service epoll-based event loop declaration.
 */

#ifndef AUDIO_SERVICE_LOOP_H
#define AUDIO_SERVICE_LOOP_H

#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "audio_service.h"

/**
 * @brief Run the audio service event loop.
 *
 * Monitors the SM socket fd and the HAL device fd with epoll.
 * Returns when g_running == 0 (SIGTERM/SIGINT) or on fatal error.
 *
 * @param ctx  Audio service context (HAL device must already be started).
 * @param ipc  Connected and registered IPC handle.
 * @param cfg  Loaded service configuration.
 * @return SVC_OK on clean shutdown, SVC_ERR_* on fatal error.
 */
int audio_service_loop_run(audio_service_ctx_t *ctx, svc_ipc_t *ipc,
                           service_config_t *cfg);

#endif /* AUDIO_SERVICE_LOOP_H */
