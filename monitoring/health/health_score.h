/**
 * @file    health_score.h
 * @brief   Health score computation engine.
 *
 * Computes a composite health score (0-100) for each service and the overall
 * system based on violation counts, error rates, and anomaly detection.
 *
 * Health score formula (per service):
 *   score = 100
 *     - (restart_count        × 10)
 *     - (drop_rate_pct        ×  5)
 *     - (p99_over_threshold   × 15)
 *     - (seccomp_violations   × 50)
 *     - (fd_leak_detected     × 20)
 *     - (hmac_failures        × 30)
 *     - (memory_pool_exhausted× 25)
 *     - (uring_cq_overflow    × 20)
 *
 * System health = minimum of all service health scores.
 */
#pragma once

#include <stdint.h>
#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Compute health score for a single service.
 * @param  s  Service metrics struct.
 * @return Health score 0-100 (100 = perfect health).
 */
int health_compute_service_score(const service_metrics_t *s);

/**
 * @brief  Compute overall system health score.
 * @param  services  Array of service metrics.
 * @param  count     Number of services.
 * @return System health score 0-100 (minimum of all service scores).
 */
int health_compute_system_score(const service_metrics_t *services, uint32_t count);

/**
 * @brief  Convert health score to human-readable grade.
 * @param  score  Health score 0-100.
 * @return Grade string: "CRITICAL", "DEGRADED", "GOOD", or "EXCELLENT".
 */
const char *health_score_to_grade(int score);

/**
 * @brief  Convert health score to ncurses color pair index.
 * @param  score  Health score 0-100.
 * @return Color pair index (1=red, 2=yellow, 3=green).
 */
int health_score_to_color(int score);
