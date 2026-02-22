#ifndef SM_HEALTH_H
#define SM_HEALTH_H

/*
 * sm_health.h - Periodic service health monitor.
 */

#include "../infrastructure/sm_registry.h"

#define SM_HEALTH_CHECK_INTERVAL  3   /* seconds between health checks */
#define SM_HEALTH_RESTART_DELAY   2   /* base restart delay in seconds */
#define SM_HEALTH_MAX_RESTARTS    3   /* transitions to SERVICE_DEAD after this many */

/*
 * Run one health check pass over all registered services.
 * Called from the main event loop every SM_HEALTH_CHECK_INTERVAL seconds.
 */
void sm_health_check(void);

#endif /* SM_HEALTH_H */