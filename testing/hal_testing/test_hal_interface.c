/**
 * @file test_hal_interface.c
 * @brief Unit tests for hal_interface.c — device lifecycle and state machine.
 *
 * Tests the core device struct without any hardware; no registry ops here.
 * Registry ops are tested separately in test_registry.c.
 */

#include "../hal_interface.h"
#include "test_framework.h"

#include <stdint.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────── */

static int dummy_open(hw_device_t *d) {
  d->state = HAL_STATE_OPEN;
  return 0;
}
static int dummy_close(hw_device_t *d) {
  d->state = HAL_STATE_CLOSED;
  return 0;
}
static int dummy_start(hw_device_t *d) {
  d->state = HAL_STATE_ACTIVE;
  return 0;
}
static int dummy_stop(hw_device_t *d) {
  d->state = HAL_STATE_OPEN;
  return 0;
}
static ssize_t dummy_read(hw_device_t *d, void *b, size_t s) {
  (void)d;
  (void)b;
  (void)s;
  return 4;
}
static ssize_t dummy_write(hw_device_t *d, const void *b, size_t s) {
  (void)d;
  (void)b;
  (void)s;
  return (ssize_t)s;
}
static int dummy_control(hw_device_t *d, uint32_t c, void *a) {
  (void)d;
  (void)c;
  (void)a;
  return HAL_SUCCESS;
}
static int dummy_get_info(hw_device_t *d, void *i) {
  (void)d;
  (void)i;
  return HAL_SUCCESS;
}

static const hw_device_ops_t DUMMY_OPS = {
    .open = dummy_open,
    .close = dummy_close,
    .start = dummy_start,
    .stop = dummy_stop,
    .read = dummy_read,
    .write = dummy_write,
    .control = dummy_control,
    .get_info = dummy_get_info,
};

static void dummy_cleanup(hw_device_t *d) { (void)d; }

/* ── test functions ───────────────────────────────────────────────────── */

static int test_init_sets_all_fields(void) {
  hw_device_t dev;
  int rc = hal_device_init(&dev, "test-device", HAL_DEVICE_TYPE_SENSOR);

  ASSERT_EQ(rc, HAL_SUCCESS);
  ASSERT_STR_EQ(dev.name, "test-device");
  ASSERT_EQ(dev.type, HAL_DEVICE_TYPE_SENSOR);
  ASSERT_EQ(dev.state, HAL_STATE_CLOSED);
  ASSERT_EQ(dev.fd, -1);
  ASSERT_EQ(atomic_load(&dev.ref_count), 1);
  ASSERT_EQ(dev.version, HAL_CURRENT_VERSION);

  hal_device_destroy(&dev);
  return 0;
}

static int test_init_null_device_rejected(void) {
  int rc = hal_device_init(NULL, "name", HAL_DEVICE_TYPE_GPIO);
  ASSERT_EQ(rc, HAL_ERROR_INVALID);
  return 0;
}

static int test_init_null_name_rejected(void) {
  hw_device_t dev;
  int rc = hal_device_init(&dev, NULL, HAL_DEVICE_TYPE_GPIO);
  ASSERT_EQ(rc, HAL_ERROR_INVALID);
  return 0;
}

static int test_name_truncated_to_max_len(void) {
  /* Build a name one character longer than the buffer allows. */
  char long_name[HAL_MAX_NAME_LEN + 8];
  memset(long_name, 'x', sizeof(long_name) - 1u);
  long_name[sizeof(long_name) - 1u] = '\0';

  hw_device_t dev;
  int rc = hal_device_init(&dev, long_name, HAL_DEVICE_TYPE_AUDIO);
  ASSERT_EQ(rc, HAL_SUCCESS);

  /* Name must be null-terminated and fit in the buffer. */
  ASSERT_EQ(dev.name[HAL_MAX_NAME_LEN - 1u], '\0');
  ASSERT_EQ((int)strlen(dev.name), (int)(HAL_MAX_NAME_LEN - 1u));

  hal_device_destroy(&dev);
  return 0;
}

static int test_version_pack_macro(void) {
  uint32_t v = HAL_VERSION_PACK(2, 3, 7);
  ASSERT_EQ((v >> 16) & 0xFF, 2u);
  ASSERT_EQ((v >> 8) & 0xFF, 3u);
  ASSERT_EQ(v & 0xFF, 7u);
  return 0;
}

static int test_ref_count_increment(void) {
  hw_device_t dev;
  hal_device_init(&dev, "ref-test", HAL_DEVICE_TYPE_SENSOR);
  ASSERT_EQ(atomic_load(&dev.ref_count), 1);

  hal_device_ref(&dev);
  ASSERT_EQ(atomic_load(&dev.ref_count), 2);

  hal_device_ref(&dev);
  hal_device_ref(&dev);
  ASSERT_EQ(atomic_load(&dev.ref_count), 4);

  /*
   * Manually reset to 1 before destroy to avoid the unref teardown path
   * calling ops->close (ops is NULL here — we're testing the counter only).
   */
  atomic_store(&dev.ref_count, 1);
  hal_device_destroy(&dev);
  return 0;
}

static int test_ref_null_is_noop(void) {
  /* Must not crash. */
  hal_device_ref(NULL);
  hal_device_unref(NULL);
  return 0;
}

static int test_state_machine_open_close(void) {
  hw_device_t dev;
  hal_device_init(&dev, "sm-test", HAL_DEVICE_TYPE_GPIO);
  dev.ops = &DUMMY_OPS;
  dev.cleanup = dummy_cleanup;
  dev.priv = (void *)1u; /* non-NULL so ops aren't skipped */

  ASSERT_EQ(dev.state, HAL_STATE_CLOSED);

  ASSERT_EQ(dev.ops->open(&dev), HAL_SUCCESS);
  ASSERT_EQ(dev.state, HAL_STATE_OPEN);

  ASSERT_EQ(dev.ops->start(&dev), HAL_SUCCESS);
  ASSERT_EQ(dev.state, HAL_STATE_ACTIVE);

  ASSERT_EQ(dev.ops->stop(&dev), HAL_SUCCESS);
  ASSERT_EQ(dev.state, HAL_STATE_OPEN);

  ASSERT_EQ(dev.ops->close(&dev), HAL_SUCCESS);
  ASSERT_EQ(dev.state, HAL_STATE_CLOSED);

  hal_device_destroy(&dev);
  return 0;
}

static int test_lock_rdlock_unlock_noop_on_null(void) {
  /* Must not crash on NULL input. */
  hal_device_lock(NULL);
  hal_device_rdlock(NULL);
  hal_device_unlock(NULL);
  return 0;
}

static int test_lock_and_unlock(void) {
  hw_device_t dev;
  hal_device_init(&dev, "lock-test", HAL_DEVICE_TYPE_AUDIO);

  /* Acquire write-side then release — must not deadlock. */
  hal_device_lock(&dev);
  hal_device_unlock(&dev);

  /* Acquire read-side then release. */
  hal_device_rdlock(&dev);
  hal_device_unlock(&dev);

  hal_device_destroy(&dev);
  return 0;
}

static int test_error_string_coverage(void) {
  /* Every defined error code must return a non-empty string. */
  hal_error_t codes[] = {
      HAL_SUCCESS,          HAL_ERROR_GENERIC, HAL_ERROR_NO_DEVICE,
      HAL_ERROR_BUSY,       HAL_ERROR_IO,      HAL_ERROR_INVALID,
      HAL_ERROR_NO_MEMORY,  HAL_ERROR_TIMEOUT, HAL_ERROR_NOT_SUPPORT,
      HAL_ERROR_PERMISSION,
  };
  for (size_t i = 0; i < HAL_ARRAY_SIZE(codes); i++) {
    const char *s = hal_error_string(codes[i]);
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(strlen(s) > 0u);
  }
  /* Unknown code must also return a non-empty fallback. */
  const char *unk = hal_error_string((hal_error_t)-99);
  ASSERT_NOT_NULL(unk);
  return 0;
}

static int test_version_output(void) {
  int maj = -1, min = -1, pat = -1;
  hal_get_version(&maj, &min, &pat);
  ASSERT_EQ(maj, HAL_VERSION_MAJOR);
  ASSERT_EQ(min, HAL_VERSION_MINOR);
  ASSERT_EQ(pat, HAL_VERSION_PATCH);

  /* Partial calls (NULL args) must not crash. */
  hal_get_version(NULL, NULL, NULL);
  hal_get_version(&maj, NULL, NULL);
  return 0;
}

static int test_array_size_macro(void) {
  int arr5[5];
  char arr10[10];
  ASSERT_EQ(HAL_ARRAY_SIZE(arr5), 5u);
  ASSERT_EQ(HAL_ARRAY_SIZE(arr10), 10u);
  return 0;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  printf("\n  hal_interface tests\n");
  printf("  %-52s %s\n", "Test", "Result");
  printf("  %-52s %s\n", "----------------------------------------------------",
         "------");

  TEST_RUN("init sets all fields correctly", test_init_sets_all_fields);
  TEST_RUN("init rejects NULL device pointer", test_init_null_device_rejected);
  TEST_RUN("init rejects NULL name pointer", test_init_null_name_rejected);
  TEST_RUN("name truncated to HAL_MAX_NAME_LEN",
           test_name_truncated_to_max_len);
  TEST_RUN("version pack macro encodes correctly", test_version_pack_macro);
  TEST_RUN("ref_count increments correctly", test_ref_count_increment);
  TEST_RUN("ref/unref NULL pointer is no-op", test_ref_null_is_noop);
  TEST_RUN("state machine: open→active→closed", test_state_machine_open_close);
  TEST_RUN("lock/rdlock/unlock NULL is no-op",
           test_lock_rdlock_unlock_noop_on_null);
  TEST_RUN("write-lock and read-lock cycle", test_lock_and_unlock);
  TEST_RUN("error string covers all error codes", test_error_string_coverage);
  TEST_RUN("hal_get_version matches defines", test_version_output);
  TEST_RUN("HAL_ARRAY_SIZE macro is correct", test_array_size_macro);

  return TEST_SUMMARY("hal_interface");
}
