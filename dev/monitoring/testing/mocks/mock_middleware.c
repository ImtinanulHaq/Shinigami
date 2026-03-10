/**
 * @file mock_middleware.c
 * @brief Mock middleware implementation
 */
#define _POSIX_C_SOURCE 200809L
#include "mock_middleware.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

int mock_middleware_init(mock_middleware_t *mock)
{
    memset(mock, 0, sizeof(*mock));
    
    /* Initialize 4 default services */
    const char *service_names[] = {"audio_service", "camera_service", "sensor_service", "gpio_service"};
    for (int i = 0; i < 4; i++) {
        mock->services[i].cpu_percent = 2.5f;
        mock->services[i].rss_bytes = 8 * 1024 * 1024;  /* 8MB */
        mock->services[i].fd_count = 15;
        mock->services[i].running = true;
        mock->services[i].frames_per_second = 30;
    }
    
    /* Initialize 3 memory pools */
    const char *pool_names[] = {"small_pool", "medium_pool", "large_pool"};
    for (int i = 0; i < 3; i++) {
        mock->pools[i].total_blocks = 256;
        mock->pools[i].used_blocks = 100 + i * 20;
        mock->pools[i].fail_count = 0;
        mock->pools[i].alloc_per_second = 50.0f;
    }
    
    /* Initialize 2 io_uring instances */
    for (int i = 0; i < 2; i++) {
        mock->urings[i].sq_depth = 256;
        mock->urings[i].cq_depth = 512;
        mock->urings[i].p99_latency_ms = 1.2f;
        mock->urings[i].pending_ops = 10 + i * 5;
    }
    
    /* Initialize 3 ring buffers */
    for (int i = 0; i < 3; i++) {
        mock->ringbufs[i].capacity = 1024;
        mock->ringbufs[i].used = 400 + i * 50;
        mock->ringbufs[i].drop_count = 0;
        mock->ringbufs[i].throughput_mbps = 5.5f;
    }
    
    /* Initialize HAL */
    mock->hal.device_count = 4;
    mock->hal.throughput_ops_per_s = 1200.0f;
    mock->hal.error_count = 0;
    
    /* Initialize sysinfo */
    mock->sysinfo.cpu_percent = 25.0f;
    mock->sysinfo.ram_used_bytes = 4ULL * 1024 * 1024 * 1024;   /* 4GB */
    mock->sysinfo.ram_total_bytes = 16ULL * 1024 * 1024 * 1024; /* 16GB */
    mock->sysinfo.swap_used_bytes = 0;
    mock->sysinfo.swap_total_bytes = 8ULL * 1024 * 1024 * 1024; /* 8GB */
    mock->sysinfo.load_1min = 1.5f;
    
    return 0;
}

void mock_middleware_destroy(mock_middleware_t *mock)
{
    (void)mock;
    /* Nothing to clean up for simple mock */
}

static uint64_t get_timestamp_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

void mock_middleware_generate_snapshot(const mock_middleware_t *mock, mon_snapshot_t *snap)
{
    memset(snap, 0, sizeof(*snap));
    
    uint64_t now = get_timestamp_ms();
    
    /* Sysinfo */
    snap->sysinfo.cpu_total_pct = mock->sysinfo.cpu_percent;
    snap->sysinfo.num_cores = 4;
    for (int i = 0; i < 4; i++) {
        snap->sysinfo.core_pct[i] = mock->sysinfo.cpu_percent + (i * 2.0f);
    }
    snap->sysinfo.ram_used_bytes = mock->sysinfo.ram_used_bytes;
    snap->sysinfo.ram_total_bytes = mock->sysinfo.ram_total_bytes;
    snap->sysinfo.swap_used_bytes = mock->sysinfo.swap_used_bytes;
    snap->sysinfo.swap_total_bytes = mock->sysinfo.swap_total_bytes;
    snap->sysinfo.load_1min = mock->sysinfo.load_1min;
    snap->sysinfo.load_5min = mock->sysinfo.load_1min * 0.9f;
    snap->sysinfo.load_15min = mock->sysinfo.load_1min * 0.8f;
    snap->sysinfo.ts_ms = now;
    
    /* Services */
    const char *service_names[] = {"audio_service", "camera_service", "sensor_service", "gpio_service"};
    for (int i = 0; i < 4; i++) {
        service_metrics_t *s = &snap->services[i];
        strncpy(s->name, service_names[i], sizeof(s->name) - 1);
        s->pid = 1000 + i;
        s->running = mock->services[i].running ? 1 : 0;
        s->uptime_s = 3600;
        s->cpu_pct = mock->services[i].cpu_percent;
        s->rss_bytes = mock->services[i].rss_bytes;
        s->fd_count = mock->services[i].fd_count;
        s->thread_count = 3;
        s->seccomp_violations = mock->services[i].inject_seccomp_violations;
        s->hmac_failures = mock->services[i].inject_hmac_failures;
        s->replay_attacks = mock->services[i].inject_replay_attacks;
        s->frames_captured = mock->services[i].frames_per_second * 3600;
        s->frames_dropped = 0;
        s->health_score = 100;
        s->ts_ms = now;
    }
    
    /* Memory pools */
    const char *pool_names[] = {"small_pool", "medium_pool", "large_pool"};
    for (int i = 0; i < 3; i++) {
        pool_metrics_t *p = &snap->pools[i];
        strncpy(p->name, pool_names[i], sizeof(p->name) - 1);
        p->total_blocks = mock->pools[i].total_blocks;
        p->used_blocks = mock->pools[i].used_blocks;
        p->free_blocks = p->total_blocks - p->used_blocks;
        p->usage_pct = (float)p->used_blocks / p->total_blocks * 100.0f;
        p->alloc_per_s = mock->pools[i].alloc_per_second;
        p->fail_count = mock->pools[i].fail_count;
        p->ts_ms = now;
    }
    
    /* io_uring */
    const char *uring_names[] = {"main_uring", "async_uring"};
    for (int i = 0; i < 2; i++) {
        uring_metrics_t *u = &snap->urings[i];
        strncpy(u->name, uring_names[i], sizeof(u->name) - 1);
        u->sq_depth = mock->urings[i].sq_depth;
        u->cq_depth = mock->urings[i].cq_depth;
        u->sq_fill_pct = (float)mock->urings[i].pending_ops / u->sq_depth * 100.0f;
        u->pending_ops = mock->urings[i].pending_ops;
        u->lat_p99_us = (uint32_t)(mock->urings[i].p99_latency_ms * 1000.0f);
        u->completions_per_s = 1000.0;
        u->ts_ms = now;
    }
    
    /* Ring buffers */
    const char *ringbuf_names[] = {"audio_rb", "camera_rb", "sensor_rb"};
    for (int i = 0; i < 3; i++) {
        ringbuf_metrics_t *r = &snap->ringbufs[i];
        strncpy(r->name, ringbuf_names[i], sizeof(r->name) - 1);
        r->capacity = mock->ringbufs[i].capacity;
        r->used = mock->ringbufs[i].used;
        r->fill_pct = (float)r->used / r->capacity * 100.0f;
        r->drop_count = mock->ringbufs[i].drop_count;
        r->throughput_bytes_s = mock->ringbufs[i].throughput_mbps * 1024 * 1024;
        r->ts_ms = now;
    }
    
    /* HAL */
    snap->hal.device_count = mock->hal.device_count;
    snap->hal.total_throughput = mock->hal.throughput_ops_per_s;
    snap->hal.total_errors = mock->hal.error_count;
    snap->hal.ts_ms = now;
    
    /* Security */
    for (int i = 0; i < SERVICE_MAX; i++) {
        snap->security.seccomp_violations[i] = mock->services[i].inject_seccomp_violations;
        snap->security.hmac_failures[i] = mock->services[i].inject_hmac_failures;
        snap->security.replay_attacks[i] = mock->services[i].inject_replay_attacks;
    }
    snap->security.ts_ms = now;
}

void mock_set_service(mock_middleware_t *mock, uint32_t idx, const mock_service_config_t *cfg)
{
    if (idx < SERVICE_MAX) {
        mock->services[idx] = *cfg;
    }
}

void mock_set_pool(mock_middleware_t *mock, uint32_t idx, const mock_pool_config_t *cfg)
{
    if (idx < POOL_MAX) {
        mock->pools[idx] = *cfg;
    }
}

void mock_set_sysinfo(mock_middleware_t *mock, const mock_sysinfo_config_t *cfg)
{
    mock->sysinfo = *cfg;
}

void mock_crash_service(mock_middleware_t *mock, uint32_t idx)
{
    if (idx < SERVICE_MAX) {
        mock->services[idx].running = false;
    }
}

void mock_restart_service(mock_middleware_t *mock, uint32_t idx)
{
    if (idx < SERVICE_MAX) {
        mock->services[idx].running = true;
    }
}

void mock_inject_violation(mock_middleware_t *mock, uint32_t service_idx,
                           uint32_t seccomp, uint32_t hmac, uint32_t replay)
{
    if (service_idx < SERVICE_MAX) {
        mock->services[service_idx].inject_seccomp_violations = seccomp;
        mock->services[service_idx].inject_hmac_failures = hmac;
        mock->services[service_idx].inject_replay_attacks = replay;
    }
}

void mock_set_log_storm(mock_middleware_t *mock, bool enabled, uint32_t rate)
{
    mock->emit_log_storm = enabled;
    mock->log_storm_rate = rate;
}
