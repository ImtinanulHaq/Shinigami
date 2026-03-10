/**
 * @file sensor_service_loop.c
 * @brief Sensor service io_uring async I/O event loop.
 *
 * Manages two concurrent operations via io_uring:
 *   - IIO fd poll (IO_OP_POLL) -- data-ready interrupt
 *   - 1 s periodic timeout -- SM heartbeat + reload check
 *
 * SM uses a per-request connection model: each heartbeat opens a fresh
 * connection, sends SM_MSG_HEARTBEAT, receives the reply, then closes.
 * There is no persistent SM socket FD to watch in the event loop.
 *
 * Latest readings are stored in mutex-protected g_latest for IPC clients.
 */

#define _GNU_SOURCE
#include "sensor_service_loop.h"
#include "sensor_service_hal.h"
#include "../common/service_base.h"
#include "../common/service_ipc.h"
#include "../common/service_config.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../../dev/hal/interface/hal_interface.h"
#include "../../dev/hal/layers/sensors/sensor_hal.h"
#include "../../dev/core/io_uring_loop.h"

/* __ constants ___________________________________________________________ */

#define LOOP_TIMEOUT_MS   1000
#define URING_QUEUE_DEPTH 64u
#define IIO_NOTIFY_BUF    1u

/* __ latest-reading store _________________________________________________ */

typedef struct {
    sensor_data_3axis_t axis;
    sensor_data_1axis_t scalar;
    int                 is_3axis;
    time_t              timestamp;
    pthread_mutex_t     lock;
} sensor_latest_t;

static sensor_latest_t g_latest = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};

/* __ per-loop-run context _________________________________________________ */

typedef struct {
    sensor_service_ctx_t *svc_ctx;
    svc_ipc_t            *ipc;
    service_config_t     *cfg;
    io_uring_loop_t      *loop;
    time_t                start_time;

    volatile sig_atomic_t loop_running;
} sensor_loop_ctx_t;

/* __ helpers ______________________________________________________________ */

static time_t mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void apply_config_reload(sensor_service_ctx_t *ctx,
                                service_config_t     *cfg)
{
    if (service_config_reload(cfg) != 0) {
        LOG_WARN("sensor config reload failed");
        return;
    }
    uint32_t new_rate = service_config_get_uint32(cfg, "hardware",
                            "sampling_rate_hz", ctx->sampling_rate_hz);
    if (new_rate != ctx->sampling_rate_hz && new_rate > 0) {
        LOG_INFO("sensor config reload: sampling_rate_hz updated to %u", new_rate);
        ctx->sampling_rate_hz = new_rate;
    }
    LOG_INFO("sensor config reload applied");
}

/* __ io_uring callbacks ___________________________________________________ */

/* NOTE: SM uses per-request connections — no persistent SM socket FD.
 * on_sm_data is removed; SM never sends unsolicited data.
 * All SM communication is service-initiated via service_ipc_heartbeat(). */

static int on_hal_data(int fd, int result, void *user_data,
                       void *buf, size_t len)
{
    sensor_loop_ctx_t    *lctx = (sensor_loop_ctx_t *)user_data;
    sensor_service_ctx_t *ctx  = lctx->svc_ctx;
    (void)fd; (void)buf; (void)len;

    if (result <= 0) {
        if (result < 0) LOG_WARN("sensor IIO fd error: %d", result);
        return result < 0 ? -1 : 0;
    }

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    if (!dev) return 0;

    pthread_mutex_lock(&g_latest.lock);
    if (ctx->sensor_type >= 1 && ctx->sensor_type <= 3) {
        sensor_hal_read_3axis(dev, &g_latest.axis);
        g_latest.is_3axis  = 1;
        g_latest.timestamp = time(NULL);
        LOG_DEBUG("IIO 3-axis: x=%.4f y=%.4f z=%.4f",
                  g_latest.axis.x, g_latest.axis.y, g_latest.axis.z);
    } else {
        sensor_hal_read_1axis(dev, &g_latest.scalar);
        g_latest.is_3axis  = 0;
        g_latest.timestamp = time(NULL);
        LOG_DEBUG("IIO 1-axis: value=%.4f", g_latest.scalar.value);
    }
    pthread_mutex_unlock(&g_latest.lock);
    return 0;
}

static int on_health_timeout(int fd, int result, void *user_data,
                              void *buf, size_t len)
{
    sensor_loop_ctx_t *lctx = (sensor_loop_ctx_t *)user_data;
    (void)fd; (void)result; (void)buf; (void)len;

    if (g_reload) {
        apply_config_reload(lctx->svc_ctx, lctx->cfg);
        g_reload = 0;
    }
    if (service_ipc_heartbeat(lctx->ipc) != SVC_OK) {
        LOG_WARN("sensor SM heartbeat failed (will retry next interval)");
        /* Non-fatal: SM may be temporarily busy; don't exit the loop */
    }
    return 0;
}

/* __ main event loop ______________________________________________________ */

int sensor_service_loop_run(sensor_service_ctx_t *ctx, svc_ipc_t *ipc,
                            service_config_t *cfg)
{
    if (!ctx || !ipc) return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    io_loop_config_t loop_cfg = {
        .queue_depth = URING_QUEUE_DEPTH,
        .use_sqpoll  = 0,
        .buf_pool    = NULL,
        .name        = "sensor_svc",
    };
    io_uring_loop_t *loop = io_loop_create(&loop_cfg);
    if (!loop) {
        LOG_ERR("sensor: io_loop_create failed");
        return SVC_ERR_GENERIC;
    }

    sensor_loop_ctx_t lctx;
    memset(&lctx, 0, sizeof(lctx));
    lctx.svc_ctx    = ctx;
    lctx.ipc        = ipc;
    lctx.cfg        = cfg;
    lctx.loop       = loop;
    lctx.start_time = mono_now();

    LOG_INFO("sensor_service io_uring event loop starting");

    while (g_running) {
        lctx.loop_running = 1;

        if (dev && dev->fd >= 0) {
            /* IO_OP_POLL: wakes on POLLIN; callback calls sensor_hal_read_*() */
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

    LOG_INFO("sensor_service io_uring event loop exiting");
    io_loop_destroy(loop);
    return SVC_OK;
}
