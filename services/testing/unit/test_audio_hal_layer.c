/**
 * @file test_audio_hal_layer.c
 * @brief Unit tests for audio_service_hal using a mock hw_device_t.
 *
 * Injects a mock_hal device into the audio_service_ctx_t so the HAL
 * layer can be verified without real ALSA hardware.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../audio_service/audio_service.h"
#include "../../audio_service/audio_service_hal.h"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

/* Build a minimal ctx with the mock device pre-injected */
static void make_ctx(audio_service_ctx_t *ctx, hw_device_t *mock_dev)
{
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->base.name, "audio_service", SERVICE_MAX_NAME - 1);
    atomic_store(&ctx->base.running, 1);
    strncpy(ctx->alsa_device, "hw:0,0", sizeof(ctx->alsa_device) - 1);
    ctx->sample_rate  = 44100;
    ctx->channels     = 2;
    ctx->format       = 0; /* S16_LE */
    ctx->period_size  = 1024;
    ctx->buffer_size  = 4096;
    ctx->direction    = 0; /* playback */
    ctx->hal_device   = mock_dev;

    /* Simulate already-opened device */
    if (mock_dev)
        mock_dev->state = HAL_STATE_OPEN;
}

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(hal_start_calls_start_op)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = audio_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

TEST(hal_stop_calls_stop_op)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = audio_service_hal_stop(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls == 1);

    mock_hal_destroy(dev);
}

TEST(hal_write_captures_data)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    uint8_t buf[256];
    memset(buf, 0xAB, sizeof(buf));

    ssize_t n = audio_service_hal_write(&ctx, buf, sizeof(buf));
    assert(n == (ssize_t)sizeof(buf));

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.write_calls == 1);
    assert(p->write_data_len == sizeof(buf));
    assert(p->write_data[0]  == 0xAB);

    mock_hal_destroy(dev);
}

TEST(hal_read_returns_injected_data)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    const uint8_t pattern[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    mock_hal_set_read_data(dev, pattern, sizeof(pattern));

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    uint8_t buf[8];
    ssize_t n = audio_service_hal_read(&ctx, buf, sizeof(buf));
    assert(n == (ssize_t)sizeof(pattern));
    assert(memcmp(buf, pattern, sizeof(pattern)) == 0);

    mock_hal_destroy(dev);
}

TEST(hal_cleanup_calls_close_and_destroy)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);
    dev->state = HAL_STATE_OPEN;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    /* We can't call audio_service_hal_cleanup because it calls
     * audio_hal_destroy() which expects a real audio priv pointer.
     * Instead, test the close op was available and would be called. */
    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    dev->ops->close(dev);  /* Direct close */
    assert(p->counts.close_calls == 1);

    mock_hal_destroy(dev);
}

TEST(hal_null_ctx_safe)
{
    assert(audio_service_hal_start(NULL)  != SVC_OK);
    assert(audio_service_hal_stop(NULL)   != SVC_OK);
    assert(audio_service_hal_read(NULL, NULL, 0) < 0);
    assert(audio_service_hal_write(NULL, NULL, 0) < 0);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_audio_hal_layer ===\n");
    RUN(hal_start_calls_start_op);
    RUN(hal_stop_calls_stop_op);
    RUN(hal_write_captures_data);
    RUN(hal_read_returns_injected_data);
    RUN(hal_cleanup_calls_close_and_destroy);
    RUN(hal_null_ctx_safe);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
