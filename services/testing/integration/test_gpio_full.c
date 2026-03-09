/**
 * @file test_gpio_full.c
 * @brief Full integration test for the GPIO service.
 *
 * Scenario:
 *   1. Start mock_sm on /tmp/test_gpio_sm.sock.
 *   2. Register via service_ipc; inject mock GPIO device.
 *   3. Exercise set_value / get_value / wait_interrupt via mock HAL.
 *   4. Send health; test reload-config push from mock SM.
 *   5. Unregister, cleanup, verify counters.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_gpio_full.c \
 *       ../../gpio_service/gpio_service_hal.c \
 *       ../../common/service_base.c \
 *       ../../common/service_ipc.c \
 *       ../../common/service_config.c \
 *       ../mocks/mock_hal.c ../mocks/mock_sm.c \
 *       -I../../gpio_service -I../../common -I../mocks \
 *       -lpthread -lssl -lcrypto \
 *       -o test_gpio_full
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../mocks/mock_hal.h"
#include "../mocks/mock_sm.h"
#include "../../gpio_service/gpio_service.h"
#include "../../gpio_service/gpio_service_hal.h"
#include "../../common/service_base.h"
#include "../../common/service_ipc.h"

/* ── constants ────────────────────────────────────────────────────────── */

#define MOCK_SOCK        "/tmp/test_gpio_sm.sock"
#define WAIT_TIMEOUT_MS  2000

/* ── micro test framework ─────────────────────────────────────────────── */

static int g_tests_run = 0, g_tests_passed = 0, g_tests_failed = 0;

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do {                                             \
    g_tests_run++;                                                   \
    printf("  [RUN ]  test_" #name "\n");                           \
    test_##name();                                                   \
    g_tests_passed++;                                                \
    printf("  [ OK ]  test_" #name "\n");                           \
} while (0)

/* ── shared mock SM ───────────────────────────────────────────────────── */

static mock_sm_t g_sm;

static void setup_sm(void)
{
    memset(&g_sm, 0, sizeof(g_sm));
    assert(mock_sm_start(&g_sm, MOCK_SOCK) == 0);
}

static void teardown_sm(void)
{
    mock_sm_stop(&g_sm);
    unlink(MOCK_SOCK);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

static svc_ipc_t *make_connected_ipc(void)
{
    svc_ipc_t *ipc = calloc(1, sizeof(*ipc));
    assert(ipc);
    assert(service_ipc_init(ipc, GPIO_SERVICE_NAME, NULL) == SVC_OK);
    strncpy(ipc->socket_path, MOCK_SOCK, sizeof(ipc->socket_path) - 1);
    assert(service_ipc_connect(ipc) == SVC_OK);
    return ipc;
}

static void make_gpio_ctx(gpio_service_ctx_t *ctx, hw_device_t *dev)
{
    memset(ctx, 0, sizeof(*ctx));
    service_base_init(&ctx->base, GPIO_SERVICE_NAME);
    ctx->base.foreground      = 1;
    ctx->pin_number           = GPIO_SERVICE_DEFAULT_PIN;
    ctx->direction            = GPIO_SERVICE_DEFAULT_DIR;
    ctx->initial_value        = 0;
    ctx->edge                 = GPIO_SERVICE_DEFAULT_EDGE;
    ctx->interrupt_timeout_ms = 0;
    ctx->hal_device           = dev;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/**
 * @brief Full lifecycle: register → heartbeat → unregister.
 */
TEST(full_lifecycle_register_unregister)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/gpio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);
    assert(mock_sm_is_registered(&g_sm, GPIO_SERVICE_NAME));

    assert(service_ipc_heartbeat(ipc) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(service_ipc_unregister(ipc) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/**
 * @brief set_value() while IPC is active — write_calls incremented.
 */
TEST(full_set_value_with_ipc)
{
    setup_sm();

    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_gpio_ctx(&ctx, dev);

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/gpio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(gpio_service_hal_set_value(&ctx, 1) == SVC_OK);
    assert(gpio_service_hal_set_value(&ctx, 0) == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.write_calls == 2);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);
    mock_hal_destroy(dev);
    teardown_sm();
}

/**
 * @brief get_value() while IPC is active — read_calls incremented.
 */
TEST(full_get_value_with_ipc)
{
    setup_sm();

    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    uint8_t high = 1;
    mock_hal_set_read_data(dev, &high, sizeof(high));

    gpio_service_ctx_t ctx;
    make_gpio_ctx(&ctx, dev);

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/gpio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    int value = -1;
    assert(gpio_service_hal_get_value(&ctx, &value) == SVC_OK);
    assert(value == 1);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);
    mock_hal_destroy(dev);
    teardown_sm();
}

/**
 * @brief wait_interrupt() dispatches control(); returns immediately on zero
 *        timeout.
 */
TEST(full_wait_interrupt_with_ipc)
{
    setup_sm();

    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_gpio_ctx(&ctx, dev);

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/gpio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    int rc = gpio_service_hal_wait_interrupt(&ctx, 0);
    assert(rc == SVC_OK || rc == SVC_ERR_TIMEOUT);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.control_calls >= 1);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);
    mock_hal_destroy(dev);
    teardown_sm();
}

/**
 * @brief Send health status; mock SM records it.
 */
TEST(full_send_health_ok)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/gpio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    svc_health_status_t st = {
        .uptime_sec = 60,
        .error_count    = 0,
    };
    assert(service_ipc_send_health(ipc, &st) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);
    assert(g_sm.health_ok_count >= 1);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/**
 * @brief Mock SM sends shutdown; service receives without crashing.
 */
TEST(full_sm_send_shutdown)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/gpio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(mock_sm_send_shutdown(&g_sm) == 0);
    usleep(100 * 1000);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_gpio_full (integration) ===\n\n");

    RUN(full_lifecycle_register_unregister);
    RUN(full_set_value_with_ipc);
    RUN(full_get_value_with_ipc);
    RUN(full_wait_interrupt_with_ipc);
    RUN(full_send_health_ok);
    RUN(full_sm_send_shutdown);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
