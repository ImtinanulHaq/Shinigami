/**
 * @file stress_full_system.c
 * @brief Full system stress test — all components running simultaneously.
 *
 * Ring buffer producers (mock HAL), consumers (mock services),
 * SM registry writers/readers, all hammering concurrently for
 * STRESS_DURATION seconds.  Asserts zero data loss and zero
 * registry corruption.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hal.h"
#include "../../../dev/hal/interface/hal_interface.h"

#include "../../../dev/core/ring_buffer.h"
#include "../../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../../dev/core/service_manager/infrastructure/sm_protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef STRESS_DURATION_SEC
#define STRESS_DURATION_SEC 60
#endif

#define N_SERVICES   4
#define RB_CAPACITY  RING_BUFFER_MAX_CAPACITY  /* max allowed = 1024 */
#define ITEM_SIZE    64

typedef struct {
    rb_handle_t  *rb;
    mock_hal_state_t hal;
    volatile int *stop;
    tu_barrier_t *barrier;
    tu_counter_t *produced;
} fs_prod_arg_t;

typedef struct {
    rb_handle_t  *rb;
    volatile int *stop;
    tu_barrier_t *barrier;
    tu_counter_t *consumed;
} fs_cons_arg_t;

typedef struct {
    int          id;
    volatile int *stop;
    tu_barrier_t *barrier;
    tu_counter_t *ops;
} fs_sm_arg_t;

/* Mutex to serialise concurrent ring_buffer_write (SPMC not MPMC) */
static pthread_mutex_t g_fs_write_mu = PTHREAD_MUTEX_INITIALIZER;

static void *fs_producer(void *arg)
{
    fs_prod_arg_t *a = (fs_prod_arg_t *)arg;
    tu_barrier_wait(a->barrier);
    uint8_t buf[ITEM_SIZE];
    uint32_t seed = 0xCAFEBABE;
    while (!*a->stop) {
        tu_rand_fill(buf, sizeof(buf), seed++);
        mock_hal_read(&a->hal, buf, sizeof(buf));
        pthread_mutex_lock(&g_fs_write_mu);
        int rbr = ring_buffer_write(a->rb, buf);
        pthread_mutex_unlock(&g_fs_write_mu);
        if (rbr == RB_SUCCESS)
            tu_counter_inc(a->produced);
    }
    return NULL;
}

static void *fs_consumer(void *arg)
{
    fs_cons_arg_t *a = (fs_cons_arg_t *)arg;
    tu_barrier_wait(a->barrier);
    uint8_t buf[ITEM_SIZE];
    while (!*a->stop || !ring_buffer_is_empty(a->rb)) {
        pthread_mutex_lock(&g_fs_write_mu);
        int frr = ring_buffer_read(a->rb, buf);
        pthread_mutex_unlock(&g_fs_write_mu);
        if (frr == RB_SUCCESS)
            tu_counter_inc(a->consumed);
    }
    return NULL;
}

static void *fs_sm_worker(void *arg)
{
    fs_sm_arg_t *a = (fs_sm_arg_t *)arg;
    tu_barrier_wait(a->barrier);

    char name[SM_MAX_NAME];
    snprintf(name, sizeof(name), "fs_svc_%d", a->id);

    while (!*a->stop) {
        service_entry_t e;
        memset(&e, 0, sizeof(e));
        strncpy(e.name, name, sizeof(e.name) - 1);
        snprintf(e.socket_path, sizeof(e.socket_path), "/tmp/fs%d.sock", a->id);
        e.pid    = (pid_t)(20000 + a->id);
        e.status = SERVICE_RUNNING;

        if (sm_registry_add(&e) == 0) {
            tu_counter_inc(a->ops);
            sm_registry_update_heartbeat(name);
            sm_registry_remove(name);
        }
    }
    return NULL;
}

static rb_handle_t     *g_rb;
static mock_hal_state_t g_hals[N_SERVICES];

TEST_GROUP(StressFullSystem);
TEST_SETUP(StressFullSystem)
{
    sm_registry_init();
    g_rb = ring_buffer_create("fs_stress_rb", RB_CAPACITY, ITEM_SIZE);
    TEST_ASSERT_NOT_NULL(g_rb);
    for (int i = 0; i < N_SERVICES; i++) {
        mock_hal_reset(&g_hals[i]);
        g_hals[i].open_ret = g_hals[i].read_ret = HAL_SUCCESS;
        mock_hal_open(&g_hals[i]);
    }
}

TEST_TEAR_DOWN(StressFullSystem)
{
    for (int i = 0; i < N_SERVICES; i++) mock_hal_close(&g_hals[i]);
    ring_buffer_destroy(g_rb, "fs_stress_rb");
    sm_registry_cleanup();
}

TEST(StressFullSystem, AllComponents_ConcurrentSoak)
{
    int duration = STRESS_DURATION_SEC;
    const char *env = getenv("STRESS_DURATION");
    if (env) duration = atoi(env);

    volatile int stop = 0;
    int total_threads = N_SERVICES * 3; /* prod + cons + sm per "service" */
    tu_barrier_t barrier;
    tu_barrier_init(&barrier, total_threads + 1);

    tu_counter_t produced = {0};
    tu_counter_t consumed = {0};
    tu_counter_t sm_ops   = {0};

    pthread_t      ptids[N_SERVICES], ctids[N_SERVICES], smtids[N_SERVICES];
    fs_prod_arg_t  pargs[N_SERVICES];
    fs_cons_arg_t  cargs[N_SERVICES];
    fs_sm_arg_t    sargs[N_SERVICES];

    for (int i = 0; i < N_SERVICES; i++) {
        pargs[i] = (fs_prod_arg_t){ g_rb, g_hals[i], &stop, &barrier, &produced };
        cargs[i] = (fs_cons_arg_t){ g_rb, &stop, &barrier, &consumed };
        sargs[i] = (fs_sm_arg_t){ i, &stop, &barrier, &sm_ops };
        pthread_create(&ptids[i],  NULL, fs_producer,  &pargs[i]);
        pthread_create(&ctids[i],  NULL, fs_consumer,  &cargs[i]);
        pthread_create(&smtids[i], NULL, fs_sm_worker, &sargs[i]);
    }

    tu_barrier_wait(&barrier);
    tu_sleep_ms(duration * 1000);
    stop = 1;

    for (int i = 0; i < N_SERVICES; i++) {
        pthread_join(ptids[i],  NULL);
        pthread_join(ctids[i],  NULL);
        pthread_join(smtids[i], NULL);
    }

    /* Drain residual items */
    uint8_t tmp[ITEM_SIZE];
    while (ring_buffer_read(g_rb, tmp) == RB_SUCCESS)
        tu_counter_inc(&consumed);

    printf("[stress_full_system] produced=%ld consumed=%ld sm_ops=%ld registry=%d\n",
           (long)tu_counter_get(&produced),
           (long)tu_counter_get(&consumed),
           (long)tu_counter_get(&sm_ops),
           sm_registry_count());

    TEST_ASSERT_EQUAL_INT64(tu_counter_get(&produced), tu_counter_get(&consumed));
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());
    tu_barrier_destroy(&barrier);
}

TEST_GROUP_RUNNER(StressFullSystem) { RUN_TEST_CASE(StressFullSystem, AllComponents_ConcurrentSoak); }
static void run_all_groups(void) { RUN_TEST_GROUP(StressFullSystem); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
