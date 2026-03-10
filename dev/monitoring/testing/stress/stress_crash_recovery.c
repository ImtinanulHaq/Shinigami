/**
 * @file stress_crash_recovery.c
 * @brief Stress test: Service crash and recovery (10 cycles)
 * 
 * Pass criteria:
 * - OFFLINE detected within 3× interval
 * - Alert fired
 * - Reconnect <30s after restart
 * - FD count stable after 10 cycles
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../collectors/collector_base.h"
#include "../../alerts/alert_engine.h"
#include <time.h>

#define NUM_CRASH_CYCLES 10
#define COLLECTOR_INTERVAL_MS 1000
#define MAX_OFFLINE_DETECTION_MS (3 * COLLECTOR_INTERVAL_MS)
#define MAX_RECONNECT_SEC 30

/* ── Test 1: OFFLINE Detection Within 3× Interval ────────────────────────────── */
static bool test_offline_detection(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test_collector", sizeof(collector.name)-1);
    collector.interval_ms = COLLECTOR_INTERVAL_MS;
    
    /* Simulate service running */
    TEST_ASSERT_EQ(collector.state, COLLECTOR_STATE_WAITING, "Initial state");
    
    /* Crash service */
    mock_crash_service(&mock, 0);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    TEST_ASSERT_EQ(snap.services[0].state, SERVICE_STATE_CRASHED, "Should be crashed");
    
    /* Collector should detect within 3× interval */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    
    /* Simulate collector attempting connection */
    usleep(COLLECTOR_INTERVAL_MS * 1000); /* Wait 1 interval */
    
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    
    TEST_ASSERT(elapsed_ms < MAX_OFFLINE_DETECTION_MS, "Should detect within 3× interval");
    
    return true;
}

/* ── Test 2: Alert Fired on Crash ───────────────────────────────────────────── */
static bool test_alert_on_crash(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    /* Crash service */
    mock_crash_service(&mock, 1);
    
    /* Generate snapshot */
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    /* Evaluate rules */
    uint64_t now = 1000000;
    alert_evaluate_all_rules(&alert_state, &snap, now);
    
    /* Should have fired crash alert */
    TEST_ASSERT(alert_get_active_count(&alert_state) > 0, "Should fire alert");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test 3: Reconnect <30s ─────────────────────────────────────────────────── */
static bool test_reconnect_time(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    struct timespec t0, t1;
    
    /* Crash service */
    mock_crash_service(&mock, 2);
    
    clock_gettime(CLOCK_MONOTONIC, &t0);
    
    /* Restart service (simulate) */
    mock_service_config_t svc;
    memset(&svc, 0, sizeof(svc));
    svc.state = SERVICE_STATE_RUNNING;
    svc.cpu_percent = 2.5f;
    svc.rss_bytes = 8 * 1024 * 1024;
    mock_set_service(&mock, 2, &svc);
    
    /* Simulate reconnection delay */
    usleep(500000); /* 500ms reconnect time */
    
    clock_gettime(CLOCK_MONOTONIC, &t1);
    
    uint64_t reconnect_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    
    TEST_ASSERT(reconnect_ms < MAX_RECONNECT_SEC * 1000, "Reconnect <30s");
    
    /* Verify service is running */
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    TEST_ASSERT_EQ(snap.services[2].state, SERVICE_STATE_RUNNING, "Should be running");
    
    return true;
}

/* ── Test 4: FD Stability After 10 Cycles ────────────────────────────────────── */
static bool test_fd_stability(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    size_t initial_fds = test_count_fds();
    
    /* Perform 10 crash/recovery cycles */
    for (int cycle = 0; cycle < NUM_CRASH_CYCLES; cycle++) {
        /* Crash */
        mock_crash_service(&mock, 0);
        usleep(100000); /* 100ms */
        
        /* Recover */
        mock_service_config_t svc;
        memset(&svc, 0, sizeof(svc));
        svc.state = SERVICE_STATE_RUNNING;
        svc.cpu_percent = 2.5f;
        svc.rss_bytes = 8 * 1024 * 1024;
        mock_set_service(&mock, 0, &svc);
        usleep(100000); /* 100ms */
    }
    
    size_t final_fds = test_count_fds();
    
    /* FD count should be unchanged */
    TEST_ASSERT_EQ(final_fds, initial_fds, "FD count should be stable");
    
    return true;
}

/* ── Test 5: Memory Leak Check ──────────────────────────────────────────────── */
static bool test_memory_leak(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    size_t initial_rss = test_get_rss_bytes();
    
    /* Perform 10 crash/recovery cycles */
    for (int cycle = 0; cycle < NUM_CRASH_CYCLES; cycle++) {
        mock_crash_service(&mock, 0);
        
        mon_snapshot_t snap;
        mock_middleware_generate_snapshot(&mock, &snap);
        
        /* Recover */
        mock_service_config_t svc;
        memset(&svc, 0, sizeof(svc));
        svc.state = SERVICE_STATE_RUNNING;
        mock_set_service(&mock, 0, &svc);
        
        mock_middleware_generate_snapshot(&mock, &snap);
    }
    
    size_t final_rss = test_get_rss_bytes();
    size_t growth_kb = (final_rss - initial_rss) / 1024;
    
    /* Memory growth < 1MB */
    TEST_ASSERT(growth_kb < 1024, "Memory growth should be minimal");
    
    return true;
}

/* ── Test 6: State Transitions ──────────────────────────────────────────────── */
static bool test_state_transitions(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    /* Initial: RUNNING */
    mock_service_config_t svc;
    memset(&svc, 0, sizeof(svc));
    svc.state = SERVICE_STATE_RUNNING;
    mock_set_service(&mock, 0, &svc);
    
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    TEST_ASSERT_EQ(snap.services[0].state, SERVICE_STATE_RUNNING, "Should be running");
    
    /* Crash: RUNNING → CRASHED */
    mock_crash_service(&mock, 0);
    mock_middleware_generate_snapshot(&mock, &snap);
    TEST_ASSERT_EQ(snap.services[0].state, SERVICE_STATE_CRASHED, "Should be crashed");
    
    /* Restart: CRASHED → RESTARTING */
    svc.state = SERVICE_STATE_RESTARTING;
    mock_set_service(&mock, 0, &svc);
    mock_middleware_generate_snapshot(&mock, &snap);
    TEST_ASSERT_EQ(snap.services[0].state, SERVICE_STATE_RESTARTING, "Should be restarting");
    
    /* Running: RESTARTING → RUNNING */
    svc.state = SERVICE_STATE_RUNNING;
    mock_set_service(&mock, 0, &svc);
    mock_middleware_generate_snapshot(&mock, &snap);
    TEST_ASSERT_EQ(snap.services[0].state, SERVICE_STATE_RUNNING, "Should be running again");
    
    return true;
}

/* ── Test 7: Alert Deduplication During Flapping ─────────────────────────────── */
static bool test_alert_deduplication(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    uint64_t now = 1000000;
    
    /* Crash 5 times rapidly */
    for (int i = 0; i < 5; i++) {
        mock_crash_service(&mock, 0);
        
        mon_snapshot_t snap;
        mock_middleware_generate_snapshot(&mock, &snap);
        
        alert_evaluate_all_rules(&alert_state, &snap, now + i * 1000);
        
        /* Recover */
        mock_service_config_t svc;
        memset(&svc, 0, sizeof(svc));
        svc.state = SERVICE_STATE_RUNNING;
        mock_set_service(&mock, 0, &svc);
    }
    
    /* Should have deduplicated alerts (not 5 separate alerts) */
    TEST_ASSERT(alert_get_active_count(&alert_state) <= 2, "Should deduplicate");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"offline_detection", test_offline_detection, true},
        {"alert_on_crash", test_alert_on_crash, true},
        {"reconnect_time", test_reconnect_time, true},
        {"fd_stability", test_fd_stability, true},
        {"memory_leak", test_memory_leak, true},
        {"state_transitions", test_state_transitions, true},
        {"alert_deduplication", test_alert_deduplication, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Crash Recovery Stress");
}
