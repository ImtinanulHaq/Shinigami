/**
 * @file test_sensor_hal.c
 * @brief Sensor HAL unit tests — spec-correct API.
 *
 * Types/functions from sensor_hal.h:
 *   sensor_data_3axis_t  { float x, y, z; uint64_t timestamp; }
 *   sensor_data_1axis_t  { float value; uint64_t timestamp; }
 *   sensor_config_t      { sensor_type_t type; uint32_t sampling_rate_hz;
 *                          uint32_t scale; int enable_buffer; }
 *   sensor_hal_create(name, iio_id, cfg)
 *   sensor_hal_destroy(dev)
 *   sensor_hal_default_config(sensor_type)
 *   sensor_hal_read_3axis(dev, data_out)
 *   sensor_hal_read_1axis(dev, data_out)
 *
 * Mock types from mock_hardware.h:
 *   mock_sensor_hw_t     { int event_fd; int trigger_fd; float x,y,z,scalar; }
 *   mock_sensor_hw_create(dev)
 *   mock_sensor_hw_trigger_3axis(dev, x, y, z)
 *   mock_sensor_hw_trigger_scalar(dev, val)
 *   mock_sensor_hw_destroy(dev)
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
#include <unistd.h>

static mock_sensor_hw_t g_mock_hw;

static const sensor_config_t g_cfg = {
    .type              = SENSOR_TYPE_ACCEL,
    .sampling_rate_hz  = 100,
    .scale             = 1,
    .enable_buffer     = 0,
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
    sensor_config_t cfg = sensor_hal_default_config(SENSOR_TYPE_ACCEL);
    TEST_ASSERT_TRUE(cfg.sampling_rate_hz > 0);
}

TEST(SensorHAL, DefaultConfig_Gyro_TypeSet)
{
    sensor_config_t cfg = sensor_hal_default_config(SENSOR_TYPE_GYRO);
    TEST_ASSERT_EQUAL_INT(SENSOR_TYPE_GYRO, (int)cfg.type);
}

TEST(SensorHAL, Read3Axis_NullDev_Fails)
{
    sensor_data_3axis_t data;
    int r = sensor_hal_read_3axis(NULL, &data);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(SensorHAL, Read3Axis_NullOutputPtr_Fails)
{
    hw_device_t *d = sensor_hal_create("accel", "", &g_cfg);
    if (!d) TEST_IGNORE_MESSAGE("sensor_hal_create with empty path skipped");
    int r = sensor_hal_read_3axis(d, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
    sensor_hal_destroy(d);
}

TEST(SensorHAL, Read1Axis_NullDev_Fails)
{
    sensor_data_1axis_t data;
    int r = sensor_hal_read_1axis(NULL, &data);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(SensorHAL, Read1Axis_NullOut_Fails)
{
    hw_device_t *d = sensor_hal_create("temp", "", &g_cfg);
    if (!d) TEST_IGNORE_MESSAGE("sensor_hal_create with empty path skipped");
    int r = sensor_hal_read_1axis(d, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
    sensor_hal_destroy(d);
}

TEST(SensorHAL, DestroyNull_DoesNotCrash)
{
    sensor_hal_destroy(NULL);
}

TEST(SensorHAL, MockHW_Trigger3Axis_SignalDelivered)
{
    float ex = 1.5f, ey = -0.5f, ez = 9.81f;
    int r = mock_sensor_hw_trigger_3axis(&g_mock_hw, ex, ey, ez);
    TEST_ASSERT_TRUE(r >= 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, ex, g_mock_hw.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, ey, g_mock_hw.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, ez, g_mock_hw.z);
}

TEST(SensorHAL, MockHW_TriggerScalar_SignalDelivered)
{
    float value = 42.0f;
    int r = mock_sensor_hw_trigger_scalar(&g_mock_hw, value);
    TEST_ASSERT_TRUE(r >= 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, value, g_mock_hw.scalar);
}

TEST(SensorHAL, MockHW_EventFd_IsValid)
{
    TEST_ASSERT_TRUE(g_mock_hw.event_fd >= 0);
}

TEST_GROUP_RUNNER(SensorHAL)
{
    RUN_TEST_CASE(SensorHAL, Create_NullName_ReturnsNull);
    RUN_TEST_CASE(SensorHAL, Create_NullConfig_ReturnsNull);
    RUN_TEST_CASE(SensorHAL, DefaultConfig_NonZeroSampleRate);
    RUN_TEST_CASE(SensorHAL, DefaultConfig_Gyro_TypeSet);
    RUN_TEST_CASE(SensorHAL, Read3Axis_NullDev_Fails);
    RUN_TEST_CASE(SensorHAL, Read3Axis_NullOutputPtr_Fails);
    RUN_TEST_CASE(SensorHAL, Read1Axis_NullDev_Fails);
    RUN_TEST_CASE(SensorHAL, Read1Axis_NullOut_Fails);
    RUN_TEST_CASE(SensorHAL, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(SensorHAL, MockHW_Trigger3Axis_SignalDelivered);
    RUN_TEST_CASE(SensorHAL, MockHW_TriggerScalar_SignalDelivered);
    RUN_TEST_CASE(SensorHAL, MockHW_EventFd_IsValid);
}

static void run_all_groups(void) { RUN_TEST_GROUP(SensorHAL); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
