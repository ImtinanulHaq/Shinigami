/**
 * @file    health_history.h
 * @brief   Health score history tracking.
 *
 * Maintains a circular buffer of health scores over time for each service
 * and the system, with 1-second resolution and 3600-second (1 hour) history.
 */
#pragma once

#include <stdint.h>
#include <pthread.h>

#define HEALTH_HISTORY_DURATION_SECS  3600   /**< 1 hour of history */
#define HEALTH_HISTORY_RESOLUTION_MS  1000   /**< 1-second resolution */
#define HEALTH_HISTORY_CAPACITY       (HEALTH_HISTORY_DURATION_SECS)

typedef struct {
    char             name[32];           /**< Service name or "system" */
    uint32_t         head;               /**< Write index in circular buffer */
    uint32_t         capacity;           /**< Buffer capacity */
    uint64_t         last_update_ms;     /**< Last update timestamp */
    int             *scores;             /**< Circular buffer of scores 0-100 */
    uint64_t        *timestamps_ms;      /**< Circular buffer of timestamps */
    pthread_rwlock_t lock;               /**< Reader-writer lock */
} health_history_t;

/**
 * @brief  Initialize a health history buffer.
 * @param  h     Health history struct.
 * @param  name  Service name or "system".
 * @return 0 on success, -1 on malloc failure.
 */
int health_history_init(health_history_t *h, const char *name);

/**
 * @brief  Destroy a health history buffer.
 * @param  h  Health history struct.
 */
void health_history_destroy(health_history_t *h);

/**
 * @brief  Push a new health score to the history.
 * @param  h          Health history struct.
 * @param  score      Health score 0-100.
 * @param  timestamp  Monotonic timestamp in milliseconds.
 */
void health_history_push(health_history_t *h, int score, uint64_t timestamp);

/**
 * @brief  Get the latest N scores from history.
 * @param  h      Health history struct.
 * @param  out    Output buffer (must be at least N elements).
 * @param  n      Number of scores to retrieve.
 * @return Number of scores written (may be less than N if history is not full).
 */
uint32_t health_history_get_latest(health_history_t *h, int *out, uint32_t n);

/**
 * @brief  Get a sparkline-friendly sample of the history.
 * @param  h         Health history struct.
 * @param  out       Output buffer (must be at least count elements).
 * @param  count     Number of samples to retrieve.
 * @param  duration  Duration in seconds to sample from (e.g., 60 for last minute).
 * @return Number of samples written.
 */
uint32_t health_history_get_sparkline(health_history_t *h, int *out,
                                       uint32_t count, uint32_t duration);
