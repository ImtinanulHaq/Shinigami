/**
 * @file    term_engine.h
 * @brief   ncurses terminal engine for window management, resizing, and rendering.
 */
#ifndef TERM_ENGINE_H
#define TERM_ENGINE_H

#include <ncurses.h>
#include <panel.h>

/**
 * Initialize ncurses and check terminal size.
 */
int term_engine_init(void);

/**
 * Shutdown ncurses cleanly.
 */
void term_engine_shutdown(void);

/**
 * Check if terminal size meets minimum requirements (from main_config.h).
 */
int term_engine_check_size(void);

/**
 * Get current terminal dimensions.
 */
void term_engine_get_size(int *cols, int *rows);

/**
 * Refresh all panels and update display.
 */
void term_engine_refresh(void);

/**
 * Clear the entire screen with default background.
 */
void term_engine_clear(void);

/**
 * Handle terminal resize (SIGWINCH).
 */
void term_engine_on_resize(void);

/**
 * Draw a box with given color pair using box-drawing characters.
 */
void term_engine_draw_box(WINDOW *w, int color_pair);

/**
 * Draw a horizontal line at row y with given color_pair.
 */
void term_engine_draw_hline(WINDOW *w, int y, int color_pair);

/**
 * Create a new window with proper initialization.
 */
WINDOW *term_engine_newwin(int h, int w, int y, int x);

/**
 * Delete a window and associated panel.
 */
void term_engine_delwin(WINDOW *w, PANEL *p);

/**
 * Clear all text attributes and renditions.
 */
void term_clear_attrib(void);

#endif
