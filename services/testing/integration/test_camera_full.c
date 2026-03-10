/**
 * @file test_camera_full.c
 * @brief Full integration test for the camera service.
 *
 * Scenario:
 *   1. Start mock_sm on /tmp/test_camera_sm.sock.
 *   2. Connect via service_ipc; register, capture frame (mock), send health.
 *   3. Mock SM pushes reload-config; service handles it gracefully.
 *   4. Unregister and disconnect; verify mock SM state.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_camera_full.c \
 *       ../../camera_service/camera_service_hal.c \
 *       ../../common/service_base.c \
 *       ../../common/service_ipc.c \
 *       ../../common/service_config.c \
 *       ../mocks/mock_hal.c ../mocks/mock_sm.c \
 *       -I../../camera_service -I../../common -I../mocks \
 *       -lpthread -lssl -lcrypto \
 *       -o test_camera_full
 */

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../mocks/mock_hal.h"
#include "../mocks/mock_sm.h"
#include "../../camera_service/camera_service.h"
#include "../../camera_service/camera_service_hal.h"
#include "../../common/service_base.h"
#include "../../common/service_ipc.h"

/* ── constants ────────────────────────────────────────────────────────── */

#define MOCK_SOCK        "/tmp/test_camera_sm.sock"
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

/* ── shared mock SM instance ──────────────────────────────────────────── */

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
    assert(service_ipc_init(ipc, CAMERA_SERVICE_NAME, NULL) == SVC_OK);
    strncpy(ipc->socket_path, MOCK_SOCK, sizeof(ipc->socket_path) - 1);
    assert(service_ipc_connect(ipc) == SVC_OK);
    return ipc;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/**
 * @brief Full lifecycle: register → heartbeat → unregister.
 */
TEST(full_lifecycle_register_unregister)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/camera_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);
    assert(mock_sm_is_registered(&g_sm, CAMERA_SERVICE_NAME));

    assert(service_ipc_heartbeat(ipc) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(service_ipc_unregister(ipc) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/**
 * @brief Capture a frame using mock HAL, verify read_calls and frame size.
 */
TEST(full_hal_capture_frame)
{
    setup_sm();

    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    /* 640×480 × 2 bytes/pixel (YUYV) = 614 400 bytes synthetic frame */
    static uint8_t frame[1024]; /* small synthetic frame for the test */
    for (int i = 0; i < 1024; i++) frame[i] = (uint8_t)(i & 0xFF);
    mock_hal_set_read_data(dev, frame, sizeof(frame));
    dev->state = HAL_STATE_ACTIVE;

    camera_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_base_init(&ctx.base, CAMERA_SERVICE_NAME);
    ctx.base.foreground = 1;
    ctx.hal_device      = dev;
    ctx.width           = CAMERA_SERVICE_DEFAULT_WIDTH;
    ctx.height          = CAMERA_SERVICE_DEFAULT_HEIGHT;
    ctx.fps             = CAMERA_SERVICE_DEFAULT_FPS;

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/camera_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    /* Capture one frame */
    void    *data  = NULL;
    size_t   sz    = 0;
    uint32_t idx   = 0;
    int rc = camera_service_hal_capture(&ctx, &data, &sz, &idx);
    assert(rc == SVC_OK);
    assert(data != NULL && sz > 0);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    /* Return the buffer */
    assert(camera_service_hal_return(&ctx, idx) == SVC_OK);
    assert(p->counts.write_calls >= 1);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);
    mock_hal_destroy(dev);
    teardown_sm();
}

/**
 * @brief Mock SM pushes reload-config; service receives it without crashing.
 */
TEST(full_sm_send_reload)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/camera_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(mock_sm_send_reload(&g_sm) == 0);
    usleep(100 * 1000);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/**
 * @brief Send health status; mock SM records health_ok_count.
 */
TEST(full_send_health_ok)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/camera_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    svc_health_status_t st = {
        .uptime_sec = 10,
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
 * @brief Ping round-trip to mock SM.
 */
TEST(full_ping_roundtrip)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/camera_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(service_ipc_ping(ipc) == SVC_OK);

    service_ipc_disconnect(ipc);
    free(ipc);
    teardown_sm();
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_camera_full (integration) ===\n\n");

    RUN(full_lifecycle_register_unregister);
    RUN(full_hal_capture_frame);
    RUN(full_sm_send_reload);
    RUN(full_send_health_ok);
    RUN(full_ping_roundtrip);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
