/**
 * @file test_camera_hal_layer.c
 * @brief Unit tests for camera_service_hal using a mock hw_device_t.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../camera_service/camera_service.h"
#include "../../camera_service/camera_service_hal.h"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

static void make_ctx(camera_service_ctx_t *ctx, hw_device_t *mock_dev)
{
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->base.name, "camera_service", SERVICE_MAX_NAME - 1);
    atomic_store(&ctx->base.running, 1);
    strncpy(ctx->v4l2_device, "/dev/video0", sizeof(ctx->v4l2_device) - 1);
    ctx->width        = 640;
    ctx->height       = 480;
    ctx->fps          = 30;
    ctx->buffer_count = 4;
    ctx->format       = 1; /* YUYV */
    ctx->hal_device   = mock_dev;

    if (mock_dev)
        mock_dev->state = HAL_STATE_OPEN;
}

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(hal_start_increments_start_count)
{
    hw_device_t *dev = mock_hal_create("camera0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = camera_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

TEST(hal_stop_calls_stop_op)
{
    hw_device_t *dev = mock_hal_create("camera0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = camera_service_hal_stop(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls == 1);

    mock_hal_destroy(dev);
}

TEST(hal_start_fail_propagates)
{
    hw_device_t *dev = mock_hal_create("camera0", HAL_DEVICE_TYPE_CAMERA);
    assert(dev);
    mock_hal_get_priv(dev)->start_retval = HAL_ERROR_IO;

    camera_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = camera_service_hal_start(&ctx);
    assert(rc == SVC_ERR_HAL);

    mock_hal_destroy(dev);
}

TEST(hal_null_ctx_safe)
{
    assert(camera_service_hal_start(NULL) != SVC_OK);
    assert(camera_service_hal_stop(NULL)  != SVC_OK);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_camera_hal_layer ===\n");
    RUN(hal_start_increments_start_count);
    RUN(hal_stop_calls_stop_op);
    RUN(hal_start_fail_propagates);
    RUN(hal_null_ctx_safe);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
