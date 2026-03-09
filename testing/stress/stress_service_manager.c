/**
 * @file stress_service_manager.c
 * @brief Service Manager stress test — concurrent register/deregister bursts.
 *
 * N threads repeatedly add and remove services from the SM registry,
 * then verify that the final count matches expectations and no
 * internal invariant is violated.
 */
#include "../../framework/unity.h"
#include "../../framework/unity_fixture.h"
#include "../../helpers/assert_extras.h"
#include "../../helpers/test_utils.h"

#include "../../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../../dev/core/service_manager/infrastructure/sm_protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STRESS_DURATION_SEC
#define STRESS_DURATION_SEC 60
#endif

#define N_THREADS    8

typedef struct {
    int           id;
    volatile int *stop;
    tu_barrier_t *barrier;
    tu_counter_t *reg_count;
    tu_counter_t *dereg_count;
    tu_counter_t *lookup_count;
} sm_stress_arg_t;

static void *sm_stress_thread(void *arg)
{
    sm_stress_arg_t *a = (sm_stress_arg_t *)arg;
    tu_barrier_wait(a->barrier);

    char name[SM_MAX_SERVICE_NAME];
    snprintf(name, sizeof(name), "stress_svc_%d", a->id);

    while (!*a->stop) {
        service_entry_t e;
        memset(&e, 0, sizeof(e));
        strncpy(e.name, name, sizeof(e.name) - 1);
        snprintf(e.socket_path, sizeof(e.socket_path), "/tmp/ss%d.sock", a->id);
        e.pid    = (pid_t)(10000 + a->id);
        e.status = SERVICE_STATUS_RUNNING;

        int r = sm_registry_add(&e);
        if (r == 0) {
            tu_counter_inc(a->reg_count);

            /* Lookup */
            service_entry_t out;
            if (sm_registry_find_copy(name, &out) == 0)
                tu_counter_inc(a->lookup_count);

            /* Heartbeat */
            sm_registry_update_heartbeat(name);

            /* Remove */
            if (sm_registry_remove(name) == 0)
                tu_counter_inc(a->dereg_count);
        }
    }
    return NULL;
}

TEST_GROUP(StressSM);
TEST_SETUP(StressSM)    { sm_registry_init(); }
TEST_TEAR_DOWN(StressSM) { sm_registry_cleanup(); }

TEST(StressSM, ConcurrentRegisterDeregister_Soak)
{
    int duration = STRESS_DURATION_SEC;
    const char *env = getenv("STRESS_DURATION");
    if (env) duration = atoi(env);

    volatile int stop = 0;
    tu_barrier_t barrier;
    tu_barrier_init(&barrier, N_THREADS + 1);

    tu_counter_t reg   = {0};
    tu_counter_t dereg = {0};
    tu_counter_t lkup  = {0};

    pthread_t      threads[N_THREADS];
    sm_stress_arg_t args[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        args[i] = (sm_stress_arg_t){ i, &stop, &barrier, &reg, &dereg, &lkup };
        pthread_create(&threads[i], NULL, sm_stress_thread, &args[i]);
    }

    tu_barrier_wait(&barrier);
    tu_sleep_ms(duration * 1000);
    stop = 1;
    for (int i = 0; i < N_THREADS; i++) pthread_join(threads[i], NULL);

    printf("[stress_sm] reg=%ld dereg=%ld lookup=%ld final_count=%d\n",
           (long)tu_counter_get(&reg),
           (long)tu_counter_get(&dereg),
           (long)tu_counter_get(&lkup),
           sm_registry_count());

    /* Registrations == deregistrations (each thread cleans up after itself) */
    TEST_ASSERT_EQUAL_INT64(tu_counter_get(&reg), tu_counter_get(&dereg));
    /* Registry should be empty at end */
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());

    tu_barrier_destroy(&barrier);
}

TEST_GROUP_RUNNER(StressSM) { RUN_TEST_CASE(StressSM, ConcurrentRegisterDeregister_Soak); }
static void run_all_groups(void) { RUN_TEST_GROUP(StressSM); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
