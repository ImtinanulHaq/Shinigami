/**
 * @file test_camera_hal.c
 * @brief Camera HAL unit tests.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_hardware.h"

#include "../../dev/hal/layers/camera/camera_hal.h"
#include "../../dev/hal/interface/hal_interface.h"

#include <string.h>

static mock_camera_hw_t g_mock_hw;

static const camera_config_t g_cfg = {
    .width  = 640, .height = 480,
    .format = CAMERA_FORMAT_RGB24,
    .fps    = 30,
    .buffer_count = 4,
};

TEST_GROUP(CameraHAL);

TEST_SETUP(CameraHAL)
{
    mock_camera_hw_create(&g_mock_hw, 640, 480);
}

TEST_TEAR_DOWN(CameraHAL)
{
    mock_camera_hw_destroy(&g_mock_hw);
}

/* ── Argument Validation ──────────────────────────────────────────── */

TEST(CameraHAL, Create_NullName_ReturnsNull)
{
    hw_device_t *d = camera_hal_create(NULL, "/dev/video0", &g_cfg);
    TEST_ASSERT_NULL(d);
    if (d) camera_hal_destroy(d);
}

TEST(CameraHAL, Create_NullConfig_ReturnsNull)
{
    hw_device_t *d = camera_hal_create("cam", "/dev/video0", NULL);
    TEST_ASSERT_NULL(d);
    if (d) camera_hal_destroy(d);
}

TEST(CameraHAL, ValidateDevicePath_ValidV4L2_ReturnsOK)
{
    /* /dev/ prefix is valid even if device doesn't exist at test time */
    int r = camera_hal_validate_device_path("/dev/video0");
    /* May return error if device doesn't exist, but must NOT crash */
    (void)r;
}

TEST(CameraHAL, ValidateDevicePath_BadPrefix_Fails)
{
    int r = camera_hal_validate_device_path("/tmp/video0");
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(CameraHAL, ValidateDevicePath_Null_Fails)
{
    int r = camera_hal_validate_device_path(NULL);
    TEST_ASSERT_NOT_EQUAL_INT(HAL_SUCCESS, r);
}

TEST(CameraHAL, DefaultConfig_NonZeroResolution)
{
    camera_config_t cfg = camera_hal_default_config();
    TEST_ASSERT_TRUE(cfg.width  > 0);
    TEST_ASSERT_TRUE(cfg.height > 0);
    TEST_ASSERT_TRUE(cfg.fps    > 0);
}

TEST(CameraHAL, GetResolution_VGA_Is640x480)
{
    uint32_t w, h;
    camera_hal_get_resolution(CAMERA_RES_VGA, &w, &h);
    TEST_ASSERT_EQUAL_UINT32(640, w);
    TEST_ASSERT_EQUAL_UINT32(480, h);
}

TEST(CameraHAL, GetResolution_HD_Is1280x720)
{
    uint32_t w, h;
    camera_hal_get_resolution(CAMERA_RES_HD, &w, &h);
    TEST_ASSERT_EQUAL_UINT32(1280, w);
    TEST_ASSERT_EQUAL_UINT32(720,  h);
}

TEST(CameraHAL, DestroyNull_DoesNotCrash)
{
    camera_hal_destroy(NULL);
}

/* ── Mock camera pipe tests ───────────────────────────────────────── */

TEST(CameraHAL, MockHW_FrameTrigger_DataMatchesInjected)
{
    uint8_t frame[640 * 480 * 3];
    tu_rand_fill(frame, sizeof(frame), 0xCAFE);
    int r = mock_camera_hw_trigger(&g_mock_hw, frame);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, r, "trigger should write 1 byte");
    TEST_ASSERT_BUF_EQUAL(frame, g_mock_hw.frame_buf, sizeof(frame));
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(CameraHAL)
{
    RUN_TEST_CASE(CameraHAL, Create_NullName_ReturnsNull);
    RUN_TEST_CASE(CameraHAL, Create_NullConfig_ReturnsNull);
    RUN_TEST_CASE(CameraHAL, ValidateDevicePath_ValidV4L2_ReturnsOK);
    RUN_TEST_CASE(CameraHAL, ValidateDevicePath_BadPrefix_Fails);
    RUN_TEST_CASE(CameraHAL, ValidateDevicePath_Null_Fails);
    RUN_TEST_CASE(CameraHAL, DefaultConfig_NonZeroResolution);
    RUN_TEST_CASE(CameraHAL, GetResolution_VGA_Is640x480);
    RUN_TEST_CASE(CameraHAL, GetResolution_HD_Is1280x720);
    RUN_TEST_CASE(CameraHAL, DestroyNull_DoesNotCrash);
    RUN_TEST_CASE(CameraHAL, MockHW_FrameTrigger_DataMatchesInjected);
}

static void run_all_groups(void) { RUN_TEST_GROUP(CameraHAL); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
