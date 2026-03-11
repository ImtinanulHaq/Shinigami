/**
 * @file    ui_layout.h
 * @brief   Main layout manager for TUI panels.
 */
#pragma once

#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Render the entire UI layout with the given snapshot.
 * @param  snapshot  Current system snapshot.
 */
void ui_layout_render(const mon_snapshot_t *snapshot);

/**
 * @brief  Switch to a different tab/panel.
 * @param  tab_index  Tab index (0-based).
 */
void ui_layout_set_tab(int tab_index);

/**
 * @brief  Get the current active tab index.
 * @return Current tab index.
 */
int ui_layout_get_tab(void);

/**
 * @brief  Forward scroll commands to the active panel.
 * @param  direction  UI_INPUT_UP/DOWN/PGUP/PGDN
 */
void ui_layout_scroll(int direction);
