/**
 * @file    tui_main.c
 * @brief   Entry point for mw_tui — Middleware Monitor interactive client.
 *
 * Connects to monitord via Unix socket, receives pushed snapshots, and
 * renders an interactive ncurses TUI with multiple panels.
 *
 * Main loop strategy:
 *   - Socket is set non-blocking (O_NONBLOCK).
 *   - select() with a 30 ms timeout multiplexes the socket with ncurses
 *     keyboard input, so key presses are always responsive.
 *   - Rendering only happens when a new snapshot arrives or the user
 *     forces a refresh ('r').
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <ncurses.h>

#include "../protocol/monitor_ipc_protocol.h"
#include "../protocol/monitor_wire_format.h"
#include "ui_engine.h"
#include "ui_layout.h"
#include "ui_input.h"

static volatile sig_atomic_t g_stop_flag = 0;

static void signal_handler(int sig) { (void)sig; g_stop_flag = 1; }

static int connect_to_monitord(const char *socket_path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    /* Set non-blocking so recv never stalls the UI */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");   /* Enable UTF-8 for box-drawing characters */

    const char *socket_path = "/tmp/middleware_monitor.sock";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc)
            socket_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--socket /path/to/socket]\n", argv[0]);
            return 0;
        }
    }

    int sock_fd = connect_to_monitord(socket_path);
    if (sock_fd < 0) {
        fprintf(stderr, "Failed to connect to monitord at %s\n", socket_path);
        return 1;
    }

    if (ui_engine_init() != 0) {
        fprintf(stderr, "Failed to initialize UI\n");
        close(sock_fd);
        return 1;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    mon_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    int current_tab  = 0;
    int need_redraw  = 1;   /* Render immediately with empty snapshot */
    int got_snapshot = 0;

    while (!g_stop_flag) {
        /* ── Keyboard input (always non-blocking via timeout(0)) ── */
        int inp = ui_input_poll();
        switch (inp) {
        case UI_INPUT_QUIT:
            g_stop_flag = 1;
            continue;

        case UI_INPUT_NEXT_TAB:
            current_tab = (current_tab + 1) % 10;
            ui_layout_set_tab(current_tab);
            need_redraw = 1;
            break;

        case UI_INPUT_PREV_TAB:
            current_tab = (current_tab - 1 + 10) % 10;
            ui_layout_set_tab(current_tab);
            need_redraw = 1;
            break;

        case UI_INPUT_REFRESH:
            need_redraw = 1;
            break;

        default:
            /* Number key direct tab jump */
            if (inp >= UI_INPUT_TAB_N && inp < UI_INPUT_TAB_N + 10) {
                current_tab = inp - UI_INPUT_TAB_N;
                ui_layout_set_tab(current_tab);
                need_redraw = 1;
            } else if (inp == UI_INPUT_UP   || inp == UI_INPUT_DOWN ||
                       inp == UI_INPUT_PGUP || inp == UI_INPUT_PGDN) {
                ui_layout_scroll(inp);
                need_redraw = 1;
            }
            break;
        }

        /* ── Non-blocking network check (30 ms window) ─────────── */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock_fd, &rfds);
        struct timeval tv = {0, 30000};   /* 30 ms */

        int nready = select(sock_fd + 1, &rfds, NULL, NULL, &tv);
        if (nready < 0 && errno == EINTR) continue;

        if (nready > 0 && FD_ISSET(sock_fd, &rfds)) {
            mon_msg_header_t hdr;
            size_t           read_len;
            int rc = mon_recv_msg(sock_fd, &hdr, &snapshot,
                                  sizeof(snapshot), &read_len);
            if (rc == MON_WIRE_OK && hdr.type == MON_MSG_SNAPSHOT_RESP) {
                got_snapshot = 1;
                need_redraw  = 1;
            } else if (rc == MON_WIRE_ERR_EOF) {
                /* monitord disconnected */
                break;
            }
            /* MON_WIRE_ERR_AGAIN = nothing ready yet, ignore */
        }

        /* ── Render ─────────────────────────────────────────────── */
        if (need_redraw && (got_snapshot || inp != UI_INPUT_NONE)) {
            ui_layout_render(&snapshot);
            refresh();
            need_redraw = 0;
        }
    }

    ui_engine_shutdown();
    close(sock_fd);
    return 0;
}

