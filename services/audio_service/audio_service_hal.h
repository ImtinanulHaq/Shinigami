/**
 * @file audio_service_hal.h
 * @brief Audio HAL integration interface.
 *
 * Wraps the lower-level audio_hal_create / open / start / read / write /
 * stop / close / destroy calls behind a service-oriented API that takes
 * the audio_service_ctx_t context.
 */

#ifndef AUDIO_SERVICE_HAL_H
#define AUDIO_SERVICE_HAL_H

#include "audio_service.h"

/**
 * @brief Create and open the audio HAL device using ctx config values.
 * @return SVC_OK on success, SVC_ERR_HAL on failure.
 */
int audio_service_hal_init(audio_service_ctx_t *ctx);

/**
 * @brief Start the HAL device (transition OPEN → ACTIVE).
 * @return SVC_OK on success.
 */
int audio_service_hal_start(audio_service_ctx_t *ctx);

/**
 * @brief Read audio data from the HAL device.
 * @param buf   Destination buffer.
 * @param size  Buffer size in bytes.
 * @return Number of bytes read, or negative svc_error_t.
 */
ssize_t audio_service_hal_read(audio_service_ctx_t *ctx, void *buf,
                               size_t size);

/**
 * @brief Write audio data to the HAL device.
 * @param buf   Source buffer.
 * @param size  Data size in bytes.
 * @return Number of bytes written, or negative svc_error_t.
 */
ssize_t audio_service_hal_write(audio_service_ctx_t *ctx, const void *buf,
                                size_t size);

/**
 * @brief Stop the HAL device (ACTIVE → OPEN).
 * @return SVC_OK on success.
 */
int audio_service_hal_stop(audio_service_ctx_t *ctx);

/**
 * @brief Close and destroy the HAL device, releasing all resources.
 */
void audio_service_hal_cleanup(audio_service_ctx_t *ctx);

#endif /* AUDIO_SERVICE_HAL_H */
