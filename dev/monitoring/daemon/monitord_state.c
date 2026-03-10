/**
 * @file    monitord_state.c
 * @brief   Central state implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "monitord_state.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

int monitord_state_init(monitord_state_t *state)
{
    memset(state, 0, sizeof(*state));

    /* Initialize all locks in alphabetical order */
    pthread_rwlock_init(&state->lock_alerts, NULL);
    pthread_rwlock_init(&state->lock_collectors, NULL);
    pthread_rwlock_init(&state->lock_hal, NULL);
    pthread_rwlock_init(&state->lock_health, NULL);
    pthread_rwlock_init(&state->lock_ipc_channels, NULL);
    pthread_rwlock_init(&state->lock_pools, NULL);
    pthread_rwlock_init(&state->lock_proxy, NULL);
    pthread_rwlock_init(&state->lock_ringbufs, NULL);
    pthread_rwlock_init(&state->lock_security, NULL);
    pthread_rwlock_init(&state->lock_services, NULL);
    pthread_rwlock_init(&state->lock_sysinfo, NULL);
    pthread_rwlock_init(&state->lock_urings, NULL);

    /* Initialize health history */
    health_history_init(&state->health_history_system, "system");
    for (int i = 0; i < SERVICE_MAX; i++) {
        char name[32];
        snprintf(name, sizeof(name), "service_%d", i);
        health_history_init(&state->health_history_services[i], name);
    }

    /* Initialize alert state */
    alert_state_init(&state->alert_state);

    /* Initialize trace store */
    trace_store_init(&state->trace_store);

    /* Record boot time */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    state->boot_time_ms = (uint64_t)ts.tv_sec * 1000 +
                          (uint64_t)(ts.tv_nsec / 1000000);

    return 0;
}

void monitord_state_destroy(monitord_state_t *state)
{
    /* Destroy health history */
    health_history_destroy(&state->health_history_system);
    for (int i = 0; i < SERVICE_MAX; i++) {
        health_history_destroy(&state->health_history_services[i]);
    }

    /* Destroy alert state */
    alert_state_destroy(&state->alert_state);

    /* Destroy trace store */
    trace_store_destroy(&state->trace_store);

    /* Destroy all locks */
    pthread_rwlock_destroy(&state->lock_alerts);
    pthread_rwlock_destroy(&state->lock_collectors);
    pthread_rwlock_destroy(&state->lock_hal);
    pthread_rwlock_destroy(&state->lock_health);
    pthread_rwlock_destroy(&state->lock_ipc_channels);
    pthread_rwlock_destroy(&state->lock_pools);
    pthread_rwlock_destroy(&state->lock_proxy);
    pthread_rwlock_destroy(&state->lock_ringbufs);
    pthread_rwlock_destroy(&state->lock_security);
    pthread_rwlock_destroy(&state->lock_services);
    pthread_rwlock_destroy(&state->lock_sysinfo);
    pthread_rwlock_destroy(&state->lock_urings);
}

void monitord_state_serialize_snapshot(monitord_state_t *state,
                                        mon_snapshot_t *snapshot)
{
    /* Acquire ALL locks in alphabetical order */
    pthread_rwlock_rdlock(&state->lock_alerts);
    pthread_rwlock_rdlock(&state->lock_collectors);
    pthread_rwlock_rdlock(&state->lock_hal);
    pthread_rwlock_rdlock(&state->lock_health);
    pthread_rwlock_rdlock(&state->lock_ipc_channels);
    pthread_rwlock_rdlock(&state->lock_pools);
    pthread_rwlock_rdlock(&state->lock_proxy);
    pthread_rwlock_rdlock(&state->lock_ringbufs);
    pthread_rwlock_rdlock(&state->lock_security);
    pthread_rwlock_rdlock(&state->lock_services);
    pthread_rwlock_rdlock(&state->lock_sysinfo);
    pthread_rwlock_rdlock(&state->lock_urings);

    /* Fast memcpy of each section */
    memcpy(&snapshot->sysinfo, &state->sysinfo, sizeof(sysinfo_metrics_t));
    memcpy(&snapshot->sm, &state->sm, sizeof(sm_metrics_t));
    memcpy(&snapshot->watchdog, &state->watchdog, sizeof(watchdog_metrics_t));
    memcpy(snapshot->hal, &state->hal, sizeof(hal_metrics_t) * HAL_MAX_DEVICES);
    memcpy(snapshot->services, state->services,
           sizeof(service_metrics_t) * SERVICE_MAX);
    memcpy(snapshot->pools, state->pools,
           sizeof(pool_metrics_t) * POOL_MAX);
    memcpy(snapshot->urings, state->urings,
           sizeof(uring_metrics_t) * URING_MAX);
    memcpy(snapshot->ring_buffers, state->ringbufs,
           sizeof(ringbuf_metrics_t) * RINGBUF_MAX);
    memcpy(&snapshot->security, &state->security, sizeof(security_metrics_t));
    memcpy(snapshot->proxies, state->proxy,
           sizeof(proxy_metrics_t) * PROXY_MAX);
    memcpy(snapshot->ipc_channels, state->ipc_channels,
           sizeof(ipc_metrics_t) * IPC_CHAN_MAX);

    snapshot->snapshot_seq = state->snapshot_seq++;

    /* Release ALL locks in reverse order */
    pthread_rwlock_unlock(&state->lock_urings);
    pthread_rwlock_unlock(&state->lock_sysinfo);
    pthread_rwlock_unlock(&state->lock_services);
    pthread_rwlock_unlock(&state->lock_security);
    pthread_rwlock_unlock(&state->lock_ringbufs);
    pthread_rwlock_unlock(&state->lock_proxy);
    pthread_rwlock_unlock(&state->lock_pools);
    pthread_rwlock_unlock(&state->lock_ipc_channels);
    pthread_rwlock_unlock(&state->lock_health);
    pthread_rwlock_unlock(&state->lock_hal);
    pthread_rwlock_unlock(&state->lock_collectors);
    pthread_rwlock_unlock(&state->lock_alerts);
}
