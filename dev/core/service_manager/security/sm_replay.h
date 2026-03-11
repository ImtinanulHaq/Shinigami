#ifndef SM_REPLAY_H
#define SM_REPLAY_H

/*
 * sm_replay.h  -  Replay-attack protection for IPC messages.
 *
 * Every outgoing IPC message MUST embed an sm_replay_token_t.
 * The receiver MUST call sm_replay_check() before processing the message.
 *
 * Protection model:
 *   - Freshness:  messages older than SM_REPLAY_WINDOW_SECS are rejected.
 *   - Uniqueness: each nonce is stored in a ring buffer; a second message
 *                 with the same nonce within the window is rejected.
 *
 * Usage (sender):
 *   sm_replay_token_t tok;
 *   sm_replay_stamp(&tok);
 *   // embed tok in message before calling sm_hmac_sha256()
 *
 * Usage (receiver):
 *   // verify HMAC first, then:
 *   if (sm_replay_check(&msg->tok) != 0) { reject(); }
 *
 * Thread-safety: sm_replay_stamp() and sm_replay_check() are fully
 * thread-safe.  sm_replay_init() must be called exactly once at startup.
 */

#include <stdint.h>

/* ── CONSTANTS ──────────────────────────────────────────────────────────────── */

/** Max age of an accepted message, in seconds. */
#define SM_REPLAY_WINDOW_SECS   5

/**
 * Ring buffer depth.  Must be a power of 2.
 * Rule of thumb: depth >= peak_throughput_rps * SM_REPLAY_WINDOW_SECS * 2.
 * 512 covers 50 RPS sustained with 2× headroom.
 */
#define SM_REPLAY_RING_SIZE   512

/* ── TOKEN ──────────────────────────────────────────────────────────────────── */

/**
 * Replay protection token — embed this verbatim in every IPC message
 * before computing the HMAC.  Both fields are included in the HMAC input
 * so they cannot be tampered with independently.
 */
typedef struct {
    uint64_t timestamp_ms;   /**< CLOCK_REALTIME milliseconds at send time */
    uint64_t nonce;           /**< 64-bit cryptographically random value    */
} sm_replay_token_t;

/* ── PUBLIC API ─────────────────────────────────────────────────────────────── */

/**
 * sm_replay_init() — Initialise the nonce ring buffer.
 *
 * Must be called once at startup, before any worker threads are started.
 * Calling a second time re-initialises the ring (safe but loses history).
 */
void sm_replay_init(void);

/**
 * sm_replay_stamp() — Fill *tok with a fresh timestamp + random nonce.
 *
 * Returns  0 on success.
 * Returns -1 if getrandom(2) fails (very unlikely; treat as fatal).
 */
int sm_replay_stamp(sm_replay_token_t *tok);

/**
 * sm_replay_check() — Validate an incoming token.
 *
 * Checks:
 *   1. tok->timestamp_ms is within SM_REPLAY_WINDOW_SECS of now.
 *   2. tok->nonce has not been seen before within the live window.
 *
 * On acceptance the nonce is inserted into the ring so future replay
 * attempts with the same nonce are rejected.
 *
 * Returns  0 — token is fresh and unique (accept the message).
 * Returns -1 — token is stale, from the future, or a replay (reject).
 */
int sm_replay_check(const sm_replay_token_t *tok);

#endif /* SM_REPLAY_H */
