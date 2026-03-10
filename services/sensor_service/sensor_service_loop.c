/**
 * @file sensor_service_loop.c
 * @brief Sensor service io_uring async I/O event loop.
 *
 * Manages three concurrent operations via io_uring:
 *   - SM socket read -- health / shutdown / reload
 *   - IIO fd read -- data-ready interrupt
 *   - 1 s periodic timeout -- SM ping + reload check
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
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/security/verify/verify.h"

/* __ constants ___________________________________________________________ */

#define LOOP_TIMEOUT_MS   1000
#define SM_BUF_SIZE       (sizeof(sm_hdr_t) + SM_MAX_PAYLOAD_SIZE)
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
    int                   need_reconnect;
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

/* __ SM dispatch __________________________________________________________ */

static void dispatch_sm_message(sensor_loop_ctx_t *lctx,
                                const void *buf, size_t len)
{
    if (len < sizeof(sm_hdr_t)) return;
    const sm_hdr_t *hdr = (const sm_hdr_t *)buf;
    if (hdr->magic != SM_PROTOCOL_MAGIC) return;

    size_t plen = (hdr->length < SM_MAX_PAYLOAD_SIZE)
                  ? hdr->length : SM_MAX_PAYLOAD_SIZE;
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
                LOG_WARN("sensor SM: verify failed (type=%u)", hdr->type);
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
        LOG_INFO("sensor SM: shutdown commanded");
        g_running          = 0;
        lctx->loop_running = 0;
        break;
    case SVC_MSG_RELOAD_CONFIG:
        LOG_INFO("sensor SM: config reload commanded");
        g_reload = 1;
        break;
    default:
        LOG_DEBUG("sensor SM: unhandled type %u", hdr->type);
        break;
    }
}

/* __ io_uring callbacks ___________________________________________________ */

static int on_sm_data(int fd, int result, void *user_data,
                      void *buf, size_t len)
{
    sensor_loop_ctx_t *lctx = (sensor_loop_ctx_t *)user_data;
    (void)fd;
    if (result <= 0) {
        LOG_WARN("sensor SM socket: result=%d -- reconnecting", result);
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
    if (service_ipc_ping(lctx->ipc) != SVC_OK) {
        LOG_WARN("sensor SM ping failed -- reconnecting");
        lctx->need_reconnect = 1;
        lctx->loop_running   = 0;
        return -1;
    }
    return 0;
}

/* __ reconnect ___________________________________________________________ */

static int reconnect_sm(svc_ipc_t *ipc, io_uring_loop_t *loop, int old_fd)
{
    io_loop_unregister_fd(loop, old_fd);
    if (old_fd >= 0) service_ipc_disconnect(ipc);
    LOG_WARN("sensor: reconnecting to SM (backoff=%ds)",
             ipc->reconnect_backoff_sec);
    sleep((unsigned)ipc->reconnect_backoff_sec);
    ipc->reconnect_backoff_sec = (ipc->reconnect_backoff_sec * 2 >
                                   ipc->reconnect_backoff_max)
                                  ? ipc->reconnect_backoff_max
                                  : ipc->reconnect_backoff_sec * 2;
    if (service_ipc_connect(ipc) != SVC_OK) { LOG_WARN("sensor: SM reconnect failed"); return -1; }
    service_ipc_register(ipc, "", SERVICE_LAYER_VERSION_STR, getpid());
    ipc->reconnect_backoff_sec = 1;
    LOG_INFO("sensor: SM reconnected");
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
        lctx.loop_running   = 1;
        lctx.need_reconnect = 0;

        io_op_desc_t sm_op = {
            .fd = ipc->fd, .op_type = IO_OP_READ, .buf_size = SM_BUF_SIZE,
            .callback = on_sm_data, .user_data = &lctx, .timeout_ms = 0,
        };
        io_loop_register_op(loop, &sm_op);

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

        if (!g_running) break;
        if (lctx.need_reconnect)
            reconnect_sm(ipc, loop, ipc->fd);
    }

    LOG_INFO("sensor_service io_uring event loop exiting");
    io_loop_destroy(loop);
    return SVC_OK;
}
