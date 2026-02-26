/**
 * @file test_registry.c
 * @brief Unit tests for the HAL global device registry.
 *
 * Exercises hal_device_register / hal_device_find / hal_device_unregister
 * including hash collision paths, tombstone behaviour on re-insert after
 * removal, and concurrent read access from multiple threads.
 */

#include "hal_interface.h"
#include "test_framework.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────── */

static void noop_cleanup(hw_device_t *d) { (void)d; }

/**
 * @brief Allocate, initialise, and assign a noop cleanup to a device.
 *
 * Devices allocated here must be freed by the caller ONLY if they were
 * never passed to hal_device_register (which takes its own ref).
 * After registration, hal_device_unregister + hal_device_unref do cleanup.
 */
static hw_device_t *make_device(const char *name, hal_device_type_t type) {
  hw_device_t *dev = malloc(sizeof(hw_device_t));
  if (!dev)
    return NULL;
  if (hal_device_init(dev, name, type) != HAL_SUCCESS) {
    free(dev);
    return NULL;
  }
  dev->cleanup = noop_cleanup;
  return dev;
}

/* ── test functions ───────────────────────────────────────────────────── */

static int test_register_and_find(void) {
  hw_device_t *dev = make_device("reg-find-dev", HAL_DEVICE_TYPE_GPIO);
  ASSERT_NOT_NULL(dev);

  int rc = hal_device_register(dev);
  ASSERT_EQ(rc, HAL_SUCCESS);

  hw_device_t *found = hal_device_find("reg-find-dev");
  ASSERT_NOT_NULL(found);
  ASSERT_EQ(found, dev);

  /* Release the reference returned by hal_device_find. */
  hal_device_unref(found);

  hal_device_unregister(dev);
  /* Drop our own creation ref — device freed here. */
  hal_device_unref(dev);
  return 0;
}

static int test_find_missing_returns_null(void) {
  hw_device_t *found = hal_device_find("this-device-does-not-exist");
  ASSERT_NULL(found);
  return 0;
}

static int test_duplicate_name_rejected(void) {
  hw_device_t *a = make_device("dup-name", HAL_DEVICE_TYPE_SENSOR);
  hw_device_t *b = make_device("dup-name", HAL_DEVICE_TYPE_AUDIO);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);

  ASSERT_EQ(hal_device_register(a), HAL_SUCCESS);
  ASSERT_EQ(hal_device_register(b), HAL_ERROR_BUSY);

  hal_device_unregister(a);
  hal_device_unref(a);
  hal_device_unref(b); /* b was never registered */
  return 0;
}

static int test_unregister_then_find_returns_null(void) {
  hw_device_t *dev = make_device("unregister-me", HAL_DEVICE_TYPE_CAMERA);
  ASSERT_NOT_NULL(dev);

  hal_device_register(dev);
  hal_device_unregister(dev);
  hal_device_unref(dev);

  hw_device_t *found = hal_device_find("unregister-me");
  ASSERT_NULL(found);
  return 0;
}

static int test_reregister_after_unregister(void) {
  /*
   * After unregistering, the slot becomes a tombstone.  Re-inserting
   * the same name must succeed — the hash table must reuse tombstone
   * slots rather than reporting "full" or "busy".
   */
  hw_device_t *first = make_device("reuse-slot", HAL_DEVICE_TYPE_SENSOR);
  ASSERT_NOT_NULL(first);
  hal_device_register(first);
  hal_device_unregister(first);
  hal_device_unref(first);

  hw_device_t *second = make_device("reuse-slot", HAL_DEVICE_TYPE_GPIO);
  ASSERT_NOT_NULL(second);
  int rc = hal_device_register(second);
  ASSERT_EQ(rc, HAL_SUCCESS);

  hw_device_t *found = hal_device_find("reuse-slot");
  ASSERT_NOT_NULL(found);
  ASSERT_EQ(found, second);

  hal_device_unref(found);
  hal_device_unregister(second);
  hal_device_unref(second);
  return 0;
}

static int test_register_multiple_devices(void) {
  /* Register 10 devices and confirm each is independently findable. */
  static const int N = 10;
  char names[10][32];
  hw_device_t *devs[10];

  for (int i = 0; i < N; i++) {
    snprintf(names[i], sizeof(names[i]), "multi-dev-%02d", i);
    devs[i] = make_device(names[i], HAL_DEVICE_TYPE_SENSOR);
    ASSERT_NOT_NULL(devs[i]);
    ASSERT_EQ(hal_device_register(devs[i]), HAL_SUCCESS);
  }

  for (int i = 0; i < N; i++) {
    hw_device_t *found = hal_device_find(names[i]);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(found, devs[i]);
    hal_device_unref(found);
  }

  for (int i = 0; i < N; i++) {
    hal_device_unregister(devs[i]);
    hal_device_unref(devs[i]);
  }
  return 0;
}

static int test_hal_device_list(void) {
  hw_device_t *a = make_device("list-dev-a", HAL_DEVICE_TYPE_AUDIO);
  hw_device_t *b = make_device("list-dev-b", HAL_DEVICE_TYPE_CAMERA);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);

  hal_device_register(a);
  hal_device_register(b);

  hw_device_t *out[32];
  int count = hal_device_list(out, 32);
  ASSERT_TRUE(count >= 2);

  /* Release all references returned by hal_device_list. */
  for (int i = 0; i < count; i++)
    hal_device_unref(out[i]);

  hal_device_unregister(a);
  hal_device_unregister(b);
  hal_device_unref(a);
  hal_device_unref(b);
  return 0;
}

static int test_list_null_args(void) {
  /* Must not crash on degenerate inputs. */
  int rc = hal_device_list(NULL, 10);
  ASSERT_EQ(rc, 0);

  hw_device_t *out[4];
  rc = hal_device_list(out, 0);
  ASSERT_EQ(rc, 0);
  return 0;
}

static int test_ref_count_incremented_by_registry(void) {
  hw_device_t *dev = make_device("refcount-reg", HAL_DEVICE_TYPE_GPIO);
  ASSERT_NOT_NULL(dev);

  /* ref = 1 after init. */
  ASSERT_EQ(atomic_load(&dev->ref_count), 1);

  hal_device_register(dev);
  /* registry took its own ref → 2 */
  ASSERT_EQ(atomic_load(&dev->ref_count), 2);

  hw_device_t *found = hal_device_find("refcount-reg");
  /* find took another ref → 3 */
  ASSERT_EQ(atomic_load(&dev->ref_count), 3);

  hal_device_unref(found);
  /* back to 2 */
  ASSERT_EQ(atomic_load(&dev->ref_count), 2);

  hal_device_unregister(dev);
  /* registry released → 1 */
  ASSERT_EQ(atomic_load(&dev->ref_count), 1);

  hal_device_unref(dev);
  /* device freed — cannot read ref_count anymore; test just checks no crash */
  return 0;
}

/* ── concurrent read test ─────────────────────────────────────────────── */

#define READER_THREADS 8
#define READER_ITERS 2000

struct reader_args {
  const char *name;
  int failures;
};

static void *concurrent_reader(void *arg) {
  struct reader_args *ra = (struct reader_args *)arg;
  for (int i = 0; i < READER_ITERS; i++) {
    hw_device_t *found = hal_device_find(ra->name);
    if (!found) {
      ra->failures++;
    } else {
      hal_device_unref(found);
    }
  }
  return NULL;
}

static int test_concurrent_reads(void) {
  hw_device_t *dev = make_device("concurrent-read", HAL_DEVICE_TYPE_SENSOR);
  ASSERT_NOT_NULL(dev);
  hal_device_register(dev);

  pthread_t threads[READER_THREADS];
  struct reader_args args[READER_THREADS];

  for (int i = 0; i < READER_THREADS; i++) {
    args[i].name = "concurrent-read";
    args[i].failures = 0;
    pthread_create(&threads[i], NULL, concurrent_reader, &args[i]);
  }

  int total_failures = 0;
  for (int i = 0; i < READER_THREADS; i++) {
    pthread_join(threads[i], NULL);
    total_failures += args[i].failures;
  }

  hal_device_unregister(dev);
  hal_device_unref(dev);

  ASSERT_EQ(total_failures, 0);
  return 0;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  printf("\n  registry tests\n");
  printf("  %-52s %s\n", "Test", "Result");
  printf("  %-52s %s\n", "----------------------------------------------------",
         "------");

  TEST_RUN("register + find returns same pointer", test_register_and_find);
  TEST_RUN("find missing name returns NULL", test_find_missing_returns_null);
  TEST_RUN("duplicate name returns HAL_ERROR_BUSY",
           test_duplicate_name_rejected);
  TEST_RUN("unregister makes device unfindable",
           test_unregister_then_find_returns_null);
  TEST_RUN("re-register after unregister succeeds",
           test_reregister_after_unregister);
  TEST_RUN("10 independent devices all findable",
           test_register_multiple_devices);
  TEST_RUN("hal_device_list returns all devices", test_hal_device_list);
  TEST_RUN("hal_device_list handles NULL/zero args", test_list_null_args);
  TEST_RUN("registry increments/decrements ref_count",
           test_ref_count_incremented_by_registry);
  TEST_RUN("concurrent reads from 8 threads (rdlock)", test_concurrent_reads);

  return TEST_SUMMARY("registry");
}
