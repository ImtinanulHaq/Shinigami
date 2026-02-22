#define _POSIX_C_SOURCE 200809L

/*
 * sm_service_tier.c - Service tier management
 */

#include "../lifecycle/sm_service_tier.h"

int sm_tier_get_restart_priority(service_tier_t tier)
{
    switch (tier) {
        case SVC_TIER_CRITICAL: return 0;  /* highest priority */
        case SVC_TIER_HIGH:     return 1;
        case SVC_TIER_NORMAL:   return 2;
        case SVC_TIER_LOW:      return 3;  /* lowest priority */
        default:                return 2;
    }
}

int sm_tier_get_backoff_ms(service_tier_t tier, int restart_count)
{
    int base = 0;
    
    switch (tier) {
        case SVC_TIER_CRITICAL:
            return 100;  /* very fast restarts */
        case SVC_TIER_HIGH:
            base = 500;
            break;
        case SVC_TIER_NORMAL:
            base = 2000;
            break;
        case SVC_TIER_LOW:
            base = 5000;
            break;
        default:
            base = 2000;
    }
    
    /* Exponential backoff */
    int backoff = base;
    for (int i = 1; i < restart_count; i++) {
        backoff *= 2;
        if (backoff > 120000) backoff = 120000;  /* cap at 2 minutes */
    }
    
    return backoff;
}

int sm_tier_get_restart_delay(service_tier_t tier, int attempt)
{
    int max_attempts;
    
    switch (tier) {
        case SVC_TIER_CRITICAL: max_attempts = 10; break;
        case SVC_TIER_HIGH:     max_attempts = 5;  break;
        case SVC_TIER_NORMAL:   max_attempts = 3;  break;
        case SVC_TIER_LOW:      max_attempts = 1;  break;
        default:                max_attempts = 3;
    }
    
    if (attempt >= max_attempts) return -1;  /* no more restarts */
    return sm_tier_get_backoff_ms(tier, attempt);
}
