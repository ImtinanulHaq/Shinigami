/**
 * @file    health_history.c
 * @brief   Health score history implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "health_history.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

int health_history_init(health_history_t *h, const char *name)
{
    memset(h, 0, sizeof(*h));
    strncpy(h->name, name, sizeof(h->name) - 1);
    h->capacity = HEALTH_HISTORY_CAPACITY;

    h->scores = calloc(h->capacity, sizeof(int));
    h->timestamps_ms = calloc(h->capacity, sizeof(uint64_t));

    if (!h->scores || !h->timestamps_ms) {
        free(h->scores);
        free(h->timestamps_ms);
        return -1;
    }

    pthread_rwlock_init(&h->lock, NULL);
    return 0;
}

void health_history_destroy(health_history_t *h)
{
    if (h->scores) {
        free(h->scores);
        h->scores = NULL;
    }
    if (h->timestamps_ms) {
        free(h->timestamps_ms);
        h->timestamps_ms = NULL;
    }
    pthread_rwlock_destroy(&h->lock);
}

void health_history_push(health_history_t *h, int score, uint64_t timestamp)
{
    pthread_rwlock_wrlock(&h->lock);

    /* Rate limiting: skip if within resolution window */
    if (h->last_update_ms &&
        timestamp - h->last_update_ms < HEALTH_HISTORY_RESOLUTION_MS) {
        pthread_rwlock_unlock(&h->lock);
        return;
    }

    h->scores[h->head] = score;
    h->timestamps_ms[h->head] = timestamp;
    h->head = (h->head + 1) % h->capacity;
    h->last_update_ms = timestamp;

    pthread_rwlock_unlock(&h->lock);
}

uint32_t health_history_get_latest(health_history_t *h, int *out, uint32_t n)
{
    if (n == 0 || n > h->capacity) n = h->capacity;

    pthread_rwlock_rdlock(&h->lock);

    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (h->head + h->capacity - n + i) % h->capacity;
        if (h->timestamps_ms[idx] == 0) break;  /* Not filled yet */
        out[count++] = h->scores[idx];
    }

    pthread_rwlock_unlock(&h->lock);
    return count;
}

uint32_t health_history_get_sparkline(health_history_t *h, int *out,
                                       uint32_t count, uint32_t duration)
{
    if (count == 0) return 0;

    pthread_rwlock_rdlock(&h->lock);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
    uint64_t cutoff = now - (uint64_t)duration * 1000;

    /* Sample evenly across the duration */
    uint32_t written = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t target_ts = cutoff + (i * duration * 1000) / count;
        
        /* Find closest timestamp to target_ts */
        int best_score = 100;  /* Default to healthy if no data */
        uint64_t best_diff = UINT64_MAX;

        for (uint32_t j = 0; j < h->capacity; j++) {
            if (h->timestamps_ms[j] == 0) continue;
            if (h->timestamps_ms[j] < cutoff) continue;

            uint64_t diff = (h->timestamps_ms[j] > target_ts)
                ? (h->timestamps_ms[j] - target_ts)
                : (target_ts - h->timestamps_ms[j]);

            if (diff < best_diff) {
                best_diff = diff;
                best_score = h->scores[j];
            }
        }

        out[written++] = best_score;
    }

    pthread_rwlock_unlock(&h->lock);
    return written;
}
