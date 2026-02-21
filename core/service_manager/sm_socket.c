#define _POSIX_C_SOURCE 200809L

/*
 * sm_socket.c - Unix domain server socket setup and cleanup.
 *
 * Fixes applied:
 *   - All error paths after socket() close the fd before returning to prevent
 *     file descriptor leaks.
 *   - chmod failure is treated as a fatal error; a wrongly-permissioned socket
 *     is a security defect and we refuse to run with it.
 */

#include "sm_socket.h"
#include "sm_logging.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* ── STATE ──────────────────────────────────────────────────────────────────── */

static int server_fd = -1;

/* ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────── */

int sm_socket_setup(void)
{
    struct sockaddr_un addr;
    int                flags;

    /* Remove a stale socket file from a previous run */
    unlink(SM_SOCKET_PATH);

    /* Create the UNIX stream socket */
    server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        sm_log(SM_LOG_ERROR, "socket: socket() failed: %m");
        return -1;
    }

    /* Set non-blocking so epoll_wait() is the only blocking point */
    flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        sm_log(SM_LOG_ERROR, "socket: F_SETFL O_NONBLOCK failed: %m");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    /* Bind to the well-known path */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SM_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sm_log(SM_LOG_ERROR, "socket: bind(%s) failed: %m", SM_SOCKET_PATH);
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    /*
     * Set permissions BEFORE listen() so no client can connect before the
     * permissions are correct.  Treat chmod failure as fatal: running with
     * wrong permissions is a security defect.
     */
    if (chmod(SM_SOCKET_PATH, SM_SOCKET_MODE) < 0) {
        sm_log(SM_LOG_ERROR, "socket: chmod(%04o) failed: %m", SM_SOCKET_MODE);
        close(server_fd);
        server_fd = -1;
        unlink(SM_SOCKET_PATH);
        return -1;
    }

    if (listen(server_fd, SM_BACKLOG) < 0) {
        sm_log(SM_LOG_ERROR, "socket: listen() failed: %m");
        close(server_fd);
        server_fd = -1;
        unlink(SM_SOCKET_PATH);
        return -1;
    }

    sm_log(SM_LOG_INFO, "socket: listening on %s (mode %04o backlog %d)",
           SM_SOCKET_PATH, SM_SOCKET_MODE, SM_BACKLOG);
    return 0;
}

void sm_socket_cleanup(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(SM_SOCKET_PATH);
    sm_log(SM_LOG_INFO, "socket: closed and removed");
}

int sm_socket_get_fd(void)
{
    return server_fd;
}

int sm_socket_validate_perms(void)
{
    struct stat st;

    if (stat(SM_SOCKET_PATH, &st) < 0) {
        sm_log(SM_LOG_ERROR, "socket: stat failed: %m");
        return -1;
    }

    if ((st.st_mode & 0777) != SM_SOCKET_MODE) {
        sm_log(SM_LOG_WARN, "socket: permissions %04o (expected %04o) - correcting",
               st.st_mode & 0777, SM_SOCKET_MODE);
        if (chmod(SM_SOCKET_PATH, SM_SOCKET_MODE) < 0) {
            sm_log(SM_LOG_ERROR, "socket: chmod correction failed: %m");
            return -1;
        }
    }

    return 0;
}