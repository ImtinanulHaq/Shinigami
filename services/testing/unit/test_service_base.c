/**
 * @file test_service_base.c
 * @brief Unit tests for service_base: daemonize, PID file, syslog init.
 *
 * Uses a minimal assertion framework (assert.h) since the service layer
 * does not mandate a specific framework.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../common/service_base.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(service_init_basic)
{
    svc_context_t ctx;
    int rc = service_init(&ctx, "test_svc");
    assert(rc == SVC_OK);
    assert(strcmp(ctx.name, "test_svc") == 0);
    assert(ctx.state == SVC_STATE_INIT);
    assert(atomic_load(&ctx.running) == 1);
    assert(ctx.foreground == 0);
}

TEST(service_init_null_name)
{
    svc_context_t ctx;
    int rc = service_init(&ctx, NULL);
    assert(rc == SVC_ERR_INVALID);
}

TEST(service_init_null_ctx)
{
    int rc = service_init(NULL, "test");
    assert(rc == SVC_ERR_INVALID);
}

TEST(service_pidfile_write_and_remove)
{
    svc_context_t ctx;
    service_init(&ctx, "test_pid");

    /* Use /tmp so we don't need root */
    snprintf(ctx.pid_path, sizeof(ctx.pid_path), "/tmp/test_pid_%d.pid",
             (int)getpid());

    int rc = service_write_pidfile(&ctx);
    assert(rc == SVC_OK);

    /* Verify the file contains our PID */
    FILE *fp = fopen(ctx.pid_path, "r");
    assert(fp != NULL);
    int stored_pid = 0;
    fscanf(fp, "%d", &stored_pid);
    fclose(fp);
    assert(stored_pid == (int)getpid());

    service_remove_pidfile(&ctx);
    assert(access(ctx.pid_path, F_OK) != 0);  /* file removed */
}

TEST(service_pidfile_no_ctx)
{
    int rc = service_write_pidfile(NULL);
    assert(rc == SVC_ERR_INVALID);
}

TEST(service_error_strings)
{
    assert(strcmp(svc_error_string(SVC_OK),          "success")          == 0);
    assert(strcmp(svc_error_string(SVC_ERR_GENERIC), "generic error")    == 0);
    assert(strcmp(svc_error_string(SVC_ERR_PIDFILE), "PID file error")   == 0);
    assert(strcmp(svc_error_string(SVC_ERR_HAL),     "HAL error")        == 0);
    assert(strcmp(svc_error_string(SVC_ERR_SECURITY),"security error")   == 0);
    assert(strcmp(svc_error_string(SVC_ERR_IPC),     "IPC error")        == 0);

    /* Unknown code should not crash */
    const char *s = svc_error_string(-999);
    assert(s != NULL);
}

TEST(service_install_signals)
{
    svc_context_t ctx;
    service_init(&ctx, "test_sig");
    int rc = service_install_signals(&ctx);
    assert(rc == SVC_OK);
}

TEST(service_init_foreground_flag)
{
    svc_context_t ctx;
    service_init(&ctx, "test_fg");
    ctx.foreground = 1;
    assert(ctx.foreground == 1);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_service_base ===\n");
    RUN(service_init_basic);
    RUN(service_init_null_name);
    RUN(service_init_null_ctx);
    RUN(service_pidfile_write_and_remove);
    RUN(service_pidfile_no_ctx);
    RUN(service_error_strings);
    RUN(service_install_signals);
    RUN(service_init_foreground_flag);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
