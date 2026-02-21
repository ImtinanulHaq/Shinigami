#ifndef SM_RATE_LIMIT_H
#define SM_RATE_LIMIT_H

/*
 * sm_rate_limit.h - Per-PID and global token-bucket rate limiting.
 *
 * Token bucket provides smooth rate limiting without the boundary-burst
 * problem of fixed-window counters.  A bucket starts full; each request
 * consumes one token; tokens refill at a fixed rate up to the bucket capacity.
 */

#include <time.h>
#include <sys/types.h>

/* Per-PID bucket: capacity and refill rate */
#define SM_RATE_PID_CAPACITY   10       /* max burst per PID */
#define SM_RATE_PID_REFILL     10.0     /* tokens per second per PID */

/* Global bucket: caps total throughput regardless of PID count */
#define SM_RATE_GLOBAL_CAPACITY 50      /* max global burst */
#define SM_RATE_GLOBAL_REFILL   50.0    /* tokens per second globally */

/* Maximum number of PID entries tracked simultaneously */
#define SM_RATE_TABLE_SIZE     256

int  sm_rate_limit_init(void);

/*
 * Check and consume one token for the given PID.
 * Returns SM_OK if allowed, SM_ERR_RATELIMIT if throttled.
 */
int  sm_rate_limit_check(pid_t pid);

void sm_rate_limit_cleanup(void);

#endif /* SM_RATE_LIMIT_H */