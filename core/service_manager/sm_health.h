#ifndef SM_HEALTH_H
#define SM_HEALTH_H

#include "sm_registry.h"

// ── HEALTH CHECK CONFIGURATION ────────────────────────────────────────────────

#define SM_HEALTH_CHECK_INTERVAL  3      // seconds between checks
#define SM_HEALTH_RESTART_DELAY   2      // seconds before restart
#define SM_HEALTH_MAX_RESTARTS    3      // max restarts before giving up

// ── FUNCTIONS ──────────────────────────────────────────────────────────────────

// Run health check on all services
void sm_health_check(void);

#endif // SM_HEALTH_H
