/**
 * @file    monitord_config.h
 * @brief   Configuration management for monitord.
 *
 * Reads from /etc/middleware/monitord.ini or $HOME/.config/monitord.ini.
 * Example INI format:
 *
 *   [general]
 *   refresh_interval_ms = 1000
 *   snapshot_interval_ms = 1000
 *
 *   [network]
 *   unix_socket_path = /tmp/middleware_monitor.sock
 *   http_port = 9090
 *   http_bind_addr = 0.0.0.0
 *
 *   [logging]
 *   log_level = INFO
 *   log_path = /var/log/monitord.log
 *   alert_log_path = /var/log/monitord_alerts.log
 *
 *   [alerts]
 *   webhook_url = http://localhost:8080/alerts
 */
#pragma once

#include <stdint.h>

typedef struct {
    /* General */
    uint32_t refresh_interval_ms;
    uint32_t snapshot_interval_ms;

    /* Network */
    char     unix_socket_path[256];
    uint16_t http_port;
    char     http_bind_addr[64];

    /* Logging */
    char     log_level[16];
    char     log_path[256];
    char     alert_log_path[256];

    /* Alerts */
    char     webhook_url[512];
} monitord_config_t;

/**
 * @brief  Load configuration from INI file (or use defaults).
 * @param  config  Config struct to populate.
 * @param  path    INI file path (NULL = search default locations).
 * @return 0 on success, -1 on fatal error (missing critical setting).
 */
int monitord_config_load(monitord_config_t *config, const char *path);

/**
 * @brief  Reload configuration from disk.
 * @param  config  Config struct to update.
 * @return 0 on success, -1 on error (config unchanged).
 */
int monitord_config_reload(monitord_config_t *config);
