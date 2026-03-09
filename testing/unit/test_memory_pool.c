/**
 * @file test_memory_pool.c
 * @brief Memory pool unit tests — groups: create, alloc/free,
 *        stats, reset, thread safety, error paths.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"

#include "../../dev/core/memory_pool.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ── Fixture ──────────────────────────────────────────────────────── */

#define POOL_BLOCK_SIZE   128
#define POOL_BLOCK_COUNT  16

static memory_pool_t *g_pool;

TEST_GROUP(MemoryPool);

TEST_SETUP(MemoryPool)
{
    memory_pool_config_t cfg = {
        .block_size  = POOL_BLOCK_SIZE,
        .block_count = POOL_BLOCK_COUNT,
        .thread_safe = 0,
        .name        = "test_pool",
    };
    g_pool = memory_pool_create(&cfg);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_pool, "memory_pool_create returned NULL");
}

TEST_TEAR_DOWN(MemoryPool)
{
    memory_pool_destroy(g_pool);
    g_pool = NULL;
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 1 — Create / Destroy
   ════════════════════════════════════════════════════════════════════ */

TEST(MemoryPool, Create_ValidConfig_ReturnsNonNull)
{
    TEST_ASSERT_NOT_NULL(g_pool);
}

TEST(MemoryPool, Create_NullConfig_ReturnsNull)
{
    memory_pool_t *p = memory_pool_create(NULL);
    TEST_ASSERT_NULL_MESSAGE(p, "Expected NULL for NULL config");
}

TEST(MemoryPool, Create_ZeroBlockSize_ReturnsNull)
{
    memory_pool_config_t cfg = { .block_size = 0, .block_count = 8 };
    memory_pool_t *p = memory_pool_create(&cfg);
    TEST_ASSERT_NULL(p);
}

TEST(MemoryPool, Create_ZeroBlockCount_ReturnsNull)
{
    memory_pool_config_t cfg = { .block_size = 64, .block_count = 0 };
    memory_pool_t *p = memory_pool_create(&cfg);
    TEST_ASSERT_NULL(p);
}

TEST(MemoryPool, DestroyNull_DoesNotCrash)
{
    memory_pool_destroy(NULL);
}

TEST(MemoryPool, Stats_InitialState_AllFree)
{
    memory_pool_stats_t s;
    int r = memory_pool_get_stats(g_pool, &s);
    TEST_ASSERT_EQUAL_INT(MPOOL_OK, r);
    TEST_ASSERT_EQUAL_SIZE(POOL_BLOCK_COUNT, s.total_blocks);
    TEST_ASSERT_EQUAL_SIZE(POOL_BLOCK_COUNT, s.free_blocks);
    TEST_ASSERT_EQUAL_SIZE(0,                s.used_blocks);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 2 — Alloc / Free Correctness
   ════════════════════════════════════════════════════════════════════ */

TEST(MemoryPool, Alloc_ReturnsNonNull)
{
    void *p = memory_pool_alloc(g_pool);
    TEST_ASSERT_NOT_NULL(p);
    memory_pool_free(g_pool, p);
}

TEST(MemoryPool, Alloc_ReturnedBlockIsZeroed)
{
    void *p = memory_pool_alloc(g_pool);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_BUF_ZERO(p, POOL_BLOCK_SIZE);
    memory_pool_free(g_pool, p);
}

TEST(MemoryPool, Alloc_WrittenDataSurvives_UntilFree)
{
    void *p = memory_pool_alloc(g_pool);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0xAB, POOL_BLOCK_SIZE);
    uint8_t expected[POOL_BLOCK_SIZE];
    memset(expected, 0xAB, POOL_BLOCK_SIZE);
    TEST_ASSERT_BUF_EQUAL(expected, p, POOL_BLOCK_SIZE);
    memory_pool_free(g_pool, p);
}

TEST(MemoryPool, AllocAll_ThenFreeAll_NoLeak)
{
    void *ptrs[POOL_BLOCK_COUNT];
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) {
        ptrs[i] = memory_pool_alloc(g_pool);
        TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], "Expected block allocation to succeed");
    }
    /* Pool should be full now */
    void *extra = memory_pool_alloc(g_pool);
    TEST_ASSERT_NULL_MESSAGE(extra, "Expected NULL when pool is full");

    /* Free all */
    for (int i = 0; i < POOL_BLOCK_COUNT; i++)
        memory_pool_free(g_pool, ptrs[i]);

    /* Verify stats */
    memory_pool_stats_t s;
    memory_pool_get_stats(g_pool, &s);
    TEST_ASSERT_EQUAL_SIZE(POOL_BLOCK_COUNT, s.free_blocks);
    TEST_ASSERT_EQUAL_SIZE(0,                s.used_blocks);
}

TEST(MemoryPool, AllocFull_ReturnsNull)
{
    void *ptrs[POOL_BLOCK_COUNT];
    for (int i = 0; i < POOL_BLOCK_COUNT; i++)
        ptrs[i] = memory_pool_alloc(g_pool);

    void *p = memory_pool_alloc(g_pool);
    TEST_ASSERT_NULL_MESSAGE(p, "Expected NULL when pool is full");

    for (int i = 0; i < POOL_BLOCK_COUNT; i++)
        memory_pool_free(g_pool, ptrs[i]);
}

TEST(MemoryPool, FreeInvalidPtr_DoesNotCorrupt)
{
    /* Free a stack pointer — must not corrupt the pool */
    uint8_t local[64];
    memory_pool_free(g_pool, local);   /* invalid ptr — must be logged, not crash */

    /* Pool must still work after the bad free */
    void *p = memory_pool_alloc(g_pool);
    TEST_ASSERT_NOT_NULL(p);
    memory_pool_free(g_pool, p);
}

TEST(MemoryPool, FreeNull_DoesNotCrash)
{
    memory_pool_free(g_pool, NULL);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 3 — Statistics
   ════════════════════════════════════════════════════════════════════ */

TEST(MemoryPool, Stats_AfterOneAlloc_UsedIs1)
{
    void *p = memory_pool_alloc(g_pool);
    memory_pool_stats_t s;
    memory_pool_get_stats(g_pool, &s);
    TEST_ASSERT_EQUAL_SIZE(1, s.used_blocks);
    TEST_ASSERT_EQUAL_SIZE(POOL_BLOCK_COUNT - 1, s.free_blocks);
    TEST_ASSERT_EQUAL_SIZE(1, s.alloc_count);
    memory_pool_free(g_pool, p);
}

TEST(MemoryPool, Stats_PeakUsed_TracksHighWaterMark)
{
    void *ptrs[4];
    for (int i = 0; i < 4; i++) ptrs[i] = memory_pool_alloc(g_pool);
    for (int i = 0; i < 4; i++) memory_pool_free(g_pool, ptrs[i]);

    memory_pool_stats_t s;
    memory_pool_get_stats(g_pool, &s);
    TEST_ASSERT_EQUAL_SIZE(4, s.peak_used);
}

TEST(MemoryPool, Stats_FailCount_IncreasesOnFullPool)
{
    void *ptrs[POOL_BLOCK_COUNT];
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) ptrs[i] = memory_pool_alloc(g_pool);

    /* These should fail */
    memory_pool_alloc(g_pool);
    memory_pool_alloc(g_pool);

    memory_pool_stats_t s;
    memory_pool_get_stats(g_pool, &s);
    TEST_ASSERT_TRUE_MESSAGE(s.fail_count >= 2,
        "fail_count should be at least 2 after 2 failed allocs");

    for (int i = 0; i < POOL_BLOCK_COUNT; i++) memory_pool_free(g_pool, ptrs[i]);
}

TEST(MemoryPool, GetStats_NullPool_ReturnsError)
{
    memory_pool_stats_t s;
    int r = memory_pool_get_stats(NULL, &s);
    TEST_ASSERT_EQUAL_INT(MPOOL_ERR_NULL, r);
}

TEST(MemoryPool, GetStats_NullOut_ReturnsError)
{
    int r = memory_pool_get_stats(g_pool, NULL);
    TEST_ASSERT_EQUAL_INT(MPOOL_ERR_NULL, r);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 4 — Reset
   ════════════════════════════════════════════════════════════════════ */

TEST(MemoryPool, Reset_AllBlocksFree_CountersCleared)
{
    void *p = memory_pool_alloc(g_pool);
    (void)p; /* intentionally leak inside pool, then reset */

    int r = memory_pool_reset(g_pool);
    TEST_ASSERT_EQUAL_INT(MPOOL_OK, r);

    memory_pool_stats_t s;
    memory_pool_get_stats(g_pool, &s);
    TEST_ASSERT_EQUAL_SIZE(POOL_BLOCK_COUNT, s.free_blocks);
    TEST_ASSERT_EQUAL_SIZE(0, s.used_blocks);
    TEST_ASSERT_EQUAL_SIZE(0, s.alloc_count);
}

TEST(MemoryPool, Reset_ThenAllocAll_Works)
{
    void *p = memory_pool_alloc(g_pool);
    (void)p;
    memory_pool_reset(g_pool);

    void *ptrs[POOL_BLOCK_COUNT];
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) {
        ptrs[i] = memory_pool_alloc(g_pool);
        TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], "Alloc after reset should succeed");
    }
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) memory_pool_free(g_pool, ptrs[i]);
}

TEST(MemoryPool, Reset_Null_ReturnsError)
{
    int r = memory_pool_reset(NULL);
    TEST_ASSERT_EQUAL_INT(MPOOL_ERR_NULL, r);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 5 — Thread Safety
   ════════════════════════════════════════════════════════════════════ */

#define TS_THREADS  8
#define TS_ITER     1000

typedef struct {
    memory_pool_t *pool;
    tu_barrier_t  *barrier;
    int            alloc_ok;
    int            free_ok;
} ts_arg_t;

static void *ts_thread(void *arg)
{
    ts_arg_t *a = (ts_arg_t *)arg;
    tu_barrier_wait(a->barrier);

    for (int i = 0; i < TS_ITER; i++) {
        void *p = memory_pool_alloc(a->pool);
        if (p) {
            a->alloc_ok++;
            tu_sleep_ms(0);  /* yield */
            memory_pool_free(a->pool, p);
            a->free_ok++;
        }
    }
    return NULL;
}

TEST(MemoryPool, ThreadSafe_8Threads_NoCorruption)
{
    memory_pool_config_t cfg = {
        .block_size  = POOL_BLOCK_SIZE,
        .block_count = TS_THREADS * 4,
        .thread_safe = 1,   /* ← thread-safe mode */
        .name        = "ts_pool",
    };
    memory_pool_t *pool = memory_pool_create(&cfg);
    TEST_ASSERT_NOT_NULL(pool);

    tu_barrier_t barrier;
    tu_barrier_init(&barrier, TS_THREADS);

    ts_arg_t   args[TS_THREADS];
    pthread_t  tids[TS_THREADS];
    for (int i = 0; i < TS_THREADS; i++) {
        args[i].pool    = pool;
        args[i].barrier = &barrier;
        args[i].alloc_ok = args[i].free_ok = 0;
        pthread_create(&tids[i], NULL, ts_thread, &args[i]);
    }
    for (int i = 0; i < TS_THREADS; i++) pthread_join(tids[i], NULL);

    tu_barrier_destroy(&barrier);

    /* Every successful alloc must have a matching free */
    int total_alloc = 0, total_free = 0;
    for (int i = 0; i < TS_THREADS; i++) {
        total_alloc += args[i].alloc_ok;
        total_free  += args[i].free_ok;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(total_alloc, total_free,
        "Thread-safe pool: alloc/free count mismatch");

    memory_pool_stats_t s;
    memory_pool_get_stats(pool, &s);
    TEST_ASSERT_EQUAL_SIZE_MESSAGE(0, s.used_blocks,
        "Thread-safe pool: leak detected — used_blocks should be 0");

    memory_pool_destroy(pool);
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(MemoryPool)
{
    RUN_TEST_CASE(MemoryPool, Create_ValidConfig_ReturnsNonNull);
    RUN_TEST_CASE(MemoryPool, Create_NullConfig_ReturnsNull);
    RUN_TEST_CASE(MemoryPool, Create_ZeroBlockSize_ReturnsNull);
    RUN_TEST_CASE(MemoryPool, Create_ZeroBlockCount_ReturnsNull);
    RUN_TEST_CASE(MemoryPool, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(MemoryPool, Stats_InitialState_AllFree);
    RUN_TEST_CASE(MemoryPool, Alloc_ReturnsNonNull);
    RUN_TEST_CASE(MemoryPool, Alloc_ReturnedBlockIsZeroed);
    RUN_TEST_CASE(MemoryPool, Alloc_WrittenDataSurvives_UntilFree);
    RUN_TEST_CASE(MemoryPool, AllocAll_ThenFreeAll_NoLeak);
    RUN_TEST_CASE(MemoryPool, AllocFull_ReturnsNull);
    RUN_TEST_CASE(MemoryPool, FreeInvalidPtr_DoesNotCorrupt);
    RUN_TEST_CASE(MemoryPool, FreeNull_DoesNotCrash);
    RUN_TEST_CASE(MemoryPool, Stats_AfterOneAlloc_UsedIs1);
    RUN_TEST_CASE(MemoryPool, Stats_PeakUsed_TracksHighWaterMark);
    RUN_TEST_CASE(MemoryPool, Stats_FailCount_IncreasesOnFullPool);
    RUN_TEST_CASE(MemoryPool, GetStats_NullPool_ReturnsError);
    RUN_TEST_CASE(MemoryPool, GetStats_NullOut_ReturnsError);
    RUN_TEST_CASE(MemoryPool, Reset_AllBlocksFree_CountersCleared);
    RUN_TEST_CASE(MemoryPool, Reset_ThenAllocAll_Works);
    RUN_TEST_CASE(MemoryPool, Reset_Null_ReturnsError);
    RUN_TEST_CASE(MemoryPool, ThreadSafe_8Threads_NoCorruption);
}

static void run_all_groups(void)
{
    RUN_TEST_GROUP(MemoryPool);
}

int main(int argc, const char *argv[])
{
    return UnityMain(argc, argv, run_all_groups);
}
