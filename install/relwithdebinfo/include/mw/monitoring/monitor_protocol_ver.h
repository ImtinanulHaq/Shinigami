/**
 * @file    monitor_protocol_ver.h
 * @brief   Version constants for the monitord ↔ tui IPC protocol.
 *
 * Bumping PROTOCOL_VERSION is a breaking change — both binaries must agree.
 * PROTOCOL_MINOR increments are backward-compatible feature additions.
 *
 * @thread_safety  Read-only constants — safe everywhere.
 */
#pragma once

#include <stdint.h>

/** Wire protocol major version — must match on both sides. */
#define MON_PROTOCOL_VERSION        ((uint8_t)1)

/** Minor version — informational only; no compatibility gate on this. */
#define MON_PROTOCOL_MINOR          ((uint8_t)0)

/** Magic bytes placed at the start of every framed message: "MON\0". */
#define MON_MSG_MAGIC               UINT32_C(0x4D4F4E00)

/** Maximum payload bytes in a single message (16 MiB). */
#define MON_MAX_PAYLOAD_BYTES       (16u * 1024u * 1024u)

/** Default Unix-domain socket path for monitord. */
#define MON_SOCKET_PATH_DEFAULT     "/tmp/middleware_monitor.sock"

/** Default HTTP port for Prometheus /metrics scraping. */
#define MON_HTTP_PORT_DEFAULT       9090

/** Maximum simultaneous TUI clients connected to one monitord instance. */
#define MON_MAX_TUI_CLIENTS         8

/** Minimum allowed refresh rate (ms). */
#define MON_REFRESH_MIN_MS          100u

/** Maximum allowed refresh rate (ms). */
#define MON_REFRESH_MAX_MS          5000u

/** Default TUI refresh rate. */
#define MON_REFRESH_DEFAULT_MS      100u

/** Stale threshold: if last update was more than this multiple of the
 *  collector's interval, the collector transitions to STALE state. */
#define MON_STALE_MULTIPLIER        3u

/** Staggered startup delay per collector index (milliseconds). */
#define MON_STAGGER_DELAY_MS        200u
