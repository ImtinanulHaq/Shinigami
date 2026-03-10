/**
 * @file    ui_colors.c
 * @brief   Color scheme implementation.
 */
#include "ui_colors.h"
#include <ncurses.h>

void ui_colors_init(void)
{
    init_pair(COLOR_PAIR_DEFAULT,  COLOR_WHITE,  COLOR_BLACK);
    init_pair(COLOR_PAIR_HEADER,   COLOR_BLACK,  COLOR_CYAN);
    init_pair(COLOR_PAIR_GOOD,     COLOR_GREEN,  COLOR_BLACK);
    init_pair(COLOR_PAIR_WARNING,  COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_PAIR_CRITICAL, COLOR_RED,    COLOR_BLACK);
    init_pair(COLOR_PAIR_INFO,     COLOR_CYAN,   COLOR_BLACK);
    init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK,  COLOR_WHITE);
}
