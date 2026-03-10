/**
 * @file    panel_topbar.h
 * @brief   Top status bar panel (system info, health score, uptime).
 */
#pragma once

#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Render the top bar.
 * @param  snapshot  Current snapshot.
 * @param  y         Y position.
 * @param  cols      Width in columns.
 */
void panel_topbar_render(const mon_snapshot_t *snapshot, int y, int cols);
