/**
 * @file    conn_sm.h
 * @brief   Servicemanager socket connector for sending commands and receiving responses.
 */
#ifndef CONN_SM_H
#define CONN_SM_H

#include <stdint.h>
#include <time.h>

/**
 * Service state information from servicemanager.
 */
typedef struct {
    int service_id;
    const char *name;
    int status;                 /* 0=stopped, 1=running, 2=restarting */
    int uptime_sec;
    int restart_count;
    int crash_count;
} conn_sm_service_t;

/**
 * Connect to servicemanager socket.
 */
int conn_sm_connect(void);

/**
 * Disconnect from servicemanager.
 */
void conn_sm_disconnect(void);

/**
 * Check if connected.
 */
int conn_sm_is_connected(void);

/**
 * Send a command and wait for response.
 * Commands: "start", "stop", "restart", "status", "list".
 */
int conn_sm_send_command(const char *service_name, const char *command);

/**
 * Query all services (synchronous).
 */
int conn_sm_get_services(conn_sm_service_t **services_out, int *count_out);

/**
 * Get status of a specific service.
 */
int conn_sm_get_service_status(const char *name, int *status_out);

/**
 * Attempt to reconnect if disconnected.
 */
int conn_sm_reconnect(void);

/**
 * Set last error from conn_sm operations.
 */
void conn_sm_set_error(const char *error);

/**
 * Get last error.
 */
const char *conn_sm_get_error(void);

#endif
