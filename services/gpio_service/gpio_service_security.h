/**
 * @file gpio_service_security.h
 * @brief Security profile interface for the GPIO service.
 */

#ifndef GPIO_SERVICE_SECURITY_H
#define GPIO_SERVICE_SECURITY_H

#include "gpio_service.h"

/**
 * @brief Apply the full security profile for the GPIO service.
 *
 * Loads security_manager_default_config("gpio") and applies the stack:
 *   Sandbox → Capabilities → Verify → Seccomp
 *
 * @return SVC_OK on success, SVC_ERR_SECURITY on failure.
 */
int gpio_service_apply_security(gpio_service_ctx_t *ctx);

#endif /* GPIO_SERVICE_SECURITY_H */
