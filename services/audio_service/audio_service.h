/**
 * @file audio_service.h
 * @brief Public types, structs, and API for the Audio Service daemon.
 *
 * The audio service is a long-running daemon that wraps the Audio HAL
 * (ALSA PCM), registers with the Service Manager, applies the security
 * profile, and serves audio I/O requests via its event loop.
 */

#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include "../common/service_base.h"
#include "../common/service_config.h"
#include "../common/service_ipc.h"
#include "hal_interface.h"

#include <stdint.h>

/* ── defaults ─────────────────────────────────────────────────────────── */

#define AUDIO_SERVICE_NAME          "audio_service"
#define AUDIO_SERVICE_CONF          "/etc/audio_service/audio_service.conf"
#define AUDIO_SERVICE_DEFAULT_DEVICE "hw:0,0"
#define AUDIO_SERVICE_DEFAULT_RATE   44100
#define AUDIO_SERVICE_DEFAULT_CH     2
#define AUDIO_SERVICE_DEFAULT_FMT    0     /* AUDIO_FORMAT_S16_LE */
#define AUDIO_SERVICE_DEFAULT_PERIOD 1024
#define AUDIO_SERVICE_DEFAULT_BUFFER 4096

/* ── audio service context ────────────────────────────────────────────── */

/**
 * @brief Master context for the audio service daemon.
 *
 * Aggregates the base daemon context, IPC handle, config, and
 * HAL device pointer into a single structure that is threaded
 * through all audio_service_* functions.
 */
typedef struct {
    svc_context_t   base;          /**< Daemon lifecycle context           */
    svc_ipc_t       ipc;           /**< Service Manager IPC handle         */
    service_config_t config;        /**< Parsed INI configuration           */
    void           *hal_device;    /**< hw_device_t* from audio HAL        */
    int             security_applied; /**< Non-zero if security locked down */

    /* Audio-specific config (resolved from INI + defaults) */
    char            alsa_device[64];
    uint32_t        sample_rate;
    uint32_t        channels;
    int             format;        /**< audio_format_t enum value          */
    uint32_t        period_size;
    uint32_t        buffer_size;
    int             direction;     /**< 0=playback, 1=capture              */
} audio_service_ctx_t;

/* ── lifecycle API ────────────────────────────────────────────────────── */

/**
 * @brief Initialise the audio service context from config + CLI args.
 * @return SVC_OK on success.
 */
int audio_service_init(audio_service_ctx_t *ctx, int argc, char **argv);

/**
 * @brief Run the audio service (blocking — returns on shutdown).
 * @return SVC_OK on clean exit, negative error otherwise.
 */
int audio_service_run(audio_service_ctx_t *ctx);

/**
 * @brief Tear down all resources held by the audio service.
 */
void audio_service_shutdown(audio_service_ctx_t *ctx);

#endif /* AUDIO_SERVICE_H */
