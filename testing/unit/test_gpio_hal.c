/**
 * @file test_gpio_hal.c
 * @brief GPIO HAL unit tests.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hardware.h"

#include "../../dev/hal/layers/gpio/gpio_hal.h"
#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>

static mock_gpio_hw_t g_mock_hw;

static const gpio_config_t g_cfg = {
    .direction         = GPIO_DIRECTION_OUTPUT,
    .initial_value     = 0,
    .active_low        = 0,
    .interrupt_trigger = GPIO_INTERRUPT_NONE,
};

TEST_GROUP(GpioHAL);

TEST_SETUP(GpioHAL)
{
    mock_gpio_hw_create(&g_mock_hw, 4 /* pin */);
}

TEST_TEAR_DOWN(GpioHAL)
{
    mock_gpio_hw_destroy(&g_mock_hw);
}

/* ── Argument Validation ──────────────────────────────────────────── */

TEST(GpioHAL, Create_NullName_ReturnsNull)
{
    hw_device_t *d = gpio_hal_create(NULL, 4, NULL);
    TEST_ASSERT_NULL(d);
    if (d) gpio_hal_destroy(d);
}

TEST(GpioHAL, Create_NullConfig_UseDefaults_DoesNotCrash)
{
    /* NULL config may use defaults — whatever happens, must not crash */
    hw_device_t *d = gpio_hal_create("gpio_test", 4, NULL);
    /* Accept either NULL (error) or valid pointer */
    if (d) gpio_hal_destroy(d);
}

TEST(GpioHAL, SetValue_NullDev_Fails)
{
    int r = gpio_hal_set_value(NULL, GPIO_VALUE_HIGH);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(GpioHAL, GetValue_NullDev_Fails)
{
    gpio_value_t v;
    int r = gpio_hal_get_value(NULL, &v);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(GpioHAL, GetValue_NullOut_Fails)
{
    hw_device_t *d = gpio_hal_create("gpio_test", 4, &g_cfg);
    if (!d) TEST_IGNORE_MESSAGE("gpio_hal_create skipped (no sysfs)");
    int r = gpio_hal_get_value(d, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
    gpio_hal_destroy(d);
}

TEST(GpioHAL, Toggle_NullDev_Fails)
{
    int r = gpio_hal_toggle(NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(GpioHAL, WaitInterrupt_NullDev_Fails)
{
    int r = gpio_hal_wait_interrupt(NULL, 100);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(GpioHAL, GetInfo_NullDev_Fails)
{
    gpio_info_t info;
    int r = gpio_hal_get_info(NULL, &info);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(GpioHAL, DefaultConfig_DirectionOutput)
{
    gpio_config_t cfg = gpio_hal_default_config();
    TEST_ASSERT_EQUAL_INT(GPIO_DIRECTION_OUTPUT, cfg.direction);
}

TEST(GpioHAL, DestroyNull_DoesNotCrash)
{
    gpio_hal_destroy(NULL);
}

/* ── Mock GPIO pipe tests ─────────────────────────────────────────── */

TEST(GpioHAL, MockHW_SetHigh_ReadValueHigh)
{
    int r = mock_gpio_hw_set_value(&g_mock_hw, GPIO_VALUE_HIGH);
    TEST_ASSERT_EQUAL_INT(HAL_SUCCESS, r);
    TEST_ASSERT_EQUAL_INT(GPIO_VALUE_HIGH, g_mock_hw.current_value);
}

TEST(GpioHAL, MockHW_SetLow_ReadValueLow)
{
    mock_gpio_hw_set_value(&g_mock_hw, GPIO_VALUE_HIGH);
    int r = mock_gpio_hw_set_value(&g_mock_hw, GPIO_VALUE_LOW);
    TEST_ASSERT_EQUAL_INT(HAL_SUCCESS, r);
    TEST_ASSERT_EQUAL_INT(GPIO_VALUE_LOW, g_mock_hw.current_value);
}

TEST(GpioHAL, MockHW_InterruptTrigger_EventDelivered)
{
    int r = mock_gpio_hw_trigger_interrupt(&g_mock_hw);
    TEST_ASSERT_TRUE(r >= 0);

    /* event_fd is readable after trigger */
    uint8_t byte;
    ssize_t n = read(g_mock_hw.event_fd, &byte, 1);
    TEST_ASSERT_EQUAL_INT(1, (int)n);
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(GpioHAL)
{
    RUN_TEST_CASE(GpioHAL, Create_NullName_ReturnsNull);
    RUN_TEST_CASE(GpioHAL, Create_NullConfig_UseDefaults_DoesNotCrash);
    RUN_TEST_CASE(GpioHAL, SetValue_NullDev_Fails);
    RUN_TEST_CASE(GpioHAL, GetValue_NullDev_Fails);
    RUN_TEST_CASE(GpioHAL, GetValue_NullOut_Fails);
    RUN_TEST_CASE(GpioHAL, Toggle_NullDev_Fails);
    RUN_TEST_CASE(GpioHAL, WaitInterrupt_NullDev_Fails);
    RUN_TEST_CASE(GpioHAL, GetInfo_NullDev_Fails);
    RUN_TEST_CASE(GpioHAL, DefaultConfig_DirectionOutput);
    RUN_TEST_CASE(GpioHAL, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(GpioHAL, MockHW_SetHigh_ReadValueHigh);
    RUN_TEST_CASE(GpioHAL, MockHW_SetLow_ReadValueLow);
    RUN_TEST_CASE(GpioHAL, MockHW_InterruptTrigger_EventDelivered);
}

static void run_all_groups(void) { RUN_TEST_GROUP(GpioHAL); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
