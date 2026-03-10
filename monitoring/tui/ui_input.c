/**
 * @file    ui_input.c
 * @brief   Keyboard input implementation.
 */
#include "ui_input.h"
#include <ncurses.h>

int ui_input_poll(void)
{
    int ch = getch();
    if (ch == ERR) return UI_INPUT_NONE;

    switch (ch) {
    case 'q':
    case 'Q':
    case 27:  /* ESC */
        return UI_INPUT_QUIT;

    case '\t':
    case KEY_RIGHT:
        return UI_INPUT_NEXT_TAB;

    case KEY_LEFT:
        return UI_INPUT_PREV_TAB;

    case KEY_UP:
        return UI_INPUT_UP;

    case KEY_DOWN:
        return UI_INPUT_DOWN;

    case KEY_PPAGE:
        return UI_INPUT_PGUP;

    case KEY_NPAGE:
        return UI_INPUT_PGDN;

    case 'r':
    case 'R':
        return UI_INPUT_REFRESH;

    default:
        return UI_INPUT_NONE;
    }
}
