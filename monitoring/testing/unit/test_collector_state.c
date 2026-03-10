/**
 * @file test_collector_state.c
 * @brief Unit tests for collector state machine and lifecycle
 */
#include "../test_framework.h"
#include "../../collectors/collector_base.h"
#include <string.h>
#include <unistd.h>

/* ── Mock Collector Implementation ──────────────────────────────────────────── */
static int mock_connect_called = 0;
static int mock_tick_called = 0;
static int mock_disconnect_called = 0;

static int mock_connect(collector_t *self, void *state)
{
    (void)self;
    (void)state;
    mock_connect_called++;
    return 0;
}

static int mock_tick(collector_t *self, void *state)
{
    (void)self;
    (void)state;
    mock_tick_called++;
    return 0;
}

static void mock_disconnect(collector_t *self, void *state)
{
    (void)self;
    (void)state;
    mock_disconnect_called++;
}

/* ── Test 1: Initial State ──────────────────────────────────────────────────── */
static bool test_initial_state(void)
{
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test_collector", sizeof(collector.name)-1);
    collector.interval_ms = 1000;
    collector.connect = mock_connect;
    collector.tick = mock_tick;
    collector.disconnect = mock_disconnect;
    
    TEST_ASSERT_EQ(collector_init(&collector), 0, "Init failed");
    TEST_ASSERT_EQ(collector.state, COLLECTOR_STATE_WAITING, "Should start in WAITING");
    TEST_ASSERT_EQ(collector.tick_count, 0, "Tick count should be 0");
    
    collector_destroy(&collector);
    return true;
}

/* ── Test 2: State Transitions ──────────────────────────────────────────────── */
static bool test_state_transitions(void)
{
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test", sizeof(collector.name)-1);
    collector.interval_ms = 100;
    collector.connect = mock_connect;
    collector.tick = mock_tick;
    collector.disconnect = mock_disconnect;
    
    collector_init(&collector);
    
    /* Start: WAITING -> CONNECTING */
    collector_start(&collector, NULL);
    usleep(50000); /* 50ms */
    TEST_ASSERT_EQ(collector.state, COLLECTOR_STATE_CONNECTING, "Should be CONNECTING");
    
    /* Connect succeeds: CONNECTING -> SYNCING */
    usleep(150000); /* 150ms more */
    TEST_ASSERT(collector.state == COLLECTOR_STATE_SYNCING || 
                collector.state == COLLECTOR_STATE_LIVE, "Should advance");
    
    /* After several ticks: LIVE */
    sleep(1);
    TEST_ASSERT_EQ(collector.state, COLLECTOR_STATE_LIVE, "Should be LIVE");
    
    collector_stop(&collector);
    collector_destroy(&collector);
    return true;
}

/* ── Test 3: Connect/Tick/Disconnect Called ─────────────────────────────────── */
static bool test_callbacks_called(void)
{
    mock_connect_called = 0;
    mock_tick_called = 0;
    mock_disconnect_called = 0;
    
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test", sizeof(collector.name)-1);
    collector.interval_ms = 100;
    collector.connect = mock_connect;
    collector.tick = mock_tick;
    collector.disconnect = mock_disconnect;
    
    collector_init(&collector);
    collector_start(&collector, NULL);
    
    /* Wait for connect */
    sleep(1);
    TEST_ASSERT(mock_connect_called >= 1, "Connect should be called");
    TEST_ASSERT(mock_tick_called >= 5, "Tick should be called multiple times");
    
    collector_stop(&collector);
    usleep(100000);
    TEST_ASSERT(mock_disconnect_called >= 1, "Disconnect should be called");
    
    collector_destroy(&collector);
    return true;
}

/* ── Test 4: Staggered Startup ──────────────────────────────────────────────── */
static bool test_staggered_startup(void)
{
    collector_t collectors[3];
    uint64_t start_times[3];
    
    for (int i = 0; i < 3; i++) {
        memset(&collectors[i], 0, sizeof(collector_t));
        snprintf(collectors[i].name, sizeof(collectors[i].name), "collector_%d", i);
        collectors[i].index = i;
        collectors[i].interval_ms = 1000;
        collectors[i].connect = mock_connect;
        collectors[i].tick = mock_tick;
        collectors[i].disconnect = mock_disconnect;
        
        collector_init(&collectors[i]);
    }
    
    /* Start all at once */
    uint64_t now = 0;
    for (int i = 0; i < 3; i++) {
        collector_start(&collectors[i], NULL);
        start_times[i] = now;
    }
    
    /* Each should delay by index × 200ms */
    /* Collector 0: 0ms, Collector 1: 200ms, Collector 2: 400ms */
    usleep(100000); /* 100ms */
    TEST_ASSERT_EQ(collectors[0].state, COLLECTOR_STATE_CONNECTING, "Collector 0 should start");
    TEST_ASSERT_EQ(collectors[1].state, COLLECTOR_STATE_WAITING, "Collector 1 should wait");
    TEST_ASSERT_EQ(collectors[2].state, COLLECTOR_STATE_WAITING, "Collector 2 should wait");
    
    usleep(150000); /* +150ms = 250ms total */
    TEST_ASSERT_NE(collectors[1].state, COLLECTOR_STATE_WAITING, "Collector 1 should start");
    TEST_ASSERT_EQ(collectors[2].state, COLLECTOR_STATE_WAITING, "Collector 2 still waiting");
    
    usleep(200000); /* +200ms = 450ms total */
    TEST_ASSERT_NE(collectors[2].state, COLLECTOR_STATE_WAITING, "Collector 2 should start");
    
    for (int i = 0; i < 3; i++) {
        collector_stop(&collectors[i]);
        collector_destroy(&collectors[i]);
    }
    
    return true;
}

/* ── Test 5: Tick Count Increment ───────────────────────────────────────────── */
static bool test_tick_count(void)
{
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test", sizeof(collector.name)-1);
    collector.interval_ms = 100;
    collector.connect = mock_connect;
    collector.tick = mock_tick;
    collector.disconnect = mock_disconnect;
    
    collector_init(&collector);
    TEST_ASSERT_EQ(collector.tick_count, 0, "Should start at 0");
    
    collector_start(&collector, NULL);
    sleep(1); /* 1 second at 100ms interval = ~10 ticks */
    
    TEST_ASSERT(collector.tick_count >= 8, "Should have ~10 ticks");
    
    collector_stop(&collector);
    collector_destroy(&collector);
    return true;
}

/* ── Test 6: Stop Before Start ──────────────────────────────────────────────── */
static bool test_stop_before_start(void)
{
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test", sizeof(collector.name)-1);
    collector.interval_ms = 1000;
    collector.connect = mock_connect;
    collector.tick = mock_tick;
    collector.disconnect = mock_disconnect;
    
    collector_init(&collector);
    
    /* Stop without starting - should not crash */
    collector_stop(&collector);
    
    TEST_ASSERT_EQ(collector.state, COLLECTOR_STATE_WAITING, "Should stay WAITING");
    
    collector_destroy(&collector);
    return true;
}

/* ── Test 7: Restart After Stop ─────────────────────────────────────────────── */
static bool test_restart(void)
{
    collector_t collector;
    memset(&collector, 0, sizeof(collector));
    strncpy(collector.name, "test", sizeof(collector.name)-1);
    collector.interval_ms = 200;
    collector.connect = mock_connect;
    collector.tick = mock_tick;
    collector.disconnect = mock_disconnect;
    
    collector_init(&collector);
    
    /* Start, run, stop */
    collector_start(&collector, NULL);
    sleep(1);
    uint64_t first_tick_count = collector.tick_count;
    collector_stop(&collector);
    
    /* Restart */
    collector_start(&collector, NULL);
    sleep(1);
    uint64_t second_tick_count = collector.tick_count;
    
    TEST_ASSERT(second_tick_count > first_tick_count, "Should accumulate ticks");
    
    collector_stop(&collector);
    collector_destroy(&collector);
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"initial_state", test_initial_state, true},
        {"state_transitions", test_state_transitions, true},
        {"callbacks_called", test_callbacks_called, true},
        {"staggered_startup", test_staggered_startup, true},
        {"tick_count", test_tick_count, true},
        {"stop_before_start", test_stop_before_start, true},
        {"restart", test_restart, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Collector State");
}
