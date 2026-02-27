/**
 * @file testing_interface.h
 * @brief Unified testing interface — public contract.
 *
 * PLACEMENT: testing/testing_interface.h
 *
 * This is the ONLY header a component's test suite adapter needs to include.
 * It exposes the registration API, the run API, and re-exports test_types.h
 * so adapters never need to manage include chains.
 *
 * ── HOW THE SYSTEM WORKS ────────────────────────────────────────────────
 *
 * 1. REGISTRATION (at link time, via __attribute__((constructor)))
 *    Each suite adapter .c file defines a static constructor function:
 *
 *      __attribute__((constructor))
 *      static void register_hal_audio_suite(void) {
 *          ti_register_suite(&hal_audio_suite);
 *      }
 *
 *    When the linker includes that .o, the constructor fires before main().
 *    If the .o is not linked, the suite is never registered. This IS the
 *    "source different test files" mechanism — it's a linker selection
 *    problem, not a runtime loading problem.
 *
 * 2. FILTERING (at runtime, via ti_filter_t passed to ti_run())
 *    The caller builds a filter and calls ti_run(). The engine iterates
 *    every registered suite, matches against the filter, and runs matching
 *    cases through test_runner.c.
 *
 * 3. REPORTING (via test_reporter.c)
 *    After execution, the engine calls the active reporter with the
 *    ti_run_result_t. Format is controlled by TI_REPORT_FORMAT env var
 *    or by ti_set_report_format().
 *
 * ── SCALING CONTRACT ────────────────────────────────────────────────────
 *
 * Adding a new component (e.g. "security"):
 *   1. Create testing/security_testing/sec_test_suite.c
 *   2. Call ti_register_suite() from a constructor inside that file
 *   3. Add the .o to the Makefile SUITE object list for "security"
 *   Done. This file does not change. testing_interface.c does not change.
 *
 * Adding a new test level (e.g. TI_LEVEL_PERF):
 *   1. Add the bit to ti_level_t in test_types.h
 *   2. Update ti_level_name() in testing_interface.c
 *   Done. All existing suites keep working because they use numeric levels
 *   or TI_LEVEL_ALL which includes all set bits.
 */

#ifndef TESTING_INTERFACE_H
#define TESTING_INTERFACE_H

#include "test_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── engine lifecycle ────────────────────────────────────────────────────── */

/**
 * @brief Initialise the testing engine.
 *
 * Must be called once before any other ti_* function. Safe to call multiple
 * times (idempotent after first call). Initialises the suite registry,
 * result allocator, and signal handlers for crash isolation.
 *
 * @return 0 on success, -1 on allocation failure.
 */
int ti_init(void);

/**
 * @brief Destroy the engine and release all resources.
 *
 * Frees internal registry storage. Does NOT free suites themselves — those
 * are statically allocated by their adapters. Safe to call even if ti_init()
 * was never called (no-op in that case).
 */
void ti_destroy(void);

/* ── suite registration ──────────────────────────────────────────────────── */

/**
 * @brief Register a test suite with the engine.
 *
 * Thread-safe. May be called from __attribute__((constructor)) functions
 * before main() starts. Returns -1 if the registry is full (TI_MAX_SUITES
 * exceeded) or if suite is NULL.
 *
 * The suite pointer must remain valid for the lifetime of the engine — the
 * engine stores a pointer, not a copy. Statically allocated suite structs
 * (the typical pattern) satisfy this automatically.
 *
 * @param suite  Pointer to a fully populated ti_suite_t.
 * @return 0 on success, -1 on error.
 */
int ti_register_suite(ti_suite_t *suite);

/**
 * @brief Return the number of currently registered suites.
 */
int ti_suite_count(void);

/**
 * @brief Return the registered suite at index i, or NULL if out of range.
 *
 * Use with ti_suite_count() to enumerate all registered suites for
 * debugging or dry-run listing.
 */
const ti_suite_t *ti_suite_get(int i);

/* ── filter construction helpers ─────────────────────────────────────────── */

/**
 * @brief Return a zeroed filter that matches nothing.
 *
 * Start here, then set only the dimensions you care about:
 *
 *   ti_filter_t f = ti_filter_none();
 *   f.levels = TI_LEVEL_UNIT | TI_LEVEL_MODULE;
 *   ti_filter_add_component(&f, "hal");
 */
ti_filter_t ti_filter_none(void);

/**
 * @brief Return a filter that matches everything (all levels, all components).
 */
ti_filter_t ti_filter_all(void);

/**
 * @brief Add a component name to the filter's component list.
 *
 * NULL or empty string is ignored. Returns -1 if the list is full.
 */
int ti_filter_add_component(ti_filter_t *f, const char *component);

/**
 * @brief Add a domain name to the filter.
 */
int ti_filter_add_domain(ti_filter_t *f, const char *domain);

/**
 * @brief Add a tag to the filter. Only cases carrying this tag will run.
 */
int ti_filter_add_tag(ti_filter_t *f, const char *tag);

/* ── run API ─────────────────────────────────────────────────────────────── */

/**
 * @brief Run all test cases that match the filter.
 *
 * Execution order: suites are run in registration order. Within a suite,
 * cases are run in declaration order. This is deterministic and reproducible.
 *
 * @param filter  Selection criteria. NULL = run everything.
 * @return Heap-allocated result; caller must call ti_result_destroy().
 *         Returns NULL only on catastrophic allocation failure.
 */
ti_run_result_t *ti_run(const ti_filter_t *filter);

/**
 * @brief Free a ti_run_result_t returned by ti_run().
 *
 * Safe to call with NULL (no-op).
 */
void ti_result_destroy(ti_run_result_t *result);

/* ── reporting ───────────────────────────────────────────────────────────── */

/**
 * @brief Set the report format used by ti_report().
 *
 * Default: TI_REPORT_TERMINAL.
 * Can also be set via TI_REPORT_FORMAT env var: "terminal", "tap", "json".
 */
void ti_set_report_format(ti_report_format_t fmt);

/**
 * @brief Print a full report of the run result to stdout.
 *
 * Uses the format set by ti_set_report_format() or TI_REPORT_FORMAT env var.
 *
 * @param result  Run result from ti_run().
 * @param filter  The filter used to produce this result (for header display).
 */
void ti_report(const ti_run_result_t *result, const ti_filter_t *filter);

/* ── utility ─────────────────────────────────────────────────────────────── */

/**
 * @brief Return a human-readable string for a level bitmask value.
 *
 * If multiple bits are set, returns "mixed". If zero, returns "none".
 */
const char *ti_level_name(ti_level_t level);

/**
 * @brief Return a human-readable string for a result code.
 */
const char *ti_result_name(ti_result_t result);

/**
 * @brief Print all registered suites and their case counts to stdout.
 *
 * Useful for --list mode without executing any tests.
 */
void ti_list_suites(void);

/* ── convenience macro: define and register a suite in one block ─────────── */

/**
 * TI_DEFINE_SUITE(var_name, component_str, domain_str, level, cases_array)
 *
 * Declares a static ti_suite_t and a constructor that registers it.
 * Use this at the top of a suite adapter .c file.
 *
 * Example:
 *   TI_DEFINE_SUITE(hal_audio_suite, "hal", "audio", TI_LEVEL_UNIT,
 *                   hal_audio_cases);
 *
 * Expands to:
 *   static ti_suite_t hal_audio_suite = { ... };
 *   __attribute__((constructor))
 *   static void _ti_register_hal_audio_suite(void) {
 *       ti_register_suite(&hal_audio_suite);
 *   }
 */
#define TI_DEFINE_SUITE(var, comp, dom, lvl, cases_arr)                        \
  static ti_suite_t var = {                                                    \
      .component = (comp),                                                     \
      .domain = (dom),                                                         \
      .source_file = __FILE__,                                                 \
      .default_level = (lvl),                                                  \
      .cases = (cases_arr),                                                    \
  };                                                                           \
  __attribute__((constructor)) static void _ti_ctor_##var(void) {              \
    ti_register_suite(&var);                                                   \
  }

/**
 * TI_CASE(name_str, fn, level)
 * TI_CASE_TAGGED(name_str, fn, level, tag1, tag2, ...)
 *
 * Helpers for populating a ti_test_case_t array.
 *
 * Example:
 *   static ti_test_case_t hal_audio_cases[] = {
 *       TI_CASE("default config",          test_audio_default_config,
 * TI_LEVEL_UNIT), TI_CASE("bytes per frame stereo",  test_audio_bpf_stereo,
 * TI_LEVEL_UNIT), TI_CASE_TAGGED("ALSA open",        test_audio_open,
 * TI_LEVEL_MODULE, "alsa", "hw"), {0}  ← sentinel
 *   };
 */
#define TI_CASE(name_str, fn_ptr, lvl)                                         \
  {.name = (name_str), .fn = (fn_ptr), .level = (lvl)}

#define TI_CASE_TAGGED(name_str, fn_ptr, lvl, ...)                             \
  {                                                                            \
    .name = (name_str), .fn = (fn_ptr), .level = (lvl), .tags = {              \
      __VA_ARGS__,                                                             \
      NULL                                                                     \
    }                                                                          \
  }

#ifdef __cplusplus
}
#endif

#endif /* TESTING_INTERFACE_H */
