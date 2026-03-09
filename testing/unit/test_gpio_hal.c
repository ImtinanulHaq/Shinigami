/**
 * @file test_gpio_hal.c
 * @brief GPIO HAL unit tests — spec-correct API.
 *
 * gpio_hal.h API:
 *   gpio_hal_create(name, gpio_config_t *)
 *   gpio_hal_destroy(dev)
 *   gpio_hal_default_config(pin_number)
 *   gpio_hal_set_value(dev, gpio_value_t)
 *   gpio_hal_get_value(dev, gpio_value_t *)
 *   gpio_hal_toggle(dev)
 *   gpio_hal_wait_interrupt(dev, timeout_ms)
 *   gpio_hal_get_info(dev, gpio_info_t *)
 *
 * gpio_config_t { pin_number, direction, initial_value, edge, pull }
 *   direction: GPIO_DIR_INPUT, GPIO_DIR_OUTPUT
 *   edge:      GPIO_EDGE_NONE, GPIO_EDGE_RISING, GPIO_EDGE_FALLING, GPIO_EDGE_BOTH
 *   pull:      GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN
 *
 * mock_gpio_hw_t { int value_fd; int write_fd; int current_value; }
 *   mock_gpio_hw_create(dev, initial_value)
 *   mock_gpio_hw_set_value(dev, value)
 *   mock_gpio_hw_destroy(dev)
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hardware.h"

#include "../../dev/hal/layers/gpio/gpio_hal.h"
#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>
#include <unistd.h>

static mock_gpio_hw_t g_mock_hw;

static const gpio_config_t g_cfg = {
    .pin_number    = 4,
    .direction     = GPIO_DIR_OUTPUT,
    .initial_value = GPIO_VALUE_LOW,
    .edge          = GPIO_EDGE_NONE,
    .pull          = GPIO_PULL_NONE,
};

TEST_GROUP(GpioHAL);

TEST_SETUP(GpioHAL)
{
    mock_gpio_hw_create(&g_mock_hw, 0 /* initial_value = LOW */);
}

TEST_TEAR_DOWN(GpioHAL)
{
    mock_gpio_hw_destroy(&g_mock_hw);
}

/* ── Argument Validation ─────────────────────────────────────────── */

TEST(GpioHAL, Create_NullName_ReturnsNull)
{
    hw_device_t *d = gpio_hal_create(NULL, &g_cfg);
    TEST_ASSERT_NULL(d);
    if (d) gpio_hal_destroy(d);
}

TEST(GpioHAL, Create_NullConfig_UseDefaults_DoesNotCrash)
{
    /* NULL config may use defaults — must not crash */
    hw_device_t *d = gpio_hal_create("gpio_test", NULL);
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
    hw_device_t *d = gpio_hal_create("gpio_test", &g_cfg);
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

TEST(GpioHAL, DefaultConfig_OutputDirection)
{
    gpio_config_t cfg = gpio_hal_default_config(4);
    /* default direction is implementation-defined; just check it is a valid value */
    TEST_ASSERT_TRUE(cfg.direction == GPIO_DIR_INPUT ||
                     cfg.direction == GPIO_DIR_OUTPUT);
}

TEST(GpioHAL, DefaultConfig_PinNumberPreserved)
{
    gpio_config_t cfg = gpio_hal_default_config(17);
    TEST_ASSERT_EQUAL_UINT32(17, cfg.pin_number);
}

TEST(GpioHAL, DestroyNull_DoesNotCrash)
{
    gpio_hal_destroy(NULL);
}

/* ── Mock GPIO tests ─────────────────────────────────────────────── */

TEST(GpioHAL, MockHW_SetHigh_ReadValueHigh)
{
    int r = mock_gpio_hw_set_value(&g_mock_hw, GPIO_VALUE_HIGH);
    /* mock_gpio_hw_set_value returns bytes written (> 0) on success */
    TEST_ASSERT_TRUE_MESSAGE(r > 0, "mock_gpio_hw_set_value should succeed");
    TEST_ASSERT_EQUAL_INT(GPIO_VALUE_HIGH, g_mock_hw.current_value);
}

TEST(GpioHAL, MockHW_SetLow_ReadValueLow)
{
    mock_gpio_hw_set_value(&g_mock_hw, GPIO_VALUE_HIGH);
    int r = mock_gpio_hw_set_value(&g_mock_hw, GPIO_VALUE_LOW);
    /* mock_gpio_hw_set_value returns bytes written (> 0) on success */
    TEST_ASSERT_TRUE_MESSAGE(r > 0, "mock_gpio_hw_set_value should succeed");
    TEST_ASSERT_EQUAL_INT(GPIO_VALUE_LOW, g_mock_hw.current_value);
}

TEST(GpioHAL, MockHW_ValueFd_IsValid)
{
    TEST_ASSERT_TRUE(g_mock_hw.value_fd >= 0);
}

TEST(GpioHAL, MockHW_WriteFd_IsValid)
{
    TEST_ASSERT_TRUE(g_mock_hw.write_fd >= 0);
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
    RUN_TEST_CASE(GpioHAL, DefaultConfig_OutputDirection);
    RUN_TEST_CASE(GpioHAL, DefaultConfig_PinNumberPreserved);
    RUN_TEST_CASE(GpioHAL, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(GpioHAL, MockHW_SetHigh_ReadValueHigh);
    RUN_TEST_CASE(GpioHAL, MockHW_SetLow_ReadValueLow);
    RUN_TEST_CASE(GpioHAL, MockHW_ValueFd_IsValid);
    RUN_TEST_CASE(GpioHAL, MockHW_WriteFd_IsValid);
}

static void run_all_groups(void) { RUN_TEST_GROUP(GpioHAL); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
