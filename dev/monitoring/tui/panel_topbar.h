/**
 * @file    panel_topbar.h
 * @brief   Top bar — SHINIGAMI MONITOR, live clock, connection indicator.
 */
#pragma once

#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Render the top bar (row 0).
 * @param  snapshot  Current snapshot (may be NULL during init).
 * @param  y         Row to render on.
 * @param  cols      Terminal width.
 */
void panel_topbar_render(const mon_snapshot_t *snapshot, int y, int cols);

/**
 * @brief  Update the connection state shown in the top bar.
 * @param  connected  1 = connected (\u25cf LIVE), 0 = disconnected (\u2717 DISC).
 */
void panel_topbar_set_connected(int connected);
