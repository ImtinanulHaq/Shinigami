/**
 * @file test_metric_accuracy.c
 * @brief Integration test verifying mock middleware metrics match snapshot
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include <string.h>

/* ── Test 1: Service Metrics Accuracy ───────────────────────────────────────── */
static bool test_service_metrics(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Configure service 0 with specific values */
    mock_service_config_t svc;
    memset(&svc, 0, sizeof(svc));
    svc.cpu_percent = 45.5f;
    svc.rss_bytes = 10 * 1024 * 1024; /* 10 MB */
    svc.fd_count = 25;
    svc.state = SERVICE_STATE_RUNNING;
    mock_set_service(&mock, 0, &svc);
    
    /* Generate snapshot */
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    /* Verify accuracy */
    TEST_ASSERT_FLOAT_EQ(snap.services[0].cpu_pct, 45.5f, 0.1f, "CPU mismatch");
    TEST_ASSERT_EQ(snap.services[0].ram_mb, 10, "RAM mismatch");
    TEST_ASSERT_EQ(snap.services[0].fd_count, 25, "FD count mismatch");
    TEST_ASSERT_EQ(snap.services[0].state, SERVICE_STATE_RUNNING, "State mismatch");
    
    return true;
}

/* ── Test 2: Memory Pool Metrics ─────────────────────────────────────────────── */
static bool test_pool_metrics(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Configure pool 0 */
    mock_pool_config_t pool;
    memset(&pool, 0, sizeof(pool));
    pool.total_blocks = 256;
    pool.used_blocks = 199;
    pool.fail_count = 5;
    pool.alloc_per_sec = 120;
    mock_set_pool(&mock, 0, &pool);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_EQ(snap.pools[0].total_blocks, 256, "Total blocks");
    TEST_ASSERT_EQ(snap.pools[0].used_blocks, 199, "Used blocks");
    TEST_ASSERT_EQ(snap.pools[0].fail_count, 5, "Fail count");
    TEST_ASSERT_EQ(snap.pools[0].alloc_per_sec, 120, "Alloc rate");
    
    return true;
}

/* ── Test 3: IO Uring Metrics ───────────────────────────────────────────────── */
static bool test_uring_metrics(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Configure uring 0 */
    mock_uring_config_t uring;
    memset(&uring, 0, sizeof(uring));
    uring.depth = 512;
    uring.p99_latency_us = 2300; /* 2.3ms */
    uring.sq_pending = 15;
    uring.cq_ready = 8;
    mock_set_uring(&mock, 0, &uring);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_EQ(snap.urings[0].depth, 512, "Depth");
    TEST_ASSERT_EQ(snap.urings[0].p99_latency_us, 2300, "P99 latency");
    TEST_ASSERT_EQ(snap.urings[0].sq_pending, 15, "SQ pending");
    TEST_ASSERT_EQ(snap.urings[0].cq_ready, 8, "CQ ready");
    
    return true;
}

/* ── Test 4: Ring Buffer Metrics ─────────────────────────────────────────────── */
static bool test_ringbuf_metrics(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Configure ringbuf 0 */
    mock_ringbuf_config_t rb;
    memset(&rb, 0, sizeof(rb));
    rb.capacity = 1024;
    rb.used = 512;
    rb.drop_count = 7;
    rb.throughput_mbps = 3.5f;
    mock_set_ringbuf(&mock, 0, &rb);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_EQ(snap.ringbufs[0].capacity, 1024, "Capacity");
    TEST_ASSERT_EQ(snap.ringbufs[0].used, 512, "Used");
    TEST_ASSERT_EQ(snap.ringbufs[0].drop_count, 7, "Drop count");
    TEST_ASSERT_FLOAT_EQ(snap.ringbufs[0].throughput_mbps, 3.5f, 0.1f, "Throughput");
    
    return true;
}

/* ── Test 5: Sysinfo Metrics ─────────────────────────────────────────────────── */
static bool test_sysinfo_metrics(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Configure sysinfo */
    mock_sysinfo_config_t sysinfo;
    memset(&sysinfo, 0, sizeof(sysinfo));
    sysinfo.cpu_usage_pct = 35.2f;
    sysinfo.ram_total_mb = 16384; /* 16 GB */
    sysinfo.ram_used_mb = 8192;   /* 8 GB */
    sysinfo.swap_total_mb = 8192;
    sysinfo.swap_used_mb = 1024;
    sysinfo.load_avg = 2.5f;
    mock_set_sysinfo(&mock, &sysinfo);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_FLOAT_EQ(snap.sysinfo.cpu_usage_pct, 35.2f, 0.1f, "CPU usage");
    TEST_ASSERT_EQ(snap.sysinfo.ram_total_mb, 16384, "RAM total");
    TEST_ASSERT_EQ(snap.sysinfo.ram_used_mb, 8192, "RAM used");
    TEST_ASSERT_EQ(snap.sysinfo.swap_total_mb, 8192, "Swap total");
    TEST_ASSERT_EQ(snap.sysinfo.swap_used_mb, 1024, "Swap used");
    TEST_ASSERT_FLOAT_EQ(snap.sysinfo.load_avg, 2.5f, 0.1f, "Load avg");
    
    return true;
}

/* ── Test 6: Violation Injection ─────────────────────────────────────────────── */
static bool test_violation_injection(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Inject violations into service 1 */
    mock_inject_violation(&mock, 1, 3, 2, 1);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_EQ(snap.services[1].seccomp_violations, 3, "Seccomp violations");
    TEST_ASSERT_EQ(snap.services[1].hmac_failures, 2, "HMAC failures");
    TEST_ASSERT_EQ(snap.services[1].replay_violations, 1, "Replay violations");
    
    return true;
}

/* ── Test 7: Crash Service ──────────────────────────────────────────────────── */
static bool test_crash_service(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Crash service 2 */
    mock_crash_service(&mock, 2);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_EQ(snap.services[2].state, SERVICE_STATE_CRASHED, "Should be crashed");
    TEST_ASSERT_FLOAT_EQ(snap.services[2].cpu_pct, 0.0f, 0.01f, "CPU should be 0");
    
    return true;
}

/* ── Test 8: Log Storm ──────────────────────────────────────────────────────── */
static bool test_log_storm(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Enable log storm */
    mock_set_log_storm(&mock, true, 5000);
    
    TEST_ASSERT_EQ(mock.log_storm_enabled, 1, "Log storm should be enabled");
    TEST_ASSERT_EQ(mock.log_storm_rate, 5000, "Rate should be 5000/s");
    
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"service_metrics", test_service_metrics, true},
        {"pool_metrics", test_pool_metrics, true},
        {"uring_metrics", test_uring_metrics, true},
        {"ringbuf_metrics", test_ringbuf_metrics, true},
        {"sysinfo_metrics", test_sysinfo_metrics, true},
        {"violation_injection", test_violation_injection, true},
        {"crash_service", test_crash_service, true},
        {"log_storm", test_log_storm, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Metric Accuracy");
}
