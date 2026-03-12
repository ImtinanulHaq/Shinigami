/**
 * @file    ui_colors.c
 * @brief   Color scheme — green-on-black terminal aesthetic.
 */
#include "ui_colors.h"
#include <ncurses.h>

void ui_colors_init(void)
{
    init_pair(COLOR_PAIR_DEFAULT,  COLOR_WHITE,  COLOR_BLACK);  /* normal text      */
    init_pair(COLOR_PAIR_HEADER,   COLOR_WHITE,  COLOR_BLACK);  /* headers (A_BOLD) */
    init_pair(COLOR_PAIR_GOOD,     COLOR_GREEN,  COLOR_BLACK);  /* bright green     */
    init_pair(COLOR_PAIR_WARNING,  COLOR_YELLOW, COLOR_BLACK);  /* yellow           */
    init_pair(COLOR_PAIR_CRITICAL, COLOR_RED,    COLOR_BLACK);  /* red              */
    init_pair(COLOR_PAIR_INFO,     COLOR_CYAN,   COLOR_BLACK);  /* cyan             */
    init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK,  COLOR_WHITE);  /* inverted         */
    init_pair(COLOR_PAIR_BORDER,   COLOR_GREEN,  COLOR_BLACK);  /* dim green border */
    init_pair(COLOR_PAIR_OK,       COLOR_BLACK,  COLOR_GREEN);  /* OK badge bg      */
    init_pair(COLOR_PAIR_DIM,      COLOR_WHITE,  COLOR_BLACK);  /* dim labels       */
}
