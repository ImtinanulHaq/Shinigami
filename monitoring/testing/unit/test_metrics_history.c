/**
 * @file test_metrics_history.c
 * @brief Unit tests for metrics history (circular buffer)
 */
#include "../test_framework.h"
#include "../../metrics/metrics_history.h"
#include <string.h>

/* ── Test 1: Basic Push/Get ────────────────────────────────────────────────── */
static bool test_basic_push_get(void)
{
    metrics_history_t hist;
    TEST_ASSERT_EQ(metrics_history_init(&hist, 10), 0, "History init failed");
    
    /* Push 5 values */
    for (int i = 0; i < 5; i++) {
        metrics_history_push(&hist, i * 10.0);
    }
    
    /* Get latest */
    double val;
    TEST_ASSERT_EQ(metrics_history_get_latest(&hist, &val), 0, "Get latest failed");
    TEST_ASSERT_FLOAT_EQ(val, 40.0, 0.01, "Latest value wrong");
    
    /* Get N=3 */
    double buffer[3];
    size_t count = metrics_history_get_latest_n(&hist, buffer, 3);
    TEST_ASSERT_EQ(count, 3, "Get latest N returned wrong count");
    TEST_ASSERT_FLOAT_EQ(buffer[0], 40.0, 0.01, "buffer[0] wrong");
    TEST_ASSERT_FLOAT_EQ(buffer[1], 30.0, 0.01, "buffer[1] wrong");
    TEST_ASSERT_FLOAT_EQ(buffer[2], 20.0, 0.01, "buffer[2] wrong");
    
    metrics_history_destroy(&hist);
    return true;
}

/* ── Test 2: Wraparound ────────────────────────────────────────────────────── */
static bool test_wraparound(void)
{
    metrics_history_t hist;
    metrics_history_init(&hist, 5); /* capacity=5 */
    
    /* Push 10 values (double capacity) */
    for (int i = 0; i < 10; i++) {
        metrics_history_push(&hist, i * 1.0);
    }
    
    /* Should only have last 5 values: 5,6,7,8,9 */
    double buffer[5];
    size_t count = metrics_history_get_latest_n(&hist, buffer, 5);
    TEST_ASSERT_EQ(count, 5, "Should have exactly 5 values");
    
    TEST_ASSERT_FLOAT_EQ(buffer[0], 9.0, 0.01, "Latest should be 9");
    TEST_ASSERT_FLOAT_EQ(buffer[1], 8.0, 0.01, "Second should be 8");
    TEST_ASSERT_FLOAT_EQ(buffer[2], 7.0, 0.01, "Third should be 7");
    TEST_ASSERT_FLOAT_EQ(buffer[3], 6.0, 0.01, "Fourth should be 6");
    TEST_ASSERT_FLOAT_EQ(buffer[4], 5.0, 0.01, "Fifth should be 5");
    
    metrics_history_destroy(&hist);
    return true;
}

/* ── Test 3: Request More Than Available ────────────────────────────────────── */
static bool test_request_more_than_available(void)
{
    metrics_history_t hist;
    metrics_history_init(&hist, 100);
    
    /* Push only 3 values */
    metrics_history_push(&hist, 10.0);
    metrics_history_push(&hist, 20.0);
    metrics_history_push(&hist, 30.0);
    
    /* Request 50 */
    double buffer[50];
    size_t count = metrics_history_get_latest_n(&hist, buffer, 50);
    TEST_ASSERT_EQ(count, 3, "Should only return 3 values");
    
    TEST_ASSERT_FLOAT_EQ(buffer[0], 30.0, 0.01, "Latest wrong");
    TEST_ASSERT_FLOAT_EQ(buffer[1], 20.0, 0.01, "Second wrong");
    TEST_ASSERT_FLOAT_EQ(buffer[2], 10.0, 0.01, "Third wrong");
    
    metrics_history_destroy(&hist);
    return true;
}

/* ── Test 4: Empty History ──────────────────────────────────────────────────── */
static bool test_empty_history(void)
{
    metrics_history_t hist;
    metrics_history_init(&hist, 10);
    
    double val;
    int result = metrics_history_get_latest(&hist, &val);
    TEST_ASSERT_NE(result, 0, "Should fail on empty");
    
    double buffer[10];
    size_t count = metrics_history_get_latest_n(&hist, buffer, 10);
    TEST_ASSERT_EQ(count, 0, "Should return 0 for empty");
    
    metrics_history_destroy(&hist);
    return true;
}

/* ── Test 5: Capacity Calculation ───────────────────────────────────────────── */
static bool test_capacity(void)
{
    metrics_history_t hist;
    metrics_history_init(&hist, 100);
    
    TEST_ASSERT_EQ(metrics_history_get_capacity(&hist), 100, "Capacity wrong");
    TEST_ASSERT_EQ(metrics_history_get_count(&hist), 0, "Count should be 0");
    
    /* Push 50 */
    for (int i = 0; i < 50; i++) {
        metrics_history_push(&hist, i * 1.0);
    }
    
    TEST_ASSERT_EQ(metrics_history_get_capacity(&hist), 100, "Capacity changed");
    TEST_ASSERT_EQ(metrics_history_get_count(&hist), 50, "Count should be 50");
    
    /* Push 150 more (total 200) */
    for (int i = 0; i < 150; i++) {
        metrics_history_push(&hist, i * 1.0);
    }
    
    TEST_ASSERT_EQ(metrics_history_get_capacity(&hist), 100, "Capacity changed");
    TEST_ASSERT_EQ(metrics_history_get_count(&hist), 100, "Count should cap at 100");
    
    metrics_history_destroy(&hist);
    return true;
}

/* ── Test 6: Clear ───────────────────────────────────────────────────────────── */
static bool test_clear(void)
{
    metrics_history_t hist;
    metrics_history_init(&hist, 10);
    
    /* Push values */
    for (int i = 0; i < 10; i++) {
        metrics_history_push(&hist, i * 1.0);
    }
    
    TEST_ASSERT_EQ(metrics_history_get_count(&hist), 10, "Should have 10 values");
    
    /* Clear */
    metrics_history_clear(&hist);
    
    TEST_ASSERT_EQ(metrics_history_get_count(&hist), 0, "Should have 0 after clear");
    
    double val;
    TEST_ASSERT_NE(metrics_history_get_latest(&hist, &val), 0, "Should fail after clear");
    
    metrics_history_destroy(&hist);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"basic_push_get", test_basic_push_get, true},
        {"wraparound", test_wraparound, true},
        {"request_more_than_available", test_request_more_than_available, true},
        {"empty_history", test_empty_history, true},
        {"capacity", test_capacity, true},
        {"clear", test_clear, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Metrics History");
}
