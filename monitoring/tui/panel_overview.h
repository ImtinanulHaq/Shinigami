/**
 * @file    panel_overview.h
 * @brief   Overview panel (summary of all subsystems).
 */
#pragma once

#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Render the overview panel.
 * @param  snapshot  Current snapshot.
 * @param  y         Y position.
 * @param  h         Height.
 * @param  cols      Width.
 */
void panel_overview_render(const mon_snapshot_t *snapshot, int y, int h, int cols);
