/**
 * @file    monitord_server.h
 * @brief   Unix domain socket server for TUI clients.
 *
 * Listens on /tmp/middleware_monitor.sock, accepts multiple concurrent
 * clients, handles handshake (HELLO/WELCOME), and broadcasts snapshots
 * to all connected clients every refresh_interval_ms.
 */
#pragma once

#include <stdint.h>
#include "monitord_state.h"
#include "monitord_config.h"

typedef struct monitord_server monitord_server_t;

/**
 * @brief  Create and start the Unix socket server.
 * @param  config  Configuration struct.
 * @param  state   Shared state struct.
 * @return Server handle, or NULL on failure.
 */
monitord_server_t *monitord_server_start(monitord_config_t *config,
                                          monitord_state_t *state);

/**
 * @brief  Stop the server and disconnect all clients.
 * @param  server  Server handle.
 */
void monitord_server_stop(monitord_server_t *server);

/**
 * @brief  Broadcast a snapshot to all connected clients.
 * @param  server    Server handle.
 * @param  snapshot  Snapshot to send.
 * @return Number of clients successfully notified.
 */
uint32_t monitord_server_broadcast_snapshot(monitord_server_t *server,
                                              const mon_snapshot_t *snapshot);
