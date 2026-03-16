/**
 * @file    term_cmd.h
 * @brief   Command palette, parser, and executor.
 */
#ifndef TERM_CMD_H
#define TERM_CMD_H

#include <ncurses.h>

/**
 * Command handler function prototype.
 * Returns 0 on success, -1 on error.
 */
typedef int (*cmd_handler_t)(const char *args);

/**
 * Command entry in the command registry.
 */
typedef struct {
    const char *name;           /* Command name (e.g., "start-camera") */
    const char *shorthand;      /* Shorthand alias (e.g., "sc") */
    const char *description;    /* Help text */
    cmd_handler_t handler;      /* Function to call */
} term_cmd_entry_t;

/**
 * Initialize command palette system.
 */
int term_cmd_init(void);

/**
 * Shutdown command system.
 */
void term_cmd_shutdown(void);

/**
 * Parse and execute a typed command string.
 * E.g., "start-camera 0" or "help" or "service restart audio".
 */
int term_cmd_execute(const char *cmd_str);

/**
 * Get list of all registered commands.
 */
const term_cmd_entry_t *term_cmd_get_all(int *count_out);

/**
 * Find a command by name or shorthand.
 */
const term_cmd_entry_t *term_cmd_find(const char *name);

/**
 * Tab completion: find commands matching prefix.
 * Returns array of matching commands and count.
 */
const term_cmd_entry_t **term_cmd_autocomplete(const char *prefix, int *count_out);

/**
 * Render the command palette UI in a window.
 */
void term_cmd_render_palette(WINDOW *w, const char *input_buf, int cursor_pos);

/**
 * Get help text for specific command.
 */
const char *term_cmd_get_help(const char *cmd_name);

#endif
