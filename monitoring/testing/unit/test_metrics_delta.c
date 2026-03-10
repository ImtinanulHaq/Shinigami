/**
 * @file test_metrics_delta.c
 * @brief Unit tests for delta/rate computation
 */
#include "../test_framework.h"
#include "../../metrics/metrics_delta.h"
#include <string.h>

/* ── Test 1: Basic Rate Calculation ────────────────────────────────────────── */
static bool test_basic_rate(void)
{
    delta_state_t state;
    TEST_ASSERT_EQ(delta_init(&state), 0, "Init failed");
    
    /* First sample at t=0: counter=100 */
    delta_update(&state, "test.counter", 100.0, 0);
    
    /* Second sample at t=1000ms: counter=200 */
    delta_update(&state, "test.counter", 200.0, 1000);
    
    /* Rate should be 100/1s = 100/s */
    double rate = delta_get_rate(&state, "test.counter");
    TEST_ASSERT_FLOAT_EQ(rate, 100.0, 0.01, "Rate should be 100/s");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 2: Counter Wrap ───────────────────────────────────────────────────── */
static bool test_counter_wrap(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* First sample: counter near max (for 32-bit: 4294967200) */
    uint64_t max_val = 4294967200UL;
    delta_update(&state, "wrapping", (double)max_val, 0);
    
    /* Second sample 1s later: wrapped to 100 */
    /* This simulates a 32-bit counter overflow */
    delta_update(&state, "wrapping", 100.0, 1000);
    
    /* Should detect wrap and handle gracefully (rate ~196/s if detected) */
    /* Or return 0 if wrap detection skips */
    double rate = delta_get_rate(&state, "wrapping");
    
    /* Accept either: skip (rate=0) or correct calculation */
    TEST_ASSERT(rate >= 0.0, "Rate should be non-negative after wrap");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 3: Division by Zero Protection ────────────────────────────────────── */
static bool test_division_by_zero(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* Two samples with same timestamp */
    delta_update(&state, "test", 100.0, 5000);
    delta_update(&state, "test", 200.0, 5000); /* Same time */
    
    /* Should not crash, should return 0 or previous */
    double rate = delta_get_rate(&state, "test");
    TEST_ASSERT_FLOAT_EQ(rate, 0.0, 0.01, "Should handle zero time delta");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 4: Multiple Metrics ───────────────────────────────────────────────── */
static bool test_multiple_metrics(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* Metric A: 100 -> 300 in 1000ms = 200/s */
    delta_update(&state, "metric_a", 100.0, 0);
    delta_update(&state, "metric_a", 300.0, 1000);
    
    /* Metric B: 500 -> 700 in 1000ms = 200/s */
    delta_update(&state, "metric_b", 500.0, 0);
    delta_update(&state, "metric_b", 700.0, 1000);
    
    /* Metric C: 1000 -> 2000 in 2000ms = 500/s */
    delta_update(&state, "metric_c", 1000.0, 0);
    delta_update(&state, "metric_c", 2000.0, 2000);
    
    TEST_ASSERT_FLOAT_EQ(delta_get_rate(&state, "metric_a"), 200.0, 0.1, "A rate");
    TEST_ASSERT_FLOAT_EQ(delta_get_rate(&state, "metric_b"), 200.0, 0.1, "B rate");
    TEST_ASSERT_FLOAT_EQ(delta_get_rate(&state, "metric_c"), 500.0, 0.1, "C rate");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 5: Stable Value (Zero Rate) ───────────────────────────────────────── */
static bool test_stable_value(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* Value stays at 100 */
    delta_update(&state, "stable", 100.0, 0);
    delta_update(&state, "stable", 100.0, 1000);
    delta_update(&state, "stable", 100.0, 2000);
    delta_update(&state, "stable", 100.0, 3000);
    delta_update(&state, "stable", 100.0, 4000);
    delta_update(&state, "stable", 100.0, 5000);
    
    /* After 5 seconds of stability, rate should be 0 */
    double rate = delta_get_rate(&state, "stable");
    TEST_ASSERT_FLOAT_EQ(rate, 0.0, 0.01, "Stable value should show 0/s");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 6: High Frequency Updates ─────────────────────────────────────────── */
static bool test_high_frequency(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* Update every 100ms for 1 second */
    uint64_t time = 0;
    double counter = 0;
    for (int i = 0; i < 10; i++) {
        counter += 50; /* +50 each 100ms = 500/s */
        delta_update(&state, "high_freq", counter, time);
        time += 100;
    }
    
    double rate = delta_get_rate(&state, "high_freq");
    TEST_ASSERT_FLOAT_EQ(rate, 500.0, 10.0, "High freq rate");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 7: Negative Delta Detection ───────────────────────────────────────── */
static bool test_negative_delta(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* Counter goes down (reset or wrap) */
    delta_update(&state, "negative", 1000.0, 0);
    delta_update(&state, "negative", 500.0, 1000); /* Decrease */
    
    /* Should handle gracefully - either skip or reset */
    double rate = delta_get_rate(&state, "negative");
    TEST_ASSERT(rate >= 0.0, "Rate should not be negative");
    
    delta_destroy(&state);
    return true;
}

/* ── Test 8: Unknown Metric ─────────────────────────────────────────────────── */
static bool test_unknown_metric(void)
{
    delta_state_t state;
    delta_init(&state);
    
    /* Query metric that was never updated */
    double rate = delta_get_rate(&state, "nonexistent");
    TEST_ASSERT_FLOAT_EQ(rate, 0.0, 0.01, "Unknown metric should return 0");
    
    delta_destroy(&state);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"basic_rate", test_basic_rate, true},
        {"counter_wrap", test_counter_wrap, true},
        {"division_by_zero", test_division_by_zero, true},
        {"multiple_metrics", test_multiple_metrics, true},
        {"stable_value", test_stable_value, true},
        {"high_frequency", test_high_frequency, true},
        {"negative_delta", test_negative_delta, true},
        {"unknown_metric", test_unknown_metric, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Metrics Delta");
}
