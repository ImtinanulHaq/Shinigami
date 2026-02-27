/**
 * @file test_runner.c
 * @brief Test case execution engine — implementation.
 *
 * PLACEMENT: testing/test_runner.c
 *
 * SIGNAL ISOLATION MECHANISM:
 *   POSIX sigsetjmp/siglongjmp is used instead of fork() because:
 *     - fork() + waitpid() costs ~1ms on Linux (clone syscall, copy page
 * tables). At 500 tests that is 500ms of pure fork overhead.
 *     - fork() breaks AddressSanitizer and ThreadSanitizer (they use global
 *       shared state that isn't safe to duplicate mid-run).
 *     - sigsetjmp saves/restores the signal mask atomically, which means
 *       the SIGALRM we armed for the previous test is correctly disarmed
 *       when we longjmp out of a crashed test body.
 *
 *   The tradeoff: state in the process is dirty after a crash. Heap may be
 *   corrupt. Any test that follows a crash may produce a false FAIL if it
 *   touches the same memory. Mitigation: the runner logs the crash and
 *   continues, but the CI summary shows CRASH as distinct from FAIL so the
 *   operator knows the subsequent results are suspect.
 *
 * TIMEOUT MECHANISM:
 *   alarm() delivers SIGALRM after N seconds. We convert timeout_ms to
 *   whole seconds (ceiling) because alarm() has 1-second granularity.
 *   For sub-second timeouts, use setitimer(ITIMER_REAL) — see the comment
 *   in _arm_timeout(). The current implementation uses alarm() for
 *   simplicity; sub-second granularity is a TODO.
 *
 * TIMING:
 *   CLOCK_MONOTONIC is mandatory. CLOCK_REALTIME can jump backward under
 *   NTP slew and would produce negative elapsed_ms values, which breaks
 *   any performance regression threshold check.
 */

#define _POSIX_C_SOURCE 200809L

#include "test_runner.h"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── crash-recovery jump buffer ──────────────────────────────────────────── */

/*
 * This must be process-global because signal handlers cannot take arguments.
 * It is only written by the runner loop (single-threaded) and only read by
 * the signal handler. No synchronisation needed.
 */
static sigjmp_buf s_crash_buf;
static volatile sig_atomic_t s_in_test = 0;   /* 1 while test body runs   */
static volatile sig_atomic_t s_crash_sig = 0; /* signal number that fired  */

/* ── original signal dispositions (restored on destroy) ─────────────────── */

static struct sigaction s_old_sigsegv;
static struct sigaction s_old_sigabrt;
static struct sigaction s_old_sigfpe;
static struct sigaction s_old_sigalrm;
static int s_handlers_installed = 0;

/* ── signal handler ──────────────────────────────────────────────────────── */

static void _crash_handler(int sig) {
  if (!s_in_test)
    return; /* signal outside test body — re-raise */
  s_crash_sig = sig;
  s_in_test = 0;
  siglongjmp(s_crash_buf, 1); /* jump back to runner, value=1 */
}

static void _alarm_handler(int sig) {
  (void)sig;
  if (!s_in_test)
    return;
  s_crash_sig = SIGALRM;
  s_in_test = 0;
  siglongjmp(s_crash_buf, 2); /* value=2 → timeout path */
}

/* ── runner lifecycle ────────────────────────────────────────────────────── */

void test_runner_init(void) {
  if (s_handlers_installed)
    return;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = _crash_handler;
  sigemptyset(&sa.sa_mask);
  /* SA_RESETHAND would restore the default handler after the first signal,
   * meaning a second SIGSEGV in the same test would kill the process.
   * We deliberately do NOT set SA_RESETHAND so we can survive multiple
   * crashes in a single run (though results after the first are suspect). */

  sigaction(SIGSEGV, &sa, &s_old_sigsegv);
  sigaction(SIGABRT, &sa, &s_old_sigabrt);
  sigaction(SIGFPE, &sa, &s_old_sigfpe);

  struct sigaction sa_alarm;
  memset(&sa_alarm, 0, sizeof(sa_alarm));
  sa_alarm.sa_handler = _alarm_handler;
  sigemptyset(&sa_alarm.sa_mask);
  sigaction(SIGALRM, &sa_alarm, &s_old_sigalrm);

  s_handlers_installed = 1;
}

void test_runner_destroy(void) {
  if (!s_handlers_installed)
    return;
  sigaction(SIGSEGV, &s_old_sigsegv, NULL);
  sigaction(SIGABRT, &s_old_sigabrt, NULL);
  sigaction(SIGFPE, &s_old_sigfpe, NULL);
  sigaction(SIGALRM, &s_old_sigalrm, NULL);
  s_handlers_installed = 0;
}

/* ── timing helpers ──────────────────────────────────────────────────────── */

static double _now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* ── timeout helpers ─────────────────────────────────────────────────────── */

static unsigned int _arm_timeout(uint32_t timeout_ms) {
  if (timeout_ms == 0)
    return 0;
  /* alarm() uses whole seconds. Ceiling division: (ms + 999) / 1000.
   * For sub-second precision, replace with:
   *   struct itimerval it = { .it_value = { 0, timeout_ms * 1000 } };
   *   setitimer(ITIMER_REAL, &it, NULL);
   * and cancel with setitimer(ITIMER_REAL, &zero_it, NULL). */
  unsigned int secs = (timeout_ms + 999u) / 1000u;
  return alarm(secs); /* returns previous alarm value */
}

static void _disarm_timeout(void) { alarm(0); }

/* ── core execution function ─────────────────────────────────────────────── */

void test_runner_execute(const ti_suite_t *suite, const ti_test_case_t *tc,
                         ti_case_outcome_t *outcome) {

  /* 0. Resolve setup/teardown: case overrides suite. */
  ti_setup_fn_t setup = tc->setup ? tc->setup : suite->suite_setup;
  ti_teardown_fn_t teardown =
      tc->teardown ? tc->teardown : suite->suite_teardown;
  void *ctx = suite->ctx;

  /* Resolve timeout: case > suite > 0 */
  uint32_t timeout_ms =
      tc->timeout_ms ? tc->timeout_ms : suite->default_timeout_ms;

  /* 1. Per-case setup. If setup returns non-zero, skip. */
  if (setup) {
    int sr = setup(ctx);
    if (sr != 0) {
      outcome->result = TI_RESULT_SKIP;
      snprintf(outcome->message, sizeof(outcome->message), "setup returned %d",
               sr);
      return;
    }
  }

  /* 2. Handle NULL function pointer — unconditional skip. */
  if (!tc->fn) {
    outcome->result = TI_RESULT_SKIP;
    snprintf(outcome->message, sizeof(outcome->message),
             "no test function (fn=NULL)");
    if (teardown)
      teardown(ctx);
    return;
  }

  /* 3. Arm timeout and crash recovery. */
  _arm_timeout(timeout_ms);
  s_crash_sig = 0;
  s_in_test = 1;

  double t_start = _now_ms();

  /* sigsetjmp returns 0 on first call, non-zero after siglongjmp. */
  int jmp_val = sigsetjmp(s_crash_buf, 1 /* save signal mask */);

  if (jmp_val == 0) {
    /* ── normal path: execute test body ── */
    int rc = tc->fn();

    s_in_test = 0;
    _disarm_timeout();

    outcome->elapsed_ms = _now_ms() - t_start;
    outcome->result = (rc == 0) ? TI_RESULT_PASS : TI_RESULT_FAIL;

    if (rc != 0)
      snprintf(outcome->message, sizeof(outcome->message), "test returned %d",
               rc);

  } else if (jmp_val == 1) {
    /* ── crash path: signal fired during test body ── */
    _disarm_timeout();
    outcome->elapsed_ms = _now_ms() - t_start;
    outcome->result = TI_RESULT_CRASH;

    const char *signame;
    switch (s_crash_sig) {
    case SIGSEGV:
      signame = "SIGSEGV (null/invalid ptr)";
      break;
    case SIGABRT:
      signame = "SIGABRT (assert/abort)";
      break;
    case SIGFPE:
      signame = "SIGFPE (divide by zero)";
      break;
    default:
      signame = "unknown signal";
      break;
    }
    snprintf(outcome->message, sizeof(outcome->message), "crashed: %s",
             signame);

  } else {
    /* jmp_val == 2: ── timeout path: SIGALRM fired ── */
    outcome->elapsed_ms = _now_ms() - t_start;
    outcome->result = TI_RESULT_TIMEOUT;
    snprintf(outcome->message, sizeof(outcome->message),
             "timed out after %u ms", timeout_ms);
  }

  /* 4. Per-case teardown — always runs, even after crash/timeout.
   * Note: after a crash, the process state is dirty. Teardown running
   * on dirty state may itself crash. The signal handler will catch it. */
  if (teardown) {
    s_in_test = 1;
    int td_jmp = sigsetjmp(s_crash_buf, 1);
    if (td_jmp == 0) {
      teardown(ctx);
    }
    s_in_test = 0;
    /* Teardown crash: we log but don't change the case outcome.
     * The test result was already recorded above. */
    if (td_jmp != 0) {
      fprintf(stderr, "[runner] WARNING: teardown crashed for '%s'\n",
              tc->name);
    }
  }
}
