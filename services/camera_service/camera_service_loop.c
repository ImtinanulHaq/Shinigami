/**
 * @file camera_service_loop.c
 * @brief Camera service epoll event loop.
 *
 * Monitors two file descriptors:
 *   - ipc->fd:  SM socket — health check / shutdown / reload commands
 *   - dev->fd:  V4L2 fd  — frame ready notifications
 *
 * On each HAL fd event: dequeue a V4L2 frame, log sequence number, re-enqueue.
 * A circular frame queue of configurable depth is maintained for IPC clients.
 *
 * All SM messages are verified with verify_check_message() before processing.
 */

#define _GNU_SOURCE
#include "camera_service_loop.h"
#include "camera_service_hal.h"
#include "../common/service_base.h"
#include "../common/service_ipc.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "../../dev/hal/interface/hal_interface.h"
#include "../../dev/hal/layers/camera/camera_hal.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/security/verify/verify.h"

/* ── constants ────────────────────────────────────────────────────────── */

#define EPOLL_TIMEOUT_MS   1000
#define MAX_EPOLL_EVENTS   8
#define FRAME_QUEUE_DEPTH  8      /* circular frame queue depth              */
#define CAPTURE_TIMEOUT_MS 100

/* ── simple circular frame queue ─────────────────────────────────────── */

typedef struct {
    camera_frame_t frames[FRAME_QUEUE_DEPTH];
    int            head, tail, count;
} frame_queue_t;

static void fq_push(frame_queue_t *fq, const camera_frame_t *f)
{
    fq->frames[fq->tail] = *f;
    fq->tail = (fq->tail + 1) % FRAME_QUEUE_DEPTH;
    if (fq->count < FRAME_QUEUE_DEPTH) fq->count++;
    else fq->head = (fq->head + 1) % FRAME_QUEUE_DEPTH; /* overwrite oldest */
}

/* ── helpers ──────────────────────────────────────────────────────────── */

static time_t mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* ── SM message dispatcher ────────────────────────────────────────────── */

static int handle_sm_message(svc_ipc_t *ipc, camera_service_ctx_t *ctx,
                              time_t start_time)
{
    sm_hdr_t hdr;
    uint8_t  payload[SM_MAX_PAYLOAD_SIZE];

    ssize_t n = recv(ipc->fd, &hdr, sizeof(hdr), MSG_WAITALL);
    if (n <= 0) return SVC_ERR_IPC;
    if (hdr.magic != SM_PROTOCOL_MAGIC) return SVC_OK;

    size_t plen = (hdr.length < SM_MAX_PAYLOAD_SIZE) ? hdr.length
                                                      : SM_MAX_PAYLOAD_SIZE;
    if (plen > 0 && recv(ipc->fd, payload, plen, MSG_WAITALL) != (ssize_t)plen)
        return SVC_ERR_IPC;

    /* Verify signature */
    if (ipc->verify_ctx) {
        verify_context_t *vctx = (verify_context_t *)ipc->verify_ctx;
        size_t blob_len = SM_HDR_HMAC_OFFSET + plen;
        uint8_t *blob = malloc(blob_len);
        if (blob) {
            memcpy(blob, &hdr, SM_HDR_HMAC_OFFSET);
            if (plen) memcpy(blob + SM_HDR_HMAC_OFFSET, payload, plen);
            message_auth_t auth;
            memcpy(auth.hmac, hdr.hmac, 32);
            auth.timestamp = hdr.timestamp;
            auth.nonce     = hdr.nonce;
            int vrc        = verify_check_message(vctx, blob, blob_len, &auth);
            free(blob);
            if (vrc != 0) {
                LOG_WARN("camera SM message: verify failed (type=%u)", hdr.type);
                ctx->base.error_count++;
                return SVC_OK;
            }
        }
    }

    switch (hdr.type) {
    case SVC_MSG_HEALTH_CHECK: {
        svc_health_status_t st;
        memset(&st, 0, sizeof(st));
        st.uptime_sec  = (uint32_t)(mono_now() - start_time);
        st.error_count = (uint32_t)ctx->base.error_count;
        st.hal_state   = ctx->hal_device
                         ? (uint8_t)((hw_device_t *)ctx->hal_device)->state
                         : 0;
        st.svc_state   = (uint8_t)ctx->base.state;
        service_ipc_send_health(ipc, &st);
        break;
    }
    case SVC_MSG_SHUTDOWN:
        LOG_INFO("SM commanded shutdown");
        g_running = 0;
        break;
    case SVC_MSG_RELOAD_CONFIG:
        LOG_INFO("SM commanded config reload");
        g_reload = 1;
        break;
    default:
        LOG_DEBUG("unhandled SM message type %u", hdr.type);
        break;
    }
    return SVC_OK;
}

/* ── HAL event handler ────────────────────────────────────────────────── */

static void handle_hal_event(camera_service_ctx_t *ctx, frame_queue_t *fq)
{
    if (!ctx->hal_device) return;
    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    camera_frame_t frame;
    int rc = camera_hal_capture_frame(dev, &frame, CAPTURE_TIMEOUT_MS);
    if (rc != HAL_SUCCESS) {
        LOG_DEBUG("capture_frame returned %d", rc);
        ctx->base.error_count++;
        return;
    }

    LOG_DEBUG("frame seq=%u size=%zu", frame.sequence, frame.size);
    fq_push(fq, &frame);

    /* Return the buffer slot back to the driver */
    camera_hal_return_frame(dev, &frame);
}

/* ── reconnect ────────────────────────────────────────────────────────── */

static int reconnect_sm(svc_ipc_t *ipc, int epfd)
{
    if (ipc->fd >= 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, ipc->fd, NULL);
        service_ipc_disconnect(ipc);
    }
    LOG_WARN("reconnecting to SM (backoff=%ds)...", ipc->reconnect_backoff_sec);
    sleep((unsigned)ipc->reconnect_backoff_sec);
    ipc->reconnect_backoff_sec = (ipc->reconnect_backoff_sec * 2 >
                                   ipc->reconnect_backoff_max)
                                  ? ipc->reconnect_backoff_max
                                  : ipc->reconnect_backoff_sec * 2;

    if (service_ipc_connect(ipc) != SVC_OK) return -1;
    service_ipc_register(ipc, "", SERVICE_LAYER_VERSION_STR, getpid());

    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd = ipc->fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ipc->fd, &ev);
    ipc->reconnect_backoff_sec = 1;
    LOG_INFO("SM reconnected");
    return 0;
}

/* ── config reload ────────────────────────────────────────────────────── */

static void apply_config_reload(camera_service_ctx_t *ctx,
                                service_config_t *cfg)
{
    if (service_config_reload(cfg) != 0) {
        LOG_WARN("camera config reload failed");
        return;
    }
    /* Apply non-critical changes */
    uint32_t new_fps = service_config_get_uint32(cfg, "hardware", "fps",
                           ctx->fps);
    if (new_fps != ctx->fps && new_fps > 0 && new_fps <= 240) {
        LOG_INFO("config reload: fps %u → %u", ctx->fps, new_fps);
        ctx->fps = new_fps;
    }
    LOG_INFO("camera config reload applied");
}

/* ── main event loop ──────────────────────────────────────────────────── */

int camera_service_loop_run(camera_service_ctx_t *ctx, svc_ipc_t *ipc,
                            service_config_t *cfg)
{
    if (!ctx || !ipc) return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;
    frame_queue_t fq;
    memset(&fq, 0, sizeof(fq));

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        LOG_ERR("epoll_create1: %s", strerror(errno));
        return SVC_ERR_GENERIC;
    }

    struct epoll_event ev;

    ev.events  = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd = ipc->fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ipc->fd, &ev);

    if (dev && dev->fd >= 0) {
        ev.events  = EPOLLIN | EPOLLERR;
        ev.data.fd = dev->fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, dev->fd, &ev);
    }

    time_t start_time = mono_now();
    struct epoll_event events[MAX_EPOLL_EVENTS];
    LOG_INFO("camera_service event loop started");

    while (g_running) {
        if (g_reload) {
            apply_config_reload(ctx, cfg);
            g_reload = 0;
        }

        int nfds = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, EPOLL_TIMEOUT_MS);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("epoll_wait: %s", strerror(errno));
            break;
        }
        if (nfds == 0) {
            if (service_ipc_ping(ipc) != SVC_OK) {
                LOG_WARN("SM ping timeout — reconnecting");
                reconnect_sm(ipc, epfd);
            }
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd == ipc->fd) {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    reconnect_sm(ipc, epfd); break;
                }
                if (events[i].events & EPOLLIN) {
                    if (handle_sm_message(ipc, ctx, start_time) == SVC_ERR_IPC)
                        reconnect_sm(ipc, epfd);
                }
            } else if (dev && fd == dev->fd) {
                if (events[i].events & EPOLLIN)
                    handle_hal_event(ctx, &fq);
            }
        }
    }

    if (dev && dev->fd >= 0)
        epoll_ctl(epfd, EPOLL_CTL_DEL, dev->fd, NULL);
    close(epfd);
    LOG_INFO("camera_service event loop exiting");
    return SVC_OK;
}
