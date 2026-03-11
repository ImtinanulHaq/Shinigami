#define _POSIX_C_SOURCE 200809L

/*
 * sm_advanced_ratelimit.c - Advanced rate limiting with Penalty Box.
 *
 * Token-bucket rate limiter (per PID + per service) augmented with a
 * "penalty box": if a caller exceeds the rate limit PENALTY_THRESHOLD
 * times within PENALTY_WINDOW_SECS, it is banned for PENALTY_DURATION_SECS
 * seconds.  All further requests during the ban are rejected immediately
 * without consuming token-bucket state.
 */

#include "../security/sm_advanced_ratelimit.h"
#include "../security/sm_rate_limit.h"
#include "../observability/sm_logging.h"
#include <string.h>
#include <time.h>
#include <pthread.h>

/* ── TOKEN BUCKET ───────────────────────────────────────────────────────────── */

typedef struct {
    char   service_name[64];
    pid_t  pid;
    double tokens;
    double last_refill;
} service_bucket_t;

/* ── PENALTY BOX ENTRY ──────────────────────────────────────────────────────── */

typedef struct {
    char   service_name[64];
    pid_t  pid;
    int    violation_count;      /* violations in current counting window */
    time_t window_start;         /* start of current PENALTY_WINDOW_SECS  */
    time_t ban_until;            /* 0 = not banned; future = banned        */
} penalty_entry_t;

/* ── STATE ──────────────────────────────────────────────────────────────────── */

#define MAX_SERVICE_BUCKETS 256

static struct {
    service_bucket_t buckets[MAX_SERVICE_BUCKETS];
    penalty_entry_t  penalties[MAX_SERVICE_BUCKETS];
    int              bucket_count;
    int              penalty_count;
    pthread_mutex_t  mutex;
} g_limits;

/* ── INIT ───────────────────────────────────────────────────────────────────── */

int sm_ratelimit_init(void)
{
    memset(&g_limits, 0, sizeof(g_limits));
    pthread_mutex_init(&g_limits.mutex, NULL);
    sm_log(SM_LOG_INFO,
           "ratelimit: advanced rate limiting initialised "
           "(penalty: %d violations/%ds → %ds ban)",
           PENALTY_THRESHOLD, PENALTY_WINDOW_SECS, PENALTY_DURATION_SECS);
    return 0;
}

/* ── INTERNAL: PENALTY BOX ──────────────────────────────────────────────────── */

/*
 * _find_or_create_penalty() — return a pointer to the penalty entry for
 * (pid, service_name), allocating a new slot if needed.
 * Caller must hold g_limits.mutex.
 */
static penalty_entry_t *
_find_or_create_penalty(pid_t pid, const char *service_name)
{
    for (int i = 0; i < g_limits.penalty_count; i++) {
        penalty_entry_t *p = &g_limits.penalties[i];
        if (p->pid == pid &&
            strncmp(p->service_name, service_name,
                    sizeof(p->service_name)) == 0) {
            return p;
        }
    }
    if (g_limits.penalty_count >= MAX_SERVICE_BUCKETS) return NULL;

    penalty_entry_t *p = &g_limits.penalties[g_limits.penalty_count++];
    memset(p, 0, sizeof(*p));
    strncpy(p->service_name, service_name, sizeof(p->service_name) - 1);
    p->pid = pid;
    return p;
}

/*
 * _record_violation() — increment violation counter; impose ban when
 * threshold is reached.  Caller must hold g_limits.mutex.
 */
static void _record_violation(pid_t pid, const char *service_name)
{
    penalty_entry_t *p = _find_or_create_penalty(pid, service_name);
    if (!p) return;

    time_t now = time(NULL);

    /* Reset counter when the counting window has expired */
    if (p->window_start == 0 ||
        difftime(now, p->window_start) > PENALTY_WINDOW_SECS) {
        p->violation_count = 0;
        p->window_start    = now;
    }

    p->violation_count++;

    if (p->violation_count >= PENALTY_THRESHOLD) {
        p->ban_until = now + PENALTY_DURATION_SECS;
        /* Reset counter so ban_until is the authoritative gate */
        p->violation_count = 0;
        p->window_start    = 0;
        sm_log(SM_LOG_WARN,
               "ratelimit: PENALTY BOX — '%s' (pid=%d) banned for %ds "
               "(hit %d violations in %ds)",
               service_name, (int)pid,
               PENALTY_DURATION_SECS, PENALTY_THRESHOLD, PENALTY_WINDOW_SECS);
    }
}

/*
 * _is_in_penalty_box() — return 1 if currently banned, 0 otherwise.
 * Automatically clears an expired ban.  Caller must hold g_limits.mutex.
 */
static int _is_in_penalty_box(pid_t pid, const char *service_name)
{
    for (int i = 0; i < g_limits.penalty_count; i++) {
        penalty_entry_t *p = &g_limits.penalties[i];
        if (p->pid != pid) continue;
        if (strncmp(p->service_name, service_name,
                    sizeof(p->service_name)) != 0) continue;
        if (p->ban_until == 0) return 0;
        if (time(NULL) < p->ban_until) return 1;
        /* Ban expired — clear it */
        p->ban_until       = 0;
        p->violation_count = 0;
        p->window_start    = 0;
        sm_log(SM_LOG_INFO,
               "ratelimit: penalty expired for '%s' (pid=%d)",
               service_name, (int)pid);
        return 0;
    }
    return 0;
}

/* ── SET LIMIT ──────────────────────────────────────────────────────────────── */

void sm_ratelimit_set_service_limit(const char *service_name, pid_t pid,
                                    double capacity)
{
    if (!service_name) return;

    pthread_mutex_lock(&g_limits.mutex);

    for (int i = 0; i < g_limits.bucket_count; i++) {
        if (g_limits.buckets[i].pid == pid &&
            !strcmp(g_limits.buckets[i].service_name, service_name)) {
            g_limits.buckets[i].tokens = capacity;
            pthread_mutex_unlock(&g_limits.mutex);
            return;
        }
    }

    if (g_limits.bucket_count < MAX_SERVICE_BUCKETS) {
        service_bucket_t *b = &g_limits.buckets[g_limits.bucket_count++];
        strncpy(b->service_name, service_name, sizeof(b->service_name) - 1);
        b->pid         = pid;
        b->tokens      = capacity;
        b->last_refill = 0.0;
    }

    pthread_mutex_unlock(&g_limits.mutex);
}

/* ── CHECK ──────────────────────────────────────────────────────────────────── */

int sm_ratelimit_check_extended(pid_t pid, const char *service_name,
                                uint16_t operation)
{
    (void)operation;   /* reserved for future op-type granularity */

    /* ── 1. Global + per-PID check (existing system, no lock needed) ─── */
    if (sm_rate_limit_check(pid) != 0) {
        sm_log(SM_LOG_DEBUG,
               "ratelimit: global/PID limit hit (pid=%d)", (int)pid);
        /* Record violation even for global limit hits when service named */
        if (service_name) {
            pthread_mutex_lock(&g_limits.mutex);
            _record_violation(pid, service_name);
            pthread_mutex_unlock(&g_limits.mutex);
        }
        return -1;
    }

    if (!service_name) return 0;   /* unnamed caller: global check passed */

    pthread_mutex_lock(&g_limits.mutex);

    /* ── 2. Penalty box check ─────────────────────────────────────────── */
    if (_is_in_penalty_box(pid, service_name)) {
        pthread_mutex_unlock(&g_limits.mutex);
        sm_log(SM_LOG_DEBUG,
               "ratelimit: '%s' (pid=%d) is in penalty box — request denied",
               service_name, (int)pid);
        return -1;
    }

    /* ── 3. Per-service token bucket check ───────────────────────────── */
    for (int i = 0; i < g_limits.bucket_count; i++) {
        service_bucket_t *b = &g_limits.buckets[i];
        if (b->pid != pid) continue;
        if (strcmp(b->service_name, service_name) != 0) continue;

        if (b->tokens < 1.0) {
            /* Token exhausted — record violation, possibly enter penalty box */
            _record_violation(pid, service_name);
            pthread_mutex_unlock(&g_limits.mutex);
            return -1;
        }
        b->tokens -= 1.0;
        pthread_mutex_unlock(&g_limits.mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_limits.mutex);
    return 0;   /* no bucket configured for this (pid, service) → allow */
}

/* ── RESET ──────────────────────────────────────────────────────────────────── */

void sm_ratelimit_reset_service(const char *service_name)
{
    if (!service_name) return;

    pthread_mutex_lock(&g_limits.mutex);

    /* Reset token buckets */
    for (int i = 0; i < g_limits.bucket_count; i++) {
        if (!strcmp(g_limits.buckets[i].service_name, service_name))
            g_limits.buckets[i].tokens = 10.0;
    }

    /* Clear penalty box entries */
    for (int i = 0; i < g_limits.penalty_count; i++) {
        if (!strcmp(g_limits.penalties[i].service_name, service_name)) {
            g_limits.penalties[i].ban_until       = 0;
            g_limits.penalties[i].violation_count = 0;
            g_limits.penalties[i].window_start    = 0;
        }
    }

    pthread_mutex_unlock(&g_limits.mutex);
    sm_log(SM_LOG_DEBUG, "ratelimit: reset all limits for '%s'", service_name);
}

/* ── QUERIES ────────────────────────────────────────────────────────────────── */

int sm_ratelimit_get_tokens(pid_t pid, const char *service_name)
{
    int tokens = 0;

    pthread_mutex_lock(&g_limits.mutex);
    for (int i = 0; i < g_limits.bucket_count; i++) {
        if (g_limits.buckets[i].pid == pid &&
            !strcmp(g_limits.buckets[i].service_name, service_name)) {
            tokens = (int)g_limits.buckets[i].tokens;
            break;
        }
    }
    pthread_mutex_unlock(&g_limits.mutex);
    return tokens;
}

int sm_ratelimit_is_banned(pid_t pid, const char *service_name)
{
    if (!service_name) return 0;

    pthread_mutex_lock(&g_limits.mutex);
    int banned = _is_in_penalty_box(pid, service_name);
    pthread_mutex_unlock(&g_limits.mutex);
    return banned;
}
