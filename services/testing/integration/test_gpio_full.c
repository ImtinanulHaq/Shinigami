/**
 * @file test_gpio_full.c
 * @brief Integration test — full GPIO service startup in foreground mode.
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
#include "../../gpio_service/gpio_service.h"

#define MOCK_SOCK "/tmp/integration_gpio_sm.sock"
#define TEST_CONF "/tmp/integration_gpio.conf"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

static void write_test_config(const char *path, const char *sock_path)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp,
        "[gpio]\n"
        "pin_number      = 4\n"
        "direction       = 1\n"
        "initial_value   = 0\n"
        "edge            = 0\n"
        "interrupt_timeout_ms = 5000\n"
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

TEST(gpio_service_init_from_config)
{
    write_test_config(TEST_CONF, MOCK_SOCK);

    config_t cfg;
    int rc = config_load(&cfg, TEST_CONF);
    assert(rc == 0);

    gpio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "gpio_service");
    ctx.base.foreground = 1;

    ctx.pin_number   = config_get_uint32(&cfg, "gpio", "pin_number",   4);
    ctx.direction    = config_get_uint32(&cfg, "gpio", "direction",    1);
    ctx.initial_value = config_get_uint32(&cfg, "gpio", "initial_value", 0);
    ctx.edge         = config_get_uint32(&cfg, "gpio", "edge",         0);
    ctx.interrupt_timeout_ms =
        config_get_uint32(&cfg, "gpio", "interrupt_timeout_ms", 5000);

    assert(ctx.pin_number            == 4);
    assert(ctx.direction             == 1);
    assert(ctx.interrupt_timeout_ms  == 5000);

    config_free(&cfg);
}

TEST(gpio_service_ipc_register_via_mock_sm)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    gpio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "gpio_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "gpio_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);

    assert(svc_ipc_connect(&ctx.ipc)  == SVC_OK);
    assert(svc_ipc_register(&ctx.ipc, "/tmp/gpio_service.sock", getpid()) == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(mock_sm_is_registered(&sm, "gpio_service") == 1);

    svc_ipc_unregister(&ctx.ipc);
    mock_sm_wait_request(&sm, 2000);
    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

TEST(gpio_service_registration_count)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    gpio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "gpio_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "gpio_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);

    svc_ipc_connect(&ctx.ipc);
    svc_ipc_register(&ctx.ipc, "/tmp/gpio_service.sock", getpid());
    mock_sm_wait_request(&sm, 2000);

    assert(sm.register_count == 1);

    /* Send a heartbeat and confirm counter advances */
    svc_ipc_heartbeat(&ctx.ipc);
    mock_sm_wait_request(&sm, 2000);
    assert(sm.heartbeat_count == 1);

    svc_ipc_unregister(&ctx.ipc);
    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== integration: test_gpio_full ===\n");
    RUN(gpio_service_init_from_config);
    RUN(gpio_service_ipc_register_via_mock_sm);
    RUN(gpio_service_registration_count);
    unlink(TEST_CONF);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
