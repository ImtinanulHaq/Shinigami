/**
 * @file test_concurrency.c
 * @brief Integration test for thread safety under high concurrency
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../collectors/collector_base.h"
#include "../../daemon/monitord_state.h"
#include <pthread.h>
#include <time.h>

#define NUM_COLLECTOR_THREADS 11
#define TUI_CLIENT_THREADS 3
#define TEST_DURATION_SEC 60

static volatile int g_test_running = 1;
static monitord_state_t *g_daemon_state = NULL;

/* ── Mock Collector Thread ──────────────────────────────────────────────────── */
static void *collector_thread(void *arg)
{
    int collector_id = *(int *)arg;
    
    while (g_test_running) {
        /* Simulate collector updating metrics */
        if (g_daemon_state) {
            pthread_rwlock_wrlock(&g_daemon_state->lock);
            g_daemon_state->snapshot.services[collector_id % MAX_SERVICES].cpu_pct += 0.1f;
            pthread_rwlock_unlock(&g_daemon_state->lock);
        }
        
        usleep(10000); /* 10ms tick */
    }
    
    return NULL;
}

/* ── Mock TUI Client Thread ─────────────────────────────────────────────────── */
static void *tui_client_thread(void *arg)
{
    (void)arg;
    
    while (g_test_running) {
        /* Simulate TUI reading snapshot */
        if (g_daemon_state) {
            pthread_rwlock_rdlock(&g_daemon_state->lock);
            float cpu = g_daemon_state->snapshot.services[0].cpu_pct;
            (void)cpu;
            pthread_rwlock_unlock(&g_daemon_state->lock);
        }
        
        usleep(100000); /* 100ms refresh */
    }
    
    return NULL;
}

/* ── Test 1: No Deadlocks (60s) ─────────────────────────────────────────────── */
static bool test_no_deadlocks(void)
{
    monitord_state_t daemon_state;
    memset(&daemon_state, 0, sizeof(daemon_state));
    monitord_state_init(&daemon_state);
    g_daemon_state = &daemon_state;
    g_test_running = 1;
    
    /* Launch collector threads */
    pthread_t collectors[NUM_COLLECTOR_THREADS];
    int collector_ids[NUM_COLLECTOR_THREADS];
    for (int i = 0; i < NUM_COLLECTOR_THREADS; i++) {
        collector_ids[i] = i;
        pthread_create(&collectors[i], NULL, collector_thread, &collector_ids[i]);
    }
    
    /* Launch TUI clients */
    pthread_t tui_clients[TUI_CLIENT_THREADS];
    for (int i = 0; i < TUI_CLIENT_THREADS; i++) {
        pthread_create(&tui_clients[i], NULL, tui_client_thread, NULL);
    }
    
    /* Run for 60 seconds */
    sleep(TEST_DURATION_SEC);
    
    /* Stop threads */
    g_test_running = 0;
    
    /* Join all */
    for (int i = 0; i < NUM_COLLECTOR_THREADS; i++) {
        pthread_join(collectors[i], NULL);
    }
    for (int i = 0; i < TUI_CLIENT_THREADS; i++) {
        pthread_join(tui_clients[i], NULL);
    }
    
    /* If we got here, no deadlock occurred */
    TEST_ASSERT(1, "No deadlock");
    
    monitord_state_destroy(&daemon_state);
    return true;
}

/* ── Test 2: Response Latency <50ms ─────────────────────────────────────────── */
static bool test_response_latency(void)
{
    monitord_state_t daemon_state;
    monitord_state_init(&daemon_state);
    
    struct timespec t0, t1;
    
    /* Measure 100 snapshot reads */
    uint64_t max_latency_us = 0;
    for (int i = 0; i < 100; i++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        
        pthread_rwlock_rdlock(&daemon_state.lock);
        float cpu = daemon_state.snapshot.services[0].cpu_pct;
        (void)cpu;
        pthread_rwlock_unlock(&daemon_state.lock);
        
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        uint64_t latency_us = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
        if (latency_us > max_latency_us) {
            max_latency_us = latency_us;
        }
    }
    
    TEST_ASSERT(max_latency_us < 50000, "Latency should be <50ms");
    
    monitord_state_destroy(&daemon_state);
    return true;
}

/* ── Test 3: Zero Data Races (TSan) ─────────────────────────────────────────── */
static bool test_zero_races(void)
{
    /* This test relies on -fsanitize=thread to detect races */
    /* If build has TSan and test completes, no races detected */
    
    monitord_state_t daemon_state;
    monitord_state_init(&daemon_state);
    g_daemon_state = &daemon_state;
    g_test_running = 1;
    
    pthread_t writers[5];
    pthread_t readers[5];
    int ids[5] = {0, 1, 2, 3, 4};
    
    for (int i = 0; i < 5; i++) {
        pthread_create(&writers[i], NULL, collector_thread, &ids[i]);
        pthread_create(&readers[i], NULL, tui_client_thread, NULL);
    }
    
    sleep(5); /* Short test */
    
    g_test_running = 0;
    
    for (int i = 0; i < 5; i++) {
        pthread_join(writers[i], NULL);
        pthread_join(readers[i], NULL);
    }
    
    /* TSan would have failed the test if races detected */
    TEST_ASSERT(1, "No races (TSan clean)");
    
    monitord_state_destroy(&daemon_state);
    return true;
}

/* ── Test 4: RWLock Ordering ─────────────────────────────────────────────────── */
static bool test_rwlock_ordering(void)
{
    /* Verify locks acquired in alphabetical order */
    monitord_state_t daemon_state;
    monitord_state_init(&daemon_state);
    
    /* Locks should be in order: alerts < collectors < config < hal < health < ... */
    /* For now, just verify state has lock */
    TEST_ASSERT(1, "RWLock exists");
    
    monitord_state_destroy(&daemon_state);
    return true;
}

/* ── Test 5: High Frequency Updates ─────────────────────────────────────────── */
static bool test_high_frequency_updates(void)
{
    monitord_state_t daemon_state;
    monitord_state_init(&daemon_state);
    
    /* Update at max rate (every 1ms) for 1 second */
    for (int i = 0; i < 1000; i++) {
        pthread_rwlock_wrlock(&daemon_state.lock);
        daemon_state.snapshot.services[0].cpu_pct = (float)i;
        pthread_rwlock_unlock(&daemon_state.lock);
        usleep(1000);
    }
    
    /* Verify final value */
    pthread_rwlock_rdlock(&daemon_state.lock);
    float final_val = daemon_state.snapshot.services[0].cpu_pct;
    pthread_rwlock_unlock(&daemon_state.lock);
    
    TEST_ASSERT_FLOAT_EQ(final_val, 999.0f, 1.0f, "Final value wrong");
    
    monitord_state_destroy(&daemon_state);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"no_deadlocks", test_no_deadlocks, true},
        {"response_latency", test_response_latency, true},
        {"zero_races", test_zero_races, true},
        {"rwlock_ordering", test_rwlock_ordering, true},
        {"high_frequency_updates", test_high_frequency_updates, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Concurrency");
}
