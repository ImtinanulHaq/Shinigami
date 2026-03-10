/**
 * @file test_hal_interface.c
 * @brief HAL common interface unit tests.
 *
 * Tests hal_device_init/destroy, ref counting, locking,
 * and registry operations using direct struct manipulation.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hal.h"

#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* ── Fixture ──────────────────────────────────────────────────────── */

static hw_device_t g_dev;
static mock_hal_state_t *g_mock;

static const hw_device_ops_t g_mock_ops = {
    .open     = mock_hal_open,
    .close    = mock_hal_close,
    .start    = mock_hal_start,
    .stop     = mock_hal_stop,
    .read     = mock_hal_read,
    .write    = (int(*)(struct hw_device*, const void*, size_t))mock_hal_write,
    .control  = (int(*)(struct hw_device*, uint32_t, void*))mock_hal_control,
    .get_info = NULL,
    .reset    = NULL,
};

TEST_GROUP(HAL_Interface);

TEST_SETUP(HAL_Interface)
{
    g_mock = mock_hal_get_state();
    mock_hal_reset(g_mock);
    memset(&g_dev, 0, sizeof(g_dev));
    hal_device_init(&g_dev, "test_dev", HAL_DEVICE_TYPE_AUDIO);
    g_dev.ops = &g_mock_ops;
}

TEST_TEAR_DOWN(HAL_Interface)
{
    hal_device_unregister(&g_dev);
    hal_device_destroy(&g_dev);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 1 — Init / Destroy
   ════════════════════════════════════════════════════════════════════ */

TEST(HAL_Interface, Init_ValidParams_Succeeds)
{
    hw_device_t dev2;
    int r = hal_device_init(&dev2, "dev2", HAL_DEVICE_TYPE_CAMERA);
    TEST_ASSERT_TRUE_MESSAGE(r == HAL_SUCCESS || r >= 0, "init should succeed");
    hal_device_destroy(&dev2);
}

TEST(HAL_Interface, Init_NullName_ReturnsError)
{
    hw_device_t dev2;
    int r = hal_device_init(&dev2, NULL, HAL_DEVICE_TYPE_AUDIO);
    TEST_ASSERT_TRUE_MESSAGE(r != HAL_SUCCESS, "NULL name should fail");
}

TEST(HAL_Interface, Init_SetsStateToClosedAndMagic)
{
    TEST_ASSERT_EQUAL_INT(HAL_STATE_CLOSED, (int)g_dev.state);
    TEST_ASSERT_EQUAL_UINT32(HAL_DEVICE_MAGIC, g_dev.magic);
}

TEST(HAL_Interface, Init_NameStoredCorrectly)
{
    TEST_ASSERT_EQUAL_STRING("test_dev", g_dev.name);
}

TEST(HAL_Interface, DestroyNull_DoesNotCrash)
{
    hal_device_destroy(NULL);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 2 — Ref Counting
   ════════════════════════════════════════════════════════════════════ */

TEST(HAL_Interface, RefCount_StartsAt1)
{
    TEST_ASSERT_EQUAL_INT(1, atomic_load(&g_dev.ref_count));
}

TEST(HAL_Interface, Ref_IncrementsCount)
{
    hal_device_ref(&g_dev);
    TEST_ASSERT_EQUAL_INT(2, atomic_load(&g_dev.ref_count));
    hal_device_unref(&g_dev); /* restore */
}

TEST(HAL_Interface, Unref_DecrementsCount)
{
    hal_device_ref(&g_dev);   /* → 2 */
    hal_device_unref(&g_dev); /* → 1 */
    TEST_ASSERT_EQUAL_INT(1, atomic_load(&g_dev.ref_count));
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 3 — Lock / Unlock
   ════════════════════════════════════════════════════════════════════ */

TEST(HAL_Interface, Lock_Unlock_DoesNotDeadlock)
{
    hal_device_lock(&g_dev);
    hal_device_unlock(&g_dev);
    /* If we get here, no deadlock */
}

TEST(HAL_Interface, RdLock_Unlock_Works)
{
    hal_device_rdlock(&g_dev);
    hal_device_unlock(&g_dev);
}

typedef struct { hw_device_t *dev; int acquired; } lock_arg_t;

static void *try_lock_thread(void *arg)
{
    lock_arg_t *a = (lock_arg_t *)arg;
    tu_sleep_ms(30);
    hal_device_lock(a->dev);
    a->acquired = 1;
    hal_device_unlock(a->dev);
    return NULL;
}

TEST(HAL_Interface, ConcurrentLock_SecondThreadBlocksUntilUnlocked)
{
    lock_arg_t la = { .dev = &g_dev, .acquired = 0 };
    hal_device_lock(&g_dev);  /* hold lock */

    pthread_t t;
    pthread_create(&t, NULL, try_lock_thread, &la);

    tu_sleep_ms(80); /* thread tries to lock while we hold it */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, la.acquired,
        "Second thread should not have acquired lock yet");

    hal_device_unlock(&g_dev);
    pthread_join(t, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, la.acquired,
        "Second thread should have acquired lock after unlock");
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 4 — Registry
   ════════════════════════════════════════════════════════════════════ */

TEST(HAL_Interface, Register_ThenFindByName_ReturnsDevice)
{
    int r = hal_device_register(&g_dev);
    TEST_ASSERT_TRUE_MESSAGE(r == HAL_SUCCESS || r >= 0, "register should succeed");

    hw_device_t *found = hal_device_find("test_dev");
    TEST_ASSERT_NOT_NULL_MESSAGE(found, "Should find registered device");
    TEST_ASSERT_EQUAL_STRING("test_dev", found->name);
    hal_device_unref(found); /* release the ref given by find */
}

TEST(HAL_Interface, Unregister_ThenFindByName_ReturnsNull)
{
    hal_device_register(&g_dev);
    hal_device_unregister(&g_dev);
    hw_device_t *found = hal_device_find("test_dev");
    TEST_ASSERT_NULL_MESSAGE(found, "Unregistered device should not be found");
}

TEST(HAL_Interface, FindNonExistent_ReturnsNull)
{
    hw_device_t *found = hal_device_find("no_such_device");
    TEST_ASSERT_NULL(found);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 5 — Vtable Dispatch via Mock
   ════════════════════════════════════════════════════════════════════ */

TEST(HAL_Interface, VtableDispatch_OpenCallsOps)
{
    g_dev.ops->open(&g_dev);
    TEST_ASSERT_EQUAL_INT(1, g_mock->open_calls);
}

TEST(HAL_Interface, VtableDispatch_ReadCallsOps)
{
    g_mock->read_ret = 8;
    g_mock->read_data_len = 8;
    memset(g_mock->read_data, 0x42, 8);
    uint8_t buf[8];
    g_dev.ops->read(&g_dev, buf, 8);
    TEST_ASSERT_EQUAL_INT(1, g_mock->read_calls);
    TEST_ASSERT_EQUAL_UINT8(0x42, buf[0]);
}

TEST(HAL_Interface, OpenReturnsError_WhenOpsFails)
{
    g_mock->open_ret = HAL_ERROR_NO_DEVICE;
    int r = g_dev.ops->open(&g_dev);
    TEST_ASSERT_EQUAL_INT(HAL_ERROR_NO_DEVICE, r);
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(HAL_Interface)
{
    RUN_TEST_CASE(HAL_Interface, Init_ValidParams_Succeeds);
    RUN_TEST_CASE(HAL_Interface, Init_NullName_ReturnsError);
    RUN_TEST_CASE(HAL_Interface, Init_SetsStateToClosedAndMagic);
    RUN_TEST_CASE(HAL_Interface, Init_NameStoredCorrectly);
    RUN_TEST_CASE(HAL_Interface, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(HAL_Interface, RefCount_StartsAt1);
    RUN_TEST_CASE(HAL_Interface, Ref_IncrementsCount);
    RUN_TEST_CASE(HAL_Interface, Unref_DecrementsCount);
    RUN_TEST_CASE(HAL_Interface, Lock_Unlock_DoesNotDeadlock);
    RUN_TEST_CASE(HAL_Interface, RdLock_Unlock_Works);
    RUN_TEST_CASE(HAL_Interface, ConcurrentLock_SecondThreadBlocksUntilUnlocked);
    RUN_TEST_CASE(HAL_Interface, Register_ThenFindByName_ReturnsDevice);
    RUN_TEST_CASE(HAL_Interface, Unregister_ThenFindByName_ReturnsNull);
    RUN_TEST_CASE(HAL_Interface, FindNonExistent_ReturnsNull);
    RUN_TEST_CASE(HAL_Interface, VtableDispatch_OpenCallsOps);
    RUN_TEST_CASE(HAL_Interface, VtableDispatch_ReadCallsOps);
    RUN_TEST_CASE(HAL_Interface, OpenReturnsError_WhenOpsFails);
}

static void run_all_groups(void)
{
    RUN_TEST_GROUP(HAL_Interface);
}

int main(int argc, const char *argv[])
{
    return UnityMain(argc, argv, run_all_groups);
}
