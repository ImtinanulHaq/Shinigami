/**
 * @file    ui_input.h
 * @brief   Keyboard input handling.
 */
#pragma once

#define UI_INPUT_NONE     0
#define UI_INPUT_QUIT     1
#define UI_INPUT_NEXT_TAB 2
#define UI_INPUT_PREV_TAB 3
#define UI_INPUT_UP       4
#define UI_INPUT_DOWN     5
#define UI_INPUT_PGUP     6
#define UI_INPUT_PGDN     7
#define UI_INPUT_REFRESH  8

/**
 * @brief  Poll for keyboard input (non-blocking).
 * @return UI_INPUT_* constant, or UI_INPUT_NONE if no input.
 */
int ui_input_poll(void);
