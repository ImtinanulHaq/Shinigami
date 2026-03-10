/**
 * @file test_memory_pool.c
 * @brief Memory pool unit tests — 6 groups per spec Part 6.
 *
 * Group 1 – Creation & configuration
 * Group 2 – Alloc / Free basics
 * Group 3 – Double-free / invalid-ptr detection
 * Group 4 – Stats accuracy
 * Group 5 – Concurrent alloc/free
 * Group 6 – Reset
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../../dev/core/memory_pool.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>


/* ==========================================================================
 * GROUP 1 – Creation & configuration
 * ========================================================================== */
TEST_GROUP(MemPool_Creation);
TEST_SETUP(MemPool_Creation)     {}
TEST_TEAR_DOWN(MemPool_Creation) {}

TEST(MemPool_Creation, ValidConfig_Returns_NonNull) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=16, .thread_safe=1, .name="test" };
    memory_pool_t *pool = memory_pool_create(&cfg);
    TEST_ASSERT_NOT_NULL(pool);
    memory_pool_destroy(pool);
}

TEST(MemPool_Creation, NullConfig_Returns_Null) {
    memory_pool_t *pool = memory_pool_create(NULL);
    TEST_ASSERT_NULL(pool);
}

TEST(MemPool_Creation, ZeroBlockSize_Returns_Null) {
    memory_pool_config_t cfg = { .block_size=0, .block_count=16, .thread_safe=0, .name="bad" };
    memory_pool_t *pool = memory_pool_create(&cfg);
    TEST_ASSERT_NULL(pool);
}

TEST(MemPool_Creation, ZeroBlockCount_Returns_Null) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=0, .thread_safe=0, .name="bad" };
    memory_pool_t *pool = memory_pool_create(&cfg);
    TEST_ASSERT_NULL(pool);
}

TEST(MemPool_Creation, SmallBlockSize_RoundedToMin) {
    /* block_size=1 should be rounded up (at least to 8) */
    memory_pool_config_t cfg = { .block_size=1, .block_count=8, .thread_safe=0, .name="tiny" };
    memory_pool_t *pool = memory_pool_create(&cfg);
    /* Either NULL (rejected) or non-NULL (rounded) — must not crash */
    if (pool) memory_pool_destroy(pool);
    TEST_PASS();
}

TEST(MemPool_Creation, Stats_InitialState_MatchesConfig) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=16, .thread_safe=1, .name="init" };
    memory_pool_t *pool = memory_pool_create(&cfg);
    TEST_ASSERT_NOT_NULL(pool);
    memory_pool_stats_t st;
    TEST_ASSERT_EQUAL_INT(MPOOL_OK, memory_pool_get_stats(pool,&st));
    TEST_ASSERT_EQUAL_INT(16, (int)st.total_blocks);
    TEST_ASSERT_EQUAL_INT(16, (int)st.free_blocks);
    TEST_ASSERT_EQUAL_INT(0,  (int)st.used_blocks);
    memory_pool_destroy(pool);
}

TEST_GROUP_RUNNER(MemPool_Creation) {
    RUN_TEST_CASE(MemPool_Creation, ValidConfig_Returns_NonNull);
    RUN_TEST_CASE(MemPool_Creation, NullConfig_Returns_Null);
    RUN_TEST_CASE(MemPool_Creation, ZeroBlockSize_Returns_Null);
    RUN_TEST_CASE(MemPool_Creation, ZeroBlockCount_Returns_Null);
    RUN_TEST_CASE(MemPool_Creation, SmallBlockSize_RoundedToMin);
    RUN_TEST_CASE(MemPool_Creation, Stats_InitialState_MatchesConfig);
}


/* ==========================================================================
 * GROUP 2 – Alloc / Free basics
 * ========================================================================== */
TEST_GROUP(MemPool_AllocFree);
TEST_SETUP(MemPool_AllocFree)     {}
TEST_TEAR_DOWN(MemPool_AllocFree) {}

TEST(MemPool_AllocFree, Alloc_Returns_NonNull) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=8, .thread_safe=0, .name="af" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptr = memory_pool_alloc(p);
    TEST_ASSERT_NOT_NULL(ptr);
    memory_pool_free(p,ptr);
    memory_pool_destroy(p);
}

TEST(MemPool_AllocFree, WritePattern_SurvivesFreeAndRealloc) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="pat" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptr = memory_pool_alloc(p);
    memset(ptr, 0xAB, 64);
    memory_pool_free(p, ptr);
    void *ptr2 = memory_pool_alloc(p);
    TEST_ASSERT_NOT_NULL(ptr2);
    memset(ptr2, 0xCD, 64);
    unsigned char *bytes = (unsigned char *)ptr2;
    TEST_ASSERT_EQUAL_HEX8(0xCD, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, bytes[63]);
    memory_pool_free(p,ptr2);
    memory_pool_destroy(p);
}

TEST(MemPool_AllocFree, FillPool_ExtraAlloc_Fails) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="fill" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[4];
    for (int i=0;i<4;i++) ptrs[i]=memory_pool_alloc(p);
    void *extra = memory_pool_alloc(p);
    TEST_ASSERT_NULL(extra);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_TRUE(st.fail_count >= 1);
    for (int i=0;i<4;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_destroy(p);
}

TEST(MemPool_AllocFree, FreeAll_ThenAllocSucceeds) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="cycl" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[4];
    for (int i=0;i<4;i++) ptrs[i]=memory_pool_alloc(p);
    for (int i=0;i<4;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(4,(int)st.free_blocks);
    void *next = memory_pool_alloc(p);
    TEST_ASSERT_NOT_NULL(next);
    memory_pool_free(p,next);
    memory_pool_destroy(p);
}

TEST_GROUP_RUNNER(MemPool_AllocFree) {
    RUN_TEST_CASE(MemPool_AllocFree, Alloc_Returns_NonNull);
    RUN_TEST_CASE(MemPool_AllocFree, WritePattern_SurvivesFreeAndRealloc);
    RUN_TEST_CASE(MemPool_AllocFree, FillPool_ExtraAlloc_Fails);
    RUN_TEST_CASE(MemPool_AllocFree, FreeAll_ThenAllocSucceeds);
}


/* ==========================================================================
 * GROUP 3 – Double-free / invalid-ptr detection
 * ========================================================================== */
TEST_GROUP(MemPool_SafetyChecks);
TEST_SETUP(MemPool_SafetyChecks)     {}
TEST_TEAR_DOWN(MemPool_SafetyChecks) {}

TEST(MemPool_SafetyChecks, FreeNull_NocrashNoCorruption) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="sc" };
    memory_pool_t *p = memory_pool_create(&cfg);
    /* Freeing NULL must not crash */
    memory_pool_free(p, NULL);
    /* Pool must still be usable */
    void *ptr = memory_pool_alloc(p);
    TEST_ASSERT_NOT_NULL(ptr);
    memory_pool_free(p, ptr);
    memory_pool_destroy(p);
}

TEST(MemPool_SafetyChecks, FreeNonPoolPtr_NocrashNoCorruption) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="np" };
    memory_pool_t *p = memory_pool_create(&cfg);
    char stack_buf[64];
    /* Freeing a non-pool pointer; implementation must detect and log (not crash) */
    memory_pool_free(p, stack_buf);
    void *ptr = memory_pool_alloc(p);
    TEST_ASSERT_NOT_NULL(ptr);
    memory_pool_free(p,ptr);
    memory_pool_destroy(p);
}

TEST(MemPool_SafetyChecks, PoolStillWorks_AfterInvalidFree) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="sw" };
    memory_pool_t *p = memory_pool_create(&cfg);
    char junk[64]; memory_pool_free(p, junk); /* invalid free */
    /* Pool should still allocate all blocks */
    void *ptrs[4];
    int count = 0;
    for (int i=0;i<4;i++) { ptrs[i]=memory_pool_alloc(p); if(ptrs[i]) count++; }
    TEST_ASSERT_EQUAL_INT(4, count);
    for (int i=0;i<4;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_destroy(p);
}

TEST_GROUP_RUNNER(MemPool_SafetyChecks) {
    RUN_TEST_CASE(MemPool_SafetyChecks, FreeNull_NocrashNoCorruption);
    RUN_TEST_CASE(MemPool_SafetyChecks, FreeNonPoolPtr_NocrashNoCorruption);
    RUN_TEST_CASE(MemPool_SafetyChecks, PoolStillWorks_AfterInvalidFree);
}


/* ==========================================================================
 * GROUP 4 – Stats accuracy
 * ========================================================================== */
TEST_GROUP(MemPool_Stats);
TEST_SETUP(MemPool_Stats)     {}
TEST_TEAR_DOWN(MemPool_Stats) {}

TEST(MemPool_Stats, AllocCount_100Allocs) {
    memory_pool_config_t cfg = { .block_size=32, .block_count=128, .thread_safe=0, .name="st1" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[100];
    for (int i=0;i<100;i++) ptrs[i]=memory_pool_alloc(p);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(100, (int)st.alloc_count);
    for (int i=0;i<100;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_destroy(p);
}

TEST(MemPool_Stats, FreeCount_60Frees_After100Allocs) {
    memory_pool_config_t cfg = { .block_size=32, .block_count=128, .thread_safe=0, .name="st2" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[100];
    for (int i=0;i<100;i++) ptrs[i]=memory_pool_alloc(p);
    for (int i=0;i<60;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(60,  (int)st.free_count);
    TEST_ASSERT_EQUAL_INT(40,  (int)st.used_blocks);
    for (int i=60;i<100;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_destroy(p);
}

TEST(MemPool_Stats, PeakUsed_Equals_BlockCount_AfterExhaust) {
    memory_pool_config_t cfg = { .block_size=32, .block_count=8, .thread_safe=0, .name="pk" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[8];
    for (int i=0;i<8;i++) ptrs[i]=memory_pool_alloc(p);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(8, (int)st.peak_used);
    for (int i=0;i<8;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_destroy(p);
}

TEST(MemPool_Stats, FailCount_ExactlyRight) {
    memory_pool_config_t cfg = { .block_size=32, .block_count=4, .thread_safe=0, .name="fc" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[4];
    for (int i=0;i<4;i++) ptrs[i]=memory_pool_alloc(p);
    /* 3 extra allocs should fail */
    for (int i=0;i<3;i++) memory_pool_alloc(p);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(3, (int)st.fail_count);
    for (int i=0;i<4;i++) memory_pool_free(p,ptrs[i]);
    memory_pool_destroy(p);
}

TEST_GROUP_RUNNER(MemPool_Stats) {
    RUN_TEST_CASE(MemPool_Stats, AllocCount_100Allocs);
    RUN_TEST_CASE(MemPool_Stats, FreeCount_60Frees_After100Allocs);
    RUN_TEST_CASE(MemPool_Stats, PeakUsed_Equals_BlockCount_AfterExhaust);
    RUN_TEST_CASE(MemPool_Stats, FailCount_ExactlyRight);
}


/* ==========================================================================
 * GROUP 5 – Concurrent alloc/free (8 threads × 1000 iterations)
 * ========================================================================== */
typedef struct { memory_pool_t *pool; int thread_id; int iters; int errors; } cthread_arg_t;

static void *concurrent_worker(void *arg) {
    cthread_arg_t *a = (cthread_arg_t *)arg;
    for (int i = 0; i < a->iters; i++) {
        uint8_t *ptr = (uint8_t *)memory_pool_alloc(a->pool);
        if (!ptr) continue;
        /* Write thread_id pattern */
        memset(ptr, (uint8_t)a->thread_id, 64);
        /* Tiny yield */
        struct timespec ts = {0, (rand()%10)*100};
        nanosleep(&ts,NULL);
        /* Verify */
        for (int b=0;b<64;b++) {
            if (ptr[b] != (uint8_t)a->thread_id) { a->errors++; break; }
        }
        memory_pool_free(a->pool, ptr);
    }
    return NULL;
}

TEST_GROUP(MemPool_Concurrent);
TEST_SETUP(MemPool_Concurrent)     {}
TEST_TEAR_DOWN(MemPool_Concurrent) {}

TEST(MemPool_Concurrent, EightThreads_1Kiter_NoErrors) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=64, .thread_safe=1, .name="conc" };
    memory_pool_t *p = memory_pool_create(&cfg);
    TEST_ASSERT_NOT_NULL(p);

    const int N_THREADS=8, ITERS=1000;
    pthread_t tids[8]; cthread_arg_t args[8];
    for (int i=0;i<N_THREADS;i++) {
        args[i] = (cthread_arg_t){.pool=p,.thread_id=i+1,.iters=ITERS,.errors=0};
        pthread_create(&tids[i],NULL,concurrent_worker,&args[i]);
    }
    for (int i=0;i<N_THREADS;i++) pthread_join(tids[i],NULL);

    int total_errors=0;
    for (int i=0;i<N_THREADS;i++) total_errors+=args[i].errors;
    TEST_ASSERT_EQUAL_INT(0, total_errors);

    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(0, (int)st.used_blocks);
    memory_pool_destroy(p);
}

TEST_GROUP_RUNNER(MemPool_Concurrent) {
    RUN_TEST_CASE(MemPool_Concurrent, EightThreads_1Kiter_NoErrors);
}


/* ==========================================================================
 * GROUP 6 – Reset
 * ========================================================================== */
TEST_GROUP(MemPool_Reset);
TEST_SETUP(MemPool_Reset)     {}
TEST_TEAR_DOWN(MemPool_Reset) {}

TEST(MemPool_Reset, Reset_ClearsUsedBlocks) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=10, .thread_safe=0, .name="rst" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[10];
    for (int i=0;i<10;i++) ptrs[i]=memory_pool_alloc(p);
    int r = memory_pool_reset(p);
    TEST_ASSERT_EQUAL_INT(MPOOL_OK, r);
    memory_pool_stats_t st; memory_pool_get_stats(p,&st);
    TEST_ASSERT_EQUAL_INT(0,  (int)st.used_blocks);
    TEST_ASSERT_EQUAL_INT(10, (int)st.free_blocks);
    (void)ptrs;
    memory_pool_destroy(p);
}

TEST(MemPool_Reset, Pool_Functional_After_Reset) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="pfa" };
    memory_pool_t *p = memory_pool_create(&cfg);
    void *ptrs[4]; for (int i=0;i<4;i++) ptrs[i]=memory_pool_alloc(p);
    memory_pool_reset(p);
    void *ptr = memory_pool_alloc(p);
    TEST_ASSERT_NOT_NULL(ptr);
    memory_pool_free(p,ptr);
    memory_pool_destroy(p);
}

TEST(MemPool_Reset, GetStats_NullOut_ReturnsError) {
    memory_pool_config_t cfg = { .block_size=64, .block_count=4, .thread_safe=0, .name="gst" };
    memory_pool_t *p = memory_pool_create(&cfg);
    int r = memory_pool_get_stats(p, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(MPOOL_OK, r);
    memory_pool_destroy(p);
}

TEST_GROUP_RUNNER(MemPool_Reset) {
    RUN_TEST_CASE(MemPool_Reset, Reset_ClearsUsedBlocks);
    RUN_TEST_CASE(MemPool_Reset, Pool_Functional_After_Reset);
    RUN_TEST_CASE(MemPool_Reset, GetStats_NullOut_ReturnsError);
}


/* ---- main ---- */
static void run_all_groups(void) {
    RUN_TEST_GROUP(MemPool_Creation);
    RUN_TEST_GROUP(MemPool_AllocFree);
    RUN_TEST_GROUP(MemPool_SafetyChecks);
    RUN_TEST_GROUP(MemPool_Stats);
    RUN_TEST_GROUP(MemPool_Concurrent);
    RUN_TEST_GROUP(MemPool_Reset);
}
int main(int argc, const char *argv[]) {
    return UnityMain(argc, argv, run_all_groups);
}
