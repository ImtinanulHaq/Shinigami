/**
 * @file    ui_colors.h
 * @brief   Color scheme definitions for TUI.
 */
#pragma once

/* Color pair indices */
#define COLOR_PAIR_DEFAULT    1
#define COLOR_PAIR_HEADER     2
#define COLOR_PAIR_GOOD       3
#define COLOR_PAIR_WARNING    4
#define COLOR_PAIR_CRITICAL   5
#define COLOR_PAIR_INFO       6
#define COLOR_PAIR_SELECTED   7

/**
 * @brief  Initialize all color pairs.
 */
void ui_colors_init(void);
