#define _POSIX_C_SOURCE 200809L

/*
 * sm_rate_limit.c - Token bucket rate limiting.
 *
 * Fixes applied over the previous fixed-window implementation:
 *   - Token bucket eliminates the boundary-burst problem.
 *   - Table size doubled to 256 to reduce the chance of legitimate PIDs
 *     being dropped when the table is full.
 *   - LRU-style eviction of the least-recently-used entry when the table is
 *     full, so a fork-bomb flooding the table does not permanently lock out
 *     legitimate clients.
 *   - Global bucket enforces a hard ceiling regardless of PID count.
 */

#include "sm_rate_limit.h"
#include "sm_logging.h"
#include "sm_protocol.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <float.h>

/* ── TOKEN BUCKET ───────────────────────────────────────────────────────────── */

typedef struct {
    pid_t   pid;
    double  tokens;         /* current token count */
    double  last_refill;    /* monotonic time of last refill */
} bucket_t;

static bucket_t        pid_table[SM_RATE_TABLE_SIZE];
static int             pid_table_count  = 0;
static pthread_mutex_t rate_mutex       = PTHREAD_MUTEX_INITIALIZER;

/* Global bucket state */
static double g_tokens;
static double g_last_refill;

/* Monotonic timestamp in fractional seconds - avoids wall-clock jumps */
static double mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Refill a bucket up to its capacity based on elapsed time */
static void refill_bucket(bucket_t* b, double now, double rate, double capacity)
{
    double elapsed = now - b->last_refill;
    if (elapsed > 0.0) {
        b->tokens      += elapsed * rate;
        if (b->tokens > capacity) b->tokens = capacity;
        b->last_refill  = now;
    }
}

/* ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────── */

int sm_rate_limit_init(void)
{
    pthread_mutex_lock(&rate_mutex);

    memset(pid_table, 0, sizeof(pid_table));
    pid_table_count = 0;

    g_tokens      = SM_RATE_GLOBAL_CAPACITY;
    g_last_refill = mono_now();

    pthread_mutex_unlock(&rate_mutex);
    return 0;
}

int sm_rate_limit_check(pid_t pid)
{
    double     now;
    bucket_t*  entry    = NULL;
    int        lru_idx  = 0;
    double     lru_time = DBL_MAX;
    int        i;

    pthread_mutex_lock(&rate_mutex);

    now = mono_now();

    /* Refill global bucket */
    {
        double elapsed = now - g_last_refill;
        if (elapsed > 0.0) {
            g_tokens += elapsed * SM_RATE_GLOBAL_REFILL;
            if (g_tokens > SM_RATE_GLOBAL_CAPACITY)
                g_tokens = SM_RATE_GLOBAL_CAPACITY;
            g_last_refill = now;
        }
    }

    /* Check global limit before doing per-PID work */
    if (g_tokens < 1.0) {
        sm_log(SM_LOG_WARN, "rate_limit: global bucket empty");
        pthread_mutex_unlock(&rate_mutex);
        return SM_ERR_RATELIMIT;
    }

    /* Find existing entry or track LRU slot for eviction */
    for (i = 0; i < pid_table_count; i++) {
        if (pid_table[i].pid == pid) {
            entry = &pid_table[i];
            break;
        }
        if (pid_table[i].last_refill < lru_time) {
            lru_time = pid_table[i].last_refill;
            lru_idx  = i;
        }
    }

    if (!entry) {
        if (pid_table_count < SM_RATE_TABLE_SIZE) {
            /* Append a new entry */
            entry = &pid_table[pid_table_count++];
        } else {
            /*
             * Table is full.  Evict the least-recently-used entry.
             * This means a sustained flood of new PIDs displaces old PIDs
             * rather than permanently blocking all new connections.
             */
            sm_log(SM_LOG_WARN, "rate_limit: table full, evicting lru slot %d", lru_idx);
            entry = &pid_table[lru_idx];
        }

        entry->pid         = pid;
        entry->tokens      = SM_RATE_PID_CAPACITY; /* new PID starts with full bucket */
        entry->last_refill = now;
    }

    /* Refill the PID bucket */
    refill_bucket(entry, now, SM_RATE_PID_REFILL, SM_RATE_PID_CAPACITY);

    /* Check per-PID limit */
    if (entry->tokens < 1.0) {
        sm_log(SM_LOG_WARN, "rate_limit: pid=%d bucket empty (%.2f tokens)",
               (int)pid, entry->tokens);
        pthread_mutex_unlock(&rate_mutex);
        return SM_ERR_RATELIMIT;
    }

    /* Consume one token from both buckets */
    entry->tokens -= 1.0;
    g_tokens      -= 1.0;

    pthread_mutex_unlock(&rate_mutex);
    return SM_OK;
}

void sm_rate_limit_cleanup(void)
{
    pthread_mutex_lock(&rate_mutex);
    memset(pid_table, 0, sizeof(pid_table));
    pid_table_count = 0;
    g_tokens        = 0.0;
    pthread_mutex_unlock(&rate_mutex);
}