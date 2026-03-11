/**
 * @file camera_service_loop.c
 * @brief Camera service io_uring async I/O event loop.
 *
 * Manages two concurrent operations via io_uring:
 *   - V4L2 fd poll (IO_OP_POLL) -- frame ready notifications
 *   - 1 s periodic timeout -- SM heartbeat + reload check
 *
 * SM uses a per-request connection model: each heartbeat opens a fresh
 * connection, sends SM_MSG_HEARTBEAT, receives the reply, then closes.
 * There is no persistent SM socket FD to watch in the event loop.
 *
 * On each HAL fd completion, camera_hal_capture_frame() is called to dequeue
 * a V4L2 buffer, which is stored in a circular frame_queue_t.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "camera_service_loop.h"
#include "camera_service_hal.h"
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
#include "../../dev/hal/layers/camera/camera_hal.h"
#include "../../dev/core/io_uring_loop.h"

/* __ constants ___________________________________________________________ */

#define LOOP_TIMEOUT_MS   1000
#define URING_QUEUE_DEPTH 64u
#define FRAME_QUEUE_DEPTH 8
#define CAPTURE_TIMEOUT_MS 100
/* V4L2 readiness notification is < 1 byte; we read 1 byte to satisfy SQE */
#define HAL_NOTIFY_BUF    1u

/* __ circular frame queue _________________________________________________ */

typedef struct {
    camera_frame_t frames[FRAME_QUEUE_DEPTH];
    int            head, tail, count;
} frame_queue_t;

static void fq_push(frame_queue_t *fq, const camera_frame_t *f)
{
    fq->frames[fq->tail] = *f;
    fq->tail = (fq->tail + 1) % FRAME_QUEUE_DEPTH;
    if (fq->count < FRAME_QUEUE_DEPTH) fq->count++;
    else fq->head = (fq->head + 1) % FRAME_QUEUE_DEPTH;
}

/* __ per-loop-run context _________________________________________________ */

typedef struct {
    camera_service_ctx_t *svc_ctx;
    svc_ipc_t            *ipc;
    service_config_t     *cfg;
    io_uring_loop_t      *loop;
    frame_queue_t        *fq;
    time_t                start_time;

    volatile sig_atomic_t loop_running;
} camera_loop_ctx_t;

/* __ helpers ______________________________________________________________ */

static time_t mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void apply_config_reload(camera_service_ctx_t *ctx,
                                service_config_t     *cfg)
{
    if (service_config_reload(cfg) != 0) {
        LOG_WARN("camera config reload failed");
        return;
    }
    uint32_t new_fps = service_config_get_uint32(cfg, "hardware", "fps",
                           ctx->fps);
    if (new_fps != ctx->fps && new_fps > 0 && new_fps <= 240) {
        LOG_INFO("camera config reload: fps updated to %u", new_fps);
        ctx->fps = new_fps;
    }
    LOG_INFO("camera config reload applied");
}

/* __ io_uring callbacks ___________________________________________________ */

/* NOTE: SM uses per-request connections — no persistent SM socket FD.
 * on_sm_data is removed; SM never sends unsolicited data.
 * All SM communication is service-initiated via service_ipc_heartbeat(). */

static int on_hal_data(int fd, int result, void *user_data,
                       void *buf, size_t len)
{
    camera_loop_ctx_t *lctx = (camera_loop_ctx_t *)user_data;
    (void)fd; (void)buf; (void)len;

    if (result <= 0) {
        if (result < 0) LOG_WARN("camera HAL fd error: %d", result);
        return result < 0 ? -1 : 0;
    }

    /* Dequeue V4L2 frame */
    hw_device_t   *dev = (hw_device_t *)lctx->svc_ctx->hal_device;
    camera_frame_t frame;
    int rc = camera_hal_capture_frame(dev, &frame, CAPTURE_TIMEOUT_MS);
    if (rc != HAL_SUCCESS) {
        LOG_DEBUG("camera capture_frame: %d", rc);
        lctx->svc_ctx->base.error_count++;
        return 0;
    }
    LOG_DEBUG("camera frame seq=%u size=%zu", frame.sequence, frame.size);
    fq_push(lctx->fq, &frame);
    camera_hal_return_frame(dev, &frame);
    return 0;
}

static int on_health_timeout(int fd, int result, void *user_data,
                              void *buf, size_t len)
{
    camera_loop_ctx_t *lctx = (camera_loop_ctx_t *)user_data;
    (void)fd; (void)result; (void)buf; (void)len;

    if (g_reload) {
        apply_config_reload(lctx->svc_ctx, lctx->cfg);
        g_reload = 0;
    }
    if (service_ipc_heartbeat(lctx->ipc) != SVC_OK) {
        LOG_WARN("camera SM heartbeat failed (will retry next interval)");
        /* Non-fatal: SM may be temporarily busy; don't exit the loop */
    }
    return 0;
}

/* __ main event loop ______________________________________________________ */

int camera_service_loop_run(camera_service_ctx_t *ctx, svc_ipc_t *ipc,
                            service_config_t *cfg)
{
    if (!ctx || !ipc) return SVC_ERR_INVALID;

    hw_device_t  *dev = (hw_device_t *)ctx->hal_device;
    frame_queue_t fq;
    memset(&fq, 0, sizeof(fq));

    io_loop_config_t loop_cfg = {
        .queue_depth = URING_QUEUE_DEPTH,
        .use_sqpoll  = 0,
        .buf_pool    = ctx->ipc_pool,
        .name        = "camera_svc",
    };
    io_uring_loop_t *loop = io_loop_create(&loop_cfg);
    if (!loop) {
        LOG_ERR("camera: io_loop_create failed");
        return SVC_ERR_GENERIC;
    }

    camera_loop_ctx_t lctx;
    memset(&lctx, 0, sizeof(lctx));
    lctx.svc_ctx    = ctx;
    lctx.ipc        = ipc;
    lctx.cfg        = cfg;
    lctx.loop       = loop;
    lctx.fq         = &fq;
    lctx.start_time = mono_now();

    LOG_INFO("camera_service io_uring event loop starting");

    while (g_running) {
        lctx.loop_running = 1;

        if (dev && dev->fd >= 0) {
            /* IO_OP_POLL: wakes on POLLIN; callback calls camera_hal_capture_frame() */
            io_op_desc_t hal_op = {
                .fd = dev->fd, .op_type = IO_OP_POLL,
                .buf_size = 0, .callback = on_hal_data,
                .user_data = &lctx, .timeout_ms = 0,
            };
            io_loop_register_op(loop, &hal_op);
        }

        io_op_desc_t to_op = {
            .fd = -1, .op_type = IO_OP_TIMEOUT,
            .buf_size = 0, .callback = on_health_timeout,
            .user_data = &lctx, .timeout_ms = LOOP_TIMEOUT_MS,
        };
        io_loop_register_op(loop, &to_op);

        io_loop_run(loop, &lctx.loop_running);

        /* Clear op table so re-registration on next iteration is clean */
        io_loop_clear_ops(loop);
    }

    LOG_INFO("camera_service io_uring event loop exiting");
    io_loop_destroy(loop);
    return SVC_OK;
}
