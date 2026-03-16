/**
 * @file    term_input.c
 * @brief   Keyboard input handling implementation.
 */
#include "term_input.h"
#include "../main_config.h"

/**
 * Handle a single keyboard input character.
 */
int term_input_handle_key(int ch, int *active_tab_out)
{
    if (ch == -1) {
        return 1;           /* No key available, pass through */
    }

    /* Tab shortcuts (1-8) */
    if (term_input_is_tab_shortcut(ch)) {
        if (active_tab_out) {
            *active_tab_out = ch - '1';  /* Convert '1' to 0, '8' to 7 */
        }
        return 0;           /* Handled locally */
    }

    /* Next/Prev tab navigation */
    if (term_input_is_next_tab(ch)) {
        if (active_tab_out) {
            *active_tab_out = -2;  /* Signal "next" */
        }
        return 0;           /* Handled locally */
    }

    if (term_input_is_prev_tab(ch)) {
        if (active_tab_out) {
            *active_tab_out = -3;  /* Signal "prev" */
        }
        return 0;           /* Handled locally */
    }

    /* Quit signals */
    if (term_input_is_quit(ch)) {
        return -1;          /* Exit */
    }

    /* Command prefix */
    if (term_input_is_cmd_prefix(ch)) {
        return 0;           /* Signal to open command palette (handled by main) */
    }

    return 1;               /* Pass to panel handler */
}

/**
 * Check if input is a tab shortcut (1-8).
 */
int term_input_is_tab_shortcut(int ch)
{
    return (ch >= '1' && ch <= '8');
}

/**
 * Check if input is a command prefix.
 */
int term_input_is_cmd_prefix(int ch)
{
    return (ch == ':');
}

/**
 * Check if input is a quit signal.
 */
int term_input_is_quit(int ch)
{
    return (ch == 27 || ch == 3);  /* ESC = 27, Ctrl-C = 3 */
}

/**
 * Check if input is next tab.
 */
int term_input_is_next_tab(int ch)
{
    return (ch == KEY_RIGHT || ch == KEY_NPAGE);  /* Right arrow or Page Down */
}

/**
 * Check if input is previous tab.
 */
int term_input_is_prev_tab(int ch)
{
    return (ch == KEY_LEFT || ch == KEY_PPAGE);   /* Left arrow or Page Up */
}

/**
 * Read a non-blocking keystroke.
 */
int term_input_getch_nonblock(void)
{
    return getch();  /* Already set to nodelay(TRUE) in term_engine_init */
}
