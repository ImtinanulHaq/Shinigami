/**
 * @file test_framework.h  
 * @brief Common test utilities and assertions for monitoring tests
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Test result tracking */
extern int g_tests_run;
extern int g_tests_passed;
extern int g_tests_failed;

/* ── Test Macros ──────────────────────────────────────────────────────────── */

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        g_tests_failed++; \
        return false; \
    } \
    g_tests_passed++; \
} while (0)

#define TEST_ASSERT_EQ(a, b, msg) TEST_ASSERT((a) == (b), msg)
#define TEST_ASSERT_NE(a, b, msg) TEST_ASSERT((a) != (b), msg)
#define TEST_ASSERT_LT(a, b, msg) TEST_ASSERT((a) < (b), msg)
#define TEST_ASSERT_LE(a, b, msg) TEST_ASSERT((a) <= (b), msg)
#define TEST_ASSERT_GT(a, b, msg) TEST_ASSERT((a) > (b), msg)
#define TEST_ASSERT_GE(a, b, msg) TEST_ASSERT((a) >= (b), msg)
#define TEST_ASSERT_NULL(ptr, msg) TEST_ASSERT((ptr) == NULL, msg)
#define TEST_ASSERT_NOT_NULL(ptr, msg) TEST_ASSERT((ptr) != NULL, msg)

#define TEST_ASSERT_STR_EQ(a, b, msg) TEST_ASSERT(strcmp(a, b) == 0, msg)
#define TEST_ASSERT_STR_NE(a, b, msg) TEST_ASSERT(strcmp(a, b) != 0, msg)

#define TEST_ASSERT_FLOAT_EQ(a, b, epsilon, msg) \
    TEST_ASSERT(fabs((a) - (b)) < (epsilon), msg)

/* Test function signature */
typedef bool (*test_fn_t)(void);

/* Test registration */
typedef struct {
    const char *name;
    test_fn_t   func;
    bool        enabled;
} test_case_t;

/* ── Test Utilities ───────────────────────────────────────────────────────── */

/**
 * @brief Create a unique test directory in /tmp
 * @return Allocated string with path (caller must free)
 */
char *test_create_temp_dir(void);

/**
 * @brief Recursively remove a directory and contents
 */
void test_cleanup_dir(const char *path);

/**
 * @brief Sleep for milliseconds
 */
void test_sleep_ms(uint32_t ms);

/**
 * @brief Get monotonic timestamp in milliseconds 
 */
uint64_t test_get_time_ms(void);

/**
 * @brief Run a test suite
 * @param tests    Array of test cases
 * @param count    Number of tests
 * @param suite_name  Name of test suite for reporting
 * @return 0 if all pass, non-zero otherwise
 */
int test_run_suite(test_case_t *tests, int count, const char *suite_name);

/**
 * @brief Check if process is running by PID
 */
bool test_process_running(pid_t pid);

/**
 * @brief Kill process safely
 */
void test_kill_process(pid_t pid);

/**
 * @brief Read entire file into buffer (caller must free)
 */
char *test_read_file(const char *path, size_t *size_out);

/**
 * @brief Write buffer to file
 */
bool test_write_file(const char *path, const void *data, size_t size);

/**
 * @brief Check if Unix socket exists
 */
bool test_socket_exists(const char *path);

/**
 * @brief Wait for Unix socket to appear (timeout in ms)
 */
bool test_wait_for_socket(const char *path, uint32_t timeout_ms);

/**
 * @brief Count open file descriptors for current process
 */
int test_count_fds(void);

/**
 * @brief Get RSS memory usage in bytes for current process
 */
uint64_t test_get_rss_bytes(void);
