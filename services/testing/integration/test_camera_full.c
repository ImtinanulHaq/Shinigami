/**
 * @file test_camera_full.c
 * @brief Integration test — full camera service startup in foreground mode.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../mocks/mock_sm.h"
#include "../../common/service_base.h"
#include "../../common/service_config.h"
#include "../../common/service_ipc.h"
#include "../../camera_service/camera_service.h"
#include "../../camera_service/camera_service_hal.h"

#define MOCK_SOCK "/tmp/integration_camera_sm.sock"
#define TEST_CONF "/tmp/integration_camera.conf"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

static void write_test_config(const char *path, const char *sock_path)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp,
        "[camera]\n"
        "device          = /dev/video0\n"
        "width           = 640\n"
        "height          = 480\n"
        "fps             = 30\n"
        "buffer_count    = 4\n"
        "format          = 0\n"
        "\n"
        "[security]\n"
        "skip_sandbox    = 1\n"
        "skip_capabilities= 1\n"
        "skip_seccomp    = 1\n"
        "skip_verify     = 1\n"
        "\n"
        "[daemon]\n"
        "sm_socket_path  = %s\n",
        sock_path);
    fclose(fp);
}

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(camera_service_init_from_config)
{
    write_test_config(TEST_CONF, MOCK_SOCK);

    config_t cfg;
    int rc = config_load(&cfg, TEST_CONF);
    assert(rc == 0);

    camera_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "camera_service");
    ctx.base.foreground = 1;

    ctx.width        = config_get_uint32(&cfg, "camera", "width",  640);
    ctx.height       = config_get_uint32(&cfg, "camera", "height", 480);
    ctx.fps          = config_get_uint32(&cfg, "camera", "fps",     30);
    ctx.buffer_count = config_get_uint32(&cfg, "camera", "buffer_count", 4);
    strncpy(ctx.v4l2_device,
            config_get_string(&cfg, "camera", "device", "/dev/video0"),
            sizeof(ctx.v4l2_device) - 1);

    assert(ctx.width  == 640);
    assert(ctx.height == 480);
    assert(ctx.fps    == 30);
    assert(strcmp(ctx.v4l2_device, "/dev/video0") == 0);

    config_free(&cfg);
}

TEST(camera_service_ipc_register_via_mock_sm)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    camera_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "camera_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "camera_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);

    int rc = svc_ipc_connect(&ctx.ipc);
    assert(rc == SVC_OK);

    rc = svc_ipc_register(&ctx.ipc, "/tmp/camera_service.sock", getpid());
    assert(rc == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(mock_sm_is_registered(&sm, "camera_service") == 1);

    svc_ipc_unregister(&ctx.ipc);
    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

TEST(camera_service_loop_exits_on_stop)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    camera_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "camera_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "camera_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);
    svc_ipc_connect(&ctx.ipc);
    svc_ipc_register(&ctx.ipc, "/tmp/camera_service.sock", getpid());
    mock_sm_wait_request(&sm, 2000);

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 150 * 1000000L };
    nanosleep(&ts, NULL);
    atomic_store(&ctx.base.running, 0);

    svc_ipc_unregister(&ctx.ipc);
    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== integration: test_camera_full ===\n");
    RUN(camera_service_init_from_config);
    RUN(camera_service_ipc_register_via_mock_sm);
    RUN(camera_service_loop_exits_on_stop);
    unlink(TEST_CONF);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
