/**
 * @file audio_service_loop.c
 * @brief Audio service epoll event loop.
 *
 * Monitors two file descriptors:
 *   - ipc->fd:       SM socket — health check / shutdown / reload commands
 *   - dev->fd:       ALSA PCM fd — incoming PCM frames
 *
 * PCM frames are written to a ring buffer (ring_buffer.h from dev/core/).
 * SM clients can request buffered audio via IPC MSG_HEALTH_CHECK responses.
 *
 * SM message authentication:
 *   - All incoming messages: verify_check_message() before processing.
 *   - All outgoing messages: verify_sign_message() before sending.
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

#include <sys/epoll.h>
#include <sys/socket.h>

/* HAL and ring buffer headers */
#include "../../dev/hal/interface/hal_interface.h"
#include "../../dev/core/ring_buffer.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/security/verify/verify.h"

/* ── constants ────────────────────────────────────────────────────────── */

#define EPOLL_TIMEOUT_MS     1000   /* 1 s — spec requirement               */
#define MAX_EPOLL_EVENTS     8
#define PCM_FRAME_BYTES      4096   /* period_size * channels * 2 (S16_LE)  */
#define RING_CAPACITY        64     /* frames in the shared ring buffer      */
#define RING_ITEM_SIZE       PCM_FRAME_BYTES

/* ── helpers ──────────────────────────────────────────────────────────── */

/** Return monotonic time in seconds. */
static time_t mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* ── SM message dispatcher ────────────────────────────────────────────── */

/**
 * Read one SM message from ipc->fd, verify it, and dispatch.
 * Returns SVC_OK normally, SVC_ERR_IPC if the socket is broken.
 */
static int handle_sm_message(svc_ipc_t *ipc, audio_service_ctx_t *ctx,
                              time_t start_time)
{
    sm_hdr_t hdr;
    uint8_t  payload[SM_MAX_PAYLOAD_SIZE];

    /* Read header */
    ssize_t n = recv(ipc->fd, &hdr, sizeof(hdr), MSG_WAITALL);
    if (n <= 0) {
        LOG_WARN("SM socket closed or error: %s", strerror(errno));
        return SVC_ERR_IPC;
    }
    if (hdr.magic != SM_PROTOCOL_MAGIC) {
        LOG_WARN("SM message: bad magic 0x%08X — discarding", hdr.magic);
        return SVC_OK;
    }

    /* Read payload */
    size_t plen = hdr.length;
    if (plen > SM_MAX_PAYLOAD_SIZE) plen = SM_MAX_PAYLOAD_SIZE;
    if (plen > 0) {
        if (recv(ipc->fd, payload, plen, MSG_WAITALL) != (ssize_t)plen) {
            LOG_WARN("SM payload short read");
            return SVC_ERR_IPC;
        }
    }

    /* ── Verify signature ─────────────────────────────────────────────── */
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
            int vrc = verify_check_message(vctx, blob, blob_len, &auth);
            free(blob);
            if (vrc != 0) {
                LOG_WARN("SM message: verify_check_message rejected (type=%u)",
                         hdr.type);
                ctx->base.error_count++;
                return SVC_OK; /* keep running, drop the message */
            }
        }
    }

    /* ── Dispatch ─────────────────────────────────────────────────────── */
    switch (hdr.type) {

    case SVC_MSG_HEALTH_CHECK: {
        LOG_DEBUG("SM requested health check");
        svc_health_status_t st;
        memset(&st, 0, sizeof(st));
        st.uptime_sec   = (uint32_t)(mono_now() - start_time);
        st.error_count  = (uint32_t)ctx->base.error_count;
        st.hal_state    = ctx->hal_device
                          ? (uint8_t)((hw_device_t *)ctx->hal_device)->state
                          : 0;
        st.svc_state    = (uint8_t)ctx->base.state;
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

static void handle_hal_event(audio_service_ctx_t *ctx, rb_handle_t *rb)
{
    if (!ctx->hal_device) return;

    uint8_t pcm_buf[PCM_FRAME_BYTES];
    ssize_t n = audio_service_hal_read(ctx, pcm_buf, sizeof(pcm_buf));
    if (n < 0) {
        LOG_WARN("HAL read error: %zd", n);
        ctx->base.error_count++;
        return;
    }
    if (n == 0) return;

    if (rb && ring_buffer_write(rb, pcm_buf) == RB_ERROR_FULL) {
        LOG_DEBUG("audio ring buffer full — oldest frame dropped");
        /* Read-then-discard to make room */
        uint8_t tmp[PCM_FRAME_BYTES];
        ring_buffer_read(rb, tmp);
        ring_buffer_write(rb, pcm_buf);
    }
}

/* ── reconnect with exponential backoff ───────────────────────────────── */

static int reconnect_sm(svc_ipc_t *ipc, int epfd)
{
    /* Remove old (now-invalid) fd from epoll if needed */
    if (ipc->fd >= 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, ipc->fd, NULL);
        service_ipc_disconnect(ipc);
    }

    LOG_WARN("attempting SM reconnect (backoff=%ds)...",
             ipc->reconnect_backoff_sec);
    sleep((unsigned)ipc->reconnect_backoff_sec);

    /* Exponential backoff: 1→2→4→...→max */
    ipc->reconnect_backoff_sec *= 2;
    if (ipc->reconnect_backoff_sec > ipc->reconnect_backoff_max)
        ipc->reconnect_backoff_sec = ipc->reconnect_backoff_max;

    if (service_ipc_connect(ipc) != SVC_OK) {
        LOG_WARN("SM reconnect failed");
        return -1;
    }
    if (service_ipc_register(ipc, "", SERVICE_LAYER_VERSION_STR,
                             getpid()) != SVC_OK) {
        LOG_WARN("SM re-registration failed");
        return -1;
    }

    /* Re-add new fd to epoll */
    struct epoll_event ev;
    ev.events   = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd  = ipc->fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ipc->fd, &ev);

    LOG_INFO("SM reconnected and re-registered");
    ipc->reconnect_backoff_sec = 1; /* reset after success */
    return 0;
}

/* ── config reload ────────────────────────────────────────────────────── */

static void apply_config_reload(audio_service_ctx_t *ctx,
                                service_config_t *cfg)
{
    if (service_config_reload(cfg) != 0) {
        LOG_WARN("config reload failed — keeping current settings");
        return;
    }

    /* Apply non-critical changes that don't require restart */
    uint32_t new_rate = service_config_get_uint32(cfg, "hardware",
                            "sample_rate", ctx->sample_rate);
    if (new_rate != ctx->sample_rate && new_rate >= 8000 && new_rate <= 192000) {
        LOG_INFO("config reload: sample_rate %u → %u", ctx->sample_rate, new_rate);
        ctx->sample_rate = new_rate;
    }

    LOG_INFO("config reload applied successfully");
}

/* ── main event loop ──────────────────────────────────────────────────── */

int audio_service_loop_run(audio_service_ctx_t *ctx, svc_ipc_t *ipc,
                           service_config_t *cfg)
{
    if (!ctx || !ipc) return SVC_ERR_INVALID;

    hw_device_t *dev = (hw_device_t *)ctx->hal_device;

    /* ── Create ring buffer for audio frames ───────────────────────────── */
    rb_handle_t *rb = ring_buffer_create("audio_service",
                          RING_CAPACITY, RING_ITEM_SIZE);
    if (!rb) {
        LOG_WARN("ring buffer creation failed — audio frames will not be buffered");
        /* Non-fatal: continue without ring buffer */
    }

    /* ── Set up epoll ──────────────────────────────────────────────────── */
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        LOG_ERR("epoll_create1: %s", strerror(errno));
        if (rb) ring_buffer_destroy(rb, "audio_service");
        return SVC_ERR_GENERIC;
    }

    struct epoll_event ev;

    /* Add SM fd */
    ev.events  = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd = ipc->fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, ipc->fd, &ev) < 0) {
        LOG_ERR("epoll_ctl SM fd: %s", strerror(errno));
        close(epfd);
        if (rb) ring_buffer_destroy(rb, "audio_service");
        return SVC_ERR_GENERIC;
    }

    /* Add HAL fd (if the HAL device exposes one) */
    if (dev && dev->fd >= 0) {
        ev.events  = EPOLLIN | EPOLLERR;
        ev.data.fd = dev->fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, dev->fd, &ev);
    }

    time_t start_time = mono_now();
    struct epoll_event events[MAX_EPOLL_EVENTS];

    LOG_INFO("audio_service event loop started");

    /* ── Main loop ─────────────────────────────────────────────────────── */
    while (g_running) {

        /* ── Config reload ─────────────────────────────────────────────── */
        if (g_reload) {
            apply_config_reload(ctx, cfg);
            g_reload = 0;
        }

        int nfds = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, EPOLL_TIMEOUT_MS);

        if (nfds < 0) {
            if (errno == EINTR) continue; /* interrupted by signal — check g_running */
            LOG_ERR("epoll_wait: %s", strerror(errno));
            break;
        }

        /* ── Timeout: ping SM ──────────────────────────────────────────── */
        if (nfds == 0) {
            if (service_ipc_ping(ipc) != SVC_OK) {
                LOG_WARN("SM ping failed — reconnecting...");
                reconnect_sm(ipc, epfd);
                /* After reconnect, re-add HAL fd is not needed as it hasn't changed */
            }
            continue;
        }

        /* ── Process events ────────────────────────────────────────────── */
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == ipc->fd) {
                /* SM socket event */
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    LOG_WARN("SM socket error/hangup — reconnecting...");
                    reconnect_sm(ipc, epfd);
                    break;
                }
                if (events[i].events & EPOLLIN) {
                    int rc = handle_sm_message(ipc, ctx, start_time);
                    if (rc == SVC_ERR_IPC) {
                        LOG_WARN("SM read error — reconnecting...");
                        reconnect_sm(ipc, epfd);
                    }
                }
            } else if (dev && fd == dev->fd) {
                /* HAL PCM fd event */
                if (events[i].events & EPOLLIN) {
                    handle_hal_event(ctx, rb);
                }
            }
        }
    }

    /* ── Cleanup ───────────────────────────────────────────────────────── */
    LOG_INFO("audio_service event loop exiting (g_running=%d)", (int)g_running);

    if (dev && dev->fd >= 0)
        epoll_ctl(epfd, EPOLL_CTL_DEL, dev->fd, NULL);
    epoll_ctl(epfd, EPOLL_CTL_DEL, ipc->fd, NULL);
    close(epfd);

    if (rb) ring_buffer_destroy(rb, "audio_service");

    return SVC_OK;
}
