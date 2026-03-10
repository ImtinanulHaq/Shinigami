/**
 * @file test_trace_store.c
 * @brief Unit tests for trace storage and retrieval
 */
#include "../test_framework.h"
#include "../../tracing/trace_store.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include <string.h>

/* ── Test 1: Basic Add and Retrieve ────────────────────────────────────────── */
static bool test_basic_add_retrieve(void)
{
    trace_store_t store;
    TEST_ASSERT_EQ(trace_store_init(&store, 100), 0, "Init failed");
    
    /* Add trace */
    trace_record_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.trace_id = 1;
    strncpy(trace.component, "audio_service", sizeof(trace.component)-1);
    trace.enter_us = 1000;
    trace.exit_us = 2000;
    trace.status = 0;
    
    TEST_ASSERT_EQ(trace_store_add(&store, &trace), 0, "Add failed");
    
    /* Retrieve */
    trace_record_t retrieved;
    TEST_ASSERT_EQ(trace_store_get_by_id(&store, 1, &retrieved), 0, "Get failed");
    TEST_ASSERT_EQ(retrieved.trace_id, 1, "Trace ID wrong");
    TEST_ASSERT_STR_EQ(retrieved.component, "audio_service", "Component wrong");
    TEST_ASSERT_EQ(retrieved.enter_us, 1000, "Enter time wrong");
    TEST_ASSERT_EQ(retrieved.exit_us, 2000, "Exit time wrong");
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test 2: Store 1000 Traces ──────────────────────────────────────────────── */
static bool test_thousand_traces(void)
{
    trace_store_t store;
    trace_store_init(&store, 1000);
    
    /* Add 1000 traces */
    for (uint64_t i = 0; i < 1000; i++) {
        trace_record_t trace;
        memset(&trace, 0, sizeof(trace));
        trace.trace_id = i;
        snprintf(trace.component, sizeof(trace.component), "component_%lu", i);
        trace.enter_us = i * 1000;
        trace.exit_us = (i+1) * 1000;
        
        TEST_ASSERT_EQ(trace_store_add(&store, &trace), 0, "Add trace failed");
    }
    
    /* Verify all retrievable */
    for (uint64_t i = 0; i < 1000; i++) {
        trace_record_t retrieved;
        TEST_ASSERT_EQ(trace_store_get_by_id(&store, i, &retrieved), 0, "Get failed");
        TEST_ASSERT_EQ(retrieved.trace_id, i, "Wrong trace retrieved");
    }
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test 3: Eviction on Overflow ───────────────────────────────────────────── */
static bool test_eviction(void)
{
    trace_store_t store;
    trace_store_init(&store, 10); /* Small capacity */
    
    /* Add 15 traces (exceeds capacity) */
    for (uint64_t i = 0; i < 15; i++) {
        trace_record_t trace;
        memset(&trace, 0, sizeof(trace));
        trace.trace_id = i;
        trace.enter_us = i * 1000;
        trace.exit_us = (i+1) * 1000;
        trace_store_add(&store, &trace);
    }
    
    /* First 5 should be evicted (0-4), last 10 should remain (5-14) */
    trace_record_t retrieved;
    TEST_ASSERT_NE(trace_store_get_by_id(&store, 0, &retrieved), 0, "Trace 0 should be evicted");
    TEST_ASSERT_NE(trace_store_get_by_id(&store, 4, &retrieved), 0, "Trace 4 should be evicted");
    TEST_ASSERT_EQ(trace_store_get_by_id(&store, 5, &retrieved), 0, "Trace 5 should exist");
    TEST_ASSERT_EQ(trace_store_get_by_id(&store, 14, &retrieved), 0, "Trace 14 should exist");
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test 4: Get Latest N ────────────────────────────────────────────────────── */
static bool test_get_latest_n(void)
{
    trace_store_t store;
    trace_store_init(&store, 100);
    
    /* Add 20 traces */
    for (uint64_t i = 0; i < 20; i++) {
        trace_record_t trace;
        memset(&trace, 0, sizeof(trace));
        trace.trace_id = i;
        trace.enter_us = i * 1000;
        trace.exit_us = (i+1) * 1000;
        trace_store_add(&store, &trace);
    }
    
    /* Get latest 5 */
    trace_record_t buffer[5];
    size_t count = trace_store_get_latest_n(&store, buffer, 5);
    TEST_ASSERT_EQ(count, 5, "Should return 5 traces");
    
    /* Should be traces 15-19 in reverse order */
    TEST_ASSERT_EQ(buffer[0].trace_id, 19, "Buffer[0] should be 19");
    TEST_ASSERT_EQ(buffer[1].trace_id, 18, "Buffer[1] should be 18");
    TEST_ASSERT_EQ(buffer[2].trace_id, 17, "Buffer[2] should be 17");
    TEST_ASSERT_EQ(buffer[3].trace_id, 16, "Buffer[3] should be 16");
    TEST_ASSERT_EQ(buffer[4].trace_id, 15, "Buffer[4] should be 15");
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test 5: Duration Calculation ───────────────────────────────────────────── */
static bool test_duration_calculation(void)
{
    trace_store_t store;
    trace_store_init(&store, 10);
    
    trace_record_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.trace_id = 100;
    trace.enter_us = 5000;  /* 5ms */
    trace.exit_us = 12000;  /* 12ms */
    trace_store_add(&store, &trace);
    
    trace_record_t retrieved;
    trace_store_get_by_id(&store, 100, &retrieved);
    
    uint64_t duration = retrieved.exit_us - retrieved.enter_us;
    TEST_ASSERT_EQ(duration, 7000, "Duration should be 7ms");
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test 6: Clear Store ─────────────────────────────────────────────────────── */
static bool test_clear_store(void)
{
    trace_store_t store;
    trace_store_init(&store, 10);
    
    /* Add traces */
    for (uint64_t i = 0; i < 5; i++) {
        trace_record_t trace;
        memset(&trace, 0, sizeof(trace));
        trace.trace_id = i;
        trace_store_add(&store, &trace);
    }
    
    /* Clear */
    trace_store_clear(&store);
    
    /* Verify all gone */
    trace_record_t retrieved;
    TEST_ASSERT_NE(trace_store_get_by_id(&store, 0, &retrieved), 0, "Should be cleared");
    TEST_ASSERT_NE(trace_store_get_by_id(&store, 4, &retrieved), 0, "Should be cleared");
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test 7: Out-of-Order Traces ─────────────────────────────────────────────── */
static bool test_out_of_order(void)
{
    trace_store_t store;
    trace_store_init(&store, 100);
    
    /* Add traces out of order */
    trace_record_t trace;
    
    memset(&trace, 0, sizeof(trace));
    trace.trace_id = 5;
    trace.enter_us = 5000;
    trace.exit_us = 6000;
    trace_store_add(&store, &trace);
    
    memset(&trace, 0, sizeof(trace));
    trace.trace_id = 2;
    trace.enter_us = 2000;
    trace.exit_us = 3000;
    trace_store_add(&store, &trace);
    
    memset(&trace, 0, sizeof(trace));
    trace.trace_id = 8;
    trace.enter_us = 8000;
    trace.exit_us = 9000;
    trace_store_add(&store, &trace);
    
    /* Verify all retrievable by ID */
    trace_record_t retrieved;
    TEST_ASSERT_EQ(trace_store_get_by_id(&store, 5, &retrieved), 0, "Trace 5");
    TEST_ASSERT_EQ(trace_store_get_by_id(&store, 2, &retrieved), 0, "Trace 2");
    TEST_ASSERT_EQ(trace_store_get_by_id(&store, 8, &retrieved), 0, "Trace 8");
    
    trace_store_destroy(&store);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"basic_add_retrieve", test_basic_add_retrieve, true},
        {"thousand_traces", test_thousand_traces, true},
        {"eviction", test_eviction, true},
        {"get_latest_n", test_get_latest_n, true},
        {"duration_calculation", test_duration_calculation, true},
        {"clear_store", test_clear_store, true},
        {"out_of_order", test_out_of_order, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Trace Store");
}
