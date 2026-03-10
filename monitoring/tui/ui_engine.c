/**
 * @file    ui_engine.c
 * @brief   Core ncurses initialization.
 */
#include "ui_engine.h"
#include "ui_colors.h"

int ui_engine_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  /* Hide cursor */
    timeout(0);   /* Non-blocking getch() */

    if (has_colors()) {
        start_color();
        ui_colors_init();
    }

    return 0;
}

void ui_engine_shutdown(void)
{
    endwin();
}

void ui_engine_get_size(int *rows, int *cols)
{
    getmaxyx(stdscr, *rows, *cols);
}
