/**
 * @file mock_middleware.h
 * @brief Mock middleware components for testing without real hardware
 *
 * Simulates SM, services, HAL, memory pools, io_uring, ring buffers, security module.
 * Controllable metrics, crash simulation, violation injection, rate control.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../protocol/monitor_ipc_protocol.h"

/* ── Configuration ────────────────────────────────────────────────────────── */

typedef struct {
    float    cpu_percent;
    uint64_t rss_bytes;
    uint32_t fd_count;
    bool     running;
    bool     simulate_crash;
    uint32_t inject_seccomp_violations;
    uint32_t inject_hmac_failures;
    uint32_t inject_replay_attacks;
    uint64_t frames_per_second;   /**< Configurable data rate */
} mock_service_config_t;

typedef struct {
    uint32_t used_blocks;
    uint32_t total_blocks;
    uint64_t fail_count;
    float    alloc_per_second;
} mock_pool_config_t;

typedef struct {
    uint32_t sq_depth;
    uint32_t cq_depth;
    float    p99_latency_ms;
    uint32_t pending_ops;
} mock_uring_config_t;

typedef struct {
    uint32_t capacity;
    uint32_t used;
    uint64_t drop_count;
    float    throughput_mbps;
} mock_ringbuf_config_t;

typedef struct {
    uint32_t device_count;
    float    throughput_ops_per_s;
    uint32_t error_count;
} mock_hal_config_t;

typedef struct {
    float    cpu_percent;
    uint64_t ram_used_bytes;
    uint64_t ram_total_bytes;
    uint64_t swap_used_bytes;
    uint64_t swap_total_bytes;
    float    load_1min;
} mock_sysinfo_config_t;

typedef struct {
    mock_service_config_t services[SERVICE_MAX];
    mock_pool_config_t    pools[POOL_MAX];
    mock_uring_config_t   urings[URING_MAX];
    mock_ringbuf_config_t ringbufs[RINGBUF_MAX];
    mock_hal_config_t     hal;
    mock_sysinfo_config_t sysinfo;
    
    bool     emit_log_storm;      /**< Emit 5000 logs/s from service[0] */
    uint32_t log_storm_rate;      /**< Lines per second for storm */
} mock_middleware_t;

/* ── API ──────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize mock middleware with default sensible values
 */
int mock_middleware_init(mock_middleware_t *mock);

/**
 * @brief Destroy mock middleware and clean up resources
 */
void mock_middleware_destroy(mock_middleware_t *mock);

/**
 * @brief Generate a snapshot from current mock configuration
 * @param mock   Mock middleware state
 * @param snap   Output snapshot
 */
void mock_middleware_generate_snapshot(const mock_middleware_t *mock, mon_snapshot_t *snap);

/**
 * @brief Configure a specific service
 */
void mock_set_service(mock_middleware_t *mock, uint32_t idx, const mock_service_config_t *cfg);

/**
 * @brief Configure a specific pool
 */
void mock_set_pool(mock_middleware_t *mock, uint32_t idx, const mock_pool_config_t *cfg);

/**
 * @brief Configure system-wide CPU/RAM
 */
void mock_set_sysinfo(mock_middleware_t *mock, const mock_sysinfo_config_t *cfg);

/**
 * @brief Simulate crash of a service (sets running=false)
 */
void mock_crash_service(mock_middleware_t *mock, uint32_t idx);

/**
 * @brief Restart a crashed service
 */
void mock_restart_service(mock_middleware_t *mock, uint32_t idx);

/**
 * @brief Inject security violation into service
 */
void mock_inject_violation(mock_middleware_t *mock, uint32_t service_idx,
                           uint32_t seccomp, uint32_t hmac, uint32_t replay);

/**
 * @brief Enable/disable log storm from service[0]
 */
void mock_set_log_storm(mock_middleware_t *mock, bool enabled, uint32_t rate);

#endif /* MOCK_MIDDLEWARE_H */
