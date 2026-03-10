/**
 * @file    health_score.c
 * @brief   Health score computation implementation.
 */
#include "health_score.h"
#include <limits.h>

int health_compute_service_score(const service_metrics_t *s)
{
    int score = 100;

    /* Apply penalties based on available metrics */
    score -= (s->restart_count        * 10);
    score -= (s->seccomp_violations   * 50);
    score -= (s->hmac_failures        * 30);
    score -= (s->replay_attacks       * 20);
    
    /* Use frames_dropped as drop rate indicator */
    if (s->frames_captured > 0) {
        double drop_rate_pct = (double)s->frames_dropped / (double)s->frames_captured * 100.0;
        score -= (int)(drop_rate_pct * 0.5);
    }

    /* No fd_max field in service_metrics_t, skip FD check */

    /* Clamp to 0-100 range */
    if (score < 0)   score = 0;
    if (score > 100) score = 100;

    return score;
}

int health_compute_system_score(const service_metrics_t *services, uint32_t count)
{
    if (count == 0) return 100;

    int min_score = INT_MAX;
    for (uint32_t i = 0; i < count; i++) {
        int score = health_compute_service_score(&services[i]);
        if (score < min_score)
            min_score = score;
    }

    return (min_score == INT_MAX) ? 100 : min_score;
}

const char *health_score_to_grade(int score)
{
    if (score >= 90) return "EXCELLENT";
    if (score >= 70) return "GOOD";
    if (score >= 50) return "DEGRADED";
    return "CRITICAL";
}

int health_score_to_color(int score)
{
    if (score >= 90) return 3;  /* Green */
    if (score >= 70) return 3;  /* Green */
    if (score >= 50) return 2;  /* Yellow */
    return 1;                   /* Red */
}
