/**
 * @file    alert_rules.h
 * @brief   Alert rule definitions and evaluation.
 *
 * Defines all alert rules and evaluates them against the current snapshot.
 * Rules are organized by severity:
 *   - 7 CRITICAL rules
 *   - 14 WARNING rules
 *   - 5 INFO rules
 */
#pragma once

#include <stdint.h>
#include "alert_engine.h"
#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Evaluate all alert rules against the current snapshot.
 * @param  snapshot   Current system snapshot.
 * @param  alert_st   Alert state (will be updated with new alerts).
 * @param  timestamp  Current timestamp in milliseconds.
 * @return Number of alerts fired (new + deduplicated).
 */
uint32_t alert_rules_evaluate(const mon_snapshot_t *snapshot,
                                alert_state_t *alert_st,
                                uint64_t timestamp);

/* ═══ CRITICAL RULES ═══════════════════════════════════════════════════════ */

/* 1. Service restart detected */
/* 2. Seccomp violation (sandbox escape attempt) */
/* 3. HMAC verification failure */
/* 4. Memory pool exhaustion */
/* 5. io_uring completion queue overflow */
/* 6. Ring buffer overflow (message drops) */
/* 7. Health score < 50 */

/* ═══ WARNING RULES ════════════════════════════════════════════════════════ */

/* 8.  Health score 50-70 */
/* 9.  High CPU usage (>80%) sustained for 30s */
/* 10. High RAM usage (>85%) sustained for 30s */
/* 11. FD leak detected */
/* 12. Service p99 latency > threshold */
/* 13. Message drop rate > 1% */
/* 14. HAL error rate > 5% */
/* 15. IPC queue depth > 80% capacity */
/* 16. Replay attack detected */
/* 17. Collector STALE (missed 3× interval) */
/* 18. Collector OFFLINE */
/* 19. Watchdog starvation (missed ping) */
/* 20. Config file change detected */
/* 21. Swap usage > 50% */

/* ═══ INFO RULES ═══════════════════════════════════════════════════════════ */

/* 22. Service started */
/* 23. Service stopped */
/* 24. Health score 70-90 */
/* 25. Collector reconnected */
/* 26. Config reload successful */
