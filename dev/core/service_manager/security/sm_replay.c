#define _POSIX_C_SOURCE 200809L

/*
 * sm_replay.c  -  Replay-attack protection for IPC messages.
 *
 * Design
 * ──────
 * Each incoming message carries an sm_replay_token_t (64-bit timestamp in
 * milliseconds + 64-bit cryptographically random nonce).  sm_replay_check()
 * performs two fast checks:
 *
 *   1. Freshness — reject if |now_ms - timestamp_ms| > WINDOW.
 *   2. Uniqueness — reject if the nonce is already in the ring buffer.
 *
 * The ring buffer is a fixed circular array of SM_REPLAY_RING_SIZE slots.
 * Stale slots (age > WINDOW) are evicted lazily when a new nonce is
 * written.  The write pointer is monotonically increasing and masked with
 * (SM_REPLAY_RING_SIZE - 1) to wrap around.
 *
 * Locking
 * ───────
 * A single mutex protects the ring.  The critical section is tiny (linear
 * scan up to SM_REPLAY_RING_SIZE slots + one write), so contention is
 * negligible compared to the HMAC verification that precedes this call.
 *
 * Memory ordering
 * ───────────────
 * clock_gettime() is a vDSO call on Linux — effectively free.
 */

#include "../security/sm_replay.h"
#include "../observability/sm_logging.h"

#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/random.h>

/* ── INTERNAL TYPES ─────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t nonce;
    uint64_t timestamp_ms;
} nonce_slot_t;

/* ── STATE ──────────────────────────────────────────────────────────────────── */

static nonce_slot_t    g_ring[SM_REPLAY_RING_SIZE];
static uint32_t        g_ring_head  = 0;   /* next write position (monotonic) */
static pthread_mutex_t g_ring_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── HELPERS ────────────────────────────────────────────────────────────────── */

/** Return current wall-clock time in milliseconds. */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL
         + (uint64_t)(ts.tv_nsec / 1000000U);
}

/* ── PUBLIC API ─────────────────────────────────────────────────────────────── */

void sm_replay_init(void)
{
    pthread_mutex_lock(&g_ring_mutex);
    memset(g_ring, 0, sizeof(g_ring));
    g_ring_head = 0;
    pthread_mutex_unlock(&g_ring_mutex);

    sm_log(SM_LOG_INFO,
           "replay: nonce ring initialised (depth=%d, window=%ds)",
           SM_REPLAY_RING_SIZE, SM_REPLAY_WINDOW_SECS);
}

int sm_replay_stamp(sm_replay_token_t *tok)
{
    if (!tok) return -1;

    uint64_t nonce = 0;
    if (getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce)) {
        sm_log(SM_LOG_ERROR, "replay: getrandom failed — cannot stamp token");
        return -1;
    }

    tok->timestamp_ms = now_ms();
    tok->nonce        = nonce;
    return 0;
}

int sm_replay_check(const sm_replay_token_t *tok)
{
    if (!tok) return -1;

    const uint64_t window_ms = (uint64_t)SM_REPLAY_WINDOW_SECS * 1000ULL;
    const uint64_t now       = now_ms();

    /* ── 1. Freshness check (no lock needed — read-only on local vars) ──── */
    uint64_t age_ms;
    if (tok->timestamp_ms > now) {
        /*
         * Message claims to be from the future.  Allow a small tolerance
         * (200 ms) for clock skew between sender and receiver on the same
         * host; reject anything beyond that.
         */
        age_ms = tok->timestamp_ms - now;
        if (age_ms > 200) {
            sm_log(SM_LOG_WARN,
                   "replay: token is %llums in the future — rejecting "
                   "(nonce=%016llx)",
                   (unsigned long long)age_ms,
                   (unsigned long long)tok->nonce);
            return -1;
        }
        age_ms = 0;   /* treat small future skew as "now" */
    } else {
        age_ms = now - tok->timestamp_ms;
    }

    if (age_ms > window_ms) {
        sm_log(SM_LOG_WARN,
               "replay: stale token (age=%llums > window=%dms) — rejecting "
               "(nonce=%016llx)",
               (unsigned long long)age_ms,
               SM_REPLAY_WINDOW_SECS * 1000,
               (unsigned long long)tok->nonce);
        return -1;
    }

    /* ── 2. Uniqueness check (ring scan under mutex) ────────────────────── */
    pthread_mutex_lock(&g_ring_mutex);

    for (int i = 0; i < SM_REPLAY_RING_SIZE; i++) {
        if (g_ring[i].nonce == 0) continue;   /* empty slot */
        if (g_ring[i].nonce != tok->nonce)    continue;   /* different nonce */

        /* Same nonce found — is the slot still within the live window? */
        uint64_t slot_age = (g_ring[i].timestamp_ms <= now)
                          ? (now - g_ring[i].timestamp_ms)
                          : 0;   /* clock regression — treat as live */

        if (slot_age <= window_ms) {
            pthread_mutex_unlock(&g_ring_mutex);
            sm_log(SM_LOG_WARN,
                   "replay: duplicate nonce detected — replay attack? "
                   "(nonce=%016llx, slot_age=%llums)",
                   (unsigned long long)tok->nonce,
                   (unsigned long long)slot_age);
            return -1;
        }

        /*
         * The slot is from a previous window — the nonce has naturally
         * expired. We can reuse the memory slot below.
         */
    }

    /* ── 3. Record the accepted nonce ───────────────────────────────────── */
    uint32_t slot = g_ring_head & (SM_REPLAY_RING_SIZE - 1U);
    g_ring[slot].nonce        = tok->nonce;
    g_ring[slot].timestamp_ms = tok->timestamp_ms;
    g_ring_head++;

    pthread_mutex_unlock(&g_ring_mutex);
    return 0;   /* fresh and unique — accept */
}
