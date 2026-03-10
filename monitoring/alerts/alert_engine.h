/**
 * @file    alert_engine.h
 * @brief   Alert evaluation and deduplication engine.
 *
 * The alert engine evaluates conditions on every snapshot (1/sec) and fires
 * alerts when conditions are met. Alerts are deduplicated with cooldown
 * periods (CRITICAL: 30s, WARNING: 60s, INFO: 300s). Same condition within
 * cooldown increments occurrence counter instead of creating duplicate alerts.
 */
#pragma once

#include <stdint.h>
#include <pthread.h>
#include "../protocol/monitor_ipc_protocol.h"

#define ALERT_MAX_ACTIVE   1000   /**< Maximum active alerts in memory */
#define ALERT_COOLDOWN_CRITICAL_MS   30000   /**< 30 seconds */
#define ALERT_COOLDOWN_WARNING_MS    60000   /**< 60 seconds */
#define ALERT_COOLDOWN_INFO_MS      300000   /**< 5 minutes */

/* Use types from protocol */

typedef struct {
    alert_record_t   alerts[ALERT_MAX_ACTIVE];
    uint32_t         count;              /**< Number of active alerts */
    pthread_rwlock_t lock;               /**< Reader-writer lock */
} alert_state_t;

/**
 * @brief  Initialize the alert state.
 * @param  state  Alert state struct.
 * @return 0 on success.
 */
int alert_state_init(alert_state_t *state);

/**
 * @brief  Destroy the alert state.
 * @param  state  Alert state struct.
 */
void alert_state_destroy(alert_state_t *state);

/**
 * @brief  Fire an alert (or increment occurrence count if within cooldown).
 * @param  state         Alert state.
 * @param  severity      Alert severity.
 * @param  component     Component name (e.g., "SM", "HAL", service name).
 * @param  condition     Condition description (e.g., "High CPU", "Memory leak").
 * @param  current_value Current metric value as string.
 * @param  threshold     Threshold value as string.
 * @param  suggestion    Suggested action or remediation.
 * @param  timestamp     Current timestamp in milliseconds.
 * @return Alert index in the active alerts array.
 */
uint64_t alert_fire(alert_state_t *state, alert_severity_t severity,
                     const char *component, const char *condition,
                     const char *current_value, const char *threshold,
                     const char *suggestion, uint64_t timestamp);

/**
 * @brief  Get all active alerts within their cooldown period.
 * @param  state     Alert state.
 * @param  out       Output buffer.
 * @param  max       Size of output buffer.
 * @param  timestamp Current timestamp in milliseconds.
 * @return Number of active alerts written.
 */
uint32_t alert_get_active(alert_state_t *state, alert_record_t *out,
                           uint32_t max, uint64_t timestamp);

/**
 * @brief  Clear all alerts (user-initiated reset).
 * @param  state  Alert state.
 */
void alert_clear_all(alert_state_t *state);
