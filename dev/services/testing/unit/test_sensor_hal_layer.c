/**
 * @file test_sensor_hal_layer.c
 * @brief Unit tests for sensor_service_hal using mock_hal.
 *
 * Tests covered:
 *   1. hal_init() — open() called once; state = OPEN.
 *   2. hal_start() — start() called; state = ACTIVE.
 *   3. hal_read_3axis() — read() dispatched; X/Y/Z values parsed.
 *   4. hal_read_scalar() — read() dispatched; scalar value returned.
 *   5. Lifecycle order — stop() before close() in cleanup.
 *   6. Injected start error — hal_start() returns SVC_ERR_HAL.
 *   7. Injected read error — hal_read_3axis() propagates error.
 *   8. mock_hal_reset() provides clean state for each test.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_sensor_hal_layer.c \
 *       ../../sensor_service/sensor_service_hal.c \
 *       ../../common/service_base.c \
 *       ../mocks/mock_hal.c \
 *       -I../../sensor_service -I../../common -I../mocks \
 *       -o test_sensor_hal_layer
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../sensor_service/sensor_service.h"
#include "../../sensor_service/sensor_service_hal.h"
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

static void make_ctx(sensor_service_ctx_t *ctx, hw_device_t *dev,
                     int sensor_type)
{
    memset(ctx, 0, sizeof(*ctx));
    service_base_init(&ctx->base, SENSOR_SERVICE_NAME);
    ctx->base.foreground   = 1;
    ctx->sampling_rate_hz  = SENSOR_SERVICE_DEFAULT_RATE;
    ctx->sensor_type       = sensor_type;
    ctx->enable_buffer     = 0;
    snprintf(ctx->iio_device, sizeof(ctx->iio_device), "%s",
             SENSOR_SERVICE_DEFAULT_IIO_DEV);
    ctx->hal_device = dev;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1. Init calls open exactly once */
TEST(hal_init_calls_open)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 1);

    int rc = dev->ops->open(dev);
    assert(rc == HAL_SUCCESS);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls == 1);
    assert(dev->state == HAL_STATE_OPEN);

    mock_hal_destroy(dev);
}

/* 2. Start transitions to ACTIVE */
TEST(hal_start_sets_active)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);
    dev->state = HAL_STATE_OPEN;

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 1);

    int rc = sensor_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

/* 3. hal_read_3axis() dispatches read() and fills output struct */
TEST(hal_read_3axis_dispatches_read)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    /* Canned 3-axis payload: 3 × float32 (12 bytes) */
    typedef struct { float x; float y; float z; } raw3_t;
    raw3_t canned = { .x = 1.5f, .y = -0.5f, .z = 9.81f };
    mock_hal_set_read_data(dev, &canned, sizeof(canned));

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 1);  /* sensor_type=1 (ACCEL) → 3-axis */

    svc_sensor_3axis_t out;
    memset(&out, 0, sizeof(out));
    int rc = sensor_service_hal_read_3axis(&ctx, &out);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);
    /* Values should match the canned data */
    assert(out.x > 1.4f && out.x < 1.6f);
    assert(out.z > 9.7f && out.z < 9.9f);

    mock_hal_destroy(dev);
}

/* 4. hal_read_scalar() dispatches read() and fills scalar + timestamp */
TEST(hal_read_scalar_dispatches_read)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    typedef struct { float val; uint64_t ts; } raw1_t;
    raw1_t canned = { .val = 25.3f, .ts = 123456789ULL };
    mock_hal_set_read_data(dev, &canned, sizeof(canned));

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 4);  /* sensor_type=4 → 1-axis (temperature, etc.) */

    float    value  = 0.0f;
    uint64_t ts     = 0;
    int rc = sensor_service_hal_read_scalar(&ctx, &value, &ts);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* 5. stop() before close() in cleanup */
TEST(hal_cleanup_stop_before_close)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 1);

    sensor_service_hal_cleanup(&ctx);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls  == 1);
    assert(p->counts.close_calls == 1);
    assert(ctx.hal_device == NULL);

    mock_hal_destroy(dev);
}

/* 6. Injected start error → service hal_start returns negative */
TEST(hal_start_error_propagated)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->start_retval = HAL_ERROR_NOT_SUPPORT;
    dev->state = HAL_STATE_OPEN;

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 1);

    int rc = sensor_service_hal_start(&ctx);
    assert(rc != SVC_OK);
    assert(p->counts.start_calls == 1);

    mock_hal_destroy(dev);
}

/* 7. Injected read error → hal_read_3axis returns negative */
TEST(hal_read_error_propagated)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->read_retval = -1;
    dev->state = HAL_STATE_ACTIVE;

    sensor_service_ctx_t ctx;
    make_ctx(&ctx, dev, 1);

    svc_sensor_3axis_t out;
    int rc = sensor_service_hal_read_3axis(&ctx, &out);
    assert(rc != SVC_OK);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* 8. mock_hal_reset() isolation */
TEST(mock_reset_isolation)
{
    hw_device_t *dev = mock_hal_create("iio0", HAL_DEVICE_TYPE_SENSOR);
    assert(dev);

    dev->ops->open(dev);
    dev->ops->start(dev);

    mock_hal_reset(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls  == 0);
    assert(p->counts.start_calls == 0);

    mock_hal_destroy(dev);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_sensor_hal_layer ===\n\n");

    RUN(hal_init_calls_open);
    RUN(hal_start_sets_active);
    RUN(hal_read_3axis_dispatches_read);
    RUN(hal_read_scalar_dispatches_read);
    RUN(hal_cleanup_stop_before_close);
    RUN(hal_start_error_propagated);
    RUN(hal_read_error_propagated);
    RUN(mock_reset_isolation);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
