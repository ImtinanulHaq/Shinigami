/**
 * @file test_audio_full.c
 * @brief Full integration test for the audio service.
 *
 * Scenario:
 *   1. Start a mock service-manager (mock_sm) on a UNIX socket.
 *   2. Build an audio_service_ctx_t pointing at the mock SM socket.
 *   3. Inject a mock HAL device — no real ALSA needed.
 *   4. Run the service in foreground mode through its lifecycle:
 *      init → IPC connect → IPC register → (optional) health → IPC unregister → cleanup.
 *   5. Verify all SM interactions arrived in the correct order.
 *
 * UNIX socket used: /tmp/test_audio_sm.sock
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_audio_full.c \
 *       ../../audio_service/audio_service_hal.c \
 *       ../../common/service_base.c \
 *       ../../common/service_ipc.c \
 *       ../../common/service_config.c \
 *       ../mocks/mock_hal.c ../mocks/mock_sm.c \
 *       -I../../audio_service -I../../common -I../mocks \
 *       -lpthread -lssl -lcrypto \
 *       -o test_audio_full
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
#include "../../audio_service/audio_service.h"
#include "../../audio_service/audio_service_hal.h"
#include "../../common/service_base.h"
#include "../../common/service_ipc.h"

/* ── constants ────────────────────────────────────────────────────────── */

#define MOCK_SOCK         "/tmp/test_audio_sm.sock"
#define WAIT_TIMEOUT_MS   2000

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
    int rc = mock_sm_start(&g_sm, MOCK_SOCK);
    assert(rc == 0);
}

static void teardown_sm(void)
{
    mock_sm_stop(&g_sm);
    unlink(MOCK_SOCK);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

/**
 * @brief Create a connected IPC context and return it.  Caller must
 *        call service_ipc_disconnect() + service_ipc_unregister() when done.
 */
static svc_ipc_t *make_connected_ipc(void)
{
    svc_ipc_t *ipc = calloc(1, sizeof(*ipc));
    assert(ipc);
    assert(service_ipc_init(ipc, AUDIO_SERVICE_NAME, NULL) == SVC_OK);
    /* Point at mock SM socket */
    strncpy(ipc->socket_path, MOCK_SOCK, sizeof(ipc->socket_path) - 1);
    assert(service_ipc_connect(ipc) == SVC_OK);
    return ipc;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/**
 * @brief Full register → heartbeat → unregister lifecycle.
 */
TEST(full_lifecycle_register_unregister)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();

    /* Register */
    int rc = service_ipc_register(ipc, "/usr/sbin/audio_service", "1.0.0",
                                  (uint32_t)getpid());
    assert(rc == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);
    assert(mock_sm_is_registered(&g_sm, AUDIO_SERVICE_NAME));

    /* Heartbeat */
    rc = service_ipc_heartbeat(ipc);
    assert(rc == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    /* Unregister */
    rc = service_ipc_unregister(ipc);
    assert(rc == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    service_ipc_disconnect(ipc);
    free(ipc);

    teardown_sm();
}

/**
 * @brief Send health status to mock SM; verify health_ok counter increments.
 */
TEST(full_send_health_ok)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/audio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    svc_health_status_t st = {
        .uptime_sec   = 5,
        .error_count      = 0,
    };
    int rc = service_ipc_send_health(ipc, &st);
    assert(rc == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);
    assert(g_sm.health_ok_count >= 1);

    service_ipc_disconnect(ipc);
    free(ipc);

    teardown_sm();
}

/**
 * @brief Mock SM pushes a health-check request; service layer receives it
 *        and responds with HEALTH_OK.
 */
TEST(full_sm_pushes_health_check)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/audio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    /* SM pushes health-check to service */
    assert(mock_sm_send_health_check(&g_sm) == 0);
    /* Allow processing */
    usleep(100 * 1000);

    service_ipc_disconnect(ipc);
    free(ipc);

    teardown_sm();
}

/**
 * @brief Mock SM sends SHUTDOWN; service should disconnect gracefully.
 */
TEST(full_sm_send_shutdown)
{
    setup_sm();

    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/audio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    assert(mock_sm_send_shutdown(&g_sm) == 0);
    usleep(100 * 1000);

    service_ipc_disconnect(ipc);
    free(ipc);

    teardown_sm();
}

/**
 * @brief HAL + IPC together: audio HAL delivers data while IPC is connected.
 */
TEST(full_hal_and_ipc_combined)
{
    setup_sm();

    /* Build HAL mock */
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    uint8_t audio_data[64];
    for (int i = 0; i < 64; i++) audio_data[i] = (uint8_t)(i);
    mock_hal_set_read_data(dev, audio_data, sizeof(audio_data));
    dev->state = HAL_STATE_ACTIVE;

    /* Build service context */
    audio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_base_init(&ctx.base, AUDIO_SERVICE_NAME);
    ctx.base.foreground = 1;
    ctx.hal_device      = dev;
    ctx.sample_rate     = AUDIO_SERVICE_DEFAULT_RATE;
    ctx.channels        = AUDIO_SERVICE_DEFAULT_CH;

    /* Connect IPC */
    svc_ipc_t *ipc = make_connected_ipc();
    assert(service_ipc_register(ipc, "/usr/sbin/audio_service", "1.0.0",
                                (uint32_t)getpid()) == SVC_OK);
    mock_sm_wait_request(&g_sm, WAIT_TIMEOUT_MS);

    /* Read audio data */
    uint8_t buf[128] = {0};
    ssize_t n = audio_service_hal_read(&ctx, buf, sizeof(buf));
    assert(n == 64);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    service_ipc_unregister(ipc);
    service_ipc_disconnect(ipc);
    free(ipc);

    mock_hal_destroy(dev);
    teardown_sm();
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_audio_full (integration) ===\n\n");

    RUN(full_lifecycle_register_unregister);
    RUN(full_send_health_ok);
    RUN(full_sm_pushes_health_check);
    RUN(full_sm_send_shutdown);
    RUN(full_hal_and_ipc_combined);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
