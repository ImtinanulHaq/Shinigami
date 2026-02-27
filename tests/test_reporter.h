/**
 * @file test_reporter.h
 * @brief Test result output — interface.
 *
 * PLACEMENT: testing/test_reporter.h
 *
 * The reporter is deliberately separated from the engine for two reasons:
 *
 *   1. OUTPUT FORMAT INDEPENDENCE — terminal output has ANSI codes, TAP
 *      has a strict line format, JSON needs escaping. None of that logic
 *      belongs in the execution engine.
 *
 *   2. CI PIPELINE INTEGRATION — TAP (Test Anything Protocol v13) is
 *      understood by Jenkins, GitHub Actions, and most CI runners without
 *      plugins. JSON enables dashboard ingestion. These are production
 *      concerns; keeping them in a dedicated file makes them easy to extend
 *      (e.g. add JUnit XML by adding one function here and one case in
 *      test_reporter.c).
 */

#ifndef TEST_REPORTER_H
#define TEST_REPORTER_H

#include "test_types.h"

/**
 * @brief Print the full run result in the requested format.
 *
 * @param result  Run result from ti_run().
 * @param filter  The filter used (for display in the report header). May be
 * NULL.
 * @param fmt     Output format.
 */
void test_reporter_print(const ti_run_result_t *result,
                         const ti_filter_t *filter, ti_report_format_t fmt);

#endif /* TEST_REPORTER_H */
