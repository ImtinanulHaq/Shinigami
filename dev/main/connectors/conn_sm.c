/**
 * @file    conn_sm.c
 * @brief   Servicemanager socket connector implementation.
 */
#include "conn_sm.h"
#include "../main_config.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

static int g_sm_socket = -1;
static char g_last_error[256] = "";
static time_t g_last_connect_attempt = 0;

/**
 * Connect to servicemanager socket.
 */
int conn_sm_connect(void)
{
    if (g_sm_socket >= 0) {
        return 0;  /* Already connected */
    }

    /* Rate-limit connection attempts (don't hammer socket) */
    time_t now = time(NULL);
    if (now - g_last_connect_attempt < MAIN_RECONNECT_INTERVAL / 1000) {
        return -1;
    }
    g_last_connect_attempt = now;

    /* Create Unix domain socket */
    g_sm_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_sm_socket < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "socket() failed: %s", strerror(errno));
        return -1;
    }

    /* Connect to servicemanager */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MAIN_SM_SOCKET_PRIMARY, sizeof(addr.sun_path) - 1);

    if (connect(g_sm_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        /* Try fallback socket */
        if (errno != ECONNREFUSED && errno != ENOENT) {
            snprintf(g_last_error, sizeof(g_last_error), "connect() failed: %s", strerror(errno));
        }

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, MAIN_SM_SOCKET_FALLBACK, sizeof(addr.sun_path) - 1);

        if (connect(g_sm_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(g_sm_socket);
            g_sm_socket = -1;
            snprintf(g_last_error, sizeof(g_last_error), "connect() to both sockets failed");
            return -1;
        }
    }

    /* Set receive timeout */
    struct timeval tv;
    tv.tv_sec = MAIN_SM_TIMEOUT_MS / 1000;
    tv.tv_usec = (MAIN_SM_TIMEOUT_MS % 1000) * 1000;
    setsockopt(g_sm_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return 0;
}

/**
 * Disconnect from servicemanager.
 */
void conn_sm_disconnect(void)
{
    if (g_sm_socket >= 0) {
        close(g_sm_socket);
        g_sm_socket = -1;
    }
}

/**
 * Check if connected.
 */
int conn_sm_is_connected(void)
{
    return (g_sm_socket >= 0);
}

/**
 * Send command to servicemanager.
 */
int conn_sm_send_command(const char *service_name, const char *command)
{
    if (!service_name || !command) return -1;

    if (!conn_sm_is_connected()) {
        if (conn_sm_connect() != 0) {
            return -1;
        }
    }

    /* Format command string: "service_name command" */
    char buf[256];
    snprintf(buf, sizeof(buf), "%s %s\n", service_name, command);

    if (send(g_sm_socket, buf, strlen(buf), 0) < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "send() failed: %s", strerror(errno));
        conn_sm_disconnect();
        return -1;
    }

    /* Read response (blocking with timeout) */
    char response[512];
    ssize_t n = recv(g_sm_socket, response, sizeof(response) - 1, 0);
    if (n < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "recv() failed: %s", strerror(errno));
        conn_sm_disconnect();
        return -1;
    }

    response[n] = '\0';

    /* Parse response: success if contains "OK" */
    if (strstr(response, "OK") == NULL) {
        snprintf(g_last_error, sizeof(g_last_error), "Command failed: %s", response);
        return -1;
    }

    return 0;
}

/**
 * Get all services.
 */
int conn_sm_get_services(conn_sm_service_t **services_out, int *count_out)
{
    if (!services_out || !count_out) return -1;

    if (!conn_sm_is_connected()) {
        if (conn_sm_connect() != 0) {
            return -1;
        }
    }

    /* Send "list" command */
    const char *cmd = "list\n";
    if (send(g_sm_socket, cmd, strlen(cmd), 0) < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "send() failed: %s", strerror(errno));
        conn_sm_disconnect();
        return -1;
    }

    /* Read response (may be multi-line) */
    char response[4096] = {0};
    ssize_t n = recv(g_sm_socket, response, sizeof(response) - 1, 0);
    if (n < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "recv() failed: %s", strerror(errno));
        conn_sm_disconnect();
        return -1;
    }

    response[n] = '\0';

    /* TODO: Parse response into service array */
    /* For now, return empty array */
    *services_out = NULL;
    *count_out = 0;

    return 0;
}

/**
 * Get specific service status.
 */
int conn_sm_get_service_status(const char *name, int *status_out)
{
    if (!name || !status_out) return -1;

    if (conn_sm_send_command(name, "status") != 0) {
        return -1;
    }

    /* TODO: Parse and return status */
    *status_out = 0;  /* Default to stopped */
    return 0;
}

/**
 * Attempt reconnect.
 */
int conn_sm_reconnect(void)
{
    conn_sm_disconnect();
    return conn_sm_connect();
}

/**
 * Set error message.
 */
void conn_sm_set_error(const char *error)
{
    if (error) {
        strncpy(g_last_error, error, sizeof(g_last_error) - 1);
    }
}

/**
 * Get error message.
 */
const char *conn_sm_get_error(void)
{
    return g_last_error[0] ? g_last_error : "No error";
}
