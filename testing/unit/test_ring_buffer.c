/**
 * @file test_ring_buffer.c
 * @brief Ring buffer unit tests — all 5 groups.
 *
 * Build with:
 *   -DUNITY_INCLUDE_CONFIG_H is NOT needed (Unity auto-detection used).
 * Run with:   ./test_ring_buffer
 * Coverage:   Line ≥ 90%, Branch ≥ 85%.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"

#include "../../dev/core/ring_buffer.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>

/* ── Test fixture ─────────────────────────────────────────────────── */

/* Ring buffer reserves one slot as sentinel; create with CAP+1 so
 * the usable capacity equals CAP (fills can write exactly CAP items). */
#define CAP    16
#define ISIZE  64
#define RB_CREATE_CAP  (CAP + 1)  /* actual arg to ring_buffer_create */
#define RB_NAME_PREFIX  "/test_rb_"

static rb_handle_t  *g_rb;
static char          g_name[64];

TEST_GROUP(RingBuffer);

TEST_SETUP(RingBuffer)
{
    tu_tmp_shm_name(g_name, sizeof(g_name), "rb_unit");
    g_rb = ring_buffer_create(g_name, RB_CREATE_CAP, ISIZE);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_rb, "ring_buffer_create returned NULL");
}

TEST_TEAR_DOWN(RingBuffer)
{
    ring_buffer_destroy(g_rb, g_name);
    g_rb = NULL;
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 1 — Initialisation & Destruction
   ════════════════════════════════════════════════════════════════════ */

TEST(RingBuffer, CreateWithValidParams_ReturnsNonNull)
{
    TEST_ASSERT_NOT_NULL(g_rb);
    TEST_ASSERT_NOT_NULL(g_rb->rb);
}

TEST(RingBuffer, CapacityMatchesRequested)
{
    TEST_ASSERT_EQUAL_UINT32(RB_CREATE_CAP, g_rb->rb->capacity);
}

TEST(RingBuffer, ItemSizeMatchesRequested)
{
    TEST_ASSERT_EQUAL_UINT32(ISIZE, g_rb->rb->item_size);
}

TEST(RingBuffer, CreateWithZeroCapacity_ReturnsNull)
{
    char n[64];
    tu_tmp_shm_name(n, sizeof(n), "rb_cap0");
    rb_handle_t *h = ring_buffer_create(n, 0, ISIZE);
    TEST_ASSERT_NULL_MESSAGE(h, "Expected NULL for capacity=0");
    if (h) ring_buffer_destroy(h, n);
}

TEST(RingBuffer, CreateWithZeroItemSize_ReturnsNull)
{
    char n[64];
    tu_tmp_shm_name(n, sizeof(n), "rb_isz0");
    rb_handle_t *h = ring_buffer_create(n, CAP, 0);
    TEST_ASSERT_NULL_MESSAGE(h, "Expected NULL for item_size=0");
    if (h) ring_buffer_destroy(h, n);
}

TEST(RingBuffer, DestroyNull_DoesNotCrash)
{
    ring_buffer_destroy(NULL, NULL);  /* must not crash */
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 2 — Basic Write / Read Correctness
   ════════════════════════════════════════════════════════════════════ */

TEST(RingBuffer, WriteOneThenReadBack_ByteForByteEqual)
{
    uint8_t item[ISIZE];
    tu_rand_fill(item, ISIZE, 0xCAFE1234u);

    int wr = ring_buffer_write(g_rb, item);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RB_SUCCESS, wr, "write failed");

    uint8_t out[ISIZE];
    int rd = ring_buffer_read(g_rb, out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RB_SUCCESS, rd, "read failed");
    TEST_ASSERT_BUF_EQUAL(item, out, ISIZE);
}

TEST(RingBuffer, WriteN_ThenReadN_FIFOOrder)
{
    uint8_t items[CAP][ISIZE];
    for (int i = 0; i < CAP; i++) {
        memset(items[i], i, ISIZE);
        TEST_ASSERT_EQUAL_INT(RB_SUCCESS, ring_buffer_write(g_rb, items[i]));
    }
    for (int i = 0; i < CAP; i++) {
        uint8_t out[ISIZE];
        TEST_ASSERT_EQUAL_INT(RB_SUCCESS, ring_buffer_read(g_rb, out));
        TEST_ASSERT_BUF_EQUAL(items[i], out, ISIZE);
    }
}

TEST(RingBuffer, AllZeroBytes_NotCorrupted)
{
    uint8_t item[ISIZE];
    memset(item, 0, ISIZE);
    ring_buffer_write(g_rb, item);
    uint8_t out[ISIZE];
    memset(out, 0xFF, ISIZE);
    ring_buffer_read(g_rb, out);
    TEST_ASSERT_BUF_ZERO(out, ISIZE);
}

TEST(RingBuffer, AllFFBytes_NotCorrupted)
{
    uint8_t item[ISIZE];
    memset(item, 0xFF, ISIZE);
    ring_buffer_write(g_rb, item);
    uint8_t out[ISIZE];
    memset(out, 0, ISIZE);
    ring_buffer_read(g_rb, out);
    TEST_ASSERT_EQUAL_MEMORY(item, out, ISIZE);
}

TEST(RingBuffer, AfterWriteRead_BufferReportsEmpty)
{
    uint8_t item[ISIZE];
    memset(item, 0xAB, ISIZE);
    ring_buffer_write(g_rb, item);
    ring_buffer_read(g_rb, item);
    TEST_ASSERT_EQUAL_INT(1, ring_buffer_is_empty(g_rb));
    TEST_ASSERT_EQUAL_UINT32(0, ring_buffer_count(g_rb));
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 3 — Full and Empty Boundary Conditions
   ════════════════════════════════════════════════════════════════════ */

TEST(RingBuffer, FillToCapacity_NextWriteReturnsFull)
{
    uint8_t item[ISIZE];
    memset(item, 0x55, ISIZE);
    for (int i = 0; i < (int)CAP; i++)
        TEST_ASSERT_EQUAL_INT(RB_SUCCESS, ring_buffer_write(g_rb, item));
    TEST_ASSERT_EQUAL_INT(1, ring_buffer_is_full(g_rb));

    int r = ring_buffer_write(g_rb, item);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RB_ERROR_FULL, r, "Expected FULL after capacity reached");
}

TEST(RingBuffer, BufferStillReadableAfterFull)
{
    uint8_t item[ISIZE];
    memset(item, 0xAB, ISIZE);
    for (int i = 0; i < (int)CAP; i++) ring_buffer_write(g_rb, item);
    /* Even in full state, existing items must be readable */
    uint8_t out[ISIZE];
    TEST_ASSERT_EQUAL_INT(RB_SUCCESS, ring_buffer_read(g_rb, out));
    TEST_ASSERT_BUF_EQUAL(item, out, ISIZE);
}

TEST(RingBuffer, DrainCompletely_NextReadReturnsEmpty)
{
    uint8_t item[ISIZE];
    memset(item, 0xCC, ISIZE);
    ring_buffer_write(g_rb, item);
    ring_buffer_read(g_rb, item);
    int r = ring_buffer_read(g_rb, item);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RB_ERROR_EMPTY, r, "Expected EMPTY after drain");
}

TEST(RingBuffer, FillDrainFill_SecondFillWorks)
{
    uint8_t item[ISIZE];
    memset(item, 1, ISIZE);
    /* First fill */
    for (int i = 0; i < (int)CAP; i++) ring_buffer_write(g_rb, item);
    /* Drain */
    for (int i = 0; i < (int)CAP; i++) ring_buffer_read(g_rb, item);
    TEST_ASSERT_EQUAL_INT(0, ring_buffer_count(g_rb));
    /* Second fill - verify wrap-around works */
    for (int i = 0; i < (int)CAP; i++) {
        memset(item, i + 1, ISIZE);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RB_SUCCESS,
            ring_buffer_write(g_rb, item), "Second fill failed");
    }
    for (int i = 0; i < (int)CAP; i++) {
        uint8_t out[ISIZE];
        ring_buffer_read(g_rb, out);
        TEST_ASSERT_EQUAL_INT(i + 1, out[0]);
    }
}

TEST(RingBuffer, WrapAroundBoundary_DataCorrect)
{
    uint8_t item[ISIZE];
    const int HALF = CAP / 2;

    /* Fill half, drain half — this advances head/tail */
    for (int i = 0; i < HALF; i++) {
        memset(item, i, ISIZE);
        ring_buffer_write(g_rb, item);
    }
    for (int i = 0; i < HALF; i++) ring_buffer_read(g_rb, item);

    /* Now fill to capacity — wraps around the ring */
    for (int i = 0; i < (int)CAP; i++) {
        memset(item, (uint8_t)(0xA0 + i), ISIZE);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RB_SUCCESS, ring_buffer_write(g_rb, item),
                                      "Write at wrap boundary failed");
    }
    for (int i = 0; i < (int)CAP; i++) {
        uint8_t out[ISIZE];
        ring_buffer_read(g_rb, out);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(0xA0 + i), out[0],
                                        "Data corrupted at wrap boundary");
    }
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 4 — Concurrent Access (Thread Safety)
   ════════════════════════════════════════════════════════════════════ */

#define PROD_ITEMS   100000
#define PROD_THREADS 4
#define ITEMS_PER_PROD (PROD_ITEMS / PROD_THREADS)

/* Sequence number packed at start of each item. */
typedef struct { int seq; int thread_id; uint8_t pad[ISIZE - 8]; } seq_item_t;
_Static_assert(sizeof(seq_item_t) == ISIZE, "seq_item_t size mismatch");

/* Mutex to serialise concurrent writes — ring_buffer_write is SPMC, not MPMC */
static pthread_mutex_t g_write_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    rb_handle_t *rb;
    int          thread_id;
    int          count;
    tu_barrier_t *barrier;
} producer_arg_t;

typedef struct {
    rb_handle_t  *rb;
    _Atomic long  received;
    _Atomic int   running;
    tu_barrier_t *barrier;
} consumer_arg_t;

static void *producer_thread(void *arg)
{
    producer_arg_t *a = (producer_arg_t *)arg;
    tu_barrier_wait(a->barrier);   /* synchronized start */

    for (int i = 0; i < a->count; i++) {
        seq_item_t item;
        memset(&item, 0, sizeof(item));
        item.seq       = i;
        item.thread_id = a->thread_id;
        /* ring_buffer_write is SPMC — serialise concurrent producers with mutex */
        pthread_mutex_lock(&g_write_mutex);
        while (ring_buffer_write(a->rb, &item) == RB_ERROR_FULL) {
            pthread_mutex_unlock(&g_write_mutex);
            tu_sleep_ms(0);   /* yield CPU while full */
            pthread_mutex_lock(&g_write_mutex);
        }
        pthread_mutex_unlock(&g_write_mutex);
    }
    return NULL;
}

static void *consumer_thread(void *arg)
{
    consumer_arg_t *a = (consumer_arg_t *)arg;
    tu_barrier_wait(a->barrier);   /* synchronized start */

    while (atomic_load(&a->running) || ring_buffer_count(a->rb) > 0) {
        seq_item_t out;
        if (ring_buffer_read(a->rb, &out) == RB_SUCCESS)
            atomic_fetch_add(&a->received, 1);
        else
            tu_sleep_ms(0);
    }
    return NULL;
}

TEST(RingBuffer, Concurrent_1Producer1Consumer_100k_Items)
{
    /* Use a large-capacity buffer for this test */
    char name[64];
    tu_tmp_shm_name(name, sizeof(name), "rb_conc1");
    rb_handle_t *rb = ring_buffer_create(name, RING_BUFFER_MAX_CAPACITY, ISIZE);
    TEST_ASSERT_NOT_NULL(rb);

    tu_barrier_t barrier;
    tu_barrier_init(&barrier, 2);

    producer_arg_t prod = { .rb = rb, .thread_id = 0,
                             .count = PROD_ITEMS, .barrier = &barrier };
    consumer_arg_t cons;
    memset(&cons, 0, sizeof(cons));
    cons.rb = rb;
    atomic_store(&cons.running, 1);
    cons.barrier = &barrier;
    atomic_init(&cons.received, 0);

    pthread_t pt, ct;
    pthread_create(&ct, NULL, consumer_thread, &cons);
    pthread_create(&pt, NULL, producer_thread, &prod);

    pthread_join(pt, NULL);
    atomic_store(&cons.running, 0);
    pthread_join(ct, NULL);

    tu_barrier_destroy(&barrier);

    TEST_ASSERT_EQUAL_INT_MESSAGE(PROD_ITEMS,
        (int)atomic_load(&cons.received),
        "Not all items consumed in single producer/consumer test");

    ring_buffer_destroy(rb, name);
}

TEST(RingBuffer, Concurrent_4Producers1Consumer_100k_Items)
{
    char name[64];
    tu_tmp_shm_name(name, sizeof(name), "rb_conc4");
    rb_handle_t *rb = ring_buffer_create(name, RING_BUFFER_MAX_CAPACITY, ISIZE);
    TEST_ASSERT_NOT_NULL(rb);

    tu_barrier_t barrier;
    tu_barrier_init(&barrier, PROD_THREADS + 1);

    producer_arg_t prods[PROD_THREADS];
    pthread_t      pt[PROD_THREADS];
    for (int i = 0; i < PROD_THREADS; i++) {
        prods[i].rb        = rb;
        prods[i].thread_id = i;
        prods[i].count     = ITEMS_PER_PROD;
        prods[i].barrier   = &barrier;
        pthread_create(&pt[i], NULL, producer_thread, &prods[i]);
    }

    consumer_arg_t cons;
    memset(&cons, 0, sizeof(cons));
    cons.rb = rb; atomic_store(&cons.running, 1); cons.barrier = &barrier;
    atomic_init(&cons.received, 0);
    pthread_t ct;
    pthread_create(&ct, NULL, consumer_thread, &cons);

    for (int i = 0; i < PROD_THREADS; i++) pthread_join(pt[i], NULL);
    while (ring_buffer_count(rb) > 0) tu_sleep_ms(1); /* drain before stop */
    atomic_store(&cons.running, 0);
    pthread_join(ct, NULL);

    tu_barrier_destroy(&barrier);

    TEST_ASSERT_EQUAL_INT_MESSAGE(PROD_ITEMS,
        (int)atomic_load(&cons.received),
        "Item count mismatch in 4-producer test");

    ring_buffer_destroy(rb, name);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 5 — Edge Cases & Error Paths
   ════════════════════════════════════════════════════════════════════ */

TEST(RingBuffer, WriteToNullHandle_DoesNotCrash)
{
    uint8_t item[ISIZE];
    int r = ring_buffer_write(NULL, item);
    TEST_ASSERT_NOT_EQUAL_INT(RB_SUCCESS, r);
}

TEST(RingBuffer, ReadFromNullHandle_DoesNotCrash)
{
    uint8_t item[ISIZE];
    int r = ring_buffer_read(NULL, item);
    TEST_ASSERT_NOT_EQUAL_INT(RB_SUCCESS, r);
}

TEST(RingBuffer, CountOnNullHandle_DoesNotCrash)
{
    /* Should return 0 or handle gracefully — must not crash */
    ring_buffer_count(NULL);
}

TEST(RingBuffer, SingleByteItem_CorrectRoundTrip)
{
    char n[64];
    tu_tmp_shm_name(n, sizeof(n), "rb_1byte");
    rb_handle_t *h = ring_buffer_create(n, 8, 1);
    TEST_ASSERT_NOT_NULL(h);

    uint8_t w = 0xAB, r = 0;
    TEST_ASSERT_EQUAL_INT(RB_SUCCESS, ring_buffer_write(h, &w));
    TEST_ASSERT_EQUAL_INT(RB_SUCCESS, ring_buffer_read(h, &r));
    TEST_ASSERT_EQUAL_UINT8(w, r);

    ring_buffer_destroy(h, n);
}

/* ── Test runner ──────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(RingBuffer)
{
    /* Group 1 */
    RUN_TEST_CASE(RingBuffer, CreateWithValidParams_ReturnsNonNull);
    RUN_TEST_CASE(RingBuffer, CapacityMatchesRequested);
    RUN_TEST_CASE(RingBuffer, ItemSizeMatchesRequested);
    RUN_TEST_CASE(RingBuffer, CreateWithZeroCapacity_ReturnsNull);
    RUN_TEST_CASE(RingBuffer, CreateWithZeroItemSize_ReturnsNull);
    RUN_TEST_CASE(RingBuffer, DestroyNull_DoesNotCrash);
    /* Group 2 */
    RUN_TEST_CASE(RingBuffer, WriteOneThenReadBack_ByteForByteEqual);
    RUN_TEST_CASE(RingBuffer, WriteN_ThenReadN_FIFOOrder);
    RUN_TEST_CASE(RingBuffer, AllZeroBytes_NotCorrupted);
    RUN_TEST_CASE(RingBuffer, AllFFBytes_NotCorrupted);
    RUN_TEST_CASE(RingBuffer, AfterWriteRead_BufferReportsEmpty);
    /* Group 3 */
    RUN_TEST_CASE(RingBuffer, FillToCapacity_NextWriteReturnsFull);
    RUN_TEST_CASE(RingBuffer, BufferStillReadableAfterFull);
    RUN_TEST_CASE(RingBuffer, DrainCompletely_NextReadReturnsEmpty);
    RUN_TEST_CASE(RingBuffer, FillDrainFill_SecondFillWorks);
    RUN_TEST_CASE(RingBuffer, WrapAroundBoundary_DataCorrect);
    /* Group 4 */
    RUN_TEST_CASE(RingBuffer, Concurrent_1Producer1Consumer_100k_Items);
    RUN_TEST_CASE(RingBuffer, Concurrent_4Producers1Consumer_100k_Items);
    /* Group 5 */
    RUN_TEST_CASE(RingBuffer, WriteToNullHandle_DoesNotCrash);
    RUN_TEST_CASE(RingBuffer, ReadFromNullHandle_DoesNotCrash);
    RUN_TEST_CASE(RingBuffer, CountOnNullHandle_DoesNotCrash);
    RUN_TEST_CASE(RingBuffer, SingleByteItem_CorrectRoundTrip);
}

static void run_all_groups(void)
{
    RUN_TEST_GROUP(RingBuffer);
}

int main(int argc, const char *argv[])
{
    return UnityMain(argc, argv, run_all_groups);
}
