#define _POSIX_C_SOURCE 200809L

#include "sm_socket.h"
#include "sm_logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <errno.h>

// ── STATE ──────────────────────────────────────────────────────────────────────

static int server_fd = -1;

// ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────────

int sm_socket_setup(void)
{
    // Remove old socket if exists
    unlink(SM_SOCKET_PATH);

    // Create socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        sm_log(SM_LOG_ERROR, "socket() failed: %m");
        return -1;
    }

    // Make non-blocking
    int flags = fcntl(server_fd, F_GETFL);
    if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        sm_log(SM_LOG_ERROR, "fcntl O_NONBLOCK failed: %m");
        close(server_fd);
        return -1;
    }

    // Bind socket
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SM_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sm_log(SM_LOG_ERROR, "bind() failed: %m");
        close(server_fd);
        return -1;
    }

    // Set socket permissions (owner read/write, group read/write, others none)
    if (chmod(SM_SOCKET_PATH, SM_SOCKET_MODE) < 0) {
        sm_log(SM_LOG_ERROR, "chmod() failed: %m");
        close(server_fd);
        return -1;
    }

    // Listen
    if (listen(server_fd, SM_BACKLOG) < 0) {
        sm_log(SM_LOG_ERROR, "listen() failed: %m");
        close(server_fd);
        return -1;
    }

    sm_log(SM_LOG_INFO, "socket listening on %s (mode %04o)",
           SM_SOCKET_PATH, SM_SOCKET_MODE);
    return 0;
}

void sm_socket_cleanup(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(SM_SOCKET_PATH);
    sm_log(SM_LOG_INFO, "socket closed");
}

int sm_socket_get_fd(void)
{
    return server_fd;
}

int sm_socket_validate_perms(void)
{
    struct stat st;
    if (stat(SM_SOCKET_PATH, &st) < 0) {
        sm_log(SM_LOG_ERROR, "stat socket failed: %m");
        return -1;
    }

    // Check mode (should be rw-rw---)
    if ((st.st_mode & 0777) != SM_SOCKET_MODE) {
        sm_log(SM_LOG_WARN, "socket permissions incorrect: %04o (expected %04o)",
               st.st_mode & 0777, SM_SOCKET_MODE);
        chmod(SM_SOCKET_PATH, SM_SOCKET_MODE);
    }

    return 0;
}
