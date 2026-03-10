/**
 * @file test_metrics_registry.c
 * @brief Unit tests for metrics registry
 */
#include "../test_framework.h"
#include "../../metrics/metrics_registry.h"
#include <pthread.h>

/* ── Test 1: Register and Lookup ──────────────────────────────────────────── */
static bool test_register_and_lookup(void)
{
    metrics_registry_t reg;
    TEST_ASSERT_EQ(metrics_registry_init(&reg), 0, "Registry init failed");
    
    /* Register gauge */
    metric_t *gauge = metrics_registry_register(&reg, "test.gauge", METRIC_TYPE_GAUGE);
    TEST_ASSERT_NOT_NULL(gauge, "Gauge registration failed");
    TEST_ASSERT_EQ(gauge->type, METRIC_TYPE_GAUGE, "Gauge type mismatch");
    
    /* Register counter */
    metric_t *counter = metrics_registry_register(&reg, "test.counter", METRIC_TYPE_COUNTER);
    TEST_ASSERT_NOT_NULL(counter, "Counter registration failed");
    TEST_ASSERT_EQ(counter->type, METRIC_TYPE_COUNTER, "Counter type mismatch");
    
    /* Register histogram */
    metric_t *hist = metrics_registry_register(&reg, "test.histogram", METRIC_TYPE_HISTOGRAM);
    TEST_ASSERT_NOT_NULL(hist, "Histogram registration failed");
    TEST_ASSERT_EQ(hist->type, METRIC_TYPE_HISTOGRAM, "Histogram type mismatch");
    
    /* Lookup by name */
    metric_t *found = metrics_registry_lookup(&reg, "test.gauge");
    TEST_ASSERT_NOT_NULL(found, "Gauge lookup failed");
    TEST_ASSERT_EQ(found->type, METRIC_TYPE_GAUGE, "Looked up gauge has wrong type");
    TEST_ASSERT_EQ(found, gauge, "Lookup returned wrong pointer");
    
    metrics_registry_destroy(&reg);
    return true;
}

/* ── Test 2: Duplicate Name ────────────────────────────────────────────────── */
static bool test_duplicate_name(void)
{
    metrics_registry_t reg;
    metrics_registry_init(&reg);
    
    metric_t *m1 = metrics_registry_register(&reg, "duplicate", METRIC_TYPE_GAUGE);
    TEST_ASSERT_NOT_NULL(m1, "First registration failed");
    
    metric_t *m2 = metrics_registry_register(&reg, "duplicate", METRIC_TYPE_GAUGE);
    TEST_ASSERT_NULL(m2, "Duplicate registration should have failed");
    
    metrics_registry_destroy(&reg);
    return true;
}

/* ── Test 3: 1000 Metrics ──────────────────────────────────────────────────── */
static bool test_thousand_metrics(void)
{
    metrics_registry_t reg;
    metrics_registry_init(&reg);
    
    /* Register 1000 metrics */
    for (int i = 0; i < 1000; i++) {
        char name[64];
        snprintf(name, sizeof(name), "metric_%d", i);
        metric_t *m = metrics_registry_register(&reg, name, METRIC_TYPE_GAUGE);
        TEST_ASSERT_NOT_NULL(m, "Failed to register metric");
    }
    
    /* Verify all retrievable */
    for (int i = 0; i < 1000; i++) {
        char name[64];
        snprintf(name, sizeof(name), "metric_%d", i);
        metric_t *m = metrics_registry_lookup(&reg, name);
        TEST_ASSERT_NOT_NULL(m, "Failed to lookup metric");
    }
    
    metrics_registry_destroy(&reg);
    return true;
}

/* ── Test 4: Unregister ────────────────────────────────────────────────────── */
static bool test_unregister(void)
{
    metrics_registry_t reg;
    metrics_registry_init(&reg);
    
    metrics_registry_register(&reg, "metric_a", METRIC_TYPE_GAUGE);
    metrics_registry_register(&reg, "metric_b", METRIC_TYPE_GAUGE);
    metrics_registry_register(&reg, "metric_c", METRIC_TYPE_GAUGE);
    
    /* Unregister middle one */
    int result = metrics_registry_unregister(&reg, "metric_b");
    TEST_ASSERT_EQ(result, 0, "Unregister failed");
    
    /* Verify it's gone */
    metric_t *m = metrics_registry_lookup(&reg, "metric_b");
    TEST_ASSERT_NULL(m, "Unregistered metric still found");
    
    /* Verify others unaffected */
    TEST_ASSERT_NOT_NULL(metrics_registry_lookup(&reg, "metric_a"), "metric_a affected");
    TEST_ASSERT_NOT_NULL(metrics_registry_lookup(&reg, "metric_c"), "metric_c affected");
    
    metrics_registry_destroy(&reg);
    return true;
}

/* ── Test 5: Thread Safety ──────────────────────────────────────────────────── */
#define NUM_THREADS 16
#define METRICS_PER_THREAD 64

static metrics_registry_t *g_test_reg;

static void *thread_register_metrics(void *arg)
{
    int thread_id = *(int *)arg;
    
    for (int i = 0; i < METRICS_PER_THREAD; i++) {
        char name[64];
        snprintf(name, sizeof(name), "thread_%d_metric_%d", thread_id, i);
        metrics_registry_register(g_test_reg, name, METRIC_TYPE_GAUGE);
    }
    
    return NULL;
}

static bool test_thread_safety(void)
{
    metrics_registry_t reg;
    metrics_registry_init(&reg);
    g_test_reg = &reg;
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    /* Launch threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, thread_register_metrics, &thread_ids[i]);
    }
    
    /* Join threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Verify all metrics registered */
    for (int t = 0; t < NUM_THREADS; t++) {
        for (int i = 0; i < METRICS_PER_THREAD; i++) {
            char name[64];
            snprintf(name, sizeof(name), "thread_%d_metric_%d", t, i);
            metric_t *m = metrics_registry_lookup(&reg, name);
            TEST_ASSERT_NOT_NULL(m, "Metric from threaded registration not found");
        }
    }
    
    metrics_registry_destroy(&reg);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"register_and_lookup", test_register_and_lookup, true},
        {"duplicate_name", test_duplicate_name, true},
        {"thousand_metrics", test_thousand_metrics, true},
        {"unregister", test_unregister, true},
        {"thread_safety", test_thread_safety, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Metrics Registry");
}
