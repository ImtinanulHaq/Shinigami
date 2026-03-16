/**
 * Main event loop - Single NCURSES screen with BANKAI display and command input
 */
#include "main_loop.h"
#include "connectors/conn_sm.h"
#include "connectors/conn_monitor.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int g_running = 1;
static WINDOW *g_stdscr = NULL;
static int g_max_y = 0, g_max_x = 0;

/* Display the BANKAI screen with given output */
static void display_screen(const char *command_output)
{
    clear();
    getmaxyx(stdscr, g_max_y, g_max_x);

    /* Display BANKAI in RED at top center */
    attron(COLOR_PAIR(1) | A_BOLD);
    int bankai_y = g_max_y / 4;
    mvprintw(bankai_y, (g_max_x - 6) / 2, "BANKAI");
    attroff(COLOR_PAIR(1) | A_BOLD);

    /* Display command output (middle and bottom) */
    if (command_output && strlen(command_output) > 0) {
        attron(COLOR_PAIR(2));
        mvprintw(g_max_y / 2, 2, "%s", command_output);
        attroff(COLOR_PAIR(2));
    }

    /* Display input prompt at bottom */
    attron(COLOR_PAIR(1));
    mvprintw(g_max_y - 2, 2, ">>> ");
    attroff(COLOR_PAIR(1));

    refresh();
}

/* Read command from user input */
static int read_command(char *cmd_buffer, int bufsize)
{
    int pos = 0;
    int ch;

    while (pos < bufsize - 1) {
        ch = getch();
        if (ch == 10 || ch == 13) {  /* Enter */
            cmd_buffer[pos] = '\0';
            return 0;
        }
        if (ch == 27) {  /* ESC */
            return -1;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {  /* Backspace */
            if (pos > 0) {
                pos--;
                mvprintw(g_max_y - 2, 6 + pos, " ");
                move(g_max_y - 2, 6 + pos);
                refresh();
            }
            continue;
        }
        if (ch >= 32 && ch < 127) {  /* Printable */
            cmd_buffer[pos] = (char)ch;
            mvprintw(g_max_y - 2, 6 + pos, "%c", ch);
            refresh();
            pos++;
        }
    }

    cmd_buffer[pos] = '\0';
    return 0;
}

/* Initialize ncurses and connections */
int main_loop_init(void)
{
    g_stdscr = initscr();
    if (!g_stdscr) {
        fprintf(stderr, "[ERROR] Failed to initialize ncurses\n");
        return -1;
    }

    /* Start color mode */
    if (!has_colors()) {
        endwin();
        fprintf(stderr, "[ERROR] Terminal does not support colors\n");
        return -1;
    }

    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);     /* RED on BLACK for BANKAI */
    init_pair(2, COLOR_WHITE, COLOR_BLACK);   /* WHITE on BLACK for output */

    /* Configure input */
    cbreak();
    noecho();
    nodelay(stdscr, FALSE);
    keypad(stdscr, TRUE);

    /* Connect to sockets */
    conn_sm_connect();
    conn_monitor_connect();

    g_running = 1;
    return 0;
}

/* Main event loop */
int main_loop_run(void)
{
    char cmd_buffer[512];
    char output_buffer[4096] = {0};

    while (g_running) {
        /* Display current screen */
        display_screen(output_buffer);

        /* Read command */
        if (read_command(cmd_buffer, sizeof(cmd_buffer)) != 0) {
            if (strcmp(cmd_buffer, "exit") == 0 ||
                strcmp(cmd_buffer, "quit") == 0 ||
                strcmp(cmd_buffer, "q") == 0) {
                break;
            }
        }

        /* Execute command (stub - will implement handlers) */
        if (strlen(cmd_buffer) > 0) {
            snprintf(output_buffer, sizeof(output_buffer),
                    "Command: %s\n(Handlers not yet implemented)", cmd_buffer);
        } else {
            output_buffer[0] = '\0';
        }

        /* Small delay */
        usleep(100000);
    }

    return 0;
}

/* Shutdown ncurses and connections */
void main_loop_shutdown(void)
{
    conn_sm_disconnect();
    conn_monitor_disconnect();

    if (g_stdscr) {
        endwin();
    }

    g_running = 0;
}
