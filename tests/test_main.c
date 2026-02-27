/**
 * @file test_main.c
 * @brief Unified testing interface — CLI entry point.
 *
 * PLACEMENT: testing/test_main.c
 *
 * CLI INTERFACE:
 *   ./test_runner [OPTIONS]
 *
 *   --level    <unit|module|integration|system|all>   default: all
 *   --component <name>     filter to one component     default: all
 *   --domain   <name>      filter to one domain        default: all
 *   --tag      <name>      filter by tag               default: all
 *   --format   <terminal|tap|json>                     default: terminal
 *   --fail-fast            stop on first failure
 *   --verbose              print PASSes (default: only FAILs in terminal)
 *   --list                 list all registered suites and exit
 *   --help                 print usage and exit
 *
 * MULTIPLE --component / --domain / --tag FLAGS:
 *   Each flag appends to its filter list. Multiple components are OR'd.
 *   ./test_runner --component hal --component service_manager --level unit
 *   runs unit tests for BOTH components in one pass.
 *
 * EXIT CODE CONTRACT (important for CI):
 *   0  → all executed tests passed (or were skipped)
 *   1  → one or more tests failed, crashed, or timed out
 *   2  → argument error or engine initialisation failure
 *
 * ENVIRONMENT VARIABLES (override defaults without modifying CI scripts):
 *   TI_REPORT_FORMAT=tap|json|terminal
 *   TI_FAIL_FAST=1
 *   TI_VERBOSE=1
 */

#include "testing_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── usage ───────────────────────────────────────────────────────────────── */

static void _print_usage(const char *argv0) {
  printf(
      "\nUsage: %s [OPTIONS]\n"
      "\n"
      "Level selection:\n"
      "  --level <value>      unit | module | integration | system | all\n"
      "                       Multiple levels: --level unit --level module\n"
      "\n"
      "Scope selection (OR within same flag, AND across flag types):\n"
      "  --component <name>   e.g. hal, service_manager, security\n"
      "  --domain    <name>   e.g. audio, crypto, registry\n"
      "  --tag       <name>   run only cases tagged with this value\n"
      "\n"
      "Output:\n"
      "  --format <f>         terminal (default) | tap | json\n"
      "  --verbose            print PASS results (default: FAIL/CRASH only)\n"
      "\n"
      "Execution:\n"
      "  --fail-fast          stop immediately after first failure\n"
      "\n"
      "Discovery:\n"
      "  --list               print all registered suites and exit\n"
      "\n"
      "  --help               print this message and exit\n"
      "\n"
      "Environment variables (override defaults):\n"
      "  TI_REPORT_FORMAT=tap|json|terminal\n"
      "  TI_FAIL_FAST=1\n"
      "  TI_VERBOSE=1\n"
      "\n"
      "Examples:\n"
      "  %s\n"
      "  %s --level unit --component hal\n"
      "  %s --level unit --component hal --domain audio\n"
      "  %s --level integration --format tap\n"
      "  %s --component service_manager --component hal --level module\n"
      "  %s --list\n"
      "\n",
      argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

/* ── level name → bitmask ────────────────────────────────────────────────── */

static ti_level_t _parse_level(const char *s) {
  if (!s)
    return 0;
  if (strcmp(s, "unit") == 0)
    return TI_LEVEL_UNIT;
  if (strcmp(s, "module") == 0)
    return TI_LEVEL_MODULE;
  if (strcmp(s, "integration") == 0)
    return TI_LEVEL_INTEGRATION;
  if (strcmp(s, "system") == 0)
    return TI_LEVEL_SYSTEM;
  if (strcmp(s, "all") == 0)
    return TI_LEVEL_ALL;
  fprintf(stderr,
          "[ti] unknown level '%s' (unit|module|integration|system|all)\n", s);
  return 0;
}

static ti_report_format_t _parse_format(const char *s) {
  if (!s)
    return TI_REPORT_TERMINAL;
  if (strcmp(s, "tap") == 0)
    return TI_REPORT_TAP;
  if (strcmp(s, "json") == 0)
    return TI_REPORT_JSON;
  if (strcmp(s, "terminal") == 0)
    return TI_REPORT_TERMINAL;
  fprintf(stderr, "[ti] unknown format '%s' (terminal|tap|json)\n", s);
  return TI_REPORT_TERMINAL;
}

/* ── argument parsing ────────────────────────────────────────────────────── */

static int _requires_arg(const char *flag, int i, int argc) {
  if (i + 1 >= argc) {
    fprintf(stderr, "[ti] %s requires an argument\n", flag);
    return 0;
  }
  return 1;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {

  /* Engine init — installs signal handlers, reads TI_REPORT_FORMAT env */
  if (ti_init() != 0) {
    fprintf(stderr, "[ti] engine initialisation failed\n");
    return 2;
  }

  /* Build filter from CLI args */
  ti_filter_t filter = ti_filter_all(); /* start with "run everything" */
  filter.levels = 0;                    /* will be set from --level flags */

  ti_report_format_t fmt = TI_REPORT_TERMINAL;
  int do_list = 0;
  int levels_set = 0;

  /* Read environment variable overrides before CLI args so CLI wins. */
  const char *env_fmt = getenv("TI_REPORT_FORMAT");
  const char *env_failfast = getenv("TI_FAIL_FAST");
  const char *env_verbose = getenv("TI_VERBOSE");

  if (env_fmt)
    fmt = _parse_format(env_fmt);
  if (env_failfast && env_failfast[0] == '1')
    filter.fail_fast = 1;
  if (env_verbose && env_verbose[0] == '1')
    filter.verbose = 1;

  /* CLI argument parsing — O(N) over argv */
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];

    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      _print_usage(argv[0]);
      ti_destroy();
      return 0;
    }

    if (strcmp(arg, "--list") == 0) {
      do_list = 1;
      continue;
    }

    if (strcmp(arg, "--fail-fast") == 0) {
      filter.fail_fast = 1;
      continue;
    }

    if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
      filter.verbose = 1;
      continue;
    }

    if (strcmp(arg, "--level") == 0) {
      if (!_requires_arg(arg, i, argc))
        return 2;
      ti_level_t lv = _parse_level(argv[++i]);
      if (lv == 0)
        return 2;
      filter.levels |= lv;
      levels_set = 1;
      continue;
    }

    if (strcmp(arg, "--component") == 0) {
      if (!_requires_arg(arg, i, argc))
        return 2;
      if (ti_filter_add_component(&filter, argv[++i]) != 0) {
        fprintf(stderr, "[ti] too many --component values\n");
        return 2;
      }
      continue;
    }

    if (strcmp(arg, "--domain") == 0) {
      if (!_requires_arg(arg, i, argc))
        return 2;
      if (ti_filter_add_domain(&filter, argv[++i]) != 0) {
        fprintf(stderr, "[ti] too many --domain values\n");
        return 2;
      }
      continue;
    }

    if (strcmp(arg, "--tag") == 0) {
      if (!_requires_arg(arg, i, argc))
        return 2;
      if (ti_filter_add_tag(&filter, argv[++i]) != 0) {
        fprintf(stderr, "[ti] too many --tag values\n");
        return 2;
      }
      continue;
    }

    if (strcmp(arg, "--format") == 0) {
      if (!_requires_arg(arg, i, argc))
        return 2;
      fmt = _parse_format(argv[++i]);
      continue;
    }

    fprintf(stderr, "[ti] unknown argument '%s' (run with --help)\n", arg);
    ti_destroy();
    return 2;
  }

  /* If no --level given, run all levels */
  if (!levels_set)
    filter.levels = TI_LEVEL_ALL;

  /* Apply report format (CLI overrides env var) */
  ti_set_report_format(fmt);

  /* --list mode: print suites and exit without running */
  if (do_list) {
    ti_list_suites();
    ti_destroy();
    return 0;
  }

  /* Execute */
  ti_run_result_t *result = ti_run(&filter);
  if (!result) {
    fprintf(stderr, "[ti] ti_run() returned NULL (allocation failure)\n");
    ti_destroy();
    return 2;
  }

  /* Report */
  ti_report(result, &filter);

  /* Exit code: 0=all pass, 1=any failure */
  int failures =
      result->total_fail + result->total_crash + result->total_timeout;

  ti_result_destroy(result);
  ti_destroy();

  return (failures > 0) ? 1 : 0;
}
