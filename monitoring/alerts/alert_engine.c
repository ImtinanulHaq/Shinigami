/**
 * @file    alert_engine.c
 * @brief   Alert evaluation and deduplication implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "alert_engine.h"
#include <stdlib.h>
#include <string.h>

static uint32_t get_cooldown_ms(alert_severity_t severity)
{
    switch (severity) {
    case ALERT_SEV_CRIT: return ALERT_COOLDOWN_CRITICAL_MS;
    case ALERT_SEV_WARN:  return ALERT_COOLDOWN_WARNING_MS;
    case ALERT_SEV_INFO:     return ALERT_COOLDOWN_INFO_MS;
    default:                      return ALERT_COOLDOWN_WARNING_MS;
    }
}

int alert_state_init(alert_state_t *state)
{
    memset(state, 0, sizeof(*state));
    pthread_rwlock_init(&state->lock, NULL);
    return 0;
}

void alert_state_destroy(alert_state_t *state)
{
    pthread_rwlock_destroy(&state->lock);
}

uint64_t alert_fire(alert_state_t *state, alert_severity_t severity,
                     const char *component, const char *condition,
                     const char *current_value, const char *threshold,
                     const char *suggestion, uint64_t timestamp)
{
    pthread_rwlock_wrlock(&state->lock);

    uint32_t cooldown = get_cooldown_ms(severity);

    /* Check if this alert already exists within cooldown */
    for (uint32_t i = 0; i < state->count; i++) {
        alert_record_t *a = &state->alerts[i];

        if (strcmp(a->component, component) == 0 &&
            strcmp(a->condition, condition) == 0 &&
            timestamp - a->last_ts_ms < cooldown)
        {
            /* Deduplicate: increment occurrence count */
            a->last_ts_ms = timestamp;
            a->occurrences++;
            a->active = 1;
            pthread_rwlock_unlock(&state->lock);
            return i;  /* Return array index as ID */
        }
    }

    /* New alert: add to the list */
    if (state->count >= ALERT_MAX_ACTIVE) {
        /* Evict oldest alert */
        uint32_t oldest_idx = 0;
        uint64_t oldest_ts = state->alerts[0].last_ts_ms;
        for (uint32_t i = 1; i < state->count; i++) {
            if (state->alerts[i].last_ts_ms < oldest_ts) {
                oldest_ts = state->alerts[i].last_ts_ms;
                oldest_idx = i;
            }
        }
        /* Shift array to remove oldest */
        memmove(&state->alerts[oldest_idx],
                &state->alerts[oldest_idx + 1],
                (state->count - oldest_idx - 1) * sizeof(alert_record_t));
        state->count--;
    }

    alert_record_t *a = &state->alerts[state->count];
    a->severity = severity;
    a->first_ts_ms = timestamp;
    a->last_ts_ms = timestamp;
    a->occurrences = 1;
    a->active = 1;
    strncpy(a->component, component, sizeof(a->component) - 1);
    strncpy(a->condition, condition, sizeof(a->condition) - 1);
    strncpy(a->current_value, current_value, sizeof(a->current_value) - 1);
    strncpy(a->threshold, threshold, sizeof(a->threshold) - 1);
    strncpy(a->suggestion, suggestion, sizeof(a->suggestion) - 1);

    uint64_t id = state->count;
    state->count++;
    pthread_rwlock_unlock(&state->lock);
    return id;
}

uint32_t alert_get_active(alert_state_t *state, alert_record_t *out,
                           uint32_t max, uint64_t timestamp)
{
    pthread_rwlock_rdlock(&state->lock);

    uint32_t written = 0;
    for (uint32_t i = 0; i < state->count && written < max; i++) {
        alert_record_t *a = &state->alerts[i];
        uint32_t cooldown = get_cooldown_ms(a->severity);

        if (timestamp - a->last_ts_ms < cooldown) {
            memcpy(&out[written++], a, sizeof(alert_record_t));
        }
    }

    pthread_rwlock_unlock(&state->lock);
    return written;
}

void alert_clear_all(alert_state_t *state)
{
    pthread_rwlock_wrlock(&state->lock);
    state->count = 0;
    pthread_rwlock_unlock(&state->lock);
}
