/**
 * @file audio_service_loop.c
 * @brief Audio service io_uring async I/O event loop.
 *
 * Manages three concurrent operations via io_uring:
 *   - SM socket read (io_uring_prep_read) -- health / shutdown / reload
 *   - ALSA PCM fd read (io_uring_prep_read) -- PCM frames into ring buffer
 *   - 1 s periodic timeout (io_uring_prep_timeout) -- SM ping + reload check
 *
 * SM messages arrive as sm_hdr_t + payload on a UNIX domain stream socket.
 * A single io_uring read of sizeof(sm_hdr_t) + SM_MAX_PAYLOAD_SIZE bytes
 * is issued; for co-located processes the full message arrives in one shot.
 *
 * Reconnect strategy: when the SM socket reports EOF/error, the callback sets
 * a need_reconnect flag and clears loop_running to exit io_loop_run().  The
 * outer while(g_running) loop performs the backoff sleep, reconnects, and
 * re-registers the new fd before re-entering io_loop_run().
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
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/security/verify/verify.h"

/* __ constants ___________________________________________________________ */

#define LOOP_TIMEOUT_MS   1000
#define PCM_FRAME_BYTES   4096
#define RING_CAPACITY     64
#define RING_ITEM_SIZE    PCM_FRAME_BYTES
#define SM_BUF_SIZE       (sizeof(sm_hdr_t) + SM_MAX_PAYLOAD_SIZE)
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
    int                   need_reconnect;
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

/* __ SM message dispatch (from pre-read buffer) ___________________________ */

static void dispatch_sm_message(audio_loop_ctx_t *lctx,
                                const void *buf, size_t len)
{
    if (len < sizeof(sm_hdr_t)) {
        LOG_WARN("audio SM: short read (%zu bytes) -- discarding", len);
        return;
    }

    const sm_hdr_t *hdr = (const sm_hdr_t *)buf;
    if (hdr->magic != SM_PROTOCOL_MAGIC) {
        LOG_WARN("audio SM: bad magic 0x%08X -- discarding", hdr->magic);
        return;
    }

    size_t plen = hdr->length;
    if (plen > SM_MAX_PAYLOAD_SIZE) plen = SM_MAX_PAYLOAD_SIZE;
    const uint8_t *payload = (const uint8_t *)buf + sizeof(sm_hdr_t);

    if (lctx->ipc->verify_ctx) {
        verify_context_t *vctx = (verify_context_t *)lctx->ipc->verify_ctx;
        size_t   blob_len = SM_HDR_HMAC_OFFSET + plen;
        uint8_t *blob     = malloc(blob_len);
        if (blob) {
            memcpy(blob, hdr, SM_HDR_HMAC_OFFSET);
            if (plen) memcpy(blob + SM_HDR_HMAC_OFFSET, payload, plen);
            message_auth_t auth;
            memcpy(auth.hmac, hdr->hmac, 32);
            auth.timestamp = hdr->timestamp;
            auth.nonce     = hdr->nonce;
            int vrc = verify_check_message(vctx, blob, blob_len, &auth);
            free(blob);
            if (vrc != 0) {
                LOG_WARN("audio SM: verify rejected (type=%u)", hdr->type);
                lctx->svc_ctx->base.error_count++;
                return;
            }
        }
    }

    switch (hdr->type) {
    case SVC_MSG_HEALTH_CHECK: {
        svc_health_status_t st;
        memset(&st, 0, sizeof(st));
        st.uptime_sec  = (uint32_t)(mono_now() - lctx->start_time);
        st.error_count = (uint32_t)lctx->svc_ctx->base.error_count;
        st.hal_state   = lctx->svc_ctx->hal_device
                         ? (uint8_t)((hw_device_t *)lctx->svc_ctx->hal_device)->state
                         : 0u;
        st.svc_state   = (uint8_t)lctx->svc_ctx->base.state;
        service_ipc_send_health(lctx->ipc, &st);
        break;
    }
    case SVC_MSG_SHUTDOWN:
        LOG_INFO("audio SM: shutdown commanded");
        g_running          = 0;
        lctx->loop_running = 0;
        break;
    case SVC_MSG_RELOAD_CONFIG:
        LOG_INFO("audio SM: config reload commanded");
        g_reload = 1;
        break;
    default:
        LOG_DEBUG("audio SM: unhandled type %u", hdr->type);
        break;
    }
}

/* __ io_uring callbacks ___________________________________________________ */

static int on_sm_data(int fd, int result, void *user_data,
                      void *buf, size_t len)
{
    audio_loop_ctx_t *lctx = (audio_loop_ctx_t *)user_data;
    (void)fd;
    if (result <= 0) {
        LOG_WARN("audio SM socket: result=%d -- reconnecting", result);
        lctx->need_reconnect = 1;
        lctx->loop_running   = 0;
        return -1;
    }
    dispatch_sm_message(lctx, buf, len);
    return 0;
}

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

    if (service_ipc_ping(lctx->ipc) != SVC_OK) {
        LOG_WARN("audio SM ping failed -- reconnecting");
        lctx->need_reconnect = 1;
        lctx->loop_running   = 0;
        return -1;
    }
    return 0;
}

/* __ reconnect helper _____________________________________________________ */

static int reconnect_sm(svc_ipc_t *ipc, io_uring_loop_t *loop, int old_fd)
{
    io_loop_unregister_fd(loop, old_fd);
    if (old_fd >= 0)
        service_ipc_disconnect(ipc);

    LOG_WARN("audio: reconnecting to SM (backoff=%ds)",
             ipc->reconnect_backoff_sec);
    sleep((unsigned)ipc->reconnect_backoff_sec);

    ipc->reconnect_backoff_sec *= 2;
    if (ipc->reconnect_backoff_sec > ipc->reconnect_backoff_max)
        ipc->reconnect_backoff_sec = ipc->reconnect_backoff_max;

    if (service_ipc_connect(ipc) != SVC_OK) {
        LOG_WARN("audio: SM reconnect failed");
        return -1;
    }
    if (service_ipc_register(ipc, "", SERVICE_LAYER_VERSION_STR,
                             getpid()) != SVC_OK) {
        LOG_WARN("audio: SM re-registration failed");
        return -1;
    }
    ipc->reconnect_backoff_sec = 1;
    LOG_INFO("audio: SM reconnected and re-registered");
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

        lctx.loop_running   = 1;
        lctx.need_reconnect = 0;

        /* SM socket read */
        io_op_desc_t sm_op = {
            .fd         = ipc->fd,
            .op_type    = IO_OP_READ,
            .buf_size   = SM_BUF_SIZE,
            .callback   = on_sm_data,
            .user_data  = &lctx,
            .timeout_ms = 0,
        };
        io_loop_register_op(loop, &sm_op);

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

        /* 1-second periodic timeout */
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

        /* Clear op table so re-registration on next iteration is clean */
        io_loop_clear_ops(loop);

        if (!g_running) break;

        if (lctx.need_reconnect) {
            int old_fd = ipc->fd;
            reconnect_sm(ipc, loop, old_fd);
            /* top of loop will re-register SM op with new ipc->fd */
        }
    }

    LOG_INFO("audio_service io_uring event loop exiting");
    io_loop_destroy(loop);
    if (rb) ring_buffer_destroy(rb, "audio_service");
    return SVC_OK;
}
