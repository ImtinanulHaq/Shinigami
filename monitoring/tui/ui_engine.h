/**
 * @file    ui_engine.h
 * @brief   Core ncurses initialization and utilities.
 */
#pragma once

#include <ncurses.h>

/**
 * @brief  Initialize ncurses and color pairs.
 * @return 0 on success, -1 on failure.
 */
int ui_engine_init(void);

/**
 * @brief  Shutdown ncurses and restore terminal.
 */
void ui_engine_shutdown(void);

/**
 * @brief  Get terminal dimensions.
 * @param  rows  Output: terminal rows.
 * @param  cols  Output: terminal columns.
 */
void ui_engine_get_size(int *rows, int *cols);
