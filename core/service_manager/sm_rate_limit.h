#ifndef SM_RATE_LIMIT_H
#define SM_RATE_LIMIT_H

#include <time.h>
#include <sys/types.h>

// ── RATE LIMIT POLICY ─────────────────────────────────────────────────────────

#define SM_RATE_LIMIT_WINDOW    1       // time window in seconds
#define SM_RATE_LIMIT_PER_PID   10      // max requests per PID per window
#define SM_RATE_LIMIT_GLOBAL    50      // max total requests per window

// ── FUNCTIONS ──────────────────────────────────────────────────────────────────

// Initialize rate limiting
int  sm_rate_limit_init(void);

// Check if PID exceeded rate limit
// Returns: 0 = OK, SM_ERR_RATELIMIT = exceeded
int  sm_rate_limit_check(pid_t pid);

// Clean up resources
void sm_rate_limit_cleanup(void);

#endif // SM_RATE_LIMIT_H
