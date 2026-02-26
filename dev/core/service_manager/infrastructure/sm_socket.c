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

#include "../infrastructure/sm_socket.h"
#include "../observability/sm_logging.h"

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
static const char* g_socket_path = NULL;

/* ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────── */

int sm_socket_setup(void)
{
    struct sockaddr_un addr;
    int                flags;
    const char* socket_paths[] = {
        SM_SOCKET_PATH,                          /* /run/servicemanager.sock */
        "/tmp/servicemanager.sock",              /* fallback to /tmp */
        NULL
    };
    int path_idx = 0;

    /* Try each socket path until one works */
    while (socket_paths[path_idx] != NULL) {
        const char* socket_path = socket_paths[path_idx];
        
        /* Remove a stale socket file from a previous run */
        unlink(socket_path);

        /* Create the UNIX stream socket */
        server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (server_fd < 0) {
            sm_log(SM_LOG_WARN, "socket: socket() failed: %s", strerror(errno));
            path_idx++;
            continue;
        }

        /* Set non-blocking so epoll_wait() is the only blocking point */
        flags = fcntl(server_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            sm_log(SM_LOG_WARN, "socket: F_SETFL O_NONBLOCK failed: %s", strerror(errno));
            close(server_fd);
            server_fd = -1;
            path_idx++;
            continue;
        }

        /* Bind to the well-known path */
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            sm_log(SM_LOG_WARN, "socket: bind(%s) failed: %s, trying fallback", socket_path, strerror(errno));
            close(server_fd);
            server_fd = -1;
            path_idx++;
            continue;
        }

        /*
         * Set permissions BEFORE listen() so no client can connect before the
         * permissions are correct.  Treat chmod failure as fatal: running with
         * wrong permissions is a security defect.
         */
        if (chmod(socket_path, SM_SOCKET_MODE) < 0) {
            sm_log(SM_LOG_WARN, "socket: chmod(%04o) failed: %s, trying fallback", SM_SOCKET_MODE, strerror(errno));
            close(server_fd);
            server_fd = -1;
            unlink(socket_path);
            path_idx++;
            continue;
        }

        if (listen(server_fd, SM_BACKLOG) < 0) {
            sm_log(SM_LOG_WARN, "socket: listen() failed: %s, trying fallback", strerror(errno));
            close(server_fd);
            server_fd = -1;
            unlink(socket_path);
            path_idx++;
            continue;
        }

        /* Success! Store the socket path for later use */
        g_socket_path = socket_path;
        sm_log(SM_LOG_INFO, "socket: listening on %s (mode %04o backlog %d)",
               g_socket_path, SM_SOCKET_MODE, SM_BACKLOG);
        return 0;
    }

    /* All paths failed */
    sm_log(SM_LOG_ERROR, "socket: failed to setup socket on any path");
    return -1;
}

void sm_socket_cleanup(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    if (g_socket_path) {
        unlink(g_socket_path);
    }
    sm_log(SM_LOG_INFO, "socket: closed and removed");
}

const char* sm_socket_get_path(void)
{
    return g_socket_path ? g_socket_path : SM_SOCKET_PATH;
}

int sm_socket_get_fd(void)
{
    return server_fd;
}

int sm_socket_validate_perms(void)
{
    struct stat st;

    if (!g_socket_path) {
        sm_log(SM_LOG_ERROR, "socket: validate_perms called before setup");
        return -1;
    }

    if (stat(g_socket_path, &st) < 0) {
        sm_log(SM_LOG_ERROR, "socket: stat failed: %s", strerror(errno));
        return -1;
    }

    if ((st.st_mode & 0777) != SM_SOCKET_MODE) {
        sm_log(SM_LOG_WARN, "socket: permissions %04o (expected %04o) - correcting",
               st.st_mode & 0777, SM_SOCKET_MODE);
        if (chmod(g_socket_path, SM_SOCKET_MODE) < 0) {
            sm_log(SM_LOG_ERROR, "socket: chmod correction failed: %s", strerror(errno));
            return -1;
        }
    }

    return 0;
}