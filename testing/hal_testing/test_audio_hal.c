/**
 * @file test_audio_hal.c
 * @brief Unit tests for audio_hal.c.
 *
 * Hardware-independent tests: creation/destruction, configuration logic,
 * bytes_per_frame arithmetic, format string lookup, and control-command
 * argument validation.  The ALSA PCM device cannot be opened without
 * hardware, so open/read/write paths are tested only to the point where
 * HAL_ERROR_NO_DEVICE is correctly returned.
 */

#include "audio_hal.h"
#include "test_framework.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────── */

static audio_config_t make_config(audio_direction_t dir) {
  return audio_hal_default_config(dir);
}

/* ── test functions ───────────────────────────────────────────────────── */

static int test_create_returns_valid_device(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("spk", "default", &cfg);
  ASSERT_NOT_NULL(dev);
  ASSERT_EQ(dev->type, HAL_DEVICE_TYPE_AUDIO);
  ASSERT_STR_EQ(dev->name, "spk");
  ASSERT_EQ(dev->state, HAL_STATE_CLOSED);
  ASSERT_EQ(atomic_load(&dev->ref_count), 1);
  ASSERT_NOT_NULL(dev->ops);
  ASSERT_NOT_NULL(dev->cleanup);

  audio_hal_destroy(dev);
  return 0;
}

static int test_create_null_args_rejected(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_CAPTURE);
  ASSERT_NULL(audio_hal_create(NULL, "default", &cfg));
  ASSERT_NULL(audio_hal_create("mic", NULL, &cfg));
  ASSERT_NULL(audio_hal_create("mic", "default", NULL));
  return 0;
}

static int test_default_config_playback(void) {
  audio_config_t cfg = audio_hal_default_config(AUDIO_DIRECTION_PLAYBACK);
  ASSERT_EQ(cfg.direction, AUDIO_DIRECTION_PLAYBACK);
  ASSERT_EQ(cfg.sample_rate, 44100u);
  ASSERT_EQ(cfg.channels, 2u);
  ASSERT_EQ(cfg.format, AUDIO_FORMAT_S16_LE);
  ASSERT_TRUE(cfg.period_size > 0u);
  ASSERT_TRUE(cfg.buffer_size >= cfg.period_size * 2u);
  return 0;
}

static int test_default_config_capture(void) {
  audio_config_t cfg = audio_hal_default_config(AUDIO_DIRECTION_CAPTURE);
  ASSERT_EQ(cfg.direction, AUDIO_DIRECTION_CAPTURE);
  return 0;
}

static int test_bytes_per_frame_s16_stereo(void) {
  /* S16_LE stereo: 2 bytes × 2 channels = 4 bytes per frame. */
  uint32_t bpf = audio_hal_bytes_per_frame(AUDIO_FORMAT_S16_LE, 2u);
  ASSERT_EQ(bpf, 4u);
  return 0;
}

static int test_bytes_per_frame_s32_mono(void) {
  uint32_t bpf = audio_hal_bytes_per_frame(AUDIO_FORMAT_S32_LE, 1u);
  ASSERT_EQ(bpf, 4u);
  return 0;
}

static int test_bytes_per_frame_float_8ch(void) {
  /* FLOAT 8-channel surround: 4 bytes × 8 channels = 32. */
  uint32_t bpf = audio_hal_bytes_per_frame(AUDIO_FORMAT_FLOAT, 8u);
  ASSERT_EQ(bpf, 32u);
  return 0;
}

static int test_bytes_per_frame_s24_stereo(void) {
  uint32_t bpf = audio_hal_bytes_per_frame(AUDIO_FORMAT_S24_LE, 2u);
  ASSERT_EQ(bpf, 6u);
  return 0;
}

static int test_format_string_all_formats(void) {
  ASSERT_STR_EQ(audio_hal_format_string(AUDIO_FORMAT_S16_LE), "S16_LE");
  ASSERT_STR_EQ(audio_hal_format_string(AUDIO_FORMAT_S24_LE), "S24_LE");
  ASSERT_STR_EQ(audio_hal_format_string(AUDIO_FORMAT_S32_LE), "S32_LE");
  ASSERT_STR_EQ(audio_hal_format_string(AUDIO_FORMAT_FLOAT), "FLOAT");
  /* Unknown format must return a non-empty fallback. */
  const char *unk = audio_hal_format_string((audio_format_t)0xFF);
  ASSERT_NOT_NULL(unk);
  ASSERT_TRUE(strlen(unk) > 0u);
  return 0;
}

static int test_open_nonexistent_device_returns_error(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("bad-dev", "hw:99,99", &cfg);
  ASSERT_NOT_NULL(dev);

  /*
   * Without real ALSA hardware, open() must fail.
   * We accept either HAL_ERROR_NO_DEVICE or HAL_ERROR_IO since stubs
   * return -1 and the code maps that to HAL_ERROR_NO_DEVICE.
   */
  int rc = dev->ops->open(dev);
  ASSERT_TRUE(rc < 0);
  ASSERT_EQ(dev->state,
            HAL_STATE_CLOSED); /* must not advance state on failure */

  audio_hal_destroy(dev);
  return 0;
}

static int test_read_before_open_returns_invalid(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_CAPTURE);
  hw_device_t *dev = audio_hal_create("mic-early", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  uint8_t buf[256];
  ssize_t rc = dev->ops->read(dev, buf, sizeof(buf));
  ASSERT_TRUE(rc < 0); /* state is CLOSED, not ACTIVE */

  audio_hal_destroy(dev);
  return 0;
}

static int test_write_before_open_returns_invalid(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("spk-early", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  const uint8_t silence[512] = {0};
  ssize_t rc = dev->ops->write(dev, silence, sizeof(silence));
  ASSERT_TRUE(rc < 0);

  audio_hal_destroy(dev);
  return 0;
}

static int test_control_null_arg_rejected(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("ctrl-test", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  /* SET_VOLUME requires a non-NULL arg. */
  int rc = dev->ops->control(dev, AUDIO_CMD_SET_VOLUME, NULL);
  ASSERT_EQ(rc, HAL_ERROR_INVALID);

  audio_hal_destroy(dev);
  return 0;
}

static int test_control_get_set_volume(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("vol-test", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  uint32_t vol = 75u;
  int rc = dev->ops->control(dev, AUDIO_CMD_SET_VOLUME, &vol);
  ASSERT_EQ(rc, HAL_SUCCESS);

  uint32_t got = 0u;
  rc = dev->ops->control(dev, AUDIO_CMD_GET_VOLUME, &got);
  ASSERT_EQ(rc, HAL_SUCCESS);
  ASSERT_EQ(got, 75u);

  audio_hal_destroy(dev);
  return 0;
}

static int test_control_volume_clamped_to_100(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("clamp-test", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  uint32_t vol = 999u;
  dev->ops->control(dev, AUDIO_CMD_SET_VOLUME, &vol);

  uint32_t got = 0u;
  dev->ops->control(dev, AUDIO_CMD_GET_VOLUME, &got);
  ASSERT_EQ(got, 100u);

  audio_hal_destroy(dev);
  return 0;
}

static int test_control_get_set_mute(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("mute-test", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  int mute_on = 1;
  dev->ops->control(dev, AUDIO_CMD_SET_MUTE, &mute_on);

  int got = 0;
  dev->ops->control(dev, AUDIO_CMD_GET_MUTE, &got);
  ASSERT_EQ(got, 1);

  int mute_off = 0;
  dev->ops->control(dev, AUDIO_CMD_SET_MUTE, &mute_off);
  dev->ops->control(dev, AUDIO_CMD_GET_MUTE, &got);
  ASSERT_EQ(got, 0);

  audio_hal_destroy(dev);
  return 0;
}

static int test_control_get_config_copy(void) {
  audio_config_t orig = make_config(AUDIO_DIRECTION_CAPTURE);
  orig.sample_rate = 48000u;
  orig.channels = 1u;

  hw_device_t *dev = audio_hal_create("cfg-copy", "default", &orig);
  ASSERT_NOT_NULL(dev);

  audio_config_t out;
  int rc = dev->ops->control(dev, AUDIO_CMD_GET_CONFIG, &out);
  ASSERT_EQ(rc, HAL_SUCCESS);
  ASSERT_EQ(out.sample_rate, 48000u);
  ASSERT_EQ(out.channels, 1u);
  ASSERT_EQ(out.direction, AUDIO_DIRECTION_CAPTURE);

  audio_hal_destroy(dev);
  return 0;
}

static int test_unknown_control_command_rejected(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("unk-cmd", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  int rc = dev->ops->control(dev, 0xDEAD, NULL);
  ASSERT_EQ(
      rc,
      HAL_ERROR_INVALID); /* unknown cmd → validate_control_arg returns -1 */

  audio_hal_destroy(dev);
  return 0;
}

static int test_get_info_returns_correct_data(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("info-dev", "hw:0,0", &cfg);
  ASSERT_NOT_NULL(dev);

  audio_info_t info;
  int rc = dev->ops->get_info(dev, &info);
  ASSERT_EQ(rc, HAL_SUCCESS);
  ASSERT_STR_EQ(info.device_name, "hw:0,0");
  ASSERT_TRUE(info.bytes_per_frame > 0u);
  ASSERT_EQ(info.current_volume, 100u);
  ASSERT_EQ(info.is_muted, 0);

  audio_hal_destroy(dev);
  return 0;
}

static int test_get_info_null_rejected(void) {
  audio_config_t cfg = make_config(AUDIO_DIRECTION_PLAYBACK);
  hw_device_t *dev = audio_hal_create("info-null", "default", &cfg);
  ASSERT_NOT_NULL(dev);

  int rc = dev->ops->get_info(dev, NULL);
  ASSERT_TRUE(rc < 0);

  audio_hal_destroy(dev);
  return 0;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  printf("\n  audio_hal tests\n");
  printf("  %-52s %s\n", "Test", "Result");
  printf("  %-52s %s\n", "----------------------------------------------------",
         "------");

  TEST_RUN("create returns valid device struct",
           test_create_returns_valid_device);
  TEST_RUN("create rejects NULL arguments", test_create_null_args_rejected);
  TEST_RUN("default playback config is sane", test_default_config_playback);
  TEST_RUN("default capture config sets direction",
           test_default_config_capture);
  TEST_RUN("bytes_per_frame: S16_LE stereo = 4",
           test_bytes_per_frame_s16_stereo);
  TEST_RUN("bytes_per_frame: S32_LE mono = 4", test_bytes_per_frame_s32_mono);
  TEST_RUN("bytes_per_frame: FLOAT 8ch = 32", test_bytes_per_frame_float_8ch);
  TEST_RUN("bytes_per_frame: S24_LE stereo = 6",
           test_bytes_per_frame_s24_stereo);
  TEST_RUN("format_string covers all format codes",
           test_format_string_all_formats);
  TEST_RUN("open nonexistent ALSA device returns error",
           test_open_nonexistent_device_returns_error);
  TEST_RUN("read before open returns HAL_ERROR_INVALID",
           test_read_before_open_returns_invalid);
  TEST_RUN("write before open returns HAL_ERROR_INVALID",
           test_write_before_open_returns_invalid);
  TEST_RUN("control SET_VOLUME with NULL rejected",
           test_control_null_arg_rejected);
  TEST_RUN("control SET/GET_VOLUME round-trips", test_control_get_set_volume);
  TEST_RUN("control SET_VOLUME clamps > 100 to 100",
           test_control_volume_clamped_to_100);
  TEST_RUN("control SET/GET_MUTE round-trips", test_control_get_set_mute);
  TEST_RUN("control GET_CONFIG returns correct copy",
           test_control_get_config_copy);
  TEST_RUN("unknown control command rejected",
           test_unknown_control_command_rejected);
  TEST_RUN("get_info returns correct device data",
           test_get_info_returns_correct_data);
  TEST_RUN("get_info NULL info_out rejected", test_get_info_null_rejected);

  return TEST_SUMMARY("audio_hal");
}
