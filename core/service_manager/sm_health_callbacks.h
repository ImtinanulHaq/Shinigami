#ifndef SM_HEALTH_CALLBACKS_H
#define SM_HEALTH_CALLBACKS_H

/*
 * sm_health_callbacks.h - Custom health check callbacks
 *
 * Services can register health check functions for deeper monitoring
 * beyond just heartbeat timeouts.
 */

#include <sys/types.h>

/* Health check function type - returns 0=healthy, <0=unhealthy */
typedef int (*sm_health_check_fn)(const char* service_name);

/* Register a custom health check for a service */
int sm_health_callback_register(const char* service_name, sm_health_check_fn fn);

/* Run health check for a service */
int sm_health_callback_check(const char* service_name);

/* Run all registered health checks */
void sm_health_callbacks_check_all(void);

/* Unregister callback */
int sm_health_callback_unregister(const char* service_name);

/* Cleanup */
void sm_health_callbacks_cleanup(void);

#endif /* SM_HEALTH_CALLBACKS_H */
