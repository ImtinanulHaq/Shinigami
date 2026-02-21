#ifndef SM_SERVICE_TIER_H
#define SM_SERVICE_TIER_H

/*
 * sm_service_tier.h - Service priority tiers
 *
 * Critical services restart first, normal services with backoff.
 */

typedef enum {
    SVC_TIER_CRITICAL = 0,   /* restart immediately */
    SVC_TIER_HIGH = 1,       /* restart with short backoff */
    SVC_TIER_NORMAL = 2,     /* standard backoff */
    SVC_TIER_LOW = 3,        /* best-effort, may not restart */
} service_tier_t;

/* Get restart priority */
int sm_tier_get_restart_priority(service_tier_t tier);

/* Get backoff multiplier */
int sm_tier_get_backoff_ms(service_tier_t tier, int restart_count);

/* Restart delay for this tier */
int sm_tier_get_restart_delay(service_tier_t tier, int attempt);

#endif /* SM_SERVICE_TIER_H */
