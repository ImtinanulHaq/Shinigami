/**
 * @file    monitord_state.h
 * @brief   Central state structure for monitord daemon.
 *
 * Contains all collected metrics with per-subsystem reader-writer locks.
 * Lock acquisition order (to prevent deadlock):
 *   1. alerts           7. proxy
 *   2. collectors       8. ringbufs
 *   3. hal              9. security
 *   4. health          10. services
 *   5. ipc_channels    11. sysinfo
 *   6. pools           12. urings
 */
#pragma once

#include <pthread.h>
#include "../protocol/monitor_ipc_protocol.h"
#include "../health/health_history.h"
#include "../alerts/alert_engine.h"
#include "../tracing/trace_store.h"

typedef struct monitord_state {
    /* ── Metrics ─────────────────────────────────────────────────────────── */
    sysinfo_metrics_t        sysinfo;
    sm_metrics_t             sm;
    watchdog_metrics_t       watchdog;
    hal_metrics_t            hal[HAL_MAX_DEVICES];
    service_metrics_t        services[SERVICE_MAX];
    pool_metrics_t           pools[POOL_MAX];
    uring_metrics_t          urings[URING_MAX];
    ringbuf_metrics_t        ringbufs[RINGBUF_MAX];
    security_metrics_t       security;
    proxy_metrics_t          proxy[PROXY_MAX];
    ipc_metrics_t            ipc_channels[IPC_CHAN_MAX];

    /* ── Per-subsystem locks ─────────────────────────────────────────────── */
    pthread_rwlock_t  lock_alerts;
    pthread_rwlock_t  lock_collectors;
    pthread_rwlock_t  lock_hal;
    pthread_rwlock_t  lock_health;
    pthread_rwlock_t  lock_ipc_channels;
    pthread_rwlock_t  lock_pools;
    pthread_rwlock_t  lock_proxy;
    pthread_rwlock_t  lock_ringbufs;
    pthread_rwlock_t  lock_security;
    pthread_rwlock_t  lock_services;
    pthread_rwlock_t  lock_sysinfo;
    pthread_rwlock_t  lock_urings;

    /* ── Health & Alerts ─────────────────────────────────────────────────── */
    health_history_t  health_history_system;
    health_history_t  health_history_services[SERVICE_MAX];
    alert_state_t     alert_state;

    /* ── Tracing ─────────────────────────────────────────────────────────── */
    trace_store_t     trace_store;

    /* ── Metadata ────────────────────────────────────────────────────────── */
    uint64_t          snapshot_seq;      /**< Monotonic snapshot counter */
    uint64_t          boot_time_ms;      /**< Daemon boot timestamp */
} monitord_state_t;

/**
 * @brief  Initialize the monitord state.
 * @param  state  State struct.
 * @return 0 on success, -1 on failure.
 */
int monitord_state_init(monitord_state_t *state);

/**
 * @brief  Destroy the monitord state.
 * @param  state  State struct.
 */
void monitord_state_destroy(monitord_state_t *state);

/**
 * @brief  Serialize the current state into a snapshot.
 * @param  state     State struct.
 * @param  snapshot  Output snapshot struct.
 *
 * @thread_safety  Acquires ALL 12 locks in alphabetical order, memcpy each
 *                 section, releases all locks. Total lock hold < 1ms.
 */
void monitord_state_serialize_snapshot(monitord_state_t *state,
                                        mon_snapshot_t *snapshot);
