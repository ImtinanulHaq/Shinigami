#ifndef SM_ADVANCED_RATELIMIT_H
#define SM_ADVANCED_RATELIMIT_H

/*
 * sm_advanced_ratelimit.h - Advanced rate limiting with Penalty Box.
 *
 * Per-PID + per-service-type + per-operation-type token-bucket limiting.
 *
 * Penalty Box
 * ───────────
 * If a caller hits the rate limit PENALTY_THRESHOLD times within
 * PENALTY_WINDOW_SECS, it is placed in the "penalty box" and all further
 * requests are rejected for PENALTY_DURATION_SECS seconds.  The ban
 * expires automatically; no manual reset is required.
 *
 * This prevents CPU/network waste from runaway services that hammer the
 * manager even after their token bucket is exhausted.
 */

#include <sys/types.h>
#include <stdint.h>

/* ── PENALTY BOX CONSTANTS ──────────────────────────────────────────────────── */

/** Number of rate-limit violations that trigger a ban. */
#define PENALTY_THRESHOLD      5

/** Window (seconds) in which violations are counted toward the threshold. */
#define PENALTY_WINDOW_SECS   30

/** Duration (seconds) of a penalty-box ban. */
#define PENALTY_DURATION_SECS 60

/* ── API ────────────────────────────────────────────────────────────────────── */

/** Initialise token buckets and penalty box.  Call once at startup. */
int sm_ratelimit_init(void);

/**
 * sm_ratelimit_check_extended() — Check rate limit.
 *
 * Returns  0  — request allowed.
 * Returns -1  — request denied (token exhaustion or penalty box).
 *
 * Side-effect on denial: violation counter is incremented.  If the
 * counter reaches PENALTY_THRESHOLD within PENALTY_WINDOW_SECS, a
 * PENALTY_DURATION_SECS ban is imposed automatically.
 */
int sm_ratelimit_check_extended(pid_t pid, const char *service_name,
                                uint16_t operation);

/** Configure the per-(service, pid) token-bucket capacity. */
void sm_ratelimit_set_service_limit(const char *service_name, pid_t pid,
                                    double capacity);

/** Manually reset token bucket and penalty state for a service. */
void sm_ratelimit_reset_service(const char *service_name);

/** Return current token count for (pid, service_name), or 0 if not tracked. */
int sm_ratelimit_get_tokens(pid_t pid, const char *service_name);

/**
 * sm_ratelimit_is_banned() — Query penalty-box status.
 *
 * Returns  1 — caller is currently banned.
 * Returns  0 — not banned.
 *
 * Useful for the TUI / monitoring layer to display ban status.
 */
int sm_ratelimit_is_banned(pid_t pid, const char *service_name);

#endif /* SM_ADVANCED_RATELIMIT_H */
