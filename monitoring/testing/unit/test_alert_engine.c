/**
 * @file test_alert_engine.c
 * @brief Unit tests for alert engine (deduplication, cooldowns)
 */
#include "../test_framework.h"
#include "../../alerts/alert_engine.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include <string.h>
#include <unistd.h>

/* ── Test 1: Basic Alert Firing ────────────────────────────────────────────── */
static bool test_basic_alert_fire(void)
{
    alert_state_t state;
    TEST_ASSERT_EQ(alert_engine_init(&state, 100), 0, "Init failed");
    
    uint64_t now = 1000000; /* 1s in ms */
    uint64_t alert_id = alert_fire(&state, ALERT_SEV_CRIT, 
                                     "audio_service", "CPU > 90%",
                                     "95.5%", "90%", now);
    
    TEST_ASSERT_NE(alert_id, UINT64_MAX, "Alert fire failed");
    
    /* Verify alert stored */
    alert_record_t rec;
    TEST_ASSERT_EQ(alert_get_by_id(&state, alert_id, &rec), 0, "Get by ID failed");
    TEST_ASSERT_STR_EQ(rec.component, "audio_service", "Component wrong");
    TEST_ASSERT_STR_EQ(rec.condition, "CPU > 90%", "Condition wrong");
    TEST_ASSERT_EQ(rec.severity, ALERT_SEV_CRIT, "Severity wrong");
    TEST_ASSERT_EQ(rec.occurrences, 1, "Should be first occurrence");
    TEST_ASSERT_EQ(rec.active, 1, "Should be active");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 2: CRITICAL Cooldown (30s) ────────────────────────────────────────── */
static bool test_critical_cooldown(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t t0 = 1000000;
    
    /* Fire CRITICAL alert */
    uint64_t id1 = alert_fire(&state, ALERT_SEV_CRIT, "svc", "cond", "val", "thresh", t0);
    TEST_ASSERT_NE(id1, UINT64_MAX, "First fire failed");
    
    /* Fire same alert after 10s - should be deduplicated */
    uint64_t id2 = alert_fire(&state, ALERT_SEV_CRIT, "svc", "cond", "val", "thresh", t0 + 10000);
    TEST_ASSERT_EQ(id2, id1, "Should return same ID (deduplicated)");
    
    /* Check occurrence count */
    alert_record_t rec;
    alert_get_by_id(&state, id1, &rec);
    TEST_ASSERT_EQ(rec.occurrences, 2, "Should have 2 occurrences");
    
    /* Fire after 35s - should create new alert (cooldown expired) */
    uint64_t id3 = alert_fire(&state, ALERT_SEV_CRIT, "svc", "cond", "val", "thresh", t0 + 35000);
    TEST_ASSERT_NE(id3, id1, "Should create new alert after cooldown");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 3: WARNING Cooldown (60s) ──────────────────────────────────────────── */
static bool test_warning_cooldown(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t t0 = 2000000;
    
    uint64_t id1 = alert_fire(&state, ALERT_SEV_WARN, "svc", "cond", "val", "thresh", t0);
    TEST_ASSERT_NE(id1, UINT64_MAX, "First fire failed");
    
    /* After 30s - still in cooldown */
    uint64_t id2 = alert_fire(&state, ALERT_SEV_WARN, "svc", "cond", "val", "thresh", t0 + 30000);
    TEST_ASSERT_EQ(id2, id1, "Should deduplicate");
    
    /* After 70s - cooldown expired */
    uint64_t id3 = alert_fire(&state, ALERT_SEV_WARN, "svc", "cond", "val", "thresh", t0 + 70000);
    TEST_ASSERT_NE(id3, id1, "Should create new alert");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 4: INFO Cooldown (300s) ───────────────────────────────────────────── */
static bool test_info_cooldown(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t t0 = 3000000;
    
    uint64_t id1 = alert_fire(&state, ALERT_SEV_INFO, "svc", "cond", "val", "thresh", t0);
    
    /* After 200s - still in cooldown */
    uint64_t id2 = alert_fire(&state, ALERT_SEV_INFO, "svc", "cond", "val", "thresh", t0 + 200000);
    TEST_ASSERT_EQ(id2, id1, "Should deduplicate");
    
    /* After 310s - cooldown expired */
    uint64_t id3 = alert_fire(&state, ALERT_SEV_INFO, "svc", "cond", "val", "thresh", t0 + 310000);
    TEST_ASSERT_NE(id3, id1, "Should create new alert");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 5: Different Components ───────────────────────────────────────────── */
static bool test_different_components(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t now = 4000000;
    
    /* Same condition but different components - should not dedupe */
    uint64_t id1 = alert_fire(&state, ALERT_SEV_CRIT, "audio", "CPU > 90%", "95", "90", now);
    uint64_t id2 = alert_fire(&state, ALERT_SEV_CRIT, "camera", "CPU > 90%", "95", "90", now);
    
    TEST_ASSERT_NE(id1, id2, "Different components should create separate alerts");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 6: Occurrence Counter ─────────────────────────────────────────────── */
static bool test_occurrence_counter(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t t0 = 5000000;
    
    /* Fire same alert 10 times within cooldown */
    uint64_t id = 0;
    for (int i = 0; i < 10; i++) {
        uint64_t ret = alert_fire(&state, ALERT_SEV_CRIT, "svc", "cond", "val", "thresh", t0 + i*1000);
        if (i == 0) id = ret;
    }
    
    /* Check occurrence count */
    alert_record_t rec;
    alert_get_by_id(&state, id, &rec);
    TEST_ASSERT_EQ(rec.occurrences, 10, "Should have 10 occurrences");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 7: Clear Alerts ───────────────────────────────────────────────────── */
static bool test_clear_alerts(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t now = 6000000;
    
    /* Fire multiple alerts */
    uint64_t id1 = alert_fire(&state, ALERT_SEV_CRIT, "svc1", "cond1", "v", "t", now);
    uint64_t id2 = alert_fire(&state, ALERT_SEV_WARN, "svc2", "cond2", "v", "t", now);
    
    /* Clear alerts */
    alert_engine_clear(&state);
    
    /* Verify both marked inactive */
    alert_record_t rec;
    alert_get_by_id(&state, id1, &rec);
    TEST_ASSERT_EQ(rec.active, 0, "Alert 1 should be inactive");
    
    alert_get_by_id(&state, id2, &rec);
    TEST_ASSERT_EQ(rec.active, 0, "Alert 2 should be inactive");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test 8: Get Active Count ───────────────────────────────────────────────── */
static bool test_get_active_count(void)
{
    alert_state_t state;
    alert_engine_init(&state, 100);
    
    uint64_t now = 7000000;
    
    TEST_ASSERT_EQ(alert_get_active_count(&state), 0, "Should start with 0");
    
    alert_fire(&state, ALERT_SEV_CRIT, "svc1", "cond1", "v", "t", now);
    TEST_ASSERT_EQ(alert_get_active_count(&state), 1, "Should have 1");
    
    alert_fire(&state, ALERT_SEV_WARN, "svc2", "cond2", "v", "t", now);
    TEST_ASSERT_EQ(alert_get_active_count(&state), 2, "Should have 2");
    
    alert_engine_clear(&state);
    TEST_ASSERT_EQ(alert_get_active_count(&state), 0, "Should be 0 after clear");
    
    alert_engine_destroy(&state);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"basic_alert_fire", test_basic_alert_fire, true},
        {"critical_cooldown", test_critical_cooldown, true},
        {"warning_cooldown", test_warning_cooldown, true},
        {"info_cooldown", test_info_cooldown, true},
        {"different_components", test_different_components, true},
        {"occurrence_counter", test_occurrence_counter, true},
        {"clear_alerts", test_clear_alerts, true},
        {"get_active_count", test_get_active_count, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Alert Engine");
}
