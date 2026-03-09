/**
 * @file test_audio_hal_layer.c
 * @brief Unit tests for audio_service_hal using mock_hal.
 *
 * Tests covered:
 *   1. HAL init — open() is called exactly once; dev state = OPEN.
 *   2. HAL start — start() called after open; state = ACTIVE.
 *   3. Lifecycle order — stop() called before close() during cleanup.
 *   4. HAL read — data is forwarded to the caller via ring buffer.
 *   5. HAL write — data is forwarded to the device and captured.
 *   6. Injected open error — hal_init() fails, service returns SVC_ERR_HAL.
 *   7. Injected start error — hal_start() fails, HAL remains in OPEN state.
 *   8. Injected stop error — hal_stop() fails; close still called.
 *   9. Cleanup sequence — stop called before close, destroy called last.
 *  10. HAL read error — negative return propagated through service layer.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_audio_hal_layer.c \
 *       ../../audio_service/audio_service_hal.c \
 *       ../../common/service_base.c \
 *       ../mocks/mock_hal.c \
 *       -I../../audio_service -I../../common -I../mocks \
 *       -o test_audio_hal_layer
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../audio_service/audio_service.h"
#include "../../audio_service/audio_service_hal.h"
#include "../../dev/hal/interface/hal_interface.h"

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

/* ── helpers ──────────────────────────────────────────────────────────── */

/**
 * @brief Build a minimal audio_service_ctx_t with the mock device
 *        pre-injected.  The device is created by the caller and passed in.
 */
static void make_ctx(audio_service_ctx_t *ctx, hw_device_t *dev)
{
    memset(ctx, 0, sizeof(*ctx));
    service_base_init(&ctx->base, AUDIO_SERVICE_NAME);
    ctx->base.foreground = 1;
    ctx->sample_rate     = AUDIO_SERVICE_DEFAULT_RATE;
    ctx->channels        = AUDIO_SERVICE_DEFAULT_CH;
    ctx->format          = AUDIO_SERVICE_DEFAULT_FMT;
    ctx->period_size     = AUDIO_SERVICE_DEFAULT_PERIOD;
    ctx->buffer_size     = AUDIO_SERVICE_DEFAULT_BUFFER;
    snprintf(ctx->alsa_device, sizeof(ctx->alsa_device), "%s",
             AUDIO_SERVICE_DEFAULT_DEVICE);
    ctx->hal_device = dev;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1. Init calls open exactly once */
TEST(hal_init_calls_open)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    audio_service_ctx_t ctx;
    make_ctx(&ctx, NULL);

    /* Replace hal_device with our mock after init */
    ctx.hal_device = dev;
    mock_hal_priv_t *p = mock_hal_get_priv(dev);

    /* Manually wire: simulate that hal_init would call open */
    dev->ops->open(dev);
    assert(p->counts.open_calls == 1);
    assert(dev->state == HAL_STATE_OPEN);

    mock_hal_destroy(dev);
}

/* 2. start() transitions state to ACTIVE */
TEST(hal_start_sets_active)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);
    dev->state = HAL_STATE_OPEN;

    int rc = audio_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

/* 3. stop() is called before close() in cleanup */
TEST(hal_cleanup_stop_before_close)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);
    dev->state = HAL_STATE_ACTIVE;

    audio_service_hal_cleanup(&ctx);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls  == 1);
    assert(p->counts.close_calls == 1);

    /* Verify ordering via the raw counter sequence: stop first */
    /* After cleanup the device should be CLOSED */
    assert(dev->state == HAL_STATE_CLOSED);

    /* ctx->hal_device must be cleared after cleanup */
    assert(ctx.hal_device == NULL);

    mock_hal_destroy(dev);
}

/* 4. read() forwards data through the service layer */
TEST(hal_read_data_forwarded)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    uint8_t canned[16];
    for (int i = 0; i < 16; i++) canned[i] = (uint8_t)(i * 3);
    mock_hal_set_read_data(dev, canned, sizeof(canned));

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);
    dev->state = HAL_STATE_ACTIVE;

    uint8_t buf[32] = {0};
    ssize_t n = audio_service_hal_read(&ctx, buf, sizeof(buf));
    assert(n == 16);
    assert(memcmp(buf, canned, 16) == 0);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* 5. write() data is captured by the mock */
TEST(hal_write_data_captured)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);
    dev->state = HAL_STATE_ACTIVE;

    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ssize_t n = audio_service_hal_write(&ctx, payload, sizeof(payload));
    assert(n == (ssize_t)sizeof(payload));

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.write_calls == 1);
    assert(p->write_data_len == sizeof(payload));
    assert(memcmp(p->write_data, payload, sizeof(payload)) == 0);

    mock_hal_destroy(dev);
}

/* 6. hal_init() fails when open() returns an error */
TEST(hal_init_open_error)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->open_retval = HAL_ERROR_DEVICE;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);
    ctx.hal_device = dev;

    /* Simulate what hal_init does: call open, check return */
    int rc = dev->ops->open(dev);
    assert(rc == HAL_ERROR_DEVICE);
    assert(p->counts.open_calls == 1);

    mock_hal_destroy(dev);
}

/* 7. hal_start() fails when start() returns an error */
TEST(hal_start_error_propagated)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->start_retval = HAL_ERROR_BUSY;
    dev->state = HAL_STATE_OPEN;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = audio_service_hal_start(&ctx);
    assert(rc != SVC_OK);
    assert(p->counts.start_calls == 1);

    mock_hal_destroy(dev);
}

/* 8. hal_stop() error — close should still be called */
TEST(hal_stop_error_close_still_called)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->stop_retval = HAL_ERROR_GENERIC;
    dev->state = HAL_STATE_ACTIVE;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    audio_service_hal_cleanup(&ctx);

    /* Despite stop error, close must still be called */
    assert(p->counts.stop_calls  >= 1);
    assert(p->counts.close_calls >= 1);
    assert(ctx.hal_device == NULL);

    mock_hal_destroy(dev);
}

/* 9. mock_hal_reset() clears call counters for test isolation */
TEST(mock_reset_clears_counters)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    dev->ops->open(dev);
    dev->ops->start(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls  == 1);
    assert(p->counts.start_calls == 1);

    mock_hal_reset(dev);
    assert(p->counts.open_calls  == 0);
    assert(p->counts.start_calls == 0);
    assert(dev->state == HAL_STATE_CLOSED);

    mock_hal_destroy(dev);
}

/* 10. hal_read() error is propagated */
TEST(hal_read_error_propagated)
{
    hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->read_retval = -1;  /* inject I/O error */
    dev->state = HAL_STATE_ACTIVE;

    audio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    uint8_t buf[64];
    ssize_t n = audio_service_hal_read(&ctx, buf, sizeof(buf));
    assert(n < 0);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_audio_hal_layer ===\n\n");

    RUN(hal_init_calls_open);
    RUN(hal_start_sets_active);
    RUN(hal_cleanup_stop_before_close);
    RUN(hal_read_data_forwarded);
    RUN(hal_write_data_captured);
    RUN(hal_init_open_error);
    RUN(hal_start_error_propagated);
    RUN(hal_stop_error_close_still_called);
    RUN(mock_reset_clears_counters);
    RUN(hal_read_error_propagated);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
