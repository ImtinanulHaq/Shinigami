/**
 * @file    alert_notify.h
 * @brief   Alert notification mechanisms.
 *
 * Supports:
 *   - Terminal bell (\a) for CRITICAL alerts
 *   - Log file append (monitord_alerts.log)
 *   - Webhook POST (optional, configured via INI)
 */
#pragma once

#include <stdint.h>
#include "alert_engine.h"

/**
 * @brief  Notify about a new or updated alert.
 * @param  alert  Alert record.
 * @return 0 on success, -1 on failure.
 */
int alert_notify(const alert_record_t *alert);

/**
 * @brief  Initialize the notification subsystem.
 * @param  log_path     Path to log file (or NULL for default).
 * @param  webhook_url  Webhook URL (or NULL to disable).
 * @return 0 on success.
 */
int alert_notify_init(const char *log_path, const char *webhook_url);

/**
 * @brief  Shutdown the notification subsystem.
 */
void alert_notify_shutdown(void);
