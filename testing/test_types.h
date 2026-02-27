/**
 * @file test_types.h
 * @brief Shared type definitions for the unified testing interface.
 *
 * PLACEMENT: testing/test_types.h
 *
 * This header defines every enum, constant, and plain struct used across
 * the engine. It has zero implementation — no function bodies, no static
 * variables. Any file in the testing system can include it without pulling
 * in executable code.
 *
 * DESIGN RULE: if a type is used by more than one of {testing_interface.c,
 * test_runner.c, test_reporter.c, a suite adapter}, it lives here.
 * Types private to a single translation unit stay in that .c file.
 */

#ifndef TEST_TYPES_H
#define TEST_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* ── capacity limits ─────────────────────────────────────────────────────── */

/** Maximum number of test suites that can be registered globally. */
#define TI_MAX_SUITES       128

/** Maximum number of test cases per suite. */
#define TI_MAX_CASES        512

/** Maximum string length for component/domain/tag names. */
#define TI_MAX_NAME_LEN     64

/** Maximum number of tags a single test case can carry. */
#define TI_MAX_TAGS         8

/** Maximum number of active filter entries in a single run request. */
#define TI_MAX_FILTER_VALS  16

/* ── test levels ─────────────────────────────────────────────────────────── */

/**
 * @brief The four testing levels, usable as a bitmask.
 *
 * WHY A BITMASK: a caller may want `TI_LEVEL_UNIT | TI_LEVEL_MODULE` in one
 * pass. Integer equality checks would require multiple calls or a loop.
 * Bitmask OR lets the engine filter in one comparison.
 *
 *   TI_LEVEL_UNIT        — single module, all dependencies mocked.
 *   TI_LEVEL_MODULE      — one component boundary, real internal deps.
 *   TI_LEVEL_INTEGRATION — two or more components talking to each other.
 *   TI_LEVEL_SYSTEM      — full stack, real binaries, real sockets.
 *
 * Keep these powers of two so the bitmask stays clean.
 */
typedef enum {
    TI_LEVEL_UNIT        = 1 << 0,   /* 0x01 */
    TI_LEVEL_MODULE      = 1 << 1,   /* 0x02 */
    TI_LEVEL_INTEGRATION = 1 << 2,   /* 0x04 */
    TI_LEVEL_SYSTEM      = 1 << 3,   /* 0x08 */
    TI_LEVEL_ALL         = 0x0F,     /* all four bits set */
} ti_level_t;

/* ── per-case result ─────────────────────────────────────────────────────── */

/**
 * @brief Result code returned by a single test case execution.
 *
 * TI_RESULT_PASS  — test returned 0 with no assertion failures.
 * TI_RESULT_FAIL  — test returned non-zero or an assertion fired.
 * TI_RESULT_SKIP  — test was intentionally skipped (setup indicated skip).
 * TI_RESULT_CRASH — test caused SIGSEGV/SIGABRT/SIGFPE; runner caught it.
 * TI_RESULT_TIMEOUT — test exceeded its timeout budget.
 * TI_RESULT_ERROR — engine error (bad pointer, unregistered suite, etc.).
 */
typedef enum {
    TI_RESULT_PASS    = 0,
    TI_RESULT_FAIL    = 1,
    TI_RESULT_SKIP    = 2,
    TI_RESULT_CRASH   = 3,
    TI_RESULT_TIMEOUT = 4,
    TI_RESULT_ERROR   = 5,
} ti_result_t;

/* ── function pointer types ──────────────────────────────────────────────── */

/**
 * @brief Signature of a test case function.
 *
 * Returns 0 on pass, any non-zero value on failure. This is identical to
 * the signature used by test_framework.h (HAL tests) so HAL test functions
 * can be registered without a wrapper.
 */
typedef int (*ti_test_fn_t)(void);

/**
 * @brief Signature of a setup or teardown hook.
 *
 * Setup: called before a test case (or suite). Returns 0 to proceed, non-zero
 * to skip the test case entirely (TI_RESULT_SKIP is recorded).
 *
 * Teardown: called after a test case regardless of outcome. Return value is
 * ignored (teardown failures are logged but do not change the test result).
 *
 * @param ctx  Opaque context pointer passed through from ti_suite_t.ctx.
 */
typedef int  (*ti_setup_fn_t)(void *ctx);
typedef void (*ti_teardown_fn_t)(void *ctx);

/* ── test case descriptor ────────────────────────────────────────────────── */

/**
 * @brief Describes one test case.
 *
 * Embed an array of these in a ti_suite_t. The engine iterates the array
 * and applies filters before executing each case.
 *
 * ZERO-INITIALISED SENTINEL: the engine stops iterating when it finds a
 * case with name == NULL. Terminate your case arrays with {0}.
 */
typedef struct {
    const char       *name;          /**< Human-readable name; must be unique within suite. */
    ti_test_fn_t      fn;            /**< Test body. NULL = skip unconditionally.           */
    ti_level_t        level;         /**< Which level this case belongs to.                 */
    const char       *tags[TI_MAX_TAGS]; /**< Optional filter tags; NULL-terminated list.  */
    uint32_t          timeout_ms;    /**< 0 = use suite default; non-zero overrides it.     */
    ti_setup_fn_t     setup;         /**< Per-case setup; NULL = use suite-level setup.     */
    ti_teardown_fn_t  teardown;      /**< Per-case teardown; NULL = use suite-level.        */
} ti_test_case_t;

/* ── suite descriptor ────────────────────────────────────────────────────── */

/**
 * @brief Describes a collection of related test cases.
 *
 * One suite = one component × one domain × one level (or multiple levels
 * if the suite contains cases at different levels — the case-level field
 * takes precedence in filtering).
 *
 * REGISTRATION: suites are registered via ti_register_suite(). Suites
 * that use __attribute__((constructor)) call this from their .c file's
 * constructor function so they self-register when linked.
 */
typedef struct {
    const char       *component;      /**< e.g. "hal", "service_manager", "security" */
    const char       *domain;         /**< e.g. "audio", "crypto", "sandbox"          */
    const char       *source_file;    /**< __FILE__ of the registering .c file        */
    ti_level_t        default_level;  /**< Level applied to cases with level == 0     */
    uint32_t          default_timeout_ms; /**< Per-case timeout; 0 = no timeout       */
    ti_setup_fn_t     suite_setup;    /**< Run once before all cases in this suite    */
    ti_teardown_fn_t  suite_teardown; /**< Run once after all cases in this suite     */
    void             *ctx;            /**< Passed to every setup/teardown in suite     */
    ti_test_case_t   *cases;          /**< NULL-terminated array of test cases        */
} ti_suite_t;

/* ── run filter ──────────────────────────────────────────────────────────── */

/**
 * @brief Selects which suites/cases to run.
 *
 * All fields are OR-combined within their type, AND-combined across types.
 * Example: levels = UNIT|MODULE AND component = "hal" AND domain = NULL (any).
 *
 * NULL / zero values mean "no restriction on this dimension".
 */
typedef struct {
    uint32_t    levels;                            /**< Bitmask of ti_level_t values  */
    const char *components[TI_MAX_FILTER_VALS];   /**< NULL-terminated list           */
    const char *domains[TI_MAX_FILTER_VALS];      /**< NULL-terminated list           */
    const char *tags[TI_MAX_FILTER_VALS];         /**< NULL-terminated list           */
    int         fail_fast;                         /**< Stop after first failure       */
    int         verbose;                           /**< Print each PASS, not just FAIL */
} ti_filter_t;

/* ── per-case outcome (stored in run result) ─────────────────────────────── */

typedef struct {
    const char  *suite_component;
    const char  *suite_domain;
    const char  *case_name;
    ti_level_t   level;
    ti_result_t  result;
    double       elapsed_ms;
    char         message[256];   /**< failure message or empty on pass */
} ti_case_outcome_t;

/* ── aggregate run result ────────────────────────────────────────────────── */

/**
 * @brief Returned by ti_run(). Heap-allocated; caller frees via ti_result_destroy().
 */
typedef struct {
    ti_case_outcome_t *outcomes;   /**< Array of per-case outcomes     */
    size_t             count;      /**< Number of entries in outcomes  */
    size_t             capacity;   /**< Allocated capacity             */
    int                total_run;
    int                total_pass;
    int                total_fail;
    int                total_skip;
    int                total_crash;
    int                total_timeout;
    double             total_elapsed_ms;
} ti_run_result_t;

/* ── report format ───────────────────────────────────────────────────────── */

typedef enum {
    TI_REPORT_TERMINAL = 0,   /**< ANSI-coloured human output (default) */
    TI_REPORT_TAP      = 1,   /**< TAP13 for CI pipelines               */
    TI_REPORT_JSON     = 2,   /**< JSON for dashboards                   */
} ti_report_format_t;

#endif /* TEST_TYPES_H */
