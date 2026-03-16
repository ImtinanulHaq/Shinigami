/**
 * @file    term_input.h
 * @brief   Keyboard input handling and dispatcher.
 */
#ifndef TERM_INPUT_H
#define TERM_INPUT_H

#include <ncurses.h>

/**
 * Handle a single keyboard input character.
 * Returns 0 if handled locally (tab switch), 1 if should pass to panel,
 * -1 if should exit (ESC/Ctrl-C).
 */
int term_input_handle_key(int ch, int *active_tab_out);

/**
 * Check if input is a tab shortcut (1-8).
 */
int term_input_is_tab_shortcut(int ch);

/**
 * Check if input is a command prefix (:).
 */
int term_input_is_cmd_prefix(int ch);

/**
 * Check if input is a quit signal (ESC or Ctrl-C).
 */
int term_input_is_quit(int ch);

/**
 * Check if input is next tab (Tab key).
 */
int term_input_is_next_tab(int ch);

/**
 * Check if input is previous tab (Shift+Tab).
 */
int term_input_is_prev_tab(int ch);

/**
 * Read a non-blocking keystroke.
 * Returns the key code, or -1 if no key available.
 */
int term_input_getch_nonblock(void);

#endif
