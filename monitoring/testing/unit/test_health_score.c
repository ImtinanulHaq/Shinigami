/**
 * @file test_health_score.c
 * @brief Unit tests for health score computation
 */
#include "../test_framework.h"
#include "../../health/health_score.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include <string.h>

/* ── Test 1: Perfect Health ────────────────────────────────────────────────── */
static bool test_perfect_health(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    
    /* No violations, no alerts, everything nominal */
    snap.num_services = 2;
    snap.services[0].state = SERVICE_STATE_RUNNING;
    snap.services[0].cpu_pct = 2.5f;
    snap.services[0].ram_mb = 8;
    snap.services[1].state = SERVICE_STATE_RUNNING;
    snap.services[1].cpu_pct = 3.0f;
    snap.services[1].ram_mb = 10;
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    /* Both services should be 100 */
    TEST_ASSERT_EQ(health.service_scores[0], 100, "Service 0 should be perfect");
    TEST_ASSERT_EQ(health.service_scores[1], 100, "Service 1 should be perfect");
    
    /* System health = min of all */
    TEST_ASSERT_EQ(health.system_health, 100, "System should be perfect");
    
    return true;
}

/* ── Test 2: Violations Penalty ────────────────────────────────────────────── */
static bool test_violations_penalty(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    
    snap.num_services = 1;
    snap.services[0].state = SERVICE_STATE_RUNNING;
    snap.services[0].seccomp_violations = 1;
    snap.services[0].hmac_failures = 2;
    snap.services[0].replay_violations = 1;
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    /* Each violation= -10, total -40, so 100-40=60 */
    TEST_ASSERT_EQ(health.service_scores[0], 60, "Should be 60 after violations");
    
    return true;
}

/* ── Test 3: Crashed Service ────────────────────────────────────────────────── */
static bool test_crashed_service(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    
    snap.num_services = 2;
    snap.services[0].state = SERVICE_STATE_RUNNING;
    snap.services[1].state = SERVICE_STATE_CRASHED;
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    /* Running service = 100 */
    TEST_ASSERT_EQ(health.service_scores[0], 100, "Running service should be 100");
    
    /* Crashed = 0 */
    TEST_ASSERT_EQ(health.service_scores[1], 0, "Crashed service should be 0");
    
    /* System = min, so 0 */
    TEST_ASSERT_EQ(health.system_health, 0, "System health dragged to 0");
    
    return true;
}

/* ── Test 4: Floor at Zero ─────────────────────────────────────────────────── */
static bool test_floor_at_zero(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    
    snap.num_services = 1;
    snap.services[0].state = SERVICE_STATE_RUNNING;
    
    /* Massive violations: 50 seccomp = -500 penalty */
    snap.services[0].seccomp_violations = 50;
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    /* Should floor at 0, not go negative */
    TEST_ASSERT_EQ(health.service_scores[0], 0, "Should floor at 0");
    
    return true;
}

/* ── Test 5: System = Min(Services) ─────────────────────────────────────────── */
static bool test_system_min_services(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    
    snap.num_services = 4;
    snap.services[0].state = SERVICE_STATE_RUNNING; /* 100 */
    snap.services[1].state = SERVICE_STATE_RUNNING;
    snap.services[1].seccomp_violations = 2; /* -20 = 80 */
    snap.services[2].state = SERVICE_STATE_RUNNING;
    snap.services[2].hmac_failures = 3; /* -30 = 70 */
    snap.services[3].state = SERVICE_STATE_RUNNING;
    snap.services[3].replay_violations = 5; /* -50 = 50 */
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    TEST_ASSERT_EQ(health.service_scores[0], 100, "Service 0");
    TEST_ASSERT_EQ(health.service_scores[1], 80, "Service 1");
    TEST_ASSERT_EQ(health.service_scores[2], 70, "Service 2");
    TEST_ASSERT_EQ(health.service_scores[3], 50, "Service 3");
    
    /* System = min = 50 */
    TEST_ASSERT_EQ(health.system_health, 50, "System should be 50 (min)");
    
    return true;
}

/* ── Test 6: Restarting State ───────────────────────────────────────────────── */
static bool test_restarting_state(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    
    snap.num_services = 1;
    snap.services[0].state = SERVICE_STATE_RESTARTING;
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    /* RESTARTING should get penalty -30 */
    TEST_ASSERT_EQ(health.service_scores[0], 70, "RESTARTING should be 70");
    
    return true;
}

/* ── Test 7: No Services ─────────────────────────────────────────────────────── */
static bool test_no_services(void)
{
    mon_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.num_services = 0;
    
    health_snapshot_t health;
    health_compute_scores(&snap, &health);
    
    /* System health should be 100 when no services (nothing to fail) */
    TEST_ASSERT_EQ(health.system_health, 100, "No services = 100");
    
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"perfect_health", test_perfect_health, true},
        {"violations_penalty", test_violations_penalty, true},
        {"crashed_service", test_crashed_service, true},
        {"floor_at_zero", test_floor_at_zero, true},
        {"system_min_services", test_system_min_services, true},
        {"restarting_state", test_restarting_state, true},
        {"no_services", test_no_services, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Health Score");
}
