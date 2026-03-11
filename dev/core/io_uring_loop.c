/**
 * @file io_uring_loop.c
 * @brief io_uring-backed async I/O event loop — implementation.
 *
 * Wakeup design
 * ─────────────
 * io_loop_wakeup() writes 1 to an eventfd.  That eventfd is permanently
 * registered in the ring as an IORING_OP_READ SQE (the "wakeup SQE").
 * When io_loop_wakeup() fires the write, the kernel completes the read
 * → a new CQE appears → io_uring_wait_cqe() returns immediately.
 *
 * NOTE: io_uring_register_eventfd() points notifications OUT (kernel notifies
 * an eventfd when new CQEs arrive).  That is the wrong direction for wakeup
 * and is NOT used here.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "io_uring_loop.h"

#include <liburing.h>
#include <sys/eventfd.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

/* ── SVC_OK / SVC_ERR_GENERIC ── pulled from service_base.h if available */
#ifndef SVC_OK
#  define SVC_OK          0
#  define SVC_ERR_GENERIC (-1)
#  define SVC_ERR_INVALID (-2)
#endif

/* ── constants ────────────────────────────────────────────────────────── */

#define MAX_OPS         64       /* max registered io_op_desc_t entries     */
#define WAKEUP_OP_IDX  (-1)     /* sentinel: CQE belongs to wakeup eventfd  */
#define WRITE_OP_IDX   (-2)     /* sentinel: CQE belongs to submit_write    */
#define LOG_TAG        "io_uring_loop"

/* ── internal pending-SQE descriptor ─────────────────────────────────── */

/**
 * One heap-allocated io_pending_t is set as user_data on every SQE.
 * The pointer is recovered from cqe->user_data on completion.
 */
typedef struct {
    int    op_idx;              /* WAKEUP_OP_IDX / WRITE_OP_IDX / ops[] index */
    void  *buf;                 /* I/O data buffer (pool or malloc)            */
    int    pool_buf;            /* 1 = buf came from memory_pool; 0 = malloc   */

    /* kept alive for the duration of the in-flight timeout SQE */
    struct __kernel_timespec ts;

    /* used only when op_idx == WRITE_OP_IDX */
    io_completion_cb_t write_cb;
    void              *write_user_data;
    int                write_fd;
} io_pending_t;

/* ── main structure ───────────────────────────────────────────────────── */

struct io_uring_loop {
    struct io_uring  ring;
    io_loop_config_t config;
    io_op_desc_t     ops[MAX_OPS];
    int              op_count;
    int              wakeup_fd;     /* eventfd for wakeup                    */
    memory_pool_t   *buf_pool;      /* borrowed; may be NULL                 */
    char             name[64];
};

/* ═══════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static inline int is_power_of_two(unsigned int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

/* ── buffer allocation helpers ── */

static void *alloc_buf(io_uring_loop_t *loop, size_t size, int *out_pool)
{
    *out_pool = 0;
    if (loop->buf_pool && size > 0) {
        void *p = memory_pool_alloc(loop->buf_pool);
        if (p) { *out_pool = 1; return p; }
    }
    return malloc(size);
}

static void free_buf(io_uring_loop_t *loop, void *buf, int pool_buf)
{
    if (!buf) return;
    if (pool_buf && loop->buf_pool)
        memory_pool_free(loop->buf_pool, buf);
    else
        free(buf);
}

/* ── submit helpers ── */

/** Submit an IORING_OP_READ for the wakeup eventfd (always reinvoked). */
static int submit_wakeup_read(io_uring_loop_t *loop)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->ring);
    if (!sqe) {
        syslog(LOG_WARNING, "%s: get_sqe failed (wakeup)", loop->name);
        return -ENOBUFS;
    }

    io_pending_t *p = malloc(sizeof(*p));
    if (!p) return -ENOMEM;

    memset(p, 0, sizeof(*p));
    p->op_idx    = WAKEUP_OP_IDX;
    p->buf       = malloc(sizeof(uint64_t));  /* eventfd value buffer */
    p->pool_buf  = 0;
    if (!p->buf) { free(p); return -ENOMEM; }

    io_uring_prep_read(sqe, loop->wakeup_fd,
                       p->buf, (unsigned)sizeof(uint64_t), 0);
    io_uring_sqe_set_data(sqe, p);
    return 0;
}

/** Submit an IORING_OP_READ for a registered READ op. */
static int submit_read_op(io_uring_loop_t *loop, int op_idx)
{
    io_op_desc_t *d = &loop->ops[op_idx];
    if (d->fd < 0 || d->buf_size == 0) return -EINVAL;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->ring);
    if (!sqe) {
        syslog(LOG_WARNING, "%s: get_sqe failed (read op_idx=%d)",
               loop->name, op_idx);
        return -ENOBUFS;
    }

    io_pending_t *p = malloc(sizeof(*p));
    if (!p) return -ENOMEM;

    memset(p, 0, sizeof(*p));
    p->op_idx = op_idx;
    p->buf    = alloc_buf(loop, d->buf_size, &p->pool_buf);
    if (!p->buf) { free(p); return -ENOMEM; }

    io_uring_prep_read(sqe, d->fd, p->buf, (unsigned)d->buf_size, 0);
    io_uring_sqe_set_data(sqe, p);
    return 0;
}

/** Submit an IORING_OP_POLL_ADD for a registered POLL op. */
static int submit_poll_op(io_uring_loop_t *loop, int op_idx)
{
    io_op_desc_t *d = &loop->ops[op_idx];
    if (d->fd < 0) return -EINVAL;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->ring);
    if (!sqe) {
        syslog(LOG_WARNING, "%s: get_sqe failed (poll op_idx=%d)",
               loop->name, op_idx);
        return -ENOBUFS;
    }

    io_pending_t *p = malloc(sizeof(*p));
    if (!p) return -ENOMEM;

    memset(p, 0, sizeof(*p));
    p->op_idx   = op_idx;
    p->buf      = NULL;
    p->pool_buf = 0;

    io_uring_prep_poll_add(sqe, d->fd, POLLIN);
    io_uring_sqe_set_data(sqe, p);
    return 0;
}

/** Submit an IORING_OP_TIMEOUT for a registered TIMEOUT op. */
static int submit_timeout_op(io_uring_loop_t *loop, int op_idx)
{
    io_op_desc_t *d = &loop->ops[op_idx];

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->ring);
    if (!sqe) {
        syslog(LOG_WARNING, "%s: get_sqe failed (timeout op_idx=%d)",
               loop->name, op_idx);
        return -ENOBUFS;
    }

    io_pending_t *p = malloc(sizeof(*p));
    if (!p) return -ENOMEM;

    memset(p, 0, sizeof(*p));
    p->op_idx       = op_idx;
    p->buf          = NULL;
    p->pool_buf     = 0;
    p->ts.tv_sec    = (long long)(d->timeout_ms / 1000);
    p->ts.tv_nsec   = (long long)((d->timeout_ms % 1000) * 1000000LL);

    /* count=0: no linked SQEs; flags=0: relative timeout */
    io_uring_prep_timeout(sqe, &p->ts, 0, 0);
    io_uring_sqe_set_data(sqe, p);
    return 0;
}

/* ── process one completed CQE ── */

static void process_cqe(io_uring_loop_t    *loop,
                         struct io_uring_cqe *cqe,
                         volatile sig_atomic_t *stop_flag)
{
    io_pending_t *p = (io_pending_t *)io_uring_cqe_get_data(cqe);
    int cqe_res = cqe->res;

    /* Consume the CQE before doing anything that might issue new SQEs */
    io_uring_cqe_seen(&loop->ring, cqe);

    if (!p) return;

    /* ── Wakeup ─────────────────────────────────────────────────────── */
    if (p->op_idx == WAKEUP_OP_IDX) {
        free(p->buf);
        free(p);
        /* Re-arm immediately unless we are shutting down */
        if (*stop_flag) {
            submit_wakeup_read(loop);
            io_uring_submit(&loop->ring);
        }
        return;
    }

    /* ── Ad-hoc write ───────────────────────────────────────────────── */
    if (p->op_idx == WRITE_OP_IDX) {
        if (p->write_cb)
            p->write_cb(p->write_fd, cqe_res, p->write_user_data,
                        p->buf, (size_t)(cqe_res > 0 ? cqe_res : 0));
        free_buf(loop, p->buf, p->pool_buf);
        free(p);
        return;
    }

    /* ── Registered op ──────────────────────────────────────────────── */
    int op_idx = p->op_idx;
    if (op_idx < 0 || op_idx >= loop->op_count) {
        free_buf(loop, p->buf, p->pool_buf);
        free(p);
        return;
    }

    io_op_desc_t *d = &loop->ops[op_idx];

    /* ── TIMEOUT ── */
    if (d->op_type == IO_OP_TIMEOUT) {
        /* cqe_res == -ETIME: fired normally;  other negative: cancelled */
        int cb_rc = 0;
        if (d->callback && (cqe_res == -ETIME || cqe_res == 0))
            cb_rc = d->callback(d->fd, cqe_res, d->user_data, NULL, 0);

        free(p);   /* ts was embedded in p; safe to free now */

        /* Re-arm if loop still alive and callback did not signal error */
        if (*stop_flag && cb_rc == 0 && d->fd >= -1 /* always true for timeout */) {
            submit_timeout_op(loop, op_idx);
            io_uring_submit(&loop->ring);
        }
        return;
    }

    /* ── READ ── */
    if (d->op_type == IO_OP_READ) {
        void *buf      = p->buf;
        int   pool_buf = p->pool_buf;
        free(p);

        int cb_rc = 0;

        if (cqe_res <= 0) {
            /* EOF or error — notify callback, do NOT re-arm */
            if (d->callback)
                cb_rc = d->callback(d->fd, cqe_res, d->user_data, NULL, 0);
        } else {
            if (d->callback)
                cb_rc = d->callback(d->fd, cqe_res, d->user_data,
                                    buf, (size_t)cqe_res);
        }

        free_buf(loop, buf, pool_buf);

        /* Re-arm only on success */
        if (*stop_flag && cqe_res > 0 && cb_rc == 0 && d->fd >= 0) {
            submit_read_op(loop, op_idx);
            io_uring_submit(&loop->ring);
        }
        return;
    }

    /* ── POLL ── */
    if (d->op_type == IO_OP_POLL) {
        free(p);   /* no buf for poll ops */

        int cb_rc = 0;
        /* cqe_res holds poll mask on success (POLLIN set), negative = error */
        if (d->callback)
            cb_rc = d->callback(d->fd, cqe_res, d->user_data, NULL, 0);

        /* Re-arm with a fresh POLL_ADD so the next edge wakes us again */
        if (*stop_flag && cqe_res > 0 && cb_rc == 0 && d->fd >= 0) {
            submit_poll_op(loop, op_idx);
            io_uring_submit(&loop->ring);
        }
        return;
    }

    /* Other op types (IO_OP_ACCEPT / IO_OP_CANCEL — reserved): log and free */
    syslog(LOG_WARNING, "%s: unhandled op_type=%d in process_cqe",
           loop->name, (int)d->op_type);
    free_buf(loop, p->buf, p->pool_buf);
    free(p);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

io_uring_loop_t *io_loop_create(const io_loop_config_t *config)
{
    if (!config) return NULL;

    /* queue_depth must be a power of two, minimum 8 */
    unsigned int depth = config->queue_depth ? config->queue_depth : 64u;
    if (!is_power_of_two(depth) || depth < 8) {
        syslog(LOG_ERR, LOG_TAG ": invalid queue_depth %u", depth);
        return NULL;
    }

    io_uring_loop_t *loop = calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->config   = *config;
    loop->buf_pool = config->buf_pool;
    snprintf(loop->name, sizeof(loop->name), "%s",
             config->name ? config->name : "io_uring_loop");

    /* ── initialise io_uring ── */
    unsigned int flags = 0;
    if (config->use_sqpoll)
        flags |= IORING_SETUP_SQPOLL;

    int rc = io_uring_queue_init(depth, &loop->ring, flags);
    if (rc < 0) {
        syslog(LOG_ERR, LOG_TAG " [%s]: io_uring_queue_init: %s",
               loop->name, strerror(-rc));
        free(loop);
        return NULL;
    }

    /* ── create wakeup eventfd ── */
    loop->wakeup_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (loop->wakeup_fd < 0) {
        syslog(LOG_ERR, LOG_TAG " [%s]: eventfd: %s",
               loop->name, strerror(errno));
        io_uring_queue_exit(&loop->ring);
        free(loop);
        return NULL;
    }

    syslog(LOG_INFO, LOG_TAG " [%s]: created (depth=%u sqpoll=%d)",
           loop->name, depth, config->use_sqpoll);
    return loop;
}

void io_loop_destroy(io_uring_loop_t *loop)
{
    if (!loop) return;
    syslog(LOG_INFO, LOG_TAG " [%s]: destroying", loop->name);

    /* Drain any CQEs already available so their io_pending_t are freed.
     * SQEs still in kernel flight (not yet completed) have their io_pending_t
     * freed here using a best-effort peek; the ring teardown handles the rest.
     */
    struct io_uring_cqe *cqe;
    while (io_uring_peek_cqe(&loop->ring, &cqe) == 0 && cqe) {
        io_pending_t *p = (io_pending_t *)io_uring_cqe_get_data(cqe);
        io_uring_cqe_seen(&loop->ring, cqe);
        if (p) {
            free_buf(loop, p->buf, p->pool_buf);
            free(p);
        }
    }

    io_uring_queue_exit(&loop->ring);
    close(loop->wakeup_fd);
    free(loop);
}

int io_loop_register_op(io_uring_loop_t *loop, const io_op_desc_t *desc)
{
    if (!loop || !desc) return -EINVAL;

    /* Reuse any slot whose fd was previously cleared */
    for (int i = 0; i < loop->op_count; i++) {
        if (loop->ops[i].fd < 0 && loop->ops[i].op_type == desc->op_type) {
            loop->ops[i] = *desc;
            return i;
        }
    }

    if (loop->op_count >= MAX_OPS) {
        syslog(LOG_ERR, LOG_TAG " [%s]: MAX_OPS (%d) exceeded",
               loop->name, MAX_OPS);
        return -ENOMEM;
    }

    loop->ops[loop->op_count] = *desc;
    return loop->op_count++;
}

int io_loop_unregister_fd(io_uring_loop_t *loop, int fd)
{
    if (!loop || fd < 0) return -EINVAL;
    for (int i = 0; i < loop->op_count; i++) {
        if (loop->ops[i].fd == fd) {
            loop->ops[i].fd = -1;
            return 0;
        }
    }
    return -1;
}

int io_loop_run(io_uring_loop_t *loop, volatile sig_atomic_t *stop_flag)
{
    if (!loop || !stop_flag) return SVC_ERR_INVALID;

    /* ── Submit initial SQEs for all active registered ops ── */
    for (int i = 0; i < loop->op_count; i++) {
        io_op_desc_t *d = &loop->ops[i];
        /* IO_OP_TIMEOUT uses fd=-1 intentionally; guard READ/POLL only */
        if (d->op_type == IO_OP_READ && d->fd >= 0) {
            submit_read_op(loop, i);
        } else if (d->op_type == IO_OP_POLL && d->fd >= 0) {
            submit_poll_op(loop, i);
        } else if (d->op_type == IO_OP_TIMEOUT) {
            submit_timeout_op(loop, i);  /* fd=-1 is valid for timeout */
        }
        /* IO_OP_WRITE / IO_OP_ACCEPT / IO_OP_CANCEL submitted on demand */
    }
    submit_wakeup_read(loop);

    int rc = io_uring_submit(&loop->ring);
    if (rc < 0) {
        syslog(LOG_ERR, LOG_TAG " [%s]: initial submit: %s",
               loop->name, strerror(-rc));
        return SVC_ERR_GENERIC;
    }

    syslog(LOG_INFO, LOG_TAG " [%s]: event loop started", loop->name);

    /* ── main dispatch loop ── */
    while (*stop_flag) {
        struct io_uring_cqe *cqe = NULL;
        int ret = io_uring_wait_cqe(&loop->ring, &cqe);

        if (ret < 0) {
            if (ret == -EINTR) continue;  /* signal; re-check stop_flag */
            syslog(LOG_ERR, LOG_TAG " [%s]: wait_cqe: %s",
                   loop->name, strerror(-ret));
            return SVC_ERR_GENERIC;
        }

        if (cqe)
            process_cqe(loop, cqe, stop_flag);

        /* Drain any additional CQEs that arrived in the same batch */
        while (*stop_flag) {
            cqe = NULL;
            ret = io_uring_peek_cqe(&loop->ring, &cqe);
            if (ret < 0 || !cqe) break;
            process_cqe(loop, cqe, stop_flag);
        }
    }

    syslog(LOG_INFO, LOG_TAG " [%s]: event loop stopped", loop->name);
    return SVC_OK;
}

int io_loop_submit_write(io_uring_loop_t *loop, int fd,
                         const void *buf, size_t len,
                         io_completion_cb_t cb, void *user_data)
{
    if (!loop || fd < 0 || !buf || len == 0) return -EINVAL;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->ring);
    if (!sqe) return -ENOBUFS;

    io_pending_t *p = malloc(sizeof(*p));
    if (!p) return -ENOMEM;

    memset(p, 0, sizeof(*p));
    p->op_idx         = WRITE_OP_IDX;
    p->write_cb       = cb;
    p->write_user_data = user_data;
    p->write_fd       = fd;
    p->pool_buf       = 0;

    /* Copy the payload — caller does not need to keep buf live */
    p->buf = malloc(len);
    if (!p->buf) { free(p); return -ENOMEM; }
    memcpy(p->buf, buf, len);

    io_uring_prep_write(sqe, fd, p->buf, (unsigned)len, 0);
    io_uring_sqe_set_data(sqe, p);

    int rc = io_uring_submit(&loop->ring);
    if (rc < 0) {
        free(p->buf);
        free(p);
        return rc;
    }
    return 0;
}

int io_loop_wakeup(io_uring_loop_t *loop)
{
    if (!loop) return -EINVAL;
    uint64_t val = 1ULL;
    ssize_t n = write(loop->wakeup_fd, &val, sizeof(val));
    return (n == (ssize_t)sizeof(val)) ? 0 : -1;
}

void io_loop_clear_ops(io_uring_loop_t *loop)
{
    if (!loop) return;
    /* Reset the op table so the loop can be re-entered with fresh registrations.
     * Call only when io_loop_run() is NOT executing (between outer iterations).
     */
    memset(loop->ops, 0, sizeof(loop->ops));
    loop->op_count = 0;
    syslog(LOG_DEBUG, LOG_TAG " [%s]: op table cleared", loop->name);
}
