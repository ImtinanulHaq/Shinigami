/**
 * @file test_reporter.c
 * @brief Test result output — terminal, TAP13, and JSON implementations.
 *
 * PLACEMENT: testing/test_reporter.c
 *
 * TAP13 FORMAT NOTES:
 *   TAP (Test Anything Protocol) v13 is a line-based protocol understood
 *   natively by Jenkins, GitHub Actions tap-reporter, Perl's prove, and
 *   Node's tap. The format is:
 *     TAP version 13
 *     1..N          <- plan line: N = total tests
 *     ok 1 name     <- pass
 *     not ok 2 name <- fail
 *       ---          <- YAML block (v13 extension for diagnostics)
 *       message: "..."
 *       ...
 *   CIs that consume TAP expect the plan line BEFORE the test lines.
 *   This means we must print the count before iterating results, which
 *   we have (result->total_run is available after ti_run() returns).
 *
 * JSON FORMAT NOTES:
 *   We emit a single JSON object, not a streaming NDJSON format.
 *   For large runs (10k+ tests) NDJSON would be better for memory, but
 *   most dashboards expect a single parseable object so we use that.
 *   JSON strings are escaped with _json_escape() to handle test names
 *   containing quotes, backslashes, or newlines.
 */

#include "test_reporter.h"
#include "testing_interface.h"

#include <stdio.h>
#include <string.h>

/* ── ANSI colour codes ───────────────────────────────────────────────────── */

#define C_RESET "\033[0m"
#define C_BOLD "\033[1m"
#define C_DIM "\033[2m"
#define C_GREEN "\033[0;32m"
#define C_RED "\033[0;31m"
#define C_YELLOW "\033[1;33m"
#define C_CYAN "\033[0;36m"
#define C_MAGENTA "\033[0;35m"
#define C_WHITE "\033[1;37m"

/* ── result colour selector ──────────────────────────────────────────────── */

static const char *_result_colour(ti_result_t r) {
  switch (r) {
  case TI_RESULT_PASS:
    return C_GREEN;
  case TI_RESULT_FAIL:
    return C_RED;
  case TI_RESULT_SKIP:
    return C_DIM;
  case TI_RESULT_CRASH:
    return C_MAGENTA;
  case TI_RESULT_TIMEOUT:
    return C_YELLOW;
  case TI_RESULT_ERROR:
    return C_YELLOW;
  default:
    return C_RESET;
  }
}

/* ── terminal reporter ───────────────────────────────────────────────────── */

static void _report_terminal(const ti_run_result_t *r, const ti_filter_t *f) {
  /* Header */
  printf("\n");
  printf(
      C_BOLD C_CYAN
      "╔══════════════════════════════════════════════════════════╗\n"
      "║            Unified Test Interface — Run Report           ║\n"
      "╚══════════════════════════════════════════════════════════╝\n" C_RESET);

  if (f) {
    printf(C_DIM "  filter: levels=0x%02x", f->levels);
    if (f->components[0]) {
      printf("  components=");
      for (int i = 0; i < TI_MAX_FILTER_VALS && f->components[i]; i++)
        printf("%s%s", i ? "," : "", f->components[i]);
    }
    if (f->domains[0]) {
      printf("  domains=");
      for (int i = 0; i < TI_MAX_FILTER_VALS && f->domains[i]; i++)
        printf("%s%s", i ? "," : "", f->domains[i]);
    }
    printf(C_RESET "\n");
  }

  printf("\n");

  /* Per-case table */
  const char *cur_component = NULL;
  const char *cur_domain = NULL;

  for (size_t i = 0; i < r->count; i++) {
    const ti_case_outcome_t *o = &r->outcomes[i];

    /* Print component/domain section header when they change */
    if (!cur_component || strcmp(cur_component, o->suite_component) != 0 ||
        strcmp(cur_domain, o->suite_domain) != 0) {

      cur_component = o->suite_component;
      cur_domain = o->suite_domain;

      printf(C_BOLD "\n  ▸ %s / %s  " C_DIM "(%s)" C_RESET "\n", cur_component,
             cur_domain, ti_level_name(o->level));
    }

    /* Pad test name to 50 chars for alignment */
    const char *col = _result_colour(o->result);
    const char *res = ti_result_name(o->result);

    printf("    %-50s %s%-7s%s %6.1f ms", o->case_name, col, res, C_RESET,
           o->elapsed_ms);

    if (o->message[0] != '\0')
      printf("  " C_DIM "(%s)" C_RESET, o->message);

    printf("\n");
  }

  /* Summary bar */
  printf("\n");
  printf(
      C_BOLD
      "  ──────────────────────────────────────────────────────────\n" C_RESET);

  /* Pass rate */
  int pct = (r->total_run > 0) ? (r->total_pass * 100 / r->total_run) : 100;

  printf("  Total   : %d\n", r->total_run);
  printf("  " C_GREEN "Pass    : %d%s\n" C_RESET, r->total_pass, "");
  printf("  " C_RED "Fail    : %d%s\n" C_RESET, r->total_fail, "");

  if (r->total_skip)
    printf("  " C_DIM "Skip    : %d\n" C_RESET, r->total_skip);
  if (r->total_crash)
    printf("  " C_MAGENTA "Crash   : %d\n" C_RESET, r->total_crash);
  if (r->total_timeout)
    printf("  " C_YELLOW "Timeout : %d\n" C_RESET, r->total_timeout);

  printf("  Time    : %.1f ms\n", r->total_elapsed_ms);
  printf("  Rate    : %d%%\n", pct);

  printf("\n");

  /* Final verdict */
  if (r->total_fail == 0 && r->total_crash == 0 && r->total_timeout == 0) {
    printf(C_BOLD C_GREEN "  ✓  ALL TESTS PASSED\n" C_RESET);
  } else {
    printf(C_BOLD C_RED "  ✗  FAILURES DETECTED\n" C_RESET);

    /* Print a failure digest so the operator doesn't have to scroll */
    printf(C_BOLD "\n  Failed cases:\n" C_RESET);
    for (size_t i = 0; i < r->count; i++) {
      const ti_case_outcome_t *o = &r->outcomes[i];
      if (o->result == TI_RESULT_PASS || o->result == TI_RESULT_SKIP)
        continue;
      printf("    %s%s%s / %s%s%s  → %s%s%s", C_CYAN, o->suite_component,
             C_RESET, C_CYAN, o->suite_domain, C_RESET,
             _result_colour(o->result), ti_result_name(o->result), C_RESET);
      printf("  %s\n", o->case_name);
      if (o->message[0])
        printf("      " C_DIM "%s" C_RESET "\n", o->message);
    }
  }

  printf("\n");
}

/* ── TAP13 reporter ──────────────────────────────────────────────────────── */

static void _report_tap(const ti_run_result_t *r) {
  printf("TAP version 13\n");
  printf("1..%d\n", r->total_run);

  for (size_t i = 0; i < r->count; i++) {
    const ti_case_outcome_t *o = &r->outcomes[i];
    int tap_num = (int)(i + 1);

    int is_ok = (o->result == TI_RESULT_PASS);
    int is_skip = (o->result == TI_RESULT_SKIP);

    if (is_ok) {
      printf("ok %d %s/%s/%s\n", tap_num, o->suite_component, o->suite_domain,
             o->case_name);
    } else if (is_skip) {
      printf("ok %d %s/%s/%s # SKIP %s\n", tap_num, o->suite_component,
             o->suite_domain, o->case_name, o->message);
    } else {
      printf("not ok %d %s/%s/%s\n", tap_num, o->suite_component,
             o->suite_domain, o->case_name);

      /* TAP v13 YAML diagnostic block */
      printf("  ---\n");
      printf("  result: %s\n", ti_result_name(o->result));
      if (o->message[0])
        printf("  message: \"%s\"\n", o->message);
      printf("  elapsed_ms: %.2f\n", o->elapsed_ms);
      printf("  ...\n");
    }
  }

  /* Summary comment — not part of TAP protocol but harmless */
  printf("# pass %d\n", r->total_pass);
  printf("# fail %d\n", r->total_fail);
  printf("# skip %d\n", r->total_skip);
  printf("# crash %d\n", r->total_crash);
  printf("# timeout %d\n", r->total_timeout);
  printf("# time %.1fms\n", r->total_elapsed_ms);
}

/* ── JSON reporter ───────────────────────────────────────────────────────── */

/*
 * Minimal JSON string escaper. Handles the characters that appear in
 * test names and messages: \, ", newline, tab, carriage return, and
 * other control characters (<0x20).
 */
static void _json_escape(const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '"':
      printf("\\\"");
      break;
    case '\\':
      printf("\\\\");
      break;
    case '\n':
      printf("\\n");
      break;
    case '\r':
      printf("\\r");
      break;
    case '\t':
      printf("\\t");
      break;
    default:
      if ((unsigned char)*s < 0x20)
        printf("\\u%04x", (unsigned char)*s);
      else
        putchar(*s);
      break;
    }
  }
}

static void _report_json(const ti_run_result_t *r) {
  printf("{\n");
  printf("  \"summary\": {\n");
  printf("    \"total\":   %d,\n", r->total_run);
  printf("    \"pass\":    %d,\n", r->total_pass);
  printf("    \"fail\":    %d,\n", r->total_fail);
  printf("    \"skip\":    %d,\n", r->total_skip);
  printf("    \"crash\":   %d,\n", r->total_crash);
  printf("    \"timeout\": %d,\n", r->total_timeout);
  printf("    \"elapsed_ms\": %.2f\n", r->total_elapsed_ms);
  printf("  },\n");
  printf("  \"cases\": [\n");

  for (size_t i = 0; i < r->count; i++) {
    const ti_case_outcome_t *o = &r->outcomes[i];
    printf("    {\n");
    printf("      \"component\": \"");
    _json_escape(o->suite_component);
    printf("\",\n");
    printf("      \"domain\":    \"");
    _json_escape(o->suite_domain);
    printf("\",\n");
    printf("      \"name\":      \"");
    _json_escape(o->case_name);
    printf("\",\n");
    printf("      \"level\":     \"%s\",\n", ti_level_name(o->level));
    printf("      \"result\":    \"%s\",\n", ti_result_name(o->result));
    printf("      \"elapsed_ms\": %.2f", o->elapsed_ms);
    if (o->message[0]) {
      printf(",\n      \"message\": \"");
      _json_escape(o->message);
      printf("\"");
    }
    printf("\n    }%s\n", (i + 1 < r->count) ? "," : "");
  }

  printf("  ]\n}\n");
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

void test_reporter_print(const ti_run_result_t *result,
                         const ti_filter_t *filter, ti_report_format_t fmt) {
  if (!result)
    return;

  switch (fmt) {
  case TI_REPORT_TAP:
    _report_tap(result);
    break;
  case TI_REPORT_JSON:
    _report_json(result);
    break;
  default:
    _report_terminal(result, filter);
    break;
  }
}
