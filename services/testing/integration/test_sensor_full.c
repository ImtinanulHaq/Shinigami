/**
 * @file test_sensor_full.c
 * @brief Integration test — full sensor service startup in foreground mode.
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
#include "../../sensor_service/sensor_service.h"

#define MOCK_SOCK "/tmp/integration_sensor_sm.sock"
#define TEST_CONF "/tmp/integration_sensor.conf"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

static void write_test_config(const char *path, const char *sock_path)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp,
        "[sensor]\n"
        "device          = iio:device0\n"
        "sampling_rate   = 100\n"
        "sensor_type     = 1\n"
        "enable_buffer   = 0\n"
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

TEST(sensor_service_init_from_config)
{
    write_test_config(TEST_CONF, MOCK_SOCK);

    config_t cfg;
    int rc = config_load(&cfg, TEST_CONF);
    assert(rc == 0);

    sensor_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "sensor_service");
    ctx.base.foreground = 1;

    ctx.sampling_rate_hz = config_get_uint32(&cfg, "sensor", "sampling_rate", 100);
    ctx.sensor_type      = config_get_uint32(&cfg, "sensor", "sensor_type", 1);
    strncpy(ctx.iio_device,
            config_get_string(&cfg, "sensor", "device", "iio:device0"),
            sizeof(ctx.iio_device) - 1);

    assert(ctx.sampling_rate_hz == 100);
    assert(ctx.sensor_type      == 1);
    assert(strcmp(ctx.iio_device, "iio:device0") == 0);

    config_free(&cfg);
}

TEST(sensor_service_ipc_register_via_mock_sm)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    sensor_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "sensor_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "sensor_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);

    assert(svc_ipc_connect(&ctx.ipc)  == SVC_OK);
    assert(svc_ipc_register(&ctx.ipc, "/tmp/sensor_service.sock", getpid()) == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(mock_sm_is_registered(&sm, "sensor_service") == 1);

    svc_ipc_unregister(&ctx.ipc);
    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

TEST(sensor_service_unregister_clears_entry)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    sensor_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "sensor_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "sensor_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);

    svc_ipc_connect(&ctx.ipc);
    svc_ipc_register(&ctx.ipc, "/tmp/sensor_service.sock", getpid());
    mock_sm_wait_request(&sm, 2000);

    svc_ipc_unregister(&ctx.ipc);
    mock_sm_wait_request(&sm, 2000);
    assert(sm.unregister_count == 1);

    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== integration: test_sensor_full ===\n");
    RUN(sensor_service_init_from_config);
    RUN(sensor_service_ipc_register_via_mock_sm);
    RUN(sensor_service_unregister_clears_entry);
    unlink(TEST_CONF);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
