/**
 * @file camera_service_security.h
 * @brief Security profile interface for the camera service.
 */

#ifndef CAMERA_SERVICE_SECURITY_H
#define CAMERA_SERVICE_SECURITY_H

#include "camera_service.h"

/**
 * @brief Apply the full security profile for the camera service.
 *
 * Loads security_manager_default_config("camera") and applies the stack:
 *   Sandbox → Capabilities → Verify → Seccomp
 *
 * @return SVC_OK on success, SVC_ERR_SECURITY on failure.
 */
int camera_service_apply_security(camera_service_ctx_t *ctx);

#endif /* CAMERA_SERVICE_SECURITY_H */
