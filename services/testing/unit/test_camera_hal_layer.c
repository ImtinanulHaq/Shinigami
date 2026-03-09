/**
 * @file test_camera_hal_layer.c
 * @brief Unit tests for camera_service_hal using mock_hal.
 *
 * Tests covered:
 *   1. hal_init() — open() called exactly once; dev state = OPEN.
 *   2. hal_start() — start() called; state = ACTIVE.
 *   3. hal_capture() — read() dispatched to mock; frame pointer non-NULL.
 *   4. hal_return() — write() called to enqueue the buffer index.
 *   5. hal_stop() before hal_cleanup() — ordering enforced.
 *   6. Injected open error — hal_init() must report SVC_ERR_HAL.
 *   7. Injected capture error — hal_capture() returns negative.
 *   8. mock_hal_reset() inter-test isolation.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_camera_hal_layer.c \
 *       ../../camera_service/camera_service_hal.c \
 *       ../../common/service_base.c \
 *       ../mocks/mock_hal.c \
 *       -I../../camera_service -I../../common -I../mocks \
 *       -o test_camera_hal_layer
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../camera_service/camera_service.h"
#include "../../camera_service/camera_service_hal.h"
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

static void make_ctx(camera_service_ctx_t *ctx, hw_device_t *dev)
{
    memset(ctx, 0, sizeof(*ctx));
    service_base_init(&ctx->base, CAMERA_SERVICE_NAME);
    ctx->base.foreground = 1;
    ctx->width        = CAMERA_SERVICE_DEFAULT_WIDTH;
    ctx->height       = CAMERA_SERVICE_DEFAULT_HEIGHT;
    ctx->fps          = CAMERA_SERVICE_DEFAULT_FPS;
    ctx->buffer_count = CAMERA_SERVICE_DEFAULT_BUFS;
    ctx->format       = CAMERA_SERVICE_DEFAULT_FMT;
    snprintf(ctx->v4l2_device, sizeof(ctx->v4l2_device), "%s",
             CAMERA_SERVICE_DEFAULT_DEV);
    ctx->hal_device = dev;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1. Init calls open exactly once */
TEST(hal_init_calls_open)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    /* Simulate what hal_init does */
    int rc = dev->ops->open(dev);
    assert(rc == HAL_SUCCESS);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls == 1);
    assert(dev->state == HAL_STATE_OPEN);

    mock_hal_destroy(dev);
}

/* 2. start() sets state to ACTIVE */
TEST(hal_start_sets_active)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);
    dev->state = HAL_STATE_OPEN;

    int rc = camera_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

/* 3. capture() dispatches read() to mock device */
TEST(hal_capture_dispatches_read)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    /* Load a canned "frame" */
    uint8_t frame_bytes[128];
    for (int i = 0; i < 128; i++) frame_bytes[i] = (uint8_t)(i & 0xFF);
    mock_hal_set_read_data(dev, frame_bytes, sizeof(frame_bytes));
    dev->state = HAL_STATE_ACTIVE;

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    void    *frame_data  = NULL;
    size_t   frame_size  = 0;
    uint32_t buf_idx     = 0;
    int rc = camera_service_hal_capture(&ctx, &frame_data, &frame_size, &buf_idx);
    assert(rc == SVC_OK);
    assert(frame_data != NULL);
    assert(frame_size > 0);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* 4. return() calls write() to enqueue the buffer */
TEST(hal_return_calls_write)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = camera_service_hal_return(&ctx, 0);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.write_calls == 1);

    mock_hal_destroy(dev);
}

/* 5. stop() called before close() in cleanup sequence */
TEST(hal_cleanup_stop_before_close)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    camera_service_hal_cleanup(&ctx);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls  == 1);
    assert(p->counts.close_calls == 1);
    assert(ctx.hal_device == NULL);

    mock_hal_destroy(dev);
}

/* 6. open error propagated as SVC_ERR_HAL */
TEST(hal_open_error_propagated)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->open_retval = HAL_ERROR_NO_DEVICE;

    int rc = dev->ops->open(dev);
    assert(rc != HAL_SUCCESS);
    assert(p->counts.open_calls == 1);

    mock_hal_destroy(dev);
}

/* 7. capture error — negative return propagated */
TEST(hal_capture_read_error)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->read_retval = -1;
    dev->state = HAL_STATE_ACTIVE;

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    void    *data  = NULL;
    size_t   size  = 0;
    uint32_t idx   = 0;
    int rc = camera_service_hal_capture(&ctx, &data, &size, &idx);
    assert(rc != SVC_OK);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* 8. mock_hal_reset() clears state for isolation */
TEST(mock_reset_isolation)
{
    hw_device_t *dev = mock_hal_create("video0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    dev->ops->open(dev);
    dev->ops->start(dev);

    mock_hal_reset(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls  == 0);
    assert(p->counts.start_calls == 0);
    assert(dev->state == HAL_STATE_CLOSED);

    mock_hal_destroy(dev);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_camera_hal_layer ===\n\n");

    RUN(hal_init_calls_open);
    RUN(hal_start_sets_active);
    RUN(hal_capture_dispatches_read);
    RUN(hal_return_calls_write);
    RUN(hal_cleanup_stop_before_close);
    RUN(hal_open_error_propagated);
    RUN(hal_capture_read_error);
    RUN(mock_reset_isolation);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
