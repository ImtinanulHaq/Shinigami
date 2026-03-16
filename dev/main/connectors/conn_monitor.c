/**
 * @file    conn_monitor.c
 * @brief   Monitor socket connector implementation.
 */
#include "conn_monitor.h"
#include "../main_config.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

static int g_monitor_socket = -1;
static char g_last_error[256] = "";
static conn_monitor_snapshot_t g_cached_snapshot = {0};
static time_t g_last_connect_attempt = 0;

/**
 * Connect to monitor broadcast socket.
 */
int conn_monitor_connect(void)
{
    if (g_monitor_socket >= 0) {
        return 0;  /* Already connected */
    }

    /* Rate-limit connection attempts */
    time_t now = time(NULL);
    if (now - g_last_connect_attempt < MAIN_RECONNECT_INTERVAL / 1000) {
        return -1;
    }
    g_last_connect_attempt = now;

    /* Create Unix domain socket */
    g_monitor_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_monitor_socket < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "socket() failed: %s", strerror(errno));
        return -1;
    }

    /* Set non-blocking mode */
    int flags = fcntl(g_monitor_socket, F_GETFL, 0);
    fcntl(g_monitor_socket, F_SETFL, flags | O_NONBLOCK);

    /* Connect to monitor socket */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MAIN_MONITOR_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(g_monitor_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            snprintf(g_last_error, sizeof(g_last_error), "connect() failed: %s", strerror(errno));
            close(g_monitor_socket);
            g_monitor_socket = -1;
            return -1;
        }
    }

    return 0;
}

/**
 * Disconnect from monitor.
 */
void conn_monitor_disconnect(void)
{
    if (g_monitor_socket >= 0) {
        close(g_monitor_socket);
        g_monitor_socket = -1;
    }
}

/**
 * Check if connected.
 */
int conn_monitor_is_connected(void)
{
    return (g_monitor_socket >= 0);
}

/**
 * Try to receive updated snapshot (non-blocking).
 */
int conn_monitor_recv_snapshot(conn_monitor_snapshot_t *snap_out)
{
    if (!snap_out) return -1;

    if (!conn_monitor_is_connected()) {
        if (conn_monitor_connect() != 0) {
            return -1;
        }
    }

    /* Attempt non-blocking read */
    uint8_t buf[1024];
    ssize_t n = recv(g_monitor_socket, buf, sizeof(buf), MSG_DONTWAIT);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* No data available */
        }
        snprintf(g_last_error, sizeof(g_last_error), "recv() failed: %s", strerror(errno));
        conn_monitor_disconnect();
        return -1;
    }

    if (n == 0) {
        /* Connection closed */
        conn_monitor_disconnect();
        return 0;
    }

    /* TODO: Deserialize snapshot from buf[n] */
    /* For now, just update timestamp and cache */
    memcpy(&g_cached_snapshot, snap_out, sizeof(*snap_out));
    g_cached_snapshot.timestamp = time(NULL);

    return 1;  /* New data received */
}

/**
 * Get cached snapshot.
 */
const conn_monitor_snapshot_t *conn_monitor_get_cached_snapshot(void)
{
    return &g_cached_snapshot;
}

/**
 * Reconnect.
 */
int conn_monitor_reconnect(void)
{
    conn_monitor_disconnect();
    return conn_monitor_connect();
}

/**
 * Set error.
 */
void conn_monitor_set_error(const char *error)
{
    if (error) {
        strncpy(g_last_error, error, sizeof(g_last_error) - 1);
    }
}

/**
 * Get error.
 */
const char *conn_monitor_get_error(void)
{
    return g_last_error[0] ? g_last_error : "No error";
}
