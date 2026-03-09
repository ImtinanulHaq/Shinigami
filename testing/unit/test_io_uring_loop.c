/**
 * @file test_io_uring_loop.c
 * @brief io_uring event loop unit tests.
 *
 * Groups: Create/Destroy, Op Registration, Timeout ops,
 * Poll ops, Write submission, Wakeup, Error paths.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"

#include "../../dev/core/io_uring_loop.h"
#include "../../dev/core/memory_pool.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>

/* ── Fixture ──────────────────────────────────────────────────────── */

static io_uring_loop_t *g_loop;

TEST_GROUP(IoUringLoop);

TEST_SETUP(IoUringLoop)
{
    io_loop_config_t cfg = {
        .queue_depth = 8,
        .use_sqpoll  = 0,
        .buf_pool    = NULL,
        .name        = "test_loop",
    };
    g_loop = io_loop_create(&cfg);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_loop, "io_loop_create returned NULL");
}

TEST_TEAR_DOWN(IoUringLoop)
{
    io_loop_destroy(g_loop);
    g_loop = NULL;
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 1 — Create / Destroy
   ════════════════════════════════════════════════════════════════════ */

TEST(IoUringLoop, Create_ValidConfig_ReturnsNonNull)
{
    TEST_ASSERT_NOT_NULL(g_loop);
}

TEST(IoUringLoop, Create_NullConfig_ReturnsNull)
{
    io_uring_loop_t *l = io_loop_create(NULL);
    TEST_ASSERT_NULL_MESSAGE(l, "Expected NULL for NULL config");
}

TEST(IoUringLoop, Create_ZeroQueueDepth_ReturnsNull)
{
    io_loop_config_t cfg = { .queue_depth = 0, .name = "zero_qd" };
    io_uring_loop_t *l = io_loop_create(&cfg);
    TEST_ASSERT_NULL_MESSAGE(l, "Expected NULL for queue_depth=0");
    if (l) io_loop_destroy(l);
}

TEST(IoUringLoop, DestroyNull_DoesNotCrash)
{
    io_loop_destroy(NULL);
}

TEST(IoUringLoop, CreateDestroy_NoFdLeak)
{
    int before = count_open_fds();
    io_loop_config_t cfg = { .queue_depth = 8, .name = "fdleak_test" };
    io_uring_loop_t *l = io_loop_create(&cfg);
    TEST_ASSERT_NOT_NULL(l);
    io_loop_destroy(l);
    int after = count_open_fds();
    TEST_ASSERT_NO_FD_LEAK(before, after);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 2 — Op Registration
   ════════════════════════════════════════════════════════════════════ */

static int null_cb(int fd, int result, void *ud, void *buf, size_t len)
{
    (void)fd; (void)result; (void)ud; (void)buf; (void)len;
    return 0;
}

TEST(IoUringLoop, RegisterOp_ValidTimeout_ReturnsSlot)
{
    io_op_desc_t d = {
        .fd = -1, .op_type = IO_OP_TIMEOUT,
        .buf_size = 0, .callback = null_cb,
        .timeout_ms = 100,
    };
    int slot = io_loop_register_op(g_loop, &d);
    TEST_ASSERT_TRUE_MESSAGE(slot >= 0, "Expected non-negative slot index");
}

TEST(IoUringLoop, RegisterOp_ValidRead_ReturnsSlot)
{
    int fds[2];
    tu_socketpair(fds);

    io_op_desc_t d = {
        .fd = fds[0], .op_type = IO_OP_READ,
        .buf_size = 64, .callback = null_cb,
    };
    int slot = io_loop_register_op(g_loop, &d);
    TEST_ASSERT_TRUE_MESSAGE(slot >= 0, "Expected slot for READ op");

    close(fds[0]); close(fds[1]);
}

TEST(IoUringLoop, ClearOps_ResetsSlotTable)
{
    io_op_desc_t d = {
        .fd = -1, .op_type = IO_OP_TIMEOUT,
        .buf_size = 0, .callback = null_cb,
        .timeout_ms = 100,
    };
    io_loop_register_op(g_loop, &d);
    io_loop_clear_ops(g_loop);

    /* After clear, register again at slot 0 */
    int slot = io_loop_register_op(g_loop, &d);
    TEST_ASSERT_EQUAL_INT(0, slot);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 3 — Timeout Fires
   ════════════════════════════════════════════════════════════════════ */

typedef struct {
    _Atomic int  fired;
    volatile sig_atomic_t stop;
} timeout_ctx_t;

static int timeout_cb(int fd, int result, void *ud, void *buf, size_t len)
{
    (void)fd; (void)result; (void)buf; (void)len;
    timeout_ctx_t *ctx = (timeout_ctx_t *)ud;
    atomic_fetch_add(&ctx->fired, 1);
    ctx->stop = 0;  /* signal loop to exit */
    return 0;
}

static void *run_loop_thread(void *arg)
{
    void **a = (void **)arg;
    io_uring_loop_t *loop = (io_uring_loop_t *)a[0];
    volatile sig_atomic_t *stop = (volatile sig_atomic_t *)a[1];
    io_loop_run(loop, stop);
    return NULL;
}

TEST(IoUringLoop, Timeout_FiresWithinWindow)
{
    timeout_ctx_t ctx;
    atomic_init(&ctx.fired, 0);
    ctx.stop = 1;

    io_op_desc_t d = {
        .fd = -1, .op_type = IO_OP_TIMEOUT,
        .buf_size = 0, .callback = timeout_cb,
        .user_data = &ctx, .timeout_ms = 50,
    };
    io_loop_register_op(g_loop, &d);

    /* Run loop in thread, stop after callback fires */
    void *args[2] = { g_loop, &ctx.stop };
    pthread_t t;
    pthread_create(&t, NULL, run_loop_thread, args);

    uint64_t start = tu_now_ms();
    /* Wait up to 2 seconds for timeout to fire */
    while (atomic_load(&ctx.fired) == 0 && tu_now_ms() - start < 2000)
        tu_sleep_ms(10);
    ctx.stop = 0;
    io_loop_wakeup(g_loop);
    pthread_join(t, NULL);

    TEST_ASSERT_TRUE_MESSAGE(atomic_load(&ctx.fired) >= 1,
        "Timeout callback never fired");
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 4 — Poll Op (POLLIN)
   ════════════════════════════════════════════════════════════════════ */

typedef struct {
    _Atomic int  pollin_count;
    volatile sig_atomic_t stop;
    int          write_fd;
} poll_ctx_t;

static int poll_cb(int fd, int result, void *ud, void *buf, size_t len)
{
    (void)fd; (void)result; (void)buf; (void)len;
    poll_ctx_t *ctx = (poll_ctx_t *)ud;
    atomic_fetch_add(&ctx->pollin_count, 1);
    ctx->stop = 0; /* exit loop */
    return 0;
}

TEST(IoUringLoop, Poll_FiresOnReadableData)
{
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, tu_pipe_nonblock(fds));

    poll_ctx_t ctx;
    atomic_init(&ctx.pollin_count, 0);
    ctx.stop     = 1;
    ctx.write_fd = fds[1];

    io_op_desc_t d = {
        .fd = fds[0], .op_type = IO_OP_POLL,
        .buf_size = 0, .callback = poll_cb,
        .user_data = &ctx,
    };
    io_loop_register_op(g_loop, &d);

    void *args[2] = { g_loop, &ctx.stop };
    pthread_t t;
    pthread_create(&t, NULL, run_loop_thread, args);

    tu_sleep_ms(20);
    /* Inject data to trigger POLLIN */
    write(fds[1], "x", 1);

    uint64_t start = tu_now_ms();
    while (atomic_load(&ctx.pollin_count) == 0 && tu_now_ms() - start < 2000)
        tu_sleep_ms(10);
    ctx.stop = 0;
    io_loop_wakeup(g_loop);
    pthread_join(t, NULL);

    close(fds[0]); close(fds[1]);

    TEST_ASSERT_TRUE_MESSAGE(atomic_load(&ctx.pollin_count) >= 1,
        "POLL callback never fired");
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 5 — Write Submission
   ════════════════════════════════════════════════════════════════════ */

typedef struct {
    _Atomic int  write_cb_called;
    volatile sig_atomic_t stop;
} write_ctx_t;

static int write_complete_cb(int fd, int result, void *ud, void *buf, size_t len)
{
    (void)fd; (void)result; (void)buf; (void)len;
    write_ctx_t *ctx = (write_ctx_t *)ud;
    atomic_fetch_add(&ctx->write_cb_called, 1);
    ctx->stop = 0;
    return 0;
}

TEST(IoUringLoop, SubmitWrite_DataArrivesOnPeer)
{
    int fds[2];
    tu_socketpair(fds);

    write_ctx_t ctx;
    atomic_init(&ctx.write_cb_called, 0);
    ctx.stop = 1;

    void *args[2] = { g_loop, &ctx.stop };
    pthread_t t;
    pthread_create(&t, NULL, run_loop_thread, args);

    tu_sleep_ms(20);
    const char *msg = "hello";
    int r = io_loop_submit_write(g_loop, fds[0], msg, 5,
                                  write_complete_cb, &ctx);
    TEST_ASSERT_TRUE_MESSAGE(r == 0, "io_loop_submit_write failed");

    uint64_t start = tu_now_ms();
    while (atomic_load(&ctx.write_cb_called) == 0 && tu_now_ms() - start < 2000)
        tu_sleep_ms(10);
    ctx.stop = 0;
    io_loop_wakeup(g_loop);
    pthread_join(t, NULL);

    /* Verify data arrived on peer */
    char rbuf[8] = {0};
    ssize_t n = read(fds[1], rbuf, sizeof(rbuf));
    close(fds[0]); close(fds[1]);

    TEST_ASSERT_TRUE_MESSAGE(n == 5, "Written data not received on peer");
    TEST_ASSERT_EQUAL_STRING_LEN("hello", rbuf, 5);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 6 — Wakeup from Other Thread
   ════════════════════════════════════════════════════════════════════ */

static void *wakeup_thread(void *arg)
{
    io_uring_loop_t *loop = (io_uring_loop_t *)arg;
    tu_sleep_ms(50);
    io_loop_wakeup(loop);
    return NULL;
}

TEST(IoUringLoop, Wakeup_CausesRunToReturn)
{
    volatile sig_atomic_t stop = 1;
    pthread_t t;
    pthread_create(&t, NULL, wakeup_thread, g_loop);

    uint64_t start = tu_now_ms();
    io_loop_run(g_loop, &stop);
    uint64_t elapsed = tu_now_ms() - start;

    pthread_join(t, NULL);

    /* io_loop_run should have returned well under 2 seconds */
    TEST_ASSERT_TRUE_MESSAGE(elapsed < 2000,
        "io_loop_run did not return after wakeup within 2s");
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(IoUringLoop)
{
    RUN_TEST_CASE(IoUringLoop, Create_ValidConfig_ReturnsNonNull);
    RUN_TEST_CASE(IoUringLoop, Create_NullConfig_ReturnsNull);
    RUN_TEST_CASE(IoUringLoop, Create_ZeroQueueDepth_ReturnsNull);
    RUN_TEST_CASE(IoUringLoop, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(IoUringLoop, CreateDestroy_NoFdLeak);
    RUN_TEST_CASE(IoUringLoop, RegisterOp_ValidTimeout_ReturnsSlot);
    RUN_TEST_CASE(IoUringLoop, RegisterOp_ValidRead_ReturnsSlot);
    RUN_TEST_CASE(IoUringLoop, ClearOps_ResetsSlotTable);
    RUN_TEST_CASE(IoUringLoop, Timeout_FiresWithinWindow);
    RUN_TEST_CASE(IoUringLoop, Poll_FiresOnReadableData);
    RUN_TEST_CASE(IoUringLoop, SubmitWrite_DataArrivesOnPeer);
    RUN_TEST_CASE(IoUringLoop, Wakeup_CausesRunToReturn);
}

static void run_all_groups(void)
{
    RUN_TEST_GROUP(IoUringLoop);
}

int main(int argc, const char *argv[])
{
    return UnityMain(argc, argv, run_all_groups);
}
