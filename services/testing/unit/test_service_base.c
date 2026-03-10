/**
 * @file test_service_base.c
 * @brief Unit tests for common/service_base.
 *
 * Tests covered:
 *   1. service_base_init() — null-guard, state defaults, name copy.
 *   2. service_base_write_pid() + service_base_remove_pid() — write, read back,
 *      remove, stale-file detection.
 *   3. Duplicate instance detection — kill(pid, 0) on a live PID must return
 *      SVC_ERR_ALREADY instead of re-writing the file.
 *   4. svc_error_string() — every known code returns a non-NULL, non-empty string.
 *   5. service_base_install_signals() — installs without error; SIGHUP sets
 *      g_reload; SIGTERM sets g_running=0.
 *   6. service_base_open_log() / service_base_close_log() — no crash, file sink.
 *   7. service_base_daemonize() in foreground mode — ctx->foreground=1 must
 *      cause daemonize to return SVC_OK without actually forking.
 *
 * All PID-file operations use /tmp/ so root is not required.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_service_base.c ../../common/service_base.c \
 *       -I../../common -o test_service_base
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../common/service_base.h"

/* ── micro test framework ─────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do {                                            \
    g_tests_run++;                                                  \
    printf("  [RUN ]  test_" #name "\n");                          \
    test_##name();                                                  \
    g_tests_passed++;                                               \
    printf("  [ OK ]  test_" #name "\n");                          \
} while (0)

/* Use a unique PID-file suffix to avoid collisions with real daemons */
#define TEST_SVC "test_svc_XXXXXX"

/* Override the default PID-file directory to /tmp/ in tests.
 * service_base_write_pid/remove_pid read SVC_PID_DIR from the environment
 * if set; tests set it to /tmp. */
static void set_pid_tmp(void)
{
    setenv("SVC_PID_DIR", "/tmp", 1);
}
static void clear_pid_tmp(void)
{
    unsetenv("SVC_PID_DIR");
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1a. Basic init */
TEST(init_basic)
{
    svc_context_t ctx;
    int rc = service_base_init(&ctx, "unit_test");
    assert(rc == SVC_OK);
    assert(strcmp(ctx.name, "unit_test") == 0);
    assert(ctx.state == SVC_STATE_INIT);
    assert(ctx.foreground == 0);
    assert(ctx.verbose == 0);
    assert(ctx.error_count == 0);
}

/* 1b. Null name guard */
TEST(init_null_name)
{
    svc_context_t ctx;
    int rc = service_base_init(&ctx, NULL);
    assert(rc == SVC_ERR_INVALID);
}

/* 1c. Null ctx guard */
TEST(init_null_ctx)
{
    int rc = service_base_init(NULL, "unit_test");
    assert(rc == SVC_ERR_INVALID);
}

/* 1d. Name exceeding SERVICE_MAX_NAME is truncated, not overflowed */
TEST(init_long_name)
{
    svc_context_t ctx;
    /* Build a name exactly 2× SERVICE_MAX_NAME chars */
    char buf[SERVICE_MAX_NAME * 2 + 1];
    memset(buf, 'A', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int rc = service_base_init(&ctx, buf);
    assert(rc == SVC_OK);
    assert(ctx.name[SERVICE_MAX_NAME - 1] == '\0');
}

/* 2a. Write and read back PID file */
TEST(pidfile_write_read_remove)
{
    set_pid_tmp();

    int rc = service_base_write_pid("unit_pidtest");
    assert(rc == SVC_OK);

    /* Read back and verify */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/unit_pidtest.pid");
    FILE *fp = fopen(path, "r");
    assert(fp != NULL);

    pid_t stored = 0;
    int n = fscanf(fp, "%d", (int *)&stored);
    fclose(fp);
    assert(n == 1);
    assert(stored == getpid());

    service_base_remove_pid("unit_pidtest");
    /* File must be gone */
    assert(access(path, F_OK) != 0);

    clear_pid_tmp();
}

/* 2b. Remove non-existent PID file is a no-op (no crash) */
TEST(pidfile_remove_nonexistent)
{
    set_pid_tmp();
    /* Should not crash */
    service_base_remove_pid("nonexistent_daemon_xyzzy");
    clear_pid_tmp();
}

/* 3. Duplicate instance detection */
TEST(pidfile_duplicate_detection)
{
    set_pid_tmp();

    /* Write our own PID — first call must succeed */
    int rc = service_base_write_pid("dup_test_svc");
    assert(rc == SVC_OK);

    /* Second call with the same service name: our PID is still live,
     * kill(getpid(), 0) must succeed → SVC_ERR_ALREADY expected. */
    rc = service_base_write_pid("dup_test_svc");
    assert(rc == SVC_ERR_ALREADY);

    service_base_remove_pid("dup_test_svc");

    /* After removing the PID file, writing again must succeed */
    rc = service_base_write_pid("dup_test_svc");
    assert(rc == SVC_OK);

    service_base_remove_pid("dup_test_svc");
    clear_pid_tmp();
}

/* 3b. Stale PID file from a dead process — must be overwritten */
TEST(pidfile_stale_overwrite)
{
    set_pid_tmp();

    /* Write a PID that is guaranteed to not be running (PID 2^22-1) */
    FILE *fp = fopen("/tmp/stale_svc.pid", "w");
    assert(fp != NULL);
    fprintf(fp, "%d\n", 4194303);
    fclose(fp);

    /* service_base_write_pid should detect the stale PID and overwrite */
    int rc = service_base_write_pid("stale_svc");
    assert(rc == SVC_OK);

    service_base_remove_pid("stale_svc");
    clear_pid_tmp();
}

/* 4. svc_error_string() */
TEST(error_strings)
{
    const struct { int code; const char *must_contain; } tab[] = {
        { SVC_OK,              "success"   },
        { SVC_ERR_GENERIC,     NULL        },
        { SVC_ERR_INVALID,     NULL        },
        { SVC_ERR_FORK,        NULL        },
        { SVC_ERR_PIDFILE,     NULL        },
        { SVC_ERR_ALREADY,     NULL        },
        { SVC_ERR_HAL,         NULL        },
        { SVC_ERR_IPC,         NULL        },
        { SVC_ERR_SIGNAL,      NULL        },
        { SVC_ERR_SECURITY,    NULL        },
        { -9999,               NULL        },  /* unknown — must not crash */
    };

    for (size_t i = 0; i < sizeof(tab)/sizeof(tab[0]); i++) {
        const char *s = svc_error_string(tab[i].code);
        assert(s != NULL);
        assert(s[0] != '\0');
        if (tab[i].must_contain)
            assert(strstr(s, tab[i].must_contain) != NULL);
    }
}

/* 5a. Install signals — must return SVC_OK */
TEST(install_signals_ok)
{
    int rc = service_base_install_signals();
    assert(rc == SVC_OK);
}

/* 5b. SIGHUP sets g_reload = 1 */
TEST(signal_sighup_sets_reload)
{
    service_base_install_signals();
    g_reload  = 0;
    g_running = 1;
    raise(SIGHUP);
    /* The handler is synchronous for SIGHUP — g_reload must be 1 now */
    assert(g_reload == 1);
    g_reload = 0;  /* clean up for subsequent tests */
}

/* 5c. SIGTERM sets g_running = 0 */
TEST(signal_sigterm_clears_running)
{
    service_base_install_signals();
    g_running = 1;
    raise(SIGTERM);
    assert(g_running == 0);
    g_running = 1;  /* restore for subsequent tests */
}

/* 6. service_base_open_log() / service_base_close_log() */
TEST(log_open_close)
{
    const char *log_path = "/tmp/unit_test_svc.log";
    unlink(log_path);

    service_base_open_log("unit_test", log_path);

    /* g_log_file must be non-NULL after open */
    assert(g_log_file != NULL);

    /* Write something and verify it lands in the file */
    LOG_INFO("unit test log entry %d", 42);
    service_base_close_log();

    /* g_log_file must be NULL after close */
    assert(g_log_file == NULL);

    /* Log file must exist and be non-empty */
    struct stat st;
    int rc = stat(log_path, &st);
    assert(rc == 0);
    assert(st.st_size > 0);

    unlink(log_path);
}

/* 6b. open_log with NULL path — syslog only, g_log_file stays NULL */
TEST(log_open_no_file)
{
    service_base_open_log("unit_test", NULL);
    /* With no file path, g_log_file must remain NULL */
    assert(g_log_file == NULL);
    service_base_close_log();
}

/* 7. Foreground daemonize — ctx->foreground == 1 skips fork */
TEST(daemonize_foreground_nofork)
{
    svc_context_t ctx;
    service_base_init(&ctx, "fg_test");
    ctx.foreground = 1;

    /* Must return SVC_OK without actually forking */
    int rc = service_base_daemonize(&ctx);
    assert(rc == SVC_OK);
    /* We are still in the original process */
    assert(getpid() > 1);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_service_base ===\n\n");

    RUN(init_basic);
    RUN(init_null_name);
    RUN(init_null_ctx);
    RUN(init_long_name);
    RUN(pidfile_write_read_remove);
    RUN(pidfile_remove_nonexistent);
    RUN(pidfile_duplicate_detection);
    RUN(pidfile_stale_overwrite);
    RUN(error_strings);
    RUN(install_signals_ok);
    RUN(signal_sighup_sets_reload);
    RUN(signal_sigterm_clears_running);
    RUN(log_open_close);
    RUN(log_open_no_file);
    RUN(daemonize_foreground_nofork);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);

    return (g_tests_failed > 0) ? 1 : 0;
}
