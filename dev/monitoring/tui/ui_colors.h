/**
 * @file    ui_colors.h
 * @brief   Color scheme — green-on-black terminal aesthetic.
 */
#pragma once

/* Color pair indices */
#define COLOR_PAIR_DEFAULT    1   /**< Normal text — white on black            */
#define COLOR_PAIR_HEADER     2   /**< Panel headers — white (use A_BOLD)      */
#define COLOR_PAIR_GOOD       3   /**< Bright green — healthy / running        */
#define COLOR_PAIR_WARNING    4   /**< Yellow — warn threshold                 */
#define COLOR_PAIR_CRITICAL   5   /**< Red — critical / stopped                */
#define COLOR_PAIR_INFO       6   /**< Cyan — informational                    */
#define COLOR_PAIR_SELECTED   7   /**< Selected / highlighted item             */
#define COLOR_PAIR_BORDER     8   /**< Dim green — panel borders               */
#define COLOR_PAIR_OK         9   /**< OK badge — black text on green bg       */
#define COLOR_PAIR_DIM        10  /**< Dim white — secondary labels            */

/**
 * @brief  Initialize all color pairs.
 */
void ui_colors_init(void);
