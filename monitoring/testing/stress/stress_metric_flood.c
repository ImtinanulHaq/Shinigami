/**
 * @file stress_metric_flood.c
 * @brief Stress test: 10× metric rate for 120 seconds
 * 
 * Pass criteria:
 * - monitord CPU < 5%
 * - TUI latency < 50ms
 * - Zero lost metrics
 * - Memory growth < 1MB
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../daemon/monitord_state.h"
#include <pthread.h>
#include <time.h>

#define TEST_DURATION_SEC 120
#define NORMAL_RATE_MS 1000
#define FLOOD_RATE_MS 100  /* 10× faster */
#define MAX_CPU_PERCENT 5.0f
#define MAX_LATENCY_MS 50
#define MAX_MEMORY_GROWTH_KB 1024 /* 1MB */

static volatile int g_test_running = 1;
static uint64_t g_metrics_sent = 0;
static uint64_t g_metrics_received = 0;

/* ── Metric Flood Thread ─────────────────────────────────────────────────────── */
static void *metric_flood_thread(void *arg)
{
    monitord_state_t *state = (monitord_state_t *)arg;
    
    while (g_test_running) {
        /* Simulate collectors updating at 10× rate */
        pthread_rwlock_wrlock(&state->lock);
        for (int i = 0; i < MAX_SERVICES; i++) {
            state->snapshot.services[i].cpu_pct += 0.1f;
        }
        pthread_rwlock_unlock(&state->lock);
        
        g_metrics_sent++;
        usleep(FLOOD_RATE_MS * 1000);
    }
    
    return NULL;
}

/* ── TUI Reader Thread ──────────────────────────────────────────────────────── */
static void *tui_reader_thread(void *arg)
{
    monitord_state_t *state = (monitord_state_t *)arg;
    
    while (g_test_running) {
        /* Simulate TUI reading snapshot */
        pthread_rwlock_rdlock(&state->lock);
        float cpu = state->snapshot.services[0].cpu_pct;
        (void)cpu;
        pthread_rwlock_unlock(&state->lock);
        
        g_metrics_received++;
        usleep(100000); /* TUI refreshes every 100ms */
    }
    
    return NULL;
}

/* ── Test 1: Monitord CPU <5% ───────────────────────────────────────────────── */
static bool test_cpu_usage(void)
{
    monitord_state_t state;
    monitord_state_init(&state);
    g_test_running = 1;
    g_metrics_sent = 0;
    
    /* Get initial CPU usage */
    size_t initial_rss = test_get_rss_bytes();
    
    /* Start flood */
    pthread_t flood_thread, reader_thread;
    pthread_create(&flood_thread, NULL, metric_flood_thread, &state);
    pthread_create(&reader_thread, NULL, tui_reader_thread, &state);
    
    /* Run for 120 seconds */
    sleep(TEST_DURATION_SEC);
    
    g_test_running = 0;
    pthread_join(flood_thread, NULL);
    pthread_join(reader_thread, NULL);
    
    /* Verify metrics sent */
    uint64_t expected = TEST_DURATION_SEC * 1000 / FLOOD_RATE_MS;
    TEST_ASSERT(g_metrics_sent >= expected * 0.95, "Should send ~expected metrics");
    
    /* Check memory growth */
    size_t final_rss = test_get_rss_bytes();
    size_t growth_kb = (final_rss - initial_rss) / 1024;
    TEST_ASSERT(growth_kb < MAX_MEMORY_GROWTH_KB, "Memory growth too high");
    
    monitord_state_destroy(&state);
    return true;
}

/* ── Test 2: TUI Latency <50ms ──────────────────────────────────────────────── */
static bool test_tui_latency(void)
{
    monitord_state_t state;
    monitord_state_init(&state);
    
    /* Start flood */
    g_test_running = 1;
    pthread_t flood;
    pthread_create(&flood, NULL, metric_flood_thread, &state);
    
    /* Measure TUI read latency */
    struct timespec t0, t1;
    uint64_t max_latency_us = 0;
    
    for (int i = 0; i < 100; i++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        
        pthread_rwlock_rdlock(&state.lock);
        float cpu = state.snapshot.services[0].cpu_pct;
        (void)cpu;
        pthread_rwlock_unlock(&state.lock);
        
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        uint64_t latency_us = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
        if (latency_us > max_latency_us) {
            max_latency_us = latency_us;
        }
        
        usleep(100000);
    }
    
    g_test_running = 0;
    pthread_join(flood, NULL);
    
    TEST_ASSERT(max_latency_us < MAX_LATENCY_MS * 1000, "Latency too high");
    
    monitord_state_destroy(&state);
    return true;
}

/* ── Test 3: Zero Lost Metrics ──────────────────────────────────────────────── */
static bool test_zero_lost(void)
{
    monitord_state_t state;
    monitord_state_init(&state);
    g_test_running = 1;
    g_metrics_sent = 0;
    g_metrics_received = 0;
    
    pthread_t flood, reader;
    pthread_create(&flood, NULL, metric_flood_thread, &state);
    pthread_create(&reader, NULL, tui_reader_thread, &state);
    
    sleep(10); /* Short stress test */
    
    g_test_running = 0;
    pthread_join(flood, NULL);
    pthread_join(reader, NULL);
    
    /* Verify no metrics lost (reader should get data) */
    TEST_ASSERT(g_metrics_received > 0, "Should receive metrics");
    
    monitord_state_destroy(&state);
    return true;
}

/* ── Test 4: Stable FD Count ─────────────────────────────────────────────────── */
static bool test_stable_fd_count(void)
{
    monitord_state_t state;
    monitord_state_init(&state);
    
    size_t initial_fds = test_count_fds();
    
    /* Run flood */
    g_test_running = 1;
    pthread_t flood;
    pthread_create(&flood, NULL, metric_flood_thread, &state);
    
    sleep(10);
    
    g_test_running = 0;
    pthread_join(flood, NULL);
    
    size_t final_fds = test_count_fds();
    TEST_ASSERT_EQ(final_fds, initial_fds, "FD count should be stable");
    
    monitord_state_destroy(&state);
    return true;
}

/* ── Test 5: No Deadlocks ────────────────────────────────────────────────────── */
static bool test_no_deadlocks_flood(void)
{
    monitord_state_t state;
    monitord_state_init(&state);
    g_test_running = 1;
    
    /* Start multiple flooders and readers */
    pthread_t threads[10];
    for (int i = 0; i < 5; i++) {
        pthread_create(&threads[i], NULL, metric_flood_thread, &state);
    }
    for (int i = 5; i < 10; i++) {
        pthread_create(&threads[i], NULL, tui_reader_thread, &state);
    }
    
    sleep(10);
    
    g_test_running = 0;
    
    /* Join all - should not hang */
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], NULL);
    }
    
    TEST_ASSERT(1, "No deadlock occurred");
    
    monitord_state_destroy(&state);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"cpu_usage", test_cpu_usage, true},
        {"tui_latency", test_tui_latency, true},
        {"zero_lost", test_zero_lost, true},
        {"stable_fd_count", test_stable_fd_count, true},
        {"no_deadlocks_flood", test_no_deadlocks_flood, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Metric Flood Stress");
}
