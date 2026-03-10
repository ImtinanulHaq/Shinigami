/**
 * @file    monitord_http.h
 * @brief   HTTP server for Prometheus metrics and health checks.
 *
 * Listens on port 9090 (configurable) and serves:
 *   - GET /metrics  → Prometheus text format (from metrics_export_prometheus.c)
 *   - GET /health   → JSON health status {"status": "ok", "score": 95}
 */
#pragma once

#include <stdint.h>
#include "monitord_state.h"
#include "monitord_config.h"

typedef struct monitord_http monitord_http_t;

/**
 * @brief  Create and start the HTTP server.
 * @param  config  Configuration struct.
 * @param  state   Shared state struct.
 * @return Server handle, or NULL on failure.
 */
monitord_http_t *monitord_http_start(monitord_config_t *config,
                                      monitord_state_t *state);

/**
 * @brief  Stop the HTTP server.
 * @param  http  Server handle.
 */
void monitord_http_stop(monitord_http_t *http);
