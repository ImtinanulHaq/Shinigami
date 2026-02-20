#include "sm_rate_limit.h"
#include "sm_logging.h"
#include "sm_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ── STATE ──────────────────────────────────────────────────────────────────────

typedef struct {
    pid_t  pid;
    int    count;
    time_t window_start;
} rate_limit_entry_t;

#define RATE_LIMIT_TABLE_SIZE 128
static rate_limit_entry_t rate_table[RATE_LIMIT_TABLE_SIZE];
static int                rate_table_count = 0;
static pthread_mutex_t    rate_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t             last_cleanup = 0;
static int                global_count = 0;

// ── HELPERS ────────────────────────────────────────────────────────────────────

static void cleanup_expired_slots(void)
{
    time_t now = time(NULL);

    // Clean expired entries (older than 2 windows)
    for (int i = 0; i < rate_table_count; i++) {
        if (now - rate_table[i].window_start > SM_RATE_LIMIT_WINDOW + 1) {
            // Remove by shifting
            for (int j = i; j < rate_table_count - 1; j++)
                rate_table[j] = rate_table[j + 1];
            rate_table_count--;
            i--;
        }
    }

    // Cool down global count
    if (now - last_cleanup >= SM_RATE_LIMIT_WINDOW) {
        global_count = 0;
        last_cleanup = now;
    }
}

// ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────────

int sm_rate_limit_init(void)
{
    pthread_mutex_lock(&rate_mutex);
    memset(rate_table, 0, sizeof(rate_table));
    rate_table_count = 0;
    global_count = 0;
    last_cleanup = time(NULL);
    pthread_mutex_unlock(&rate_mutex);
    return 0;
}

int sm_rate_limit_check(pid_t pid)
{
    pthread_mutex_lock(&rate_mutex);

    time_t now = time(NULL);

    cleanup_expired_slots();

    // Check global limit first
    if (global_count >= SM_RATE_LIMIT_GLOBAL) {
        sm_log(SM_LOG_WARN, "global rate limit exceeded (count=%d)", global_count);
        pthread_mutex_unlock(&rate_mutex);
        return SM_ERR_RATELIMIT;
    }

    // Find or create entry for this PID
    rate_limit_entry_t* entry = NULL;
    for (int i = 0; i < rate_table_count; i++) {
        if (rate_table[i].pid == pid) {
            entry = &rate_table[i];
            break;
        }
    }

    if (!entry) {
        // New PID - create entry
        if (rate_table_count >= RATE_LIMIT_TABLE_SIZE) {
            sm_log(SM_LOG_WARN, "rate table full, dropping PID %d", pid);
            pthread_mutex_unlock(&rate_mutex);
            return SM_ERR_RATELIMIT;
        }
        entry = &rate_table[rate_table_count++];
        entry->pid = pid;
        entry->count = 0;
        entry->window_start = now;
    }

    // Check if PID window expired and reset
    if (now - entry->window_start >= SM_RATE_LIMIT_WINDOW) {
        entry->count = 0;
        entry->window_start = now;
    }

    // Check PID limit
    if (entry->count >= SM_RATE_LIMIT_PER_PID) {
        sm_log(SM_LOG_WARN, "rate limit exceeded for PID %d (count=%d)",
               pid, entry->count);
        pthread_mutex_unlock(&rate_mutex);
        return SM_ERR_RATELIMIT;
    }

    // Update counters
    entry->count++;
    global_count++;

    pthread_mutex_unlock(&rate_mutex);
    return 0;
}

void sm_rate_limit_cleanup(void)
{
    pthread_mutex_lock(&rate_mutex);
    rate_table_count = 0;
    global_count = 0;
    pthread_mutex_unlock(&rate_mutex);
}
