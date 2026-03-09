/**
 * @file test_gpio_hal_layer.c
 * @brief Unit tests for gpio_service_hal using a mock hw_device_t.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mocks/mock_hal.h"
#include "../../gpio_service/gpio_service.h"
#include "../../gpio_service/gpio_service_hal.h"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

static void make_ctx(gpio_service_ctx_t *ctx, hw_device_t *mock_dev)
{
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->base.name, "gpio_service", SERVICE_MAX_NAME - 1);
    atomic_store(&ctx->base.running, 1);
    ctx->pin_number   = 4;
    ctx->direction    = 1; /* output */
    ctx->initial_value = 0;
    ctx->edge         = 0;
    ctx->hal_device   = mock_dev;

    if (mock_dev)
        mock_dev->state = HAL_STATE_OPEN;
}

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(hal_start_increments_start_count)
{
    hw_device_t *dev = mock_hal_create("gpio4", HAL_DEVICE_TYPE_GPIO);
    assert(dev);

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = gpio_service_hal_start(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.start_calls == 1);
    assert(dev->state == HAL_STATE_ACTIVE);

    mock_hal_destroy(dev);
}

TEST(hal_set_value_calls_control)
{
    hw_device_t *dev = mock_hal_create("gpio4", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    /* gpio_hal_set_value calls dev->ops->control, counted by mock */
    gpio_service_hal_set_value(&ctx, 1);
    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.control_calls >= 1);

    mock_hal_destroy(dev);
}

TEST(hal_get_value_calls_control)
{
    hw_device_t *dev = mock_hal_create("gpio4", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int val = 0;
    gpio_service_hal_get_value(&ctx, &val);
    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.control_calls >= 1);

    mock_hal_destroy(dev);
}

TEST(hal_stop_ops)
{
    hw_device_t *dev = mock_hal_create("gpio4", HAL_DEVICE_TYPE_GPIO);
    assert(dev);
    dev->state = HAL_STATE_ACTIVE;

    gpio_service_ctx_t ctx;
    make_ctx(&ctx, dev);

    int rc = gpio_service_hal_stop(&ctx);
    assert(rc == SVC_OK);

    mock_hal_priv_t *p = mock_hal_get_priv(dev);
    assert(p->counts.stop_calls == 1);

    mock_hal_destroy(dev);
}

TEST(hal_null_ctx_safe)
{
    assert(gpio_service_hal_start(NULL)         != SVC_OK);
    assert(gpio_service_hal_stop(NULL)           != SVC_OK);
    assert(gpio_service_hal_set_value(NULL, 0)  != SVC_OK);
    assert(gpio_service_hal_get_value(NULL, NULL) != SVC_OK);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_gpio_hal_layer ===\n");
    RUN(hal_start_increments_start_count);
    RUN(hal_set_value_calls_control);
    RUN(hal_get_value_calls_control);
    RUN(hal_stop_ops);
    RUN(hal_null_ctx_safe);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
