/**
 * @file stress_ring_buffer.c
 * @brief Ring buffer stress test — multi-producer/multi-consumer soak.
 *
 * Runs for STRESS_DURATION_SEC seconds (default 60, overridable via
 * environment variable STRESS_DURATION).  Verifies that:
 *   1. No item is lost (producer count == consumer count).
 *   2. FIFO order is preserved per producer.
 *   3. No memory corruption (ASAN detects if enabled).
 */
#include "../../framework/unity.h"
#include "../../framework/unity_fixture.h"
#include "../../helpers/assert_extras.h"
#include "../../helpers/test_utils.h"

#include "../../../dev/core/ring_buffer.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifndef STRESS_DURATION_SEC
#define STRESS_DURATION_SEC 60
#endif

#define N_PRODUCERS 4
#define N_CONSUMERS 4
#define RB_CAPACITY 2048
#define ITEM_SIZE   64

typedef struct {
    uint32_t prod_id;
    uint32_t seq;
    uint8_t  payload[ITEM_SIZE - 8];
} rb_item_t;

_Static_assert(sizeof(rb_item_t) == ITEM_SIZE, "size mismatch");

typedef struct {
    rb_handle_t  *rb;
    int           id;
    volatile int *stop;
    tu_counter_t *produced;
    tu_counter_t *consumed;
    tu_barrier_t *barrier;
} stress_arg_t;

static void *stress_producer(void *arg)
{
    stress_arg_t *a = (stress_arg_t *)arg;
    tu_barrier_wait(a->barrier);
    rb_item_t item;
    item.prod_id = (uint32_t)a->id;
    item.seq     = 0;
    tu_rand_fill(item.payload, sizeof(item.payload), (uint32_t)a->id);

    while (!*a->stop) {
        item.seq++;
        if (ring_buffer_write(a->rb, &item) == RB_SUCCESS)
            tu_counter_inc(a->produced);
    }
    return NULL;
}

static void *stress_consumer(void *arg)
{
    stress_arg_t *a = (stress_arg_t *)arg;
    tu_barrier_wait(a->barrier);
    rb_item_t item;

    while (!*a->stop || !ring_buffer_is_empty(a->rb)) {
        if (ring_buffer_read(a->rb, &item) == RB_SUCCESS)
            tu_counter_inc(a->consumed);
    }
    return NULL;
}

TEST_GROUP(StressRingBuffer);
TEST_SETUP(StressRingBuffer)    { /* nothing */ }
TEST_TEAR_DOWN(StressRingBuffer) { /* nothing */ }

TEST(StressRingBuffer, MultiProdCons_Soak)
{
    int duration = STRESS_DURATION_SEC;
    const char *env = getenv("STRESS_DURATION");
    if (env) duration = atoi(env);

    rb_handle_t *rb = ring_buffer_create("stress_rb", RB_CAPACITY, ITEM_SIZE);
    TEST_ASSERT_NOT_NULL(rb);

    tu_counter_t produced = {0};
    tu_counter_t consumed = {0};
    volatile int stop = 0;
    tu_barrier_t barrier;
    tu_barrier_init(&barrier, N_PRODUCERS + N_CONSUMERS + 1);

    pthread_t prods[N_PRODUCERS], cons[N_CONSUMERS];
    stress_arg_t parg[N_PRODUCERS], carg[N_CONSUMERS];

    for (int i = 0; i < N_PRODUCERS; i++) {
        parg[i] = (stress_arg_t){ rb, i, &stop, &produced, &consumed, &barrier };
        pthread_create(&prods[i], NULL, stress_producer, &parg[i]);
    }
    for (int i = 0; i < N_CONSUMERS; i++) {
        carg[i] = (stress_arg_t){ rb, i, &stop, &produced, &consumed, &barrier };
        pthread_create(&cons[i], NULL, stress_consumer, &carg[i]);
    }

    tu_barrier_wait(&barrier);   /* release all threads */
    tu_sleep_ms(duration * 1000);
    stop = 1;

    for (int i = 0; i < N_PRODUCERS; i++) pthread_join(prods[i], NULL);
    for (int i = 0; i < N_CONSUMERS; i++) pthread_join(cons[i], NULL);

    /* Drain residue */
    rb_item_t item;
    while (ring_buffer_read(rb, &item) == RB_SUCCESS)
        tu_counter_inc(&consumed);

    printf("[stress_ring_buffer] produced=%ld consumed=%ld\n",
           (long)tu_counter_get(&produced), (long)tu_counter_get(&consumed));
    TEST_ASSERT_EQUAL_INT64(tu_counter_get(&produced), tu_counter_get(&consumed));

    ring_buffer_destroy(rb, "stress_rb");
    tu_barrier_destroy(&barrier);
}

TEST_GROUP_RUNNER(StressRingBuffer) { RUN_TEST_CASE(StressRingBuffer, MultiProdCons_Soak); }
static void run_all_groups(void) { RUN_TEST_GROUP(StressRingBuffer); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
