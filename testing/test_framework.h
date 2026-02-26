/**
 * @file test_framework.h
 * @brief Minimal single-header test framework for the HAL test suite.
 *
 * Deliberately zero-dependency: no external framework, no dynamic memory.
 * Each test binary calls TEST_RUN() for each case and exits with the
 * number of failures (0 = all passed, which the Makefile check target
 * interprets as success).
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

/* ── counters ─────────────────────────────────────────────────────────── */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

/* ── assertion macros ─────────────────────────────────────────────────── */

#define ASSERT_EQ(actual, expected)                                            \
  do {                                                                         \
    if ((actual) != (expected)) {                                              \
      fprintf(stderr, "  ASSERT_EQ FAIL %s:%d  got=%ld  want=%ld\n", __FILE__, \
              __LINE__, (long)(actual), (long)(expected));                     \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define ASSERT_NE(actual, unexpected)                                          \
  do {                                                                         \
    if ((actual) == (unexpected)) {                                            \
      fprintf(stderr, "  ASSERT_NE FAIL %s:%d  value=%ld\n", __FILE__,         \
              __LINE__, (long)(actual));                                       \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
  do {                                                                         \
    if ((ptr) == NULL) {                                                       \
      fprintf(stderr, "  ASSERT_NOT_NULL FAIL %s:%d  got NULL\n", __FILE__,    \
              __LINE__);                                                       \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define ASSERT_NULL(ptr)                                                       \
  do {                                                                         \
    if ((ptr) != NULL) {                                                       \
      fprintf(stderr, "  ASSERT_NULL FAIL %s:%d  got non-NULL\n", __FILE__,    \
              __LINE__);                                                       \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define ASSERT_STR_EQ(actual, expected)                                        \
  do {                                                                         \
    if (strcmp((actual), (expected)) != 0) {                                   \
      fprintf(stderr,                                                          \
              "  ASSERT_STR_EQ FAIL %s:%d"                                     \
              "  got='%s'  want='%s'\n",                                       \
              __FILE__, __LINE__, (actual), (expected));                       \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define ASSERT_TRUE(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fprintf(stderr, "  ASSERT_TRUE FAIL %s:%d  expr=false\n", __FILE__,      \
              __LINE__);                                                       \
      return -1;                                                               \
    }                                                                          \
  } while (0)

/* ── test runner ──────────────────────────────────────────────────────── */

/**
 * @brief Signature of a test function: returns 0 on pass, -1 on failure.
 */
typedef int (*test_fn_t)(void);

/**
 * @brief Run one named test function and record the result.
 *
 * @param name  Human-readable test name printed to stdout.
 * @param fn    Test function to invoke.
 */
static void TEST_RUN(const char *name, test_fn_t fn) {
  g_tests_run++;
  int result = fn();
  if (result == 0) {
    printf("  %-52s PASS\n", name);
    g_tests_passed++;
  } else {
    printf("  %-52s FAIL\n", name);
    g_tests_failed++;
  }
}

/**
 * @brief Print a summary and return the number of failures.
 *
 * Call at the end of main(); return its result directly to the OS so the
 * Makefile check target interprets non-zero as test failure.
 */
static int TEST_SUMMARY(const char *suite_name) {
  printf("\n  %s: %d/%d passed", suite_name, g_tests_passed, g_tests_run);
  if (g_tests_failed > 0)
    printf("  (%d FAILED)\n", g_tests_failed);
  else
    printf("\n");
  return g_tests_failed;
}

#endif /* TEST_FRAMEWORK_H */
