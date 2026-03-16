/**
 * @file    main_config.h
 * @brief   All configuration constants for shinigami-terminal.
 *
 * RULE: Nothing is hardcoded anywhere except here.
 * Every socket path, log path, timeout, buffer size, etc. lives here.
 *
 * DO NOT hardcode strings, numbers, or paths anywhere else in the codebase.
 */
#ifndef MAIN_CONFIG_H
#define MAIN_CONFIG_H

/* ──────────────────────────────────────────────────────────────────────── */
/* SOCKET PATHS */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_SM_SOCKET_PRIMARY    "/run/servicemanager.sock"
#define MAIN_SM_SOCKET_FALLBACK   "/tmp/servicemanager.sock"
#define MAIN_SM_KEY_PRIMARY       "/run/servicemanager.key"
#define MAIN_SM_KEY_FALLBACK      "/tmp/servicemanager.key"
#define MAIN_MONITOR_SOCKET       "/tmp/middleware_monitor.sock"

/* ──────────────────────────────────────────────────────────────────────── */
/* LOG FILE PATHS */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_AUDIT_LOG_PATH       "/var/log/middleware/main-audit.log"
#define MAIN_SM_LOG_PATH          "/var/log/servicemanager.log"
#define MAIN_AUDIO_LOG_PATH       "/var/log/middleware/audio_service.log"
#define MAIN_CAMERA_LOG_PATH      "/var/log/middleware/camera_service.log"
#define MAIN_SENSOR_LOG_PATH      "/var/log/middleware/sensor_service.log"
#define MAIN_GPIO_LOG_PATH        "/var/log/middleware/gpio_service.log"
#define MAIN_MONITORD_LOG_PATH    "/var/log/middleware/monitord.log"

/* ──────────────────────────────────────────────────────────────────────── */
/* TIMING (milliseconds unless stated) */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_REFRESH_MS           33        /**< ~30 fps (1000/30) */
#define MAIN_SM_TIMEOUT_MS        500       /**< SM socket read timeout */
#define MAIN_MONITOR_TIMEOUT_MS   1000      /**< Monitor socket read timeout */
#define MAIN_STATUS_MSG_DURATION  3000      /**< Auto-clear status after ms */
#define MAIN_RECONNECT_INTERVAL   3000      /**< Retry socket after disconnect */
#define MAIN_INOTIFY_POLL_MS      100       /**< Log file watch poll */

/* ──────────────────────────────────────────────────────────────────────── */
/* BUFFER & QUEUE SIZES */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_MAX_LOG_LINES        5000      /**< Ring buffer per log file */
#define MAIN_CMD_HISTORY_SIZE     64        /**< Command history buffer */
#define MAIN_CMD_BUF_SIZE         512       /**< Max command line input */
#define MAIN_STATUS_BUF_SIZE      256       /**< Status message buffer */
#define MAIN_MAX_SERVICES         16        /**< For iteration loops */
#define MAIN_LINE_BUF_SIZE        512       /**< Log line max length */
#define MAIN_MAX_ALERTS           32        /**< Alert display limit */
#define MAIN_CPU_HISTORY_LEN      20        /**< CPU graph rolling buffer */

/* ──────────────────────────────────────────────────────────────────────── */
/* TERMINAL REQUIREMENTS */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_MIN_COLS             100       /**< Minimum terminal width */
#define MAIN_MIN_ROWS             28        /**< Minimum terminal height */

/* ──────────────────────────────────────────────────────────────────────── */
/* VERSION & APPLICATION NAME */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_VERSION              "1.0.0"
#define MAIN_NAME                 "SHINIGAMI TERMINAL"
#define MAIN_SYMBOL               "死"    /**< Japanese "death" kanji */

/* ──────────────────────────────────────────────────────────────────────── */
/* PANEL INDICES (for tab navigation)*/
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_PANEL_DASHBOARD      0
#define MAIN_PANEL_SERVICES       1
#define MAIN_PANEL_PROXIES        2
#define MAIN_PANEL_SECURITY       3
#define MAIN_PANEL_HAL            4
#define MAIN_PANEL_LOGS           5
#define MAIN_PANEL_MONITOR        6
#define MAIN_PANEL_HELP           7
#define MAIN_PANEL_COUNT          8

/* ──────────────────────────────────────────────────────────────────────── */
/* PANEL NAMES FOR TAB BAR */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_PANEL_NAME_DASHBOARD "Dashboard"
#define MAIN_PANEL_NAME_SERVICES  "Services"
#define MAIN_PANEL_NAME_PROXIES   "Proxies"
#define MAIN_PANEL_NAME_SECURITY  "Security"
#define MAIN_PANEL_NAME_HAL       "HAL"
#define MAIN_PANEL_NAME_LOGS      "Logs"
#define MAIN_PANEL_NAME_MONITOR   "Monitor"
#define MAIN_PANEL_NAME_HELP      "Help"

/* ──────────────────────────────────────────────────────────────────────── */
/* PANEL SHORTCUTS (for type-to-navigate) */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_SHORTCUT_MONITOR     "monitor"
#define MAIN_SHORTCUT_LOGS        "logs"
#define MAIN_SHORTCUT_SERVICES    "services"
#define MAIN_SHORTCUT_SECURITY    "security"
#define MAIN_SHORTCUT_HAL         "hal"
#define MAIN_SHORTCUT_HELP        "help"
#define MAIN_SHORTCUT_DASHBOARD   "dashboard"
#define MAIN_SHORTCUT_PROXIES     "proxies"

/* ──────────────────────────────────────────────────────────────────────── */
/* EXPORT PATHS (for metrics and logs) */
/* ──────────────────────────────────────────────────────────────────────── */
#define MAIN_EXPORT_LOGS_DIR      "/tmp"
#define MAIN_EXPORT_LOGS_PREFIX   "shinigami-logs-export"
#define MAIN_SNAPSHOT_EXPORT_PATH "/tmp/snapshot.json"

#endif /* MAIN_CONFIG_H */
