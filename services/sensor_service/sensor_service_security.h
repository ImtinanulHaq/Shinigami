/**
 * @file sensor_service_security.h
 * @brief Security profile interface for the sensor service.
 */

#ifndef SENSOR_SERVICE_SECURITY_H
#define SENSOR_SERVICE_SECURITY_H

#include "sensor_service.h"

/**
 * @brief Apply the full security profile for the sensor service.
 *
 * Loads security_manager_default_config("sensor") and applies the stack:
 *   Sandbox → Capabilities → Verify → Seccomp
 *
 * @return SVC_OK on success, SVC_ERR_SECURITY on failure.
 */
int sensor_service_apply_security(sensor_service_ctx_t *ctx);

#endif /* SENSOR_SERVICE_SECURITY_H */
