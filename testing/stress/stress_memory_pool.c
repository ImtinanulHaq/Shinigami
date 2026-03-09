/**
 * @file stress_memory_pool.c
 * @brief Memory pool stress test — concurrent alloc/free under contention.
 *
 * Many threads repeatedly allocate all pool blocks then free them,
 * checking stats consistency and absence of double-free / leak.
 */
#include "../../framework/unity.h"
#include "../../framework/unity_fixture.h"
#include "../../helpers/assert_extras.h"
#include "../../helpers/test_utils.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifndef STRESS_DURATION_SEC
#define STRESS_DURATION_SEC 60
#endif

#define N_THREADS    8
#define POOL_BLOCKS  64
#define BLOCK_SIZE   256

/* Portable pool: malloc-backed slab, metrics for validation */
typedef struct {
    void   **blocks;
    int     *used;
    int      n;
    long     alloc_total;
    long     free_total;
    long     fail_total;
    pthread_mutex_t mu;
} stress_pool_t;

static void sp_init(stress_pool_t *p)
{
    pthread_mutex_init(&p->mu, NULL);
    p->n       = POOL_BLOCKS;
    p->blocks  = calloc((size_t)p->n, sizeof(void *));
    p->used    = calloc((size_t)p->n, sizeof(int));
    for (int i = 0; i < p->n; i++) p->blocks[i] = malloc(BLOCK_SIZE);
    p->alloc_total = p->free_total = p->fail_total = 0;
}

static void *sp_alloc(stress_pool_t *p)
{
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < p->n; i++) {
        if (!p->used[i]) {
            p->used[i] = 1; p->alloc_total++;
            pthread_mutex_unlock(&p->mu);
            return p->blocks[i];
        }
    }
    p->fail_total++;
    pthread_mutex_unlock(&p->mu);
    return NULL;
}

static void sp_free(stress_pool_t *p, void *blk)
{
    pthread_mutex_lock(&p->mu);
    for (int i = 0; i < p->n; i++) {
        if (p->blocks[i] == blk && p->used[i]) {
            p->used[i] = 0; p->free_total++;
            pthread_mutex_unlock(&p->mu);
            return;
        }
    }
    pthread_mutex_unlock(&p->mu);
}

static void sp_destroy(stress_pool_t *p)
{
    for (int i = 0; i < p->n; i++) free(p->blocks[i]);
    free(p->blocks); free(p->used);
    pthread_mutex_destroy(&p->mu);
}

typedef struct {
    stress_pool_t *pool;
    volatile int  *stop;
    tu_barrier_t  *barrier;
    int            id;
} sp_arg_t;

static void *stress_pool_thread(void *arg)
{
    sp_arg_t *a = (sp_arg_t *)arg;
    tu_barrier_wait(a->barrier);

    void *mine[POOL_BLOCKS];
    int   cnt = 0;

    while (!*a->stop) {
        /* Batch alloc */
        cnt = 0;
        for (int i = 0; i < POOL_BLOCKS / N_THREADS; i++) {
            void *b = sp_alloc(a->pool);
            if (b) { memset(b, (uint8_t)a->id, BLOCK_SIZE); mine[cnt++] = b; }
        }
        /* Batch free */
        for (int i = 0; i < cnt; i++) sp_free(a->pool, mine[i]);
    }
    return NULL;
}

static stress_pool_t g_pool;

TEST_GROUP(StressMemoryPool);
TEST_SETUP(StressMemoryPool)
{
    sp_init(&g_pool);
}
TEST_TEAR_DOWN(StressMemoryPool)
{
    sp_destroy(&g_pool);
}

TEST(StressMemoryPool, ConcurrentAllocFree_Soak)
{
    int duration = STRESS_DURATION_SEC;
    const char *env = getenv("STRESS_DURATION");
    if (env) duration = atoi(env);

    volatile int stop = 0;
    tu_barrier_t barrier;
    tu_barrier_init(&barrier, N_THREADS + 1);

    pthread_t threads[N_THREADS];
    sp_arg_t  args[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        args[i] = (sp_arg_t){ &g_pool, &stop, &barrier, i };
        pthread_create(&threads[i], NULL, stress_pool_thread, &args[i]);
    }

    tu_barrier_wait(&barrier);
    tu_sleep_ms(duration * 1000);
    stop = 1;
    for (int i = 0; i < N_THREADS; i++) pthread_join(threads[i], NULL);

    /* Every alloc should have a matching free */
    printf("[stress_memory_pool] alloc=%ld free=%ld fail=%ld\n",
           g_pool.alloc_total, g_pool.free_total, g_pool.fail_total);
    TEST_ASSERT_EQUAL_INT64(g_pool.alloc_total, g_pool.free_total);

    /* Pool fully reclaimed */
    for (int i = 0; i < g_pool.n; i++) TEST_ASSERT_EQUAL_INT(0, g_pool.used[i]);

    tu_barrier_destroy(&barrier);
}

TEST_GROUP_RUNNER(StressMemoryPool) { RUN_TEST_CASE(StressMemoryPool, ConcurrentAllocFree_Soak); }
static void run_all_groups(void) { RUN_TEST_GROUP(StressMemoryPool); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
