/**
 * @file    term_cmd.c
 * @brief   Command palette parser and executor.
 */
#include "term_cmd.h"
#include "../main_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* Forward declarations for command handlers */
static int cmd_help(const char *args);
static int cmd_start_service(const char *args);
static int cmd_stop_service(const char *args);
static int cmd_restart_service(const char *args);
static int cmd_service_status(const char *args);
static int cmd_start_camera(const char *args);
static int cmd_stop_camera(const char *args);
static int cmd_list_services(const char *args);
static int cmd_list_proxies(const char *args);
static int cmd_export_logs(const char *args);
static int cmd_clear_logs(const char *args);
static int cmd_security_audit(const char *args);
static int cmd_hal_reset(const char *args);
static int cmd_monitor_snapshot(const char *args);
static int cmd_quit(const char *args);
static int cmd_reconnect_sm(const char *args);
static int cmd_benchmark(const char *args);

/**
 * Command registry: all 30+ commands.
 */
static const term_cmd_entry_t g_commands[] = {
    {"help",                "h",  "Show help for command",              cmd_help},
    {"start-service",       "ss", "Start a service (args: service_id)", cmd_start_service},
    {"stop-service",        "sk", "Stop a service",                     cmd_stop_service},
    {"restart-service",     "sr", "Restart a service",                  cmd_restart_service},
    {"service-status",      "st", "Get service status",                 cmd_service_status},
    {"start-camera",        "sc", "Start camera (args: device_id)",     cmd_start_camera},
    {"stop-camera",         "sc", "Stop camera",                        cmd_stop_camera},
    {"list-services",       "ls", "List all services",                  cmd_list_services},
    {"list-proxies",        "lp", "List all proxies",                   cmd_list_proxies},
    {"export-logs",         "el", "Export logs to file",                cmd_export_logs},
    {"clear-logs",          "cl", "Clear log buffers",                  cmd_clear_logs},
    {"security-audit",      "sa", "Show security audit log",            cmd_security_audit},
    {"hal-reset",           "hr", "Reset HAL devices",                  cmd_hal_reset},
    {"monitor-snapshot",    "ms", "Capture monitor snapshot",           cmd_monitor_snapshot},
    {"quit",                "q",  "Exit terminal",                      cmd_quit},
    {"reconnect-sm",        "rsm","Reconnect to servicemanager",        cmd_reconnect_sm},
    {"benchmark",           "bm", "Run performance benchmark",          cmd_benchmark},
};

#define NUM_COMMANDS (sizeof(g_commands) / sizeof(g_commands[0]))

/* Command state */
static int g_initialized = 0;

/**
 * Initialize command system.
 */
int term_cmd_init(void)
{
    if (g_initialized) return 0;
    g_initialized = 1;
    return 0;
}

/**
 * Shutdown command system.
 */
void term_cmd_shutdown(void)
{
    g_initialized = 0;
}

/**
 * Parse and execute command string.
 */
int term_cmd_execute(const char *cmd_str)
{
    if (!cmd_str || !g_initialized) return -1;

    /* Trim leading whitespace */
    while (*cmd_str && isspace(*cmd_str)) cmd_str++;

    /* Tokenize: split "start-service 0" into "start-service" and "0" */
    char cmd_name[128] = {0};
    const char *args = "";
    int i = 0;

    while (cmd_str[i] && !isspace(cmd_str[i]) && i < 127) {
        cmd_name[i] = cmd_str[i];
        i++;
    }
    cmd_name[i] = '\0';

    while (cmd_str[i] && isspace(cmd_str[i])) i++;
    args = &cmd_str[i];

    /* Find and execute command */
    const term_cmd_entry_t *entry = term_cmd_find(cmd_name);
    if (!entry || !entry->handler) {
        return -1;  /* Unknown command */
    }

    return entry->handler(args);
}

/**
 * Get all commands.
 */
const term_cmd_entry_t *term_cmd_get_all(int *count_out)
{
    if (count_out) *count_out = NUM_COMMANDS;
    return g_commands;
}

/**
 * Find command by name or shorthand.
 */
const term_cmd_entry_t *term_cmd_find(const char *name)
{
    if (!name) return NULL;

    for (int i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(g_commands[i].name, name) == 0 ||
            strcmp(g_commands[i].shorthand, name) == 0) {
            return &g_commands[i];
        }
    }
    return NULL;
}

/**
 * Tab completion.
 */
const term_cmd_entry_t **term_cmd_autocomplete(const char *prefix, int *count_out)
{
    static const term_cmd_entry_t *matches[NUM_COMMANDS];
    int match_count = 0;

    if (!prefix) {
        if (count_out) *count_out = 0;
        return NULL;
    }

    int prefix_len = strlen(prefix);
    for (int i = 0; i < NUM_COMMANDS; i++) {
        if (strncmp(g_commands[i].name, prefix, prefix_len) == 0 ||
            strncmp(g_commands[i].shorthand, prefix, prefix_len) == 0) {
            matches[match_count++] = &g_commands[i];
        }
    }

    if (count_out) *count_out = match_count;
    return (match_count > 0) ? matches : NULL;
}

/**
 * Render command palette.
 */
void term_cmd_render_palette(WINDOW *w, const char *input_buf, int cursor_pos)
{
    if (!w || !input_buf) return;

    wclear(w);
    mvwprintw(w, 0, 0, ": %s", input_buf);
    /* TODO: Show suggestions below as user types */
}

/**
 * Get help text for a command.
 */
const char *term_cmd_get_help(const char *cmd_name)
{
    const term_cmd_entry_t *entry = term_cmd_find(cmd_name);
    return (entry) ? entry->description : "Unknown command";
}

/* ──────────────────────────────────────────────────────────────────────── */
/* COMMAND HANDLERS (STUB IMPLEMENTATIONS) */
/* ──────────────────────────────────────────────────────────────────────── */

static int cmd_help(const char *args)
{
    /* TODO: Display help for all commands or specific command */
    return 0;
}

static int cmd_start_service(const char *args)
{
    /* TODO: Send SM_CMD_START to servicemanager socket */
    return 0;
}

static int cmd_stop_service(const char *args)
{
    /* TODO: Send SM_CMD_STOP to servicemanager socket */
    return 0;
}

static int cmd_restart_service(const char *args)
{
    /* TODO: Send SM_CMD_RESTART to servicemanager socket */
    return 0;
}

static int cmd_service_status(const char *args)
{
    /* TODO: Query servicemanager status */
    return 0;
}

static int cmd_start_camera(const char *args)
{
    /* TODO: Call camera_hal_open() */
    return 0;
}

static int cmd_stop_camera(const char *args)
{
    /* TODO: Call camera_hal_close() */
    return 0;
}

static int cmd_list_services(const char *args)
{
    /* TODO: List all services from snapshot */
    return 0;
}

static int cmd_list_proxies(const char *args)
{
    /* TODO: List all proxies from snapshot */
    return 0;
}

static int cmd_export_logs(const char *args)
{
    /* TODO: Export logs to file (use MAIN_EXPORT_LOGS_DIR) */
    return 0;
}

static int cmd_clear_logs(const char *args)
{
    /* TODO: Clear ring buffers for all log files */
    return 0;
}

static int cmd_security_audit(const char *args)
{
    /* TODO: Show security audit trail */
    return 0;
}

static int cmd_hal_reset(const char *args)
{
    /* TODO: Reset all HAL devices */
    return 0;
}

static int cmd_monitor_snapshot(const char *args)
{
    /* TODO: Capture and display monitor snapshot */
    return 0;
}

static int cmd_quit(const char *args)
{
    /* TODO: Set quit flag */
    return 0;
}

static int cmd_reconnect_sm(const char *args)
{
    /* TODO: Force reconnection to servicemanager */
    return 0;
}

static int cmd_benchmark(const char *args)
{
    /* TODO: Run performance benchmark, show results */
    return 0;
}
