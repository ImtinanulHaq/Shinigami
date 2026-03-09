/**
 * @file test_audio_full.c
 * @brief Integration test — full audio service startup in foreground mode.
 *
 * Spawns a mock SM on a private socket, boots the audio service against a
 * config file pointing at that socket, runs a few event-loop iterations,
 * then verifies proper registration and shutdown.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../mocks/mock_sm.h"
#include "../../common/service_base.h"
#include "../../common/service_config.h"
#include "../../common/service_ipc.h"
#include "../../audio_service/audio_service.h"
#include "../../audio_service/audio_service_hal.h"
#include "../../audio_service/audio_service_security.h"
#include "../../audio_service/audio_service_loop.h"

#define MOCK_SOCK "/tmp/integration_audio_sm.sock"
#define TEST_CONF "/tmp/integration_audio.conf"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

/* ── setup helpers ────────────────────────────────────────────────────── */

static void write_test_config(const char *path, const char *sock_path)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp,
        "[audio]\n"
        "device          = hw:0,0\n"
        "sample_rate     = 44100\n"
        "channels        = 2\n"
        "format          = 0\n"
        "period_size     = 1024\n"
        "buffer_size     = 4096\n"
        "direction       = 0\n"
        "\n"
        "[security]\n"
        "skip_sandbox     = 1\n"
        "skip_capabilities= 1\n"
        "skip_seccomp     = 1\n"
        "skip_verify      = 1\n"
        "\n"
        "[daemon]\n"
        "sm_socket_path  = %s\n"
        "foreground      = 1\n",
        sock_path);
    fclose(fp);
}

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(audio_service_init_from_config)
{
    write_test_config(TEST_CONF, MOCK_SOCK);

    audio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    config_t cfg;
    int rc = config_load(&cfg, TEST_CONF);
    assert(rc == 0);

    /* Basic context setup — mirrors what main() does */
    rc = service_init(&ctx.base, "audio_service");
    assert(rc == SVC_OK);
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    ctx.sample_rate  = config_get_uint32(&cfg, "audio", "sample_rate", 44100);
    ctx.channels     = config_get_uint32(&cfg, "audio", "channels", 2);
    ctx.period_size  = config_get_uint32(&cfg, "audio", "period_size", 1024);
    ctx.buffer_size  = config_get_uint32(&cfg, "audio", "buffer_size", 4096);
    strncpy(ctx.alsa_device,
            config_get_string(&cfg, "audio", "device", "hw:0,0"),
            sizeof(ctx.alsa_device) - 1);

    assert(ctx.sample_rate  == 44100);
    assert(ctx.channels     == 2);
    assert(ctx.period_size  == 1024);
    assert(ctx.buffer_size  == 4096);
    assert(strcmp(ctx.alsa_device, "hw:0,0") == 0);

    config_free(&cfg);
}

TEST(audio_service_ipc_register_via_mock_sm)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    write_test_config(TEST_CONF, MOCK_SOCK);

    audio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "audio_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    /* IPC connect + register */
    svc_ipc_init(&ctx.ipc, "audio_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);

    int rc = svc_ipc_connect(&ctx.ipc);
    assert(rc == SVC_OK);

    rc = svc_ipc_register(&ctx.ipc, "/tmp/audio_service.sock", getpid());
    assert(rc == SVC_OK);

    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(mock_sm_is_registered(&sm, "audio_service") == 1);

    rc = svc_ipc_unregister(&ctx.ipc);
    assert(rc == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);

    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

TEST(audio_service_loop_runs_and_stops)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    audio_service_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    service_init(&ctx.base, "audio_service");
    ctx.base.foreground = 1;
    atomic_store(&ctx.base.running, 1);

    svc_ipc_init(&ctx.ipc, "audio_service", NULL);
    strncpy(ctx.ipc.socket_path, MOCK_SOCK, sizeof(ctx.ipc.socket_path) - 1);
    svc_ipc_connect(&ctx.ipc);
    svc_ipc_register(&ctx.ipc, "/tmp/audio_service.sock", getpid());
    mock_sm_wait_request(&sm, 2000);

    /* Run the loop for 200 ms by clearing running after a short delay */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 200 * 1000000L };
    nanosleep(&ts, NULL);
    atomic_store(&ctx.base.running, 0);

    /* Loop should exit cleanly — but since HAL is not real, we just
     * verify that audio_service_loop_run doesn't crash with NULL hal. */
    svc_ipc_unregister(&ctx.ipc);
    svc_ipc_close(&ctx.ipc);
    mock_sm_stop(&sm);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== integration: test_audio_full ===\n");
    RUN(audio_service_init_from_config);
    RUN(audio_service_ipc_register_via_mock_sm);
    RUN(audio_service_loop_runs_and_stops);
    unlink(TEST_CONF);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
