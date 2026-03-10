/**
 * @file    tui_main.c
 * @brief   Entry point for middleware_monitor_tui client.
 *
 * Usage:
 *   middleware_monitor_tui [--socket /path/to/socket]
 *
 * Connects to monitord via Unix socket, receives snapshots, and renders
 * an interactive ncurses TUI with multiple panels.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ncurses.h>

#include "../protocol/monitor_ipc_protocol.h"
#include "../protocol/monitor_wire_format.h"
#include "ui_engine.h"
#include "ui_layout.h"
#include "ui_input.h"

static volatile sig_atomic_t g_stop_flag = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_stop_flag = 1;
}

static int connect_to_monitord(const char *socket_path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char **argv)
{
    const char *socket_path = "/tmp/middleware_monitor.sock";

    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--socket /path/to/socket]\n", argv[0]);
            return 0;
        }
    }

    /* Connect to monitord */
    int sock_fd = connect_to_monitord(socket_path);
    if (sock_fd < 0) {
        fprintf(stderr, "Failed to connect to monitord at %s\n", socket_path);
        return 1;
    }

    /* Initialize ncurses UI */
    if (ui_engine_init() != 0) {
        fprintf(stderr, "Failed to initialize UI\n");
        close(sock_fd);
        return 1;
    }

    /* Initialize signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Main loop */
    mon_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    int current_tab = 0;

    while (!g_stop_flag) {
        /* Check for input (non-blocking) */
        int input_result = ui_input_poll();
        
        switch (input_result) {
        case UI_INPUT_QUIT:
            g_stop_flag = 1;
            break;
            
        case UI_INPUT_NEXT_TAB:
            current_tab = (current_tab + 1) % 9;  /* 9 tabs total */
            ui_layout_set_tab(current_tab);
            break;
            
        case UI_INPUT_PREV_TAB:
            current_tab = (current_tab - 1 + 9) % 9;
            ui_layout_set_tab(current_tab);
            break;
            
        case UI_INPUT_REFRESH:
            /* Force re-render on next snapshot */
            break;
            
        default:
            break;
        }

        /* Try to read snapshot from monitord */
        mon_msg_header_t hdr;
        size_t read_len;
        if (mon_recv_msg(sock_fd, &hdr, &snapshot, sizeof(snapshot), &read_len) == MON_WIRE_OK &&
            hdr.type == MON_MSG_SNAPSHOT_RESP)
        {
            /* Render the UI */
            ui_layout_render(&snapshot);
        }

        /* Refresh screen */
        refresh();
        napms(50);  /* 50ms delay */
    }

    /* Cleanup */
    ui_engine_shutdown();
    close(sock_fd);

    return 0;
}
