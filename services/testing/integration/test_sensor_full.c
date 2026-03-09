/**
 * @file test_sensor_full.c
 * @brief Full integration test for the sensor service.
 *
 * Scenario:
 *   1. Start mock_sm on /tmp/test_sensor_sm.sock.
 *   2. Register via service_ipc; inject mock sensor data; read samples.
 *   3. Simulate both 3-axis and scalar reads via mock HAL.
 *   4. Send health; receive reload-config push from mock SM.
 *   5. Unregister, verify mock SM counters.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_sensor_full.c \
 *       ../../sensor_service/sensor_service_hal.c \
 *       ../../common/service_base.c \
 *       ../../common/service_ipc.c \
 *       ../../common/service_config.c \
 *       ../mocks/mock_hal.c ../mocks/mock_sm.c \
 *       -I../../sensor_service -I../../common -I../mocks \
 *       -lpthread -lssl -lcrypto \
 *       -o test_sensor_full
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../mocks/mock_hal.h"
#include "../mocks/mock_sm.h"
#include "../../sensor_service/sensor_service.h"
#include "../../sensor_service/sensor_service_hal.h"
#include "../../common/service_base.h"
#include "../../common/service_ipc.h"

/* ── constants ────────────────────────────────────────────────────────── */

#define MOCK_SOCK        "/tmp/test_sensor_sm.sock"
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
    assert(service_ipc_init(ipc, SENSOR_SERVICE_NAME, NULL) == SVC_OK);
    strncpy(ipc->socket_path, MOCK_SOCK, sizeof(ipc->socket_path) - 1);
    assert(service_ipc_connect(ipc) == SVC_OK);
    return ipc;
}

/** @brief Create a sensor ctx with a mock HAL pre-injected. */
static void make_sensor_ctx(sensor_service_ctx_t *ctx,
                             hw_device_t *dev, int stype)
{
    memset(ctx, 0, sizeof(*ctx));
    service_base_init(&ctx->base, SENSOR_SERVICE_NAME);
    ctx->base.foreground  = 1;
    ctx->sampling_rate_hz = SENSOR_SERVICE_DEFAULT_RATE;
    ctx->sensor_type      = stype;
    ctx->enable_buffer    = 0;
    snprintf(ctx->iio_device, sizeof(ctx->iio_device), "%s",
             SENSOR_SERVICE_DEFAULT_IIO_DEV);
    ctx->hal_device = dev;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/**
 * @brief Full lifecycle: register → heartbeat → unregister.
 */
TEST(full_lifecycle_register_unregister)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/sensor_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);
    assert(mock_sm_is_registered(&g_sm, SENSOR_SERVICE_NAME));

    assert(service_ipc_heartbeat(ipc) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(service_ipc_unregister(ipc) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/**
 * @brief 3-axis sensor read while IPC is active.
 */
TEST(full_read_3axis_with_ipc)
{
    setup_sm();

    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    typedef struct { float x; float y; float z; } raw3_t;
    raw3_t canned = { .x = 0.1f, .y = -0.2f, .z = 9.8f };
    mock_hal_set_read_data(dev, &canned, sizeof(canned));

    sensor_service_ctx_t ctx;
    make_sensor_ctx(&ctx, dev, 1);

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/sensor_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    svc_sensor_3axis_t out;
    int rc = sensor_service_hal_read_3axis(&ctx, &out);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);
    mock_hal_destroy(dev);
    teardown_sm();
}

/**
 * @brief Scalar sensor read while IPC is active.
 */
TEST(full_read_scalar_with_ipc)
{
    setup_sm();

    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    typedef struct { float val; uint64_t ts; } raw1_t;
    raw1_t canned = { .val = 36.6f, .ts = 987654321ULL };
    mock_hal_set_read_data(dev, &canned, sizeof(canned));

    sensor_service_ctx_t ctx;
    make_sensor_ctx(&ctx, dev, 4);

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/sensor_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    float    value = 0.0f;
    uint64_t ts    = 0;
    int rc = sensor_service_hal_read_scalar(&ctx, &value, &ts);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);
    mock_hal_destroy(dev);
    teardown_sm();
}

/**
 * @brief Send health status; verify mock SM records it.
 */
TEST(full_send_health_ok)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/sensor_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    svc_health_status_t st = {
        .uptime_sec = 30,
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
 * @brief Mock SM sends reload-config; service handles it without crashing.
 */
TEST(full_sm_send_reload)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/sensor_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(mock_sm_send_reload(&g_sm) == 0);
    usleep(100 * 1000);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_sensor_full (integration) ===\n\n");

    RUN(full_lifecycle_register_unregister);
    RUN(full_read_3axis_with_ipc);
    RUN(full_read_scalar_with_ipc);
    RUN(full_send_health_ok);
    RUN(full_sm_send_reload);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
