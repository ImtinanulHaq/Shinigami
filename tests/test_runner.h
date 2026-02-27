/**
 * @file test_runner.h
 * @brief Test case execution engine — interface.
 *
 * PLACEMENT: testing/test_runner.h
 *
 * The runner owns three concerns that must NOT live in testing_interface.c:
 *
 *   1. SIGNAL ISOLATION — SIGSEGV/SIGABRT/SIGFPE from a test body must
 *      be caught and converted to TI_RESULT_CRASH without killing the
 *      process. Done via sigsetjmp/siglongjmp, not fork(), because fork
 *      is expensive and breaks sanitizers.
 *
 *   2. TIMING — wall-clock elapsed time per case for performance regression
 *      detection. Uses CLOCK_MONOTONIC, not gettimeofday(), because
 *      gettimeofday can go backwards under NTP corrections.
 *
 *   3. TIMEOUT — alarm()+SIGALRM kills runaway tests. Per-case timeout
 *      from ti_test_case_t.timeout_ms overrides suite default.
 *
 * These three concerns are isolated here so testing_interface.c stays
 * readable and so the runner can be swapped (e.g. a fork-based runner
 * for true isolation) without touching any other file.
 */

#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include "test_types.h"

/**
 * @brief Initialise signal handlers and internal runner state.
 *
 * Must be called once before test_runner_execute(). Called by ti_init().
 * Installs SIGSEGV, SIGABRT, SIGFPE, and SIGALRM handlers.
 */
void test_runner_init(void);

/**
 * @brief Release runner resources and restore signal handlers.
 *
 * Called by ti_destroy().
 */
void test_runner_destroy(void);

/**
 * @brief Execute one test case and populate outcome.
 *
 * Execution sequence:
 *   1. Resolve timeout (case > suite > 0=none).
 *   2. Run per-case setup (if non-NULL). If setup returns non-zero, record
 *      TI_RESULT_SKIP and return — test body is not called.
 *   3. Arm timeout via alarm() (if timeout_ms > 0).
 *   4. Install crash recovery via sigsetjmp().
 *   5. Call tc->fn(). Record elapsed time.
 *   6. Disarm timeout. Record result: 0→PASS, non-zero→FAIL.
 *   7. Run per-case teardown (if non-NULL) regardless of result.
 *
 * If a signal fires during step 5, siglongjmp() transfers control back to
 * the runner. The signal identity is captured and stored in outcome->message.
 *
 * @param suite    Suite owning this case (for setup/teardown fallback).
 * @param tc       The test case to execute.
 * @param outcome  Caller-allocated outcome struct to populate.
 */
void test_runner_execute(const ti_suite_t *suite, const ti_test_case_t *tc,
                         ti_case_outcome_t *outcome);

#endif /* TEST_RUNNER_H */
