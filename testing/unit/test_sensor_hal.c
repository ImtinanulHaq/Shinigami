/**
 * @file test_sensor_hal.c
 * @brief Sensor HAL unit tests.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hardware.h"

#include "../../dev/hal/layers/sensors/sensor_hal.h"
#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>
#include <math.h>

static mock_sensor_hw_t g_mock_hw;

static const sensor_config_t g_cfg = {
    .sample_rate_hz = 100,
    .range_g        = 4,
};

TEST_GROUP(SensorHAL);

TEST_SETUP(SensorHAL)
{
    mock_sensor_hw_create(&g_mock_hw);
}

TEST_TEAR_DOWN(SensorHAL)
{
    mock_sensor_hw_destroy(&g_mock_hw);
}

/* ── Argument Validation ──────────────────────────────────────────── */

TEST(SensorHAL, Create_NullName_ReturnsNull)
{
    hw_device_t *d = sensor_hal_create(NULL, "/dev/iio:device0", &g_cfg);
    TEST_ASSERT_NULL(d);
    if (d) sensor_hal_destroy(d);
}

TEST(SensorHAL, Create_NullConfig_ReturnsNull)
{
    hw_device_t *d = sensor_hal_create("accel", "/dev/iio:device0", NULL);
    TEST_ASSERT_NULL(d);
    if (d) sensor_hal_destroy(d);
}

TEST(SensorHAL, DefaultConfig_NonZeroSampleRate)
{
    sensor_config_t cfg = sensor_hal_default_config();
    TEST_ASSERT_TRUE(cfg.sample_rate_hz > 0);
}

TEST(SensorHAL, Read3Axis_NullDev_Fails)
{
    float x, y, z;
    int r = sensor_hal_read_3axis(NULL, &x, &y, &z);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(SensorHAL, Read3Axis_NullOutputPtr_Fails)
{
    hw_device_t *d = sensor_hal_create("accel", "", &g_cfg);
    if (!d) TEST_IGNORE_MESSAGE("sensor_hal_create with empty path skipped");
    float x;
    int r = sensor_hal_read_3axis(d, &x, NULL, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
    sensor_hal_destroy(d);
}

TEST(SensorHAL, Read1Axis_NullDev_Fails)
{
    float v;
    int r = sensor_hal_read_1axis(NULL, &v);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(SensorHAL, Read1Axis_NullOut_Fails)
{
    hw_device_t *d = sensor_hal_create("accel", "", &g_cfg);
    if (!d) TEST_IGNORE_MESSAGE("sensor_hal_create with empty path skipped");
    int r = sensor_hal_read_1axis(d, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
    sensor_hal_destroy(d);
}

TEST(SensorHAL, DestroyNull_DoesNotCrash)
{
    sensor_hal_destroy(NULL);
}

/* ── Mock sensor pipe tests ───────────────────────────────────────── */

TEST(SensorHAL, MockHW_Trigger3Axis_SignalDelivered)
{
    /* Inject an accelerometer event through the mock pipe */
    sensor_3axis_t axes = { .x = 1.5f, .y = -0.5f, .z = 9.81f };
    int r = mock_sensor_hw_trigger_3axis(&g_mock_hw, &axes);
    TEST_ASSERT_TRUE(r >= 0);

    /* The mock write fd is now readable; verify size */
    sensor_3axis_t out;
    ssize_t n = read(g_mock_hw.read_fd, &out, sizeof(out));
    TEST_ASSERT_EQUAL_INT((int)sizeof(out), (int)n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, axes.x, out.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, axes.y, out.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, axes.z, out.z);
}

TEST(SensorHAL, MockHW_Trigger1Axis_SignalDelivered)
{
    float value = 42.0f;
    int r = mock_sensor_hw_trigger_1axis(&g_mock_hw, value);
    TEST_ASSERT_TRUE(r >= 0);

    float out;
    ssize_t n = read(g_mock_hw.read_fd, &out, sizeof(out));
    TEST_ASSERT_EQUAL_INT((int)sizeof(out), (int)n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, value, out);
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(SensorHAL)
{
    RUN_TEST_CASE(SensorHAL, Create_NullName_ReturnsNull);
    RUN_TEST_CASE(SensorHAL, Create_NullConfig_ReturnsNull);
    RUN_TEST_CASE(SensorHAL, DefaultConfig_NonZeroSampleRate);
    RUN_TEST_CASE(SensorHAL, Read3Axis_NullDev_Fails);
    RUN_TEST_CASE(SensorHAL, Read3Axis_NullOutputPtr_Fails);
    RUN_TEST_CASE(SensorHAL, Read1Axis_NullDev_Fails);
    RUN_TEST_CASE(SensorHAL, Read1Axis_NullOut_Fails);
    RUN_TEST_CASE(SensorHAL, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(SensorHAL, MockHW_Trigger3Axis_SignalDelivered);
    RUN_TEST_CASE(SensorHAL, MockHW_Trigger1Axis_SignalDelivered);
}

static void run_all_groups(void) { RUN_TEST_GROUP(SensorHAL); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
