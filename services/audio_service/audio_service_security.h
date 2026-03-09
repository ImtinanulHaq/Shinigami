/**
 * @file audio_service_security.h
 * @brief Security profile interface for the audio service.
 */

#ifndef AUDIO_SERVICE_SECURITY_H
#define AUDIO_SERVICE_SECURITY_H

#include "audio_service.h"

/**
 * @brief Apply the full security profile for the audio service.
 *
 * Loads the default security config for "audio" via
 * security_manager_default_config(), then applies the security stack
 * in the mandatory order: Sandbox → Capabilities → Verify → Seccomp.
 *
 * @param ctx  Audio service context.
 * @return SVC_OK on success, SVC_ERR_SECURITY on failure.
 */
int audio_service_apply_security(audio_service_ctx_t *ctx);

#endif /* AUDIO_SERVICE_SECURITY_H */
