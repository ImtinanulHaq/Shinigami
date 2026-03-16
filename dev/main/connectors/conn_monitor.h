/**
 * @file    conn_monitor.h
 * @brief   Monitor socket connector for receiving broadcast snapshots.
 */
#ifndef CONN_MONITOR_H
#define CONN_MONITOR_H

#include <stdint.h>
#include <time.h>

/**
 * Snapshot from monitor (same as mon_snapshot_t from monitord).
 * We receive this via broadcast and cache it for all panels to use.
 */
typedef struct {
    uint64_t timestamp;         /* Seconds since epoch */
    int cpu_percent;            /* Overall CPU % */
    int memory_percent;         /* Overall memory % */
    int service_count;          /* Number of active services */
    int alert_count;            /* Active alerts */
    int security_violations;    /* Recent security violations */
    /* TODO: Add full snapshot structure (services, proxies, security state, etc.) */
} conn_monitor_snapshot_t;

/**
 * Connect to monitor broadcast socket.
 */
int conn_monitor_connect(void);

/**
 * Disconnect from monitor.
 */
void conn_monitor_disconnect(void);

/**
 * Check if connected.
 */
int conn_monitor_is_connected(void);

/**
 * Try to receive an updated snapshot (non-blocking).
 * Returns 1 if new data received, 0 if no data, -1 on error.
 */
int conn_monitor_recv_snapshot(conn_monitor_snapshot_t *snap_out);

/**
 * Get last cached snapshot.
 */
const conn_monitor_snapshot_t *conn_monitor_get_cached_snapshot(void);

/**
 * Attempt reconnect if disconnected.
 */
int conn_monitor_reconnect(void);

/**
 * Set error.
 */
void conn_monitor_set_error(const char *error);

/**
 * Get error.
 */
const char *conn_monitor_get_error(void);

#endif
