/**
 * @file    ui_colors.c
 * @brief   Shinigami color theme — black + blood red + silver white.
 *          Matches the shinigami-kek4.onrender.com website aesthetic.
 */
#include "ui_colors.h"
#include <ncurses.h>

void ui_colors_init(void)
{
    /*
     * Website palette:
     *   Background : pure black
     *   Primary    : deep crimson / blood red  (COLOR_RED)
     *   Accent     : silver-white              (COLOR_WHITE)
     *   Danger/warn: yellow                    (COLOR_YELLOW)
     *   Dim chrome : dim white (labels)        (COLOR_WHITE no bold)
     */
    init_pair(COLOR_PAIR_DEFAULT,  COLOR_WHITE, COLOR_BLACK);  /* body text         */
    init_pair(COLOR_PAIR_HEADER,   COLOR_WHITE, COLOR_BLACK);  /* panel titles bold */
    init_pair(COLOR_PAIR_GOOD,     COLOR_RED,   COLOR_BLACK);  /* brand crimson     */
    init_pair(COLOR_PAIR_WARNING,  COLOR_YELLOW,COLOR_BLACK);  /* warn yellow       */
    init_pair(COLOR_PAIR_CRITICAL, COLOR_RED,   COLOR_BLACK);  /* crit red + blink  */
    init_pair(COLOR_PAIR_INFO,     COLOR_WHITE, COLOR_BLACK);  /* info silver       */
    init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK, COLOR_RED);    /* tab selected      */
    init_pair(COLOR_PAIR_BORDER,   COLOR_RED,   COLOR_BLACK);  /* dim red borders   */
    init_pair(COLOR_PAIR_OK,       COLOR_BLACK, COLOR_RED);    /* OK badge          */
    init_pair(COLOR_PAIR_DIM,      COLOR_WHITE, COLOR_BLACK);  /* dim labels        */
}
