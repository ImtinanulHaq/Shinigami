/**
 * @file    main_loop.c
 * @brief   Main event loop implementation.
 */
#include "main_loop.h"
#include "main_state.h"
#include "main_signals.h"
#include "terminal/term_engine.h"
#include "terminal/term_layout.h"
#include "terminal/term_input.h"
#include "terminal/term_cmd.h"
#include "terminal/term_colors.h"
#include "panels/panel_dashboard.h"
#include "panels/panel_services.h"
#include "panels/panel_proxies.h"
#include "panels/panel_security.h"
#include "panels/panel_hal.h"
#include "panels/panel_logs.h"
#include "panels/panel_monitor.h"
#include "panels/panel_help.h"
#include "connectors/conn_sm.h"
#include "connectors/conn_monitor.h"
#include "main_config.h"
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/**
 * Run main event loop.
 */
int main_loop_run(void)
{
    while (g_state.running) {
        if (main_loop_iterate() != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * One iteration of main loop.
 */
int main_loop_iterate(void)
{
    /* Check for resize */
    if (g_state.resize_needed) {
        if (term_engine_check_size() != 0) {
            return -1;  /* Terminal too small */
        }

        if (term_layout_on_resize(&g_state.layout) != 0) {
            return -1;  /* Layout rebuild failed */
        }

        g_state.resize_needed = 0;
    }

    /* Try to connect to services if not connected */
    if (!conn_sm_is_connected()) {
        conn_sm_connect();
    }

    if (!conn_monitor_is_connected()) {
        conn_monitor_connect();
    }

    /* Poll for keyboard input (non-blocking) */
    int ch = term_input_getch_nonblock();
    if (ch != -1) {
        int tab_out = -1;
        
        /* Handle command palette input */
        if (g_state.cmd_palette_visible) {
            if (ch == 27) {  /* ESC to cancel */
                g_state.cmd_palette_visible = 0;
                g_state.cmd_input_len = 0;
                g_state.cmd_input[0] = '\0';
            } else if (ch == 10 || ch == 13) {  /* Enter to execute */
                term_cmd_execute(g_state.cmd_input);
                g_state.cmd_palette_visible = 0;
                g_state.cmd_input_len = 0;
                g_state.cmd_input[0] = '\0';
            } else if (ch == KEY_BACKSPACE || ch == 127) {  /* Backspace */
                if (g_state.cmd_input_len > 0) {
                    g_state.cmd_input_len--;
                    g_state.cmd_input[g_state.cmd_input_len] = '\0';
                }
            } else if (ch >= 32 && ch < 127) {  /* Printable ASCII */
                if (g_state.cmd_input_len < (int)sizeof(g_state.cmd_input) - 1) {
                    g_state.cmd_input[g_state.cmd_input_len++] = (char)ch;
                    g_state.cmd_input[g_state.cmd_input_len] = '\0';
                }
            }
            return 0;  /* Don't process other input while in command mode */
        }
        
        int input_result = term_input_handle_key(ch, &tab_out);

        if (input_result == -1) {
            /* Exit signal */
            g_state.running = 0;
            return 0;
        }

        if (input_result == 0) {
            /* Handled as tab switch */
            if (tab_out == -2) {
                /* Next tab */
                term_layout_next_tab(&g_state.layout);
                g_state.active_tab = term_layout_get_active_tab(&g_state.layout);
            } else if (tab_out == -3) {
                /* Prev tab */
                term_layout_prev_tab(&g_state.layout);
                g_state.active_tab = term_layout_get_active_tab(&g_state.layout);
            } else if (tab_out >= 0) {
                /* Direct tab number (1-8 -> 0-7) */
                term_layout_switch_tab(&g_state.layout, tab_out);
                g_state.active_tab = term_layout_get_active_tab(&g_state.layout);
            }
        }

        /* Handle command palette (':') */
        if (ch == ':') {
            g_state.cmd_palette_visible = 1;
            g_state.cmd_input_len = 0;
            g_state.cmd_input[0] = '\0';
        }
        /* TODO: Pass remaining input to active panel */
    }

    /* Try to receive monitor snapshot (non-blocking) */
    conn_monitor_snapshot_t snap;
    if (conn_monitor_recv_snapshot(&snap) > 0) {
        /* TODO: Update panel data from snapshot */
    }

    /* Render all panels */
    wclear(g_state.layout.main_win);

    switch (g_state.active_tab) {
        case MAIN_PANEL_DASHBOARD:
            panel_dashboard_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_SERVICES:
            panel_services_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_PROXIES:
            panel_proxies_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_SECURITY:
            panel_security_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_HAL:
            panel_hal_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_LOGS:
            panel_logs_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_MONITOR:
            panel_monitor_render(g_state.layout.main_win);
            break;
        case MAIN_PANEL_HELP:
            panel_help_render(g_state.layout.main_win);
            break;
    }

    /* Update layout (tabs, topbar, bottombar) */
    term_layout_refresh(&g_state.layout);

    /* Display command input overlay if active */
    if (g_state.cmd_palette_visible) {
        term_layout_show_command_input(g_state.layout.main_win, 
                                      g_state.cmd_input, 
                                      g_state.cmd_input_len);
    }

    /* Rate limit: refresh at MAIN_REFRESH_MS */
    usleep(MAIN_REFRESH_MS * 1000);

    return 0;
}

/**
 * Stop the main loop.
 */
void main_loop_stop(void)
{
    g_state.running = 0;
}
