/**
 * @file gpio_service_loop.c
 * @brief GPIO service io_uring async I/O event loop.
 *
 * Manages two concurrent operations via io_uring:
 *   - GPIO sysfs/chardev fd poll (IO_OP_POLL) -- edge interrupt notification
 *   - 1 s periodic timeout -- SM heartbeat + reload check
 *
 * SM uses a per-request connection model: each heartbeat opens a fresh
 * connection, sends SM_MSG_HEARTBEAT, receives the reply, then closes.
 * There is no persistent SM socket FD to watch in the event loop.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gpio_service_loop.h"
#include "gpio_service_hal.h"
#include "../common/service_base.h"
#include "../common/service_ipc.h"
#include "../common/service_config.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../../dev/hal/interface/hal_interface.h"
#include "../../dev/hal/layers/gpio/gpio_hal.h"
#include "../../dev/core/io_uring_loop.h"

/* __ constants ___________________________________________________________ */

#define LOOP_TIMEOUT_MS   1000
#define URING_QUEUE_DEPTH 64u
/* GPIO sysfs value fd read returns e.g. "0\n" or "1\n"; 4 bytes is plenty  */
#define GPIO_READ_BUF     4u

/* __ per-loop-run context _________________________________________________ */

typedef struct {
    gpio_service_ctx_t   *svc_ctx;
    svc_ipc_t            *ipc;
    service_config_t     *cfg;
    io_uring_loop_t      *loop;
    time_t                start_time;

    volatile sig_atomic_t loop_running;
} gpio_loop_ctx_t;

/* __ helpers ______________________________________________________________ */

static time_t mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void apply_config_reload(gpio_service_ctx_t *ctx,
                                service_config_t   *cfg)
{
    if (service_config_reload(cfg) != 0) {
        LOG_WARN("GPIO config reload failed");
        return;
    }
    uint32_t new_timeout = service_config_get_uint32(cfg, "hardware",
                               "interrupt_timeout_ms",
                               (uint32_t)ctx->interrupt_timeout_ms);
    if (new_timeout != (uint32_t)ctx->interrupt_timeout_ms) {
        LOG_INFO("GPIO config reload: interrupt_timeout_ms updated to %u",
                 new_timeout);
        ctx->interrupt_timeout_ms = (int)new_timeout;
    }
    LOG_INFO("GPIO config reload applied");
}

/* __ io_uring callbacks ___________________________________________________ */

/* NOTE: SM uses per-request connections — no persistent SM socket FD.
 * on_sm_data is removed; SM never sends unsolicited data.
 * All SM communication is service-initiated via service_ipc_heartbeat(). */

static int on_hal_data(int fd, int result, void *user_data,
                       void *buf, size_t len)
{
    gpio_loop_ctx_t  *lctx = (gpio_loop_ctx_t *)user_data;
    gpio_service_ctx_t *ctx = lctx->svc_ctx;
    (void)fd; (void)buf; (void)len;

    if (result <= 0) {
        if (result < 0) LOG_WARN("GPIO HAL fd error: %d", result);
        return result < 0 ? -1 : 0;
    }

    /* Read current pin value and consume the edge event */
    int val = 0;
    gpio_service_hal_get_value(ctx, &val);
    LOG_INFO("GPIO pin %u edge: value=%d", ctx->pin_number, val);

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    if (dev) gpio_hal_wait_interrupt(dev, 0);

    return 0;
}

static int on_health_timeout(int fd, int result, void *user_data,
                              void *buf, size_t len)
{
    gpio_loop_ctx_t *lctx = (gpio_loop_ctx_t *)user_data;
    (void)fd; (void)result; (void)buf; (void)len;

    if (g_reload) {
        apply_config_reload(lctx->svc_ctx, lctx->cfg);
        g_reload = 0;
    }
    if (service_ipc_heartbeat(lctx->ipc) != SVC_OK) {
        LOG_WARN("GPIO SM heartbeat failed (will retry next interval)");
        /* Non-fatal: SM may be temporarily busy; don't exit the loop */
    }
    return 0;
}

/* __ main event loop ______________________________________________________ */

int gpio_service_loop_run(gpio_service_ctx_t *ctx, svc_ipc_t *ipc,
                          service_config_t *cfg)
{
    if (!ctx || !ipc) return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    io_loop_config_t loop_cfg = {
        .queue_depth = URING_QUEUE_DEPTH,
        .use_sqpoll  = 0,
        .buf_pool    = NULL,
        .name        = "gpio_svc",
    };
    io_uring_loop_t *loop = io_loop_create(&loop_cfg);
    if (!loop) {
        LOG_ERR("GPIO: io_loop_create failed");
        return SVC_ERR_GENERIC;
    }

    gpio_loop_ctx_t lctx;
    memset(&lctx, 0, sizeof(lctx));
    lctx.svc_ctx    = ctx;
    lctx.ipc        = ipc;
    lctx.cfg        = cfg;
    lctx.loop       = loop;
    lctx.start_time = mono_now();

    LOG_INFO("gpio_service io_uring event loop starting (pin=%u, edge=%u)",
             ctx->pin_number, ctx->edge);

    while (g_running) {
        lctx.loop_running = 1;

        if (dev && dev->fd >= 0) {
            /* IO_OP_POLL: wakes on POLLIN; callback calls gpio_service_hal_get_value() */
            io_op_desc_t hal_op = {
                .fd = dev->fd, .op_type = IO_OP_POLL, .buf_size = 0,
                .callback = on_hal_data, .user_data = &lctx, .timeout_ms = 0,
            };
            io_loop_register_op(loop, &hal_op);
        }

        io_op_desc_t to_op = {
            .fd = -1, .op_type = IO_OP_TIMEOUT, .buf_size = 0,
            .callback = on_health_timeout, .user_data = &lctx,
            .timeout_ms = LOOP_TIMEOUT_MS,
        };
        io_loop_register_op(loop, &to_op);

        io_loop_run(loop, &lctx.loop_running);

        /* Clear op table so re-registration on next iteration is clean */
        io_loop_clear_ops(loop);
    }

    LOG_INFO("gpio_service io_uring event loop exiting");
    io_loop_destroy(loop);
    return SVC_OK;
}
