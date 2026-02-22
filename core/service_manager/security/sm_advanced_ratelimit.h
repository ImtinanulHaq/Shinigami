#ifndef SM_ADVANCED_RATELIMIT_H
#define SM_ADVANCED_RATELIMIT_H

/*
 * sm_advanced_ratelimit.h - Advanced rate limiting
 *
 * Per-PID + per-service-type + per-operation-type rate limiting
 */

#include <sys/types.h>
#include <stdint.h>

/* Initialize advanced rate limiting */
int sm_ratelimit_init(void);

/* Check rate limit for service and operation */
int sm_ratelimit_check_extended(pid_t pid, const char* service_name, uint16_t operation);

/* Set limit for a service */
void sm_ratelimit_set_service_limit(const char* service_name, pid_t pid, double capacity);

/* Reset per-service limits */
void sm_ratelimit_reset_service(const char* service_name);

/* Get remaining tokens */
int sm_ratelimit_get_tokens(pid_t pid, const char* service_name);

#endif /* SM_ADVANCED_RATELIMIT_H */
