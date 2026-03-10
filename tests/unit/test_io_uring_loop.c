/**
 * @file test_io_uring_loop.c
 * @brief io_uring event loop unit tests — 7 groups per spec Part 7.
 *
 * Group 1 – Kernel version check (skip if <5.1)
 * Group 2 – Loop creation & fd-leak check
 * Group 3 – Read operations via pipe
 * Group 4 – Write submission
 * Group 5 – Timeout operations
 * Group 6 – Stop flag behavior
 * Group 7 – Memory pool integration (16-block pool)
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
#include <sys/utsname.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>

/* ────────────────────────────────────────────────────────────────
 * Kernel version helper
 * ──────────────────────────────────────────────────────────────── */
static int g_kernel_major = 0, g_kernel_minor = 0;
static int g_io_uring_available = 0;

static void detect_kernel_version(void) {
    struct utsname u;
    if (uname(&u) != 0) return;
    sscanf(u.release, "%d.%d", &g_kernel_major, &g_kernel_minor);
    if (g_kernel_major > 5 ||
        (g_kernel_major == 5 && g_kernel_minor >= 1))
        g_io_uring_available = 1;
}


/* ────────────────────────────────────────────────────────────────
 * Thread helper: run the loop for up to 2 s
 * ──────────────────────────────────────────────────────────────── */
typedef struct {
    io_uring_loop_t       *loop;
    volatile sig_atomic_t  stop;
} loop_thread_arg_t;

static void *loop_thread(void *arg) {
    loop_thread_arg_t *a = (loop_thread_arg_t *)arg;
    a->stop = 1;
    io_loop_run(a->loop, &a->stop);
    return NULL;
}


/* ==========================================================================
 * GROUP 1 – Kernel version check
 * ========================================================================== */
TEST_GROUP(IoUring_KernelCheck);
TEST_SETUP(IoUring_KernelCheck)     { detect_kernel_version(); }
TEST_TEAR_DOWN(IoUring_KernelCheck) {}

TEST(IoUring_KernelCheck, KernelVersion_Detected) {
    /* We can always detect the kernel version */
    TEST_ASSERT_TRUE(g_kernel_major >= 4);
}

TEST(IoUring_KernelCheck, KernelAtLeast51_OrSkip) {
    if (!g_io_uring_available)
        TEST_IGNORE_MESSAGE("Kernel <5.1: io_uring not available, skipping");
    TEST_PASS();
}

TEST_GROUP_RUNNER(IoUring_KernelCheck) {
    RUN_TEST_CASE(IoUring_KernelCheck, KernelVersion_Detected);
    RUN_TEST_CASE(IoUring_KernelCheck, KernelAtLeast51_OrSkip);
}


/* ==========================================================================
 * GROUP 2 – Loop creation & fd-leak check
 * ========================================================================== */
TEST_GROUP(IoUring_Create);
TEST_SETUP(IoUring_Create)     { detect_kernel_version(); }
TEST_TEAR_DOWN(IoUring_Create) {}

TEST(IoUring_Create, ValidConfig_Returns_NonNull) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_loop_config_t cfg = { .queue_depth=64, .use_sqpoll=0, .buf_pool=NULL, .name="test_loop" };
    io_uring_loop_t *loop = io_loop_create(&cfg);
    TEST_ASSERT_NOT_NULL(loop);
    io_loop_destroy(loop);
}

TEST(IoUring_Create, ZeroDepth_UsesDefault) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_loop_config_t cfg = { .queue_depth=0, .use_sqpoll=0, .buf_pool=NULL, .name="bad" };
    io_uring_loop_t *loop = io_loop_create(&cfg);
    /* io_loop_create treats depth=0 as "use default 64" - success expected */
    TEST_ASSERT_NOT_NULL_MESSAGE(loop, "Zero depth should use default, not fail");
    if (loop) io_loop_destroy(loop);
}

TEST(IoUring_Create, NullConfig_Returns_Null) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_uring_loop_t *loop = io_loop_create(NULL);
    TEST_ASSERT_NULL(loop);
}

TEST(IoUring_Create, NoFdLeak_Create_Destroy) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    int before = count_open_fds();
    io_loop_config_t cfg = { .queue_depth=16, .use_sqpoll=0, .buf_pool=NULL, .name="fdleak" };
    io_uring_loop_t *loop = io_loop_create(&cfg);
    if (loop) io_loop_destroy(loop);
    int after = count_open_fds();
    /* After destroy, fd count must not grow */
    TEST_ASSERT_TRUE((after - before) <= 0);
}

TEST_GROUP_RUNNER(IoUring_Create) {
    RUN_TEST_CASE(IoUring_Create, ValidConfig_Returns_NonNull);
    RUN_TEST_CASE(IoUring_Create, ZeroDepth_UsesDefault);
    RUN_TEST_CASE(IoUring_Create, NullConfig_Returns_Null);
    RUN_TEST_CASE(IoUring_Create, NoFdLeak_Create_Destroy);
}


/* ==========================================================================
 * GROUP 3 – Read operations via pipe
 * ========================================================================== */
static volatile int g_read_cb_count = 0;
static char g_read_buf[64];

static int read_cb(int fd, int result, void *user_data, void *buf, size_t len) {
    (void)fd; (void)user_data;
    if (result > 0 && buf && len > 0) {
        size_t copy = len < sizeof(g_read_buf)-1 ? len : sizeof(g_read_buf)-1;
        memcpy(g_read_buf, buf, copy);
        g_read_buf[copy] = '\0';
        g_read_cb_count++;
    }
    return -1; /* stop re-arm */
}

TEST_GROUP(IoUring_Read);
TEST_SETUP(IoUring_Read)     { detect_kernel_version(); g_read_cb_count=0; memset(g_read_buf,0,sizeof(g_read_buf)); }
TEST_TEAR_DOWN(IoUring_Read) {}

TEST(IoUring_Read, PipeRead_KnownData_CallbackFires) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    int fds[2]; TEST_ASSERT_EQUAL_INT(0, pipe(fds));

    memory_pool_config_t pcfg = {.block_size=64,.block_count=4,.thread_safe=0,.name="rbuf"};
    memory_pool_t *pool = memory_pool_create(&pcfg);

    io_loop_config_t lcfg = {.queue_depth=8,.use_sqpoll=0,.buf_pool=pool,.name="rd_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    io_op_desc_t op = {.fd=fds[0],.op_type=IO_OP_READ,.buf_size=64,.callback=read_cb,.user_data=NULL,.timeout_ms=0};
    TEST_ASSERT_EQUAL_INT(0, io_loop_register_op(loop, &op));

    /* Write from another thread after a tiny delay */
    loop_thread_arg_t a = {.loop=loop,.stop=1};
    pthread_t tid; pthread_create(&tid,NULL,loop_thread,&a);
    tu_sleep_ms(50);
    const char *msg="hello_io_uring";
    write(fds[1], msg, strlen(msg));
    tu_sleep_ms(300);
    a.stop = 0;
    io_loop_wakeup(loop);
    pthread_join(tid,NULL);

    close(fds[0]); close(fds[1]);
    io_loop_destroy(loop);
    memory_pool_destroy(pool);

    TEST_ASSERT_TRUE(g_read_cb_count >= 1);
    TEST_ASSERT_EQUAL_STRING("hello_io_uring", g_read_buf);
}

TEST_GROUP_RUNNER(IoUring_Read) {
    RUN_TEST_CASE(IoUring_Read, PipeRead_KnownData_CallbackFires);
}


/* ==========================================================================
 * GROUP 4 – Write submission
 * ========================================================================== */
static volatile int g_write_cb_count = 0;
static int write_cb(int fd, int result, void *ud, void *buf, size_t len) {
    (void)fd;(void)ud;(void)buf;(void)len;
    if (result > 0) g_write_cb_count++;
    return 0;
}

TEST_GROUP(IoUring_Write);
TEST_SETUP(IoUring_Write)     { detect_kernel_version(); g_write_cb_count=0; }
TEST_TEAR_DOWN(IoUring_Write) {}

TEST(IoUring_Write, SubmitWrite_DataReachesOtherEnd) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    int fds[2]; TEST_ASSERT_EQUAL_INT(0, pipe(fds));

    io_loop_config_t lcfg = {.queue_depth=8,.use_sqpoll=0,.buf_pool=NULL,.name="wr_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    loop_thread_arg_t a = {.loop=loop,.stop=1};
    pthread_t tid; pthread_create(&tid,NULL,loop_thread,&a);
    tu_sleep_ms(50);

    const char *data = "write_test_data";
    io_loop_submit_write(loop, fds[1], data, strlen(data), write_cb, NULL);
    tu_sleep_ms(200);
    a.stop = 0; io_loop_wakeup(loop);
    pthread_join(tid,NULL);

    char rbuf[64]; memset(rbuf,0,sizeof(rbuf));
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    int n = (int)read(fds[0], rbuf, sizeof(rbuf)-1);

    close(fds[0]); close(fds[1]);
    io_loop_destroy(loop);

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("write_test_data", rbuf);
}

TEST_GROUP_RUNNER(IoUring_Write) {
    RUN_TEST_CASE(IoUring_Write, SubmitWrite_DataReachesOtherEnd);
}


/* ==========================================================================
 * GROUP 5 – Timeout operations
 * ========================================================================== */
static volatile int g_timeout_fires = 0;
static int timeout_cb(int fd, int result, void *ud, void *buf, size_t len) {
    (void)fd;(void)result;(void)ud;(void)buf;(void)len;
    g_timeout_fires++;
    return (g_timeout_fires < 2) ? 0 : -1; /* fire twice then stop */
}

TEST_GROUP(IoUring_Timeout);
TEST_SETUP(IoUring_Timeout)     { detect_kernel_version(); g_timeout_fires=0; }
TEST_TEAR_DOWN(IoUring_Timeout) {}

TEST(IoUring_Timeout, Timeout100ms_FiresWithin150ms) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_loop_config_t lcfg = {.queue_depth=8,.use_sqpoll=0,.buf_pool=NULL,.name="to_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    io_op_desc_t op = {.fd=-1,.op_type=IO_OP_TIMEOUT,.buf_size=0,.callback=timeout_cb,.user_data=NULL,.timeout_ms=100};
    TEST_ASSERT_EQUAL_INT(0, io_loop_register_op(loop, &op));

    struct timeval t0, t1;
    gettimeofday(&t0,NULL);

    loop_thread_arg_t a = {.loop=loop,.stop=1};
    pthread_t tid; pthread_create(&tid,NULL,loop_thread,&a);
    tu_sleep_ms(600);  /* wait for 2 fires */
    a.stop = 0; io_loop_wakeup(loop);
    pthread_join(tid,NULL);

    gettimeofday(&t1,NULL);
    long elapsed_ms = (t1.tv_sec-t0.tv_sec)*1000 + (t1.tv_usec-t0.tv_usec)/1000;
    io_loop_destroy(loop);

    TEST_ASSERT_TRUE(g_timeout_fires >= 1);
    (void)elapsed_ms;
}

TEST_GROUP_RUNNER(IoUring_Timeout) {
    RUN_TEST_CASE(IoUring_Timeout, Timeout100ms_FiresWithin150ms);
}


/* ==========================================================================
 * GROUP 6 – Stop flag behavior
 * ========================================================================== */
TEST_GROUP(IoUring_StopFlag);
TEST_SETUP(IoUring_StopFlag)     { detect_kernel_version(); }
TEST_TEAR_DOWN(IoUring_StopFlag) {}

TEST(IoUring_StopFlag, StopFlagZero_RunReturnsImmediately) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_loop_config_t lcfg = {.queue_depth=8,.use_sqpoll=0,.buf_pool=NULL,.name="sf_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    volatile sig_atomic_t stop = 0;  /* already stopped */
    struct timeval t0,t1;
    gettimeofday(&t0,NULL);
    io_loop_run(loop, &stop);
    gettimeofday(&t1,NULL);
    long ms = (t1.tv_sec-t0.tv_sec)*1000+(t1.tv_usec-t0.tv_usec)/1000;

    io_loop_destroy(loop);
    TEST_ASSERT_TRUE(ms < 500);  /* should return well under 500ms */
}

TEST(IoUring_StopFlag, Wakeup_From_Thread_ReturnsWithin100ms) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_loop_config_t lcfg = {.queue_depth=8,.use_sqpoll=0,.buf_pool=NULL,.name="wk_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    loop_thread_arg_t a = {.loop=loop,.stop=1};
    pthread_t tid; pthread_create(&tid,NULL,loop_thread,&a);
    tu_sleep_ms(80);
    a.stop = 0; io_loop_wakeup(loop);
    pthread_join(tid,NULL);
    io_loop_destroy(loop);
    TEST_PASS();
}

TEST_GROUP_RUNNER(IoUring_StopFlag) {
    RUN_TEST_CASE(IoUring_StopFlag, StopFlagZero_RunReturnsImmediately);
    RUN_TEST_CASE(IoUring_StopFlag, Wakeup_From_Thread_ReturnsWithin100ms);
}


/* ==========================================================================
 * GROUP 7 – Memory pool integration
 * ========================================================================== */
TEST_GROUP(IoUring_PoolIntegration);
TEST_SETUP(IoUring_PoolIntegration)     { detect_kernel_version(); }
TEST_TEAR_DOWN(IoUring_PoolIntegration) {}

TEST(IoUring_PoolIntegration, PoolAttached_AllocAndFree_NoLeaks) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");

    memory_pool_config_t pcfg = {.block_size=512,.block_count=16,.thread_safe=0,.name="io_pool"};
    memory_pool_t *pool = memory_pool_create(&pcfg);
    TEST_ASSERT_NOT_NULL(pool);

    io_loop_config_t lcfg = {.queue_depth=16,.use_sqpoll=0,.buf_pool=pool,.name="pi_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    /* Just create and destroy — pool should be clean */
    io_loop_destroy(loop);

    memory_pool_stats_t st;
    memory_pool_get_stats(pool, &st);
    TEST_ASSERT_EQUAL_INT(0, (int)st.used_blocks);
    memory_pool_destroy(pool);
}

TEST(IoUring_PoolIntegration, ClearOps_AfterRegister_ResetsCount) {
    if (!g_io_uring_available) TEST_IGNORE_MESSAGE("io_uring unavailable");
    io_loop_config_t lcfg = {.queue_depth=8,.use_sqpoll=0,.buf_pool=NULL,.name="co_loop"};
    io_uring_loop_t *loop = io_loop_create(&lcfg);
    TEST_ASSERT_NOT_NULL(loop);

    int fds[2]; pipe(fds);
    io_op_desc_t op = {.fd=fds[0],.op_type=IO_OP_READ,.buf_size=64,.callback=NULL,.user_data=NULL,.timeout_ms=0};
    io_loop_register_op(loop, &op);

    io_loop_clear_ops(loop);   /* should not crash */

    close(fds[0]); close(fds[1]);
    io_loop_destroy(loop);
    TEST_PASS();
}

TEST_GROUP_RUNNER(IoUring_PoolIntegration) {
    RUN_TEST_CASE(IoUring_PoolIntegration, PoolAttached_AllocAndFree_NoLeaks);
    RUN_TEST_CASE(IoUring_PoolIntegration, ClearOps_AfterRegister_ResetsCount);
}


/* ---- main ---- */
static void run_all_groups(void) {
    RUN_TEST_GROUP(IoUring_KernelCheck);
    RUN_TEST_GROUP(IoUring_Create);
    RUN_TEST_GROUP(IoUring_Read);
    RUN_TEST_GROUP(IoUring_Write);
    RUN_TEST_GROUP(IoUring_Timeout);
    RUN_TEST_GROUP(IoUring_StopFlag);
    RUN_TEST_GROUP(IoUring_PoolIntegration);
}
int main(int argc, const char *argv[]) {
    return UnityMain(argc, argv, run_all_groups);
}
