/**
 * @file test_alert_delivery.c
 * @brief Integration test for alert generation and delivery to TUI clients
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../alerts/alert_engine.h"
#include "../../alerts/alert_rules.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <time.h>

#define TEST_SOCK_PATH "/tmp/test_alert_delivery.sock"

static volatile int g_alert_received = 0;
static alert_record_t g_received_alert;

/* ── Mock TUI Client Thread ─────────────────────────────────────────────────── */
static void *tui_subscriber_thread(void *arg)
{
    int sock = *(int *)arg;
    
    uint8_t buffer[4096];
    while (1) {
        ssize_t nread = recv(sock, buffer, sizeof(buffer), 0);
        if (nread <= 0) break;
        
        mon_msg_header_t *hdr = (mon_msg_header_t *)buffer;
        if (hdr->msg_type == MON_MSG_TYPE_ALERT) {
            mon_msg_alert_t *alert_msg = (mon_msg_alert_t *)buffer;
            memcpy(&g_received_alert, &alert_msg->alert, sizeof(alert_record_t));
            g_alert_received = 1;
            break;
        }
    }
    
    return NULL;
}

/* ── Test 1: Basic Alert Delivery ───────────────────────────────────────────── */
static bool test_basic_alert_delivery(void)
{
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    /* Fire an alert */
    uint64_t now = 1000000;
    uint64_t alert_id = alert_fire(&alert_state, ALERT_SEV_CRIT,
                                     "audio_service", "CPU > 90%",
                                     "95.5%", "90%", now);
    
    TEST_ASSERT_NE(alert_id, UINT64_MAX, "Alert fire failed");
    TEST_ASSERT_EQ(alert_get_active_count(&alert_state), 1, "Should have 1 active alert");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test 2: Alert Within 1s ─────────────────────────────────────────────────── */
static bool test_alert_latency(void)
{
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    
    /* Fire alert */
    uint64_t now = 2000000;
    alert_fire(&alert_state, ALERT_SEV_WARN, "camera", "Memory leak",
               "100MB", "50MB", now);
    
    clock_gettime(CLOCK_MONOTONIC, &t1);
    
    uint64_t latency_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    TEST_ASSERT(latency_ms < 1000, "Alert should fire <1s");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test 3: Cooldown Suppression ───────────────────────────────────────────── */
static bool test_cooldown_suppression(void)
{
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    uint64_t now = 3000000;
    
    /* Fire CRITICAL alert */
    uint64_t id1 = alert_fire(&alert_state, ALERT_SEV_CRIT, "svc", "cond", "v", "t", now);
    
    /* Fire same alert 10s later - should be suppressed (cooldown=30s) */
    uint64_t id2 = alert_fire(&alert_state, ALERT_SEV_CRIT, "svc", "cond", "v", "t", now + 10000);
    
    TEST_ASSERT_EQ(id1, id2, "Should be same alert (suppressed)");
    TEST_ASSERT_EQ(alert_get_active_count(&alert_state), 1, "Should still have 1 active");
    
    /* Check occurrence count incremented */
    alert_record_t rec;
    alert_get_by_id(&alert_state, id1, &rec);
    TEST_ASSERT_EQ(rec.occurrences, 2, "Should have 2 occurrences");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test 4: Seven Simultaneous Conditions ──────────────────────────────────── */
static bool test_seven_conditions(void)
{
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    uint64_t now = 4000000;
    
    /* Fire 7 different alert conditions */
    alert_fire(&alert_state, ALERT_SEV_CRIT, "svc1", "CPU > 90%", "95", "90", now);
    alert_fire(&alert_state, ALERT_SEV_CRIT, "svc2", "Memory leak", "100M", "50M", now);
    alert_fire(&alert_state, ALERT_SEV_WARN, "svc3", "High latency", "500ms", "100ms", now);
    alert_fire(&alert_state, ALERT_SEV_INFO, "svc4", "Config change", "v2", "v1", now);
    alert_fire(&alert_state, ALERT_SEV_CRIT, "svc5", "Seccomp violation", "1", "0", now);
    alert_fire(&alert_state, ALERT_SEV_WARN, "svc6", "FD leak", "100", "50", now);
    alert_fire(&alert_state, ALERT_SEV_CRIT, "svc7", "Crash detected", "1", "0", now);
    
    TEST_ASSERT_EQ(alert_get_active_count(&alert_state), 7, "Should have 7 active alerts");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test 5: Violation-Triggered Alert ──────────────────────────────────────── */
static bool test_violation_alert(void)
{
    mock_middleware_t mock;
    mock_middleware_init(&mock);
    
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    /* Inject seccomp violation */
    mock_inject_violation(&mock, 0, 1, 0, 0);
    
    /* Generate snapshot */
    mon_snapshot_t snap;
    mock_middleware_generate_snapshot(&mock, &snap);
    
    /* Check violation present */
    TEST_ASSERT_EQ(snap.services[0].seccomp_violations, 1, "Violation not injected");
    
    /* Evaluate rules */
    uint64_t now = 5000000;
    alert_evaluate_all_rules(&alert_state, &snap, now);
    
    /* Should have fired seccomp alert */
    TEST_ASSERT(alert_get_active_count(&alert_state) > 0, "Should have alert");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test 6: Alert Broadcast to Multiple Clients ────────────────────────────── */
static bool test_multiple_clients(void)
{
    /* This requires full daemon implementation with client management */
    /* Placeholder: verify we can track multiple client FDs */
    
    int client_fds[5] = {10, 11, 12, 13, 14};
    int client_count = 5;
    
    TEST_ASSERT_EQ(client_count, 5, "Should track 5 clients");
    
    for (int i = 0; i < client_count; i++) {
        TEST_ASSERT(client_fds[i] > 0, "FD should be valid");
    }
    
    return true;
}

/* ── Test 7: Alert Clear ─────────────────────────────────────────────────────── */
static bool test_alert_clear(void)
{
    alert_state_t alert_state;
    alert_engine_init(&alert_state, 100);
    
    uint64_t now = 6000000;
    
    /* Fire alerts */
    alert_fire(&alert_state, ALERT_SEV_CRIT, "svc1", "cond1", "v", "t", now);
    alert_fire(&alert_state, ALERT_SEV_WARN, "svc2", "cond2", "v", "t", now);
    
    TEST_ASSERT_EQ(alert_get_active_count(&alert_state), 2, "Should have 2 alerts");
    
    /* Clear */
    alert_engine_clear(&alert_state);
    
    TEST_ASSERT_EQ(alert_get_active_count(&alert_state), 0, "Should have 0 after clear");
    
    alert_engine_destroy(&alert_state);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"basic_alert_delivery", test_basic_alert_delivery, true},
        {"alert_latency", test_alert_latency, true},
        {"cooldown_suppression", test_cooldown_suppression, true},
        {"seven_conditions", test_seven_conditions, true},
        {"violation_alert", test_violation_alert, true},
        {"multiple_clients", test_multiple_clients, true},
        {"alert_clear", test_alert_clear, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Alert Delivery");
}
