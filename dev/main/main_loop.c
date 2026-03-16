/**
 * Main event loop - Single screen with BANKAI and command input
 */
#include "main_loop.h"
#include "cmd_handlers.h"
#include "connectors/conn_sm.h"
#include "connectors/conn_monitor.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

static int g_running = 1;
static int g_max_y = 0, g_max_x = 0;
static int g_sm_connected = 0;
static int g_monitor_connected = 0;

static void display_screen(const char *output)
{
    clear();
    getmaxyx(stdscr, g_max_y, g_max_x);

    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(g_max_y / 4, (g_max_x - 6) / 2, "BANKAI");
    attroff(COLOR_PAIR(1) | A_BOLD);

    if (output && strlen(output) > 0) {
        attron(COLOR_PAIR(2));
        const char *ptr = output;
        int y = g_max_y / 2;
        
        while (*ptr && y < g_max_y - 3) {
            const char *newline = strchr(ptr, '\n');
            int len = newline ? (int)(newline - ptr) : (int)strlen(ptr);
            mvprintw(y++, 2, "%.*s", len, ptr);
            ptr = newline ? (newline + 1) : (ptr + len);
        }
        attroff(COLOR_PAIR(2));
    }

    attron(COLOR_PAIR(1));
    mvprintw(g_max_y - 2, 2, ">>> ");
    attroff(COLOR_PAIR(1));
    refresh();
}

static int read_command(char *cmd_buf, int bufsize)
{
    int pos = 0;
    memset(cmd_buf, 0, bufsize);

    while (pos < bufsize - 1) {
        int ch = getch();
        if (ch == 10 || ch == 13) {
            return 0;
        }
        if (ch == 27) {
            return -1;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (pos > 0) {
                pos--;
                mvprintw(g_max_y - 2, 6 + pos, " ");
                move(g_max_y - 2, 6 + pos);
                refresh();
            }
            continue;
        }
        if (ch >= 32 && ch < 127) {
            cmd_buf[pos] = (char)ch;
            mvaddch(g_max_y - 2, 6 + pos, (unsigned char)ch);
            refresh();
            pos++;
        }
    }
    return 0;
}

int main_loop_init(void)
{
    initscr();
    if (!has_colors()) {
        endwin();
        fprintf(stderr, "[ERROR] No color support\n");
        return -1;
    }

    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_WHITE, COLOR_BLACK);

    cbreak();
    noecho();
    nodelay(stdscr, FALSE);
    keypad(stdscr, TRUE);

    /* Try to connect to Service Manager (retry 3 times) */
    for (int i = 0; i < 3; i++) {
        if (conn_sm_connect() == 0) {
            g_sm_connected = 1;
            break;
        }
        if (i < 2) {
            usleep(200000); /* 200ms between retries */
        }
    }

    /* Try to connect to Monitor */
    conn_monitor_connect();
    if (conn_monitor_get_cached_snapshot() != NULL) {
        g_monitor_connected = 1;
    }

    g_running = 1;
    return 0;
}

int main_loop_run(void)
{
    char cmd_buf[512];
    char output_buf[8192];
    memset(output_buf, 0, sizeof(output_buf));
    
    /* Show startup message with connection status */
    char status_sm = g_sm_connected ? '+' : '-';
    char status_mon = g_monitor_connected ? '+' : '-';
    
    snprintf(output_buf, sizeof(output_buf),
        "SHINIGAMI TERMINAL v1.0\n"
        "Middleware Control Interface\n\n"
        "[%c] Service Manager\n"
        "[%c] Monitor\n\n"
        "Type 'help' for available commands",
        status_sm, status_mon);
    while (g_running) {
        display_screen(output_buf);

        if (read_command(cmd_buf, sizeof(cmd_buf)) == 0 && strlen(cmd_buf) > 0) {
            memset(output_buf, 0, sizeof(output_buf));
            cmd_handler_execute(cmd_buf, output_buf, sizeof(output_buf));
        }

        usleep(50000);
    }

    return 0;
}

void main_loop_shutdown(void)
{
    conn_sm_disconnect();
    conn_monitor_disconnect();
    endwin();
    g_running = 0;
}
