/**
 * @file audio_service_loop.c
 * @brief Audio service io_uring async I/O event loop.
 *
 * Manages two concurrent operations via io_uring:
 *   - ALSA PCM fd read (io_uring_prep_poll) -- PCM frames into ring buffer
 *   - 1 s periodic timeout (io_uring_prep_timeout) -- SM heartbeat + reload
 *
 * SM uses a per-request connection model: each heartbeat opens a fresh
 * connection, sends SM_MSG_HEARTBEAT, receives the reply, then closes.
 * There is no persistent SM socket FD to watch in the event loop.
 */

#define _GNU_SOURCE
#include "audio_service_loop.h"
#include "audio_service_hal.h"
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
#include "../../dev/core/ring_buffer.h"
#include "../../dev/core/io_uring_loop.h"

/* __ constants ___________________________________________________________ */

#define LOOP_TIMEOUT_MS   1000
#define PCM_FRAME_BYTES   4096
#define RING_CAPACITY     64
#define RING_ITEM_SIZE    PCM_FRAME_BYTES
#define URING_QUEUE_DEPTH 64u

/* __ per-loop-run context _________________________________________________ */

typedef struct {
    audio_service_ctx_t  *svc_ctx;
    svc_ipc_t            *ipc;
    service_config_t     *cfg;
    io_uring_loop_t      *loop;
    rb_handle_t          *rb;
    time_t                start_time;

    volatile sig_atomic_t loop_running;
} audio_loop_ctx_t;

/* __ helpers ______________________________________________________________ */

static time_t mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void apply_config_reload(audio_service_ctx_t *ctx,
                                service_config_t    *cfg)
{
    if (service_config_reload(cfg) != 0) {
        LOG_WARN("audio config reload failed -- keeping current settings");
        return;
    }
    uint32_t new_rate = service_config_get_uint32(cfg, "hardware",
                            "sample_rate", ctx->sample_rate);
    if (new_rate != ctx->sample_rate && new_rate >= 8000 && new_rate <= 192000) {
        LOG_INFO("audio config reload: sample_rate updated to %u", new_rate);
        ctx->sample_rate = new_rate;
    }
    LOG_INFO("audio config reload applied");
}

/* __ io_uring callbacks ___________________________________________________ */

/* NOTE: SM uses per-request connections — no persistent SM socket FD.
 * on_sm_data is removed; SM never sends unsolicited data.
 * All SM communication is service-initiated via service_ipc_heartbeat(). */

static int on_hal_data(int fd, int result, void *user_data,
                       void *buf, size_t len)
{
    audio_loop_ctx_t *lctx = (audio_loop_ctx_t *)user_data;
    (void)fd; (void)buf; (void)len;

    if (result <= 0) {
        if (result < 0) LOG_WARN("audio HAL fd error: %d", result);
        return result < 0 ? -1 : 0;
    }
    /* IO_OP_POLL fired (POLLIN); call HAL read to dequeue the PCM frame */
    uint8_t pcm_buf[PCM_FRAME_BYTES];
    ssize_t n = audio_service_hal_read(lctx->svc_ctx, pcm_buf, sizeof(pcm_buf));
    if (n <= 0) {
        if (n < 0) LOG_WARN("audio HAL read error: %zd", n);
        return 0;
    }
    if (lctx->rb) {
        if (ring_buffer_write(lctx->rb, pcm_buf) == RB_ERROR_FULL) {
            LOG_DEBUG("audio ring buffer full -- dropping oldest frame");
            uint8_t tmp[PCM_FRAME_BYTES];
            ring_buffer_read(lctx->rb, tmp);
            ring_buffer_write(lctx->rb, pcm_buf);
        }
    }
    return 0;
}

static int on_health_timeout(int fd, int result, void *user_data,
                              void *buf, size_t len)
{
    audio_loop_ctx_t *lctx = (audio_loop_ctx_t *)user_data;
    (void)fd; (void)result; (void)buf; (void)len;

    if (g_reload) {
        apply_config_reload(lctx->svc_ctx, lctx->cfg);
        g_reload = 0;
    }

    /* Send heartbeat to SM (opens fresh per-request connection) */
    if (service_ipc_heartbeat(lctx->ipc) != SVC_OK) {
        LOG_WARN("audio SM heartbeat failed (will retry next interval)");
        /* Non-fatal: SM may be temporarily busy; don't exit the loop */
    }
    return 0;
}

/* __ main event loop ______________________________________________________ */

int audio_service_loop_run(audio_service_ctx_t *ctx, svc_ipc_t *ipc,
                           service_config_t *cfg)
{
    if (!ctx || !ipc) return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    rb_handle_t *rb = ring_buffer_create("audio_service",
                          RING_CAPACITY, RING_ITEM_SIZE);
    if (!rb)
        LOG_WARN("audio: ring buffer unavailable -- frames not buffered");

    io_loop_config_t loop_cfg = {
        .queue_depth = URING_QUEUE_DEPTH,
        .use_sqpoll  = 0,
        .buf_pool    = ctx->ipc_pool,
        .name        = "audio_svc",
    };
    io_uring_loop_t *loop = io_loop_create(&loop_cfg);
    if (!loop) {
        LOG_ERR("audio: io_loop_create failed");
        if (rb) ring_buffer_destroy(rb, "audio_service");
        return SVC_ERR_GENERIC;
    }

    audio_loop_ctx_t lctx;
    memset(&lctx, 0, sizeof(lctx));
    lctx.svc_ctx    = ctx;
    lctx.ipc        = ipc;
    lctx.cfg        = cfg;
    lctx.loop       = loop;
    lctx.rb         = rb;
    lctx.start_time = mono_now();

    LOG_INFO("audio_service io_uring event loop starting");

    while (g_running) {

        lctx.loop_running = 1;

        /* HAL PCM fd -- IO_OP_POLL: wakes on POLLIN, then on_hal_data
         * calls audio_service_hal_read() via the HAL abstraction. */
        if (dev && dev->fd >= 0) {
            io_op_desc_t hal_op = {
                .fd         = dev->fd,
                .op_type    = IO_OP_POLL,
                .buf_size   = 0,
                .callback   = on_hal_data,
                .user_data  = &lctx,
                .timeout_ms = 0,
            };
            io_loop_register_op(loop, &hal_op);
        }

        /* 1-second periodic timeout for SM heartbeat + config reload */
        io_op_desc_t to_op = {
            .fd         = -1,
            .op_type    = IO_OP_TIMEOUT,
            .buf_size   = 0,
            .callback   = on_health_timeout,
            .user_data  = &lctx,
            .timeout_ms = LOOP_TIMEOUT_MS,
        };
        io_loop_register_op(loop, &to_op);

        io_loop_run(loop, &lctx.loop_running);

        /* Clear op table for next iteration */
        io_loop_clear_ops(loop);
    }

    io_loop_destroy(loop);
    if (rb) ring_buffer_destroy(rb, "audio_service");
    return SVC_OK;
}
