/**
 * @file testing_interface.c
 * @brief Unified testing engine — registry, filter, orchestration.
 *
 * PLACEMENT: testing/testing_interface.c
 *
 * WHY THIS FILE EXISTS:
 *   Every other approach to "unified testing" either hardcodes component
 *   names (breaks at component N) or uses runtime .so loading (needs a
 *   dynamic linker, can't run on bare metal, has symbol collision risk).
 *
 *   This engine uses the linker as the composition mechanism: suites
 *   register themselves via __attribute__((constructor)) in their adapter
 *   .c files. If that .o is linked, the suite is registered. If it isn't,
 *   it doesn't exist. Zero runtime discovery overhead. Zero config files.
 *
 * THREAD SAFETY:
 *   ti_register_suite() uses an atomic increment for the registry index
 *   so it is safe to call from constructors that run in parallel (rare but
 *   theoretically possible with certain linker/loader implementations).
 *   All other functions are single-threaded (called from main()).
 */

#include "test_reporter.h"
#include "test_runner.h"
#include "testing_interface.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── internal registry ───────────────────────────────────────────────────── */

static ti_suite_t *s_registry[TI_MAX_SUITES];
static atomic_int s_suite_count = 0;
static int s_initialised = 0;
static ti_report_format_t s_report_format = TI_REPORT_TERMINAL;

/* ── engine lifecycle ──────────────────────────────────────────────────── */

int ti_init(void) {
  if (s_initialised)
    return 0;

  memset(s_registry, 0, sizeof(s_registry));

  /* Read TI_REPORT_FORMAT env var once at init time.
   * Doing it here, not in ti_report(), avoids repeated getenv calls on
   * the hot path when running thousands of tests. */
  const char *fmt_env = getenv("TI_REPORT_FORMAT");
  if (fmt_env) {
    if (strcmp(fmt_env, "tap") == 0)
      s_report_format = TI_REPORT_TAP;
    else if (strcmp(fmt_env, "json") == 0)
      s_report_format = TI_REPORT_JSON;
    else
      s_report_format = TI_REPORT_TERMINAL;
  }

  /* Install signal handlers for crash isolation.
   * This is done in test_runner_init(); we delegate rather than
   * duplicate signal-handling logic. */
  test_runner_init();

  s_initialised = 1;
  return 0;
}

void ti_destroy(void) {
  if (!s_initialised)
    return;
  atomic_store(&s_suite_count, 0);
  memset(s_registry, 0, sizeof(s_registry));
  test_runner_destroy();
  s_initialised = 0;
}

/* ── suite registration ──────────────────────────────────────────────────── */

int ti_register_suite(ti_suite_t *suite) {
  if (!suite || !suite->component || !suite->domain || !suite->cases)
    return -1;

  /* atomic_fetch_add returns the old value; we get our slot index. */
  int slot = atomic_fetch_add(&s_suite_count, 1);
  if (slot >= TI_MAX_SUITES) {
    /* Roll back — we overflowed. Print to stderr because we may be in
     * a constructor before any test infrastructure is ready. */
    atomic_fetch_sub(&s_suite_count, 1);
    fprintf(stderr,
            "[ti] registry full (TI_MAX_SUITES=%d): "
            "could not register %s/%s\n",
            TI_MAX_SUITES, suite->component, suite->domain);
    return -1;
  }

  s_registry[slot] = suite;
  return 0;
}

int ti_suite_count(void) { return atomic_load(&s_suite_count); }

const ti_suite_t *ti_suite_get(int i) {
  if (i < 0 || i >= atomic_load(&s_suite_count))
    return NULL;
  return s_registry[i];
}

/* ── filter helpers ──────────────────────────────────────────────────────── */

ti_filter_t ti_filter_none(void) {
  ti_filter_t f;
  memset(&f, 0, sizeof(f));
  return f;
}

ti_filter_t ti_filter_all(void) {
  ti_filter_t f;
  memset(&f, 0, sizeof(f));
  f.levels = TI_LEVEL_ALL;
  return f;
}

static int _add_strlist(const char *list[], const char *val) {
  for (int i = 0; i < TI_MAX_FILTER_VALS; i++) {
    if (!list[i]) {
      list[i] = val;
      return 0;
    }
  }
  return -1;
}

int ti_filter_add_component(ti_filter_t *f, const char *c) {
  if (!f || !c || c[0] == '\0')
    return -1;
  return _add_strlist(f->components, c);
}

int ti_filter_add_domain(ti_filter_t *f, const char *d) {
  if (!f || !d || d[0] == '\0')
    return -1;
  return _add_strlist(f->domains, d);
}

int ti_filter_add_tag(ti_filter_t *f, const char *t) {
  if (!f || !t || t[0] == '\0')
    return -1;
  return _add_strlist(f->tags, t);
}

/* ── filter matching ─────────────────────────────────────────────────────── */

/* Return 1 if needle is in the NULL-terminated list, or if list is empty. */
static int _matches_strlist(const char *list[], const char *needle) {
  if (!list[0])
    return 1; /* empty list = no restriction */
  for (int i = 0; i < TI_MAX_FILTER_VALS && list[i]; i++) {
    if (strcmp(list[i], needle) == 0)
      return 1;
  }
  return 0;
}

/* Return 1 if a test case's tag list contains ANY tag from the filter. */
static int _case_matches_tags(const char *filter_tags[],
                              const char *case_tags[]) {
  if (!filter_tags[0])
    return 1; /* no tag filter = all pass */
  for (int fi = 0; fi < TI_MAX_FILTER_VALS && filter_tags[fi]; fi++) {
    for (int ci = 0; ci < TI_MAX_TAGS && case_tags[ci]; ci++) {
      if (strcmp(filter_tags[fi], case_tags[ci]) == 0)
        return 1;
    }
  }
  return 0;
}

/**
 * @brief Return 1 if this suite+case combination should run given the filter.
 *
 * Matching logic:
 *   level     : case.level & filter.levels must be non-zero
 *   component : case's suite.component must be in filter.components list (OR)
 *   domain    : case's suite.domain must be in filter.domains list (OR)
 *   tags      : case must carry at least one tag from filter.tags (OR)
 *
 *   All four conditions are AND-combined.
 *   A filter dimension with no entries means "no restriction on this
 * dimension".
 */
static int _case_matches(const ti_filter_t *f, const ti_suite_t *suite,
                         const ti_test_case_t *tc) {
  /* Resolve level: case.level=0 inherits suite.default_level */
  ti_level_t case_level = tc->level ? tc->level : suite->default_level;

  if (f->levels && !(case_level & f->levels))
    return 0;
  if (!_matches_strlist(f->components, suite->component))
    return 0;
  if (!_matches_strlist(f->domains, suite->domain))
    return 0;
  if (!_case_matches_tags(f->tags, tc->tags))
    return 0;
  return 1;
}

/* ── result management ───────────────────────────────────────────────────── */

static ti_run_result_t *_result_new(void) {
  ti_run_result_t *r = calloc(1, sizeof(ti_run_result_t));
  if (!r)
    return NULL;

  /* Pre-allocate for the common case of a few hundred tests. realloc
   * doubles capacity when we overflow, so amortised cost is O(1). */
  r->capacity = 256;
  r->outcomes = calloc(r->capacity, sizeof(ti_case_outcome_t));
  if (!r->outcomes) {
    free(r);
    return NULL;
  }
  return r;
}

static int _result_append(ti_run_result_t *r, const ti_case_outcome_t *o) {
  if (r->count >= r->capacity) {
    size_t new_cap = r->capacity * 2;
    ti_case_outcome_t *p =
        realloc(r->outcomes, new_cap * sizeof(ti_case_outcome_t));
    if (!p)
      return -1;
    r->outcomes = p;
    r->capacity = new_cap;
  }
  r->outcomes[r->count++] = *o;
  return 0;
}

static void _result_tally(ti_run_result_t *r, ti_result_t res) {
  r->total_run++;
  switch (res) {
  case TI_RESULT_PASS:
    r->total_pass++;
    break;
  case TI_RESULT_FAIL:
    r->total_fail++;
    break;
  case TI_RESULT_SKIP:
    r->total_skip++;
    break;
  case TI_RESULT_CRASH:
    r->total_crash++;
    break;
  case TI_RESULT_TIMEOUT:
    r->total_timeout++;
    break;
  default:
    break;
  }
}

void ti_result_destroy(ti_run_result_t *r) {
  if (!r)
    return;
  free(r->outcomes);
  free(r);
}

/* ── main run loop ───────────────────────────────────────────────────────── */

ti_run_result_t *ti_run(const ti_filter_t *filter) {
  /* Use a match-everything filter when NULL is passed. */
  ti_filter_t all_filter = ti_filter_all();
  if (!filter)
    filter = &all_filter;

  ti_run_result_t *result = _result_new();
  if (!result)
    return NULL;

  int n_suites = atomic_load(&s_suite_count);

  for (int si = 0; si < n_suites; si++) {
    ti_suite_t *suite = s_registry[si];
    if (!suite)
      continue;

    /* Check if any case in this suite matches before running suite setup.
     * Avoids expensive setup/teardown when the whole suite is filtered. */
    int suite_has_match = 0;
    for (int ci = 0; suite->cases[ci].name; ci++) {
      if (_case_matches(filter, suite, &suite->cases[ci])) {
        suite_has_match = 1;
        break;
      }
    }
    if (!suite_has_match)
      continue;

    /* Suite-level setup */
    if (suite->suite_setup) {
      int sr = suite->suite_setup(suite->ctx);
      if (sr != 0) {
        /* Setup failed: skip entire suite, record one SKIP per case */
        for (int ci = 0; suite->cases[ci].name; ci++) {
          if (!_case_matches(filter, suite, &suite->cases[ci]))
            continue;
          ti_case_outcome_t o = {
              .suite_component = suite->component,
              .suite_domain = suite->domain,
              .case_name = suite->cases[ci].name,
              .level = suite->cases[ci].level ? suite->cases[ci].level
                                              : suite->default_level,
              .result = TI_RESULT_SKIP,
              .elapsed_ms = 0.0,
          };
          snprintf(o.message, sizeof(o.message), "suite setup failed (rc=%d)",
                   sr);
          _result_append(result, &o);
          _result_tally(result, TI_RESULT_SKIP);
        }
        continue;
      }
    }

    /* Per-case execution */
    for (int ci = 0; suite->cases[ci].name; ci++) {
      const ti_test_case_t *tc = &suite->cases[ci];
      if (!_case_matches(filter, suite, tc))
        continue;

      ti_case_outcome_t outcome = {
          .suite_component = suite->component,
          .suite_domain = suite->domain,
          .case_name = tc->name,
          .level = tc->level ? tc->level : suite->default_level,
      };

      /* Delegate actual execution to test_runner.c, which handles
       * setup/teardown, signal isolation, and timing. */
      test_runner_execute(suite, tc, &outcome);

      _result_append(result, &outcome);
      _result_tally(result, outcome.result);
      result->total_elapsed_ms += outcome.elapsed_ms;

      if (filter->fail_fast && outcome.result == TI_RESULT_FAIL) {
        /* Record remaining as skipped, then exit both loops. */
        if (suite->suite_teardown)
          suite->suite_teardown(suite->ctx);
        goto done;
      }
    }

    /* Suite-level teardown */
    if (suite->suite_teardown)
      suite->suite_teardown(suite->ctx);
  }

done:
  return result;
}

/* ── reporting ───────────────────────────────────────────────────────────── */

void ti_set_report_format(ti_report_format_t fmt) { s_report_format = fmt; }

void ti_report(const ti_run_result_t *result, const ti_filter_t *filter) {
  if (!result)
    return;
  test_reporter_print(result, filter, s_report_format);
}

/* ── utility ─────────────────────────────────────────────────────────────── */

const char *ti_level_name(ti_level_t level) {
  switch (level) {
  case TI_LEVEL_UNIT:
    return "unit";
  case TI_LEVEL_MODULE:
    return "module";
  case TI_LEVEL_INTEGRATION:
    return "integration";
  case TI_LEVEL_SYSTEM:
    return "system";
  case TI_LEVEL_ALL:
    return "all";
  default:
    if (level == 0)
      return "none";
    return "mixed";
  }
}

const char *ti_result_name(ti_result_t result) {
  switch (result) {
  case TI_RESULT_PASS:
    return "PASS";
  case TI_RESULT_FAIL:
    return "FAIL";
  case TI_RESULT_SKIP:
    return "SKIP";
  case TI_RESULT_CRASH:
    return "CRASH";
  case TI_RESULT_TIMEOUT:
    return "TIMEOUT";
  case TI_RESULT_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

void ti_list_suites(void) {
  int n = atomic_load(&s_suite_count);
  printf("\nRegistered suites (%d):\n", n);
  printf("  %-20s %-20s %-12s  cases\n", "component", "domain", "level");
  printf("  %-20s %-20s %-12s  -----\n", "--------------------",
         "--------------------", "------------");

  for (int si = 0; si < n; si++) {
    const ti_suite_t *s = s_registry[si];
    if (!s)
      continue;

    int case_count = 0;
    while (s->cases[case_count].name)
      case_count++;

    printf("  %-20s %-20s %-12s  %d\n", s->component, s->domain,
           ti_level_name(s->default_level), case_count);
  }
  printf("\n");
}
