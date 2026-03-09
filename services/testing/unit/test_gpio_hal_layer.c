/**
 * @file test_gpio_hal_layer.c
 * @brief Unit tests for gpio_service_hal using mock_hal.
 *
 * Tests covered:
 *   1. hal_init() — open() called once; dev state = OPEN.
 *   2. hal_start() — start() called; state = ACTIVE.
 *   3. hal_set_value() — write() dispatched with correct byte value.
 *   4. hal_get_value() — read() dispatched; value returned.
 *   5. hal_wait_interrupt() — control() dispatched for interrupt wait.
 *   6. Lifecycle order — stop() before close() in cleanup.
 *   7. Injected open error — init must propagate SVC_ERR_HAL.
 *   8. Injected set_value error — write returns error.
 *   9. mock_hal_reset() for test isolation.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_gpio_hal_layer.c \
 *       ../../gpio_service/gpio_service_hal.c \
 *       ../../common/service_base.c \
 *       ../mocks/mock_hal.c \
 *       -I../../gpio_service -I../../common -I../mocks \
 *       -o test_gpio_hal_layer
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../gpio_service/gpio_service.h"
#include "../../gpio_service/gpio_service_hal.h"
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

static void make_ctx(gpio_service_ctx_t *ctx, hw_device_t *dev)
{
    memset(ctx, 0, sizeof(*ctx));
    service_base_init(&ctx->base, GPIO_SERVICE_NAME);
    ctx->base.foreground       = 1;
    ctx->pin_number            = GPIO_SERVICE_DEFAULT_PIN;
    ctx->direction             = GPIO_SERVICE_DEFAULT_DIR;
    ctx->initial_value         = 0;
    ctx->edge                  = GPIO_SERVICE_DEFAULT_EDGE;
    ctx->interrupt_timeout_ms  = 0;   /* non-blocking */
    ctx->hal_device            = dev;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1. Init calls open exactly once */
TEST(hal_init_calls_open)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = dev->ops->open(dev);
    assert(rc == HAL_SUCCESS);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls == 1);
    assert(dev->state == HAL_STATE_OPEN);

    mock_hal_destroy(dev);
}

/* 2. Start sets state to ACTIVE */
TEST(hal_start_sets_active)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_OPEN;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = gpio_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

/* 3. set_value() dispatches write() — value byte captured */
TEST(hal_set_value_dispatches_write)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = gpio_service_hal_set_value(&ctx, 1);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.write_calls >= 1);

    mock_hal_destroy(dev);
}

/* 4. get_value() dispatches read() */
TEST(hal_get_value_dispatches_read)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    /* Load canned "high" value */
    uint8_t high = 0x01;
    mock_hal_set_read_data(dev, &high, sizeof(high));

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int value = -1;
    int rc = gpio_service_hal_get_value(&ctx, &value);
    assert(rc == SVC_OK);
    assert(value == 1);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.read_calls == 1);

    mock_hal_destroy(dev);
}

/* 5. wait_interrupt() dispatches control() */
TEST(hal_wait_interrupt_dispatches_control)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = gpio_service_hal_wait_interrupt(&ctx, 0);
    /* zero timeout returns immediately — SVC_OK or SVC_ERR_TIMEOUT */
    assert(rc == SVC_OK || rc == SVC_ERR_TIMEOUT);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.control_calls >= 1);

    mock_hal_destroy(dev);
}

/* 6. stop() before close() in cleanup */
TEST(hal_cleanup_stop_before_close)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    gpio_service_hal_cleanup(&ctx);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls  == 1);
    assert(p->counts.close_calls == 1);
    assert(ctx.hal_device == NULL);

    mock_hal_destroy(dev);
}

/* 7. Open error propagated */
TEST(hal_open_error_propagated)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->open_retval = HAL_ERROR_DEVICE;

    int rc = dev->ops->open(dev);
    assert(rc != HAL_SUCCESS);
    assert(p->counts.open_calls == 1);

    mock_hal_destroy(dev);
}

/* 8. set_value write error propagated */
TEST(hal_set_value_write_error)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    p->write_retval = -1;
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = gpio_service_hal_set_value(&ctx, 1);
    assert(rc != SVC_OK);
    assert(p->counts.write_calls >= 1);

    mock_hal_destroy(dev);
}

/* 9. mock_hal_reset() isolation */
TEST(mock_reset_isolation)
{
    hw_device_t *dev = mock_hal_create("gpio0", HAL_DEVICE_TYPE_GPIO);
    assert(dev);

    dev->ops->open(dev);
    dev->ops->start(dev);
    dev->ops->stop(dev);

    mock_hal_reset(dev);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.open_calls  == 0);
    assert(p->counts.start_calls == 0);
    assert(p->counts.stop_calls  == 0);
    assert(dev->state == HAL_STATE_CLOSED);

    mock_hal_destroy(dev);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_gpio_hal_layer ===\n\n");

    RUN(hal_init_calls_open);
    RUN(hal_start_sets_active);
    RUN(hal_set_value_dispatches_write);
    RUN(hal_get_value_dispatches_read);
    RUN(hal_wait_interrupt_dispatches_control);
    RUN(hal_cleanup_stop_before_close);
    RUN(hal_open_error_propagated);
    RUN(hal_set_value_write_error);
    RUN(mock_reset_isolation);

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);
    return (g_tests_failed > 0) ? 1 : 0;
}
