/**
 * @file io_uring_loop.h
 * @brief High-performance async I/O event loop backed by Linux io_uring.
 *
 * Replaces the epoll-based loops that each service daemon formerly used.
 * Internally the library uses a kernel-side io_uring ring (queue_depth SQEs),
 * an eventfd for wakeups, and an optional memory_pool_t for I/O data buffers.
 *
 * ## Lifecycle
 * 1. io_loop_create()          — allocate + initialise the ring
 * 2. io_loop_register_op()     — register fds + callbacks (called 2-3 times)
 * 3. io_loop_run()             — block until *stop_flag becomes 0
 * 4. io_loop_destroy()         — drain + tear-down the ring
 *
 * ## Thread safety
 * An io_uring_loop_t instance is NOT thread-safe.  All calls must come from
 * the same thread that called io_loop_create().  io_loop_wakeup() is the
 * single function that may be called from a different thread or signal handler.
 *
 * ## Error codes
 * Functions return 0 on success, negative errno values on failure, or the
 * SVC_ERR_* codes from service_base.h where appropriate.
 */

#ifndef IO_URING_LOOP_H
#define IO_URING_LOOP_H

#include <stddef.h>
#include <signal.h>  /* sig_atomic_t */
#include "memory_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── operation type ───────────────────────────────────────────────────── */

/**
 * @brief Identifies which io_uring opcode backs a registered descriptor.
 *
 * IO_OP_READ:    submit IORING_OP_READ on fd; callback fires with data in buf.
 *                Use for stream fds where data is consumed via read() (e.g. SM
 *                socket, ALSA PCM raw device).
 * IO_OP_WRITE:   reserved; use io_loop_submit_write() for ad-hoc writes.
 * IO_OP_ACCEPT:  reserved for future IORING_OP_ACCEPT support on listener fds.
 * IO_OP_TIMEOUT: submit IORING_OP_TIMEOUT; fires every timeout_ms ms.
 *                Automatically re-armed while callback returns 0.  Set fd=-1.
 * IO_OP_CANCEL:  reserved — cancellation submitted internally via
 *                io_loop_submit_write() cancellation path.
 * IO_OP_POLL:    submit IORING_OP_POLL_ADD on fd; callback fires on POLLIN
 *                readiness.  buf is NULL; the caller queries the device via
 *                its HAL API.  Use for V4L2 video, GPIO sysfs, IIO event fds
 *                and any fd that must not be read directly.
 */
typedef enum {
    IO_OP_READ    = 0,
    IO_OP_WRITE   = 1,
    IO_OP_ACCEPT  = 2,  /**< Reserved – not yet implemented. */
    IO_OP_TIMEOUT = 3,
    IO_OP_CANCEL  = 4,  /**< Reserved – not yet implemented. */
    IO_OP_POLL    = 5,
} io_op_type_t;

/* ── completion callback ──────────────────────────────────────────────── */

/**
 * @brief Completion callback invoked by io_loop_run() when an SQE finishes.
 *
 * @param fd        File descriptor that produced the event (or -1 for timeout).
 * @param result    CQE result: bytes transferred (>0) or negative errno.
 * @param user_data Opaque pointer supplied in io_op_desc_t or
 *                  io_loop_submit_write().
 * @param buf       Pointer to the I/O data buffer (NULL for timeout events).
 * @param len       Number of valid bytes in @p buf (0 for timeout / error).
 *
 * @return  0  — success; for reads, the SQE is re-armed automatically.
 *          <0 — error; the SQE is NOT re-armed (used to signal disconnect).
 */
typedef int (*io_completion_cb_t)(int fd, int result, void *user_data,
                                  void *buf, size_t len);

/* ── operation descriptor ─────────────────────────────────────────────── */

/**
 * @brief Describes one logical I/O operation to be managed by the loop.
 *
 * Pass to io_loop_register_op() before calling io_loop_run().
 *
 * @note  buf_size is only meaningful for IO_OP_READ.
 * @note  timeout_ms is only meaningful for IO_OP_TIMEOUT; set fd to -1 for
 *        pure timeout ops (the loop will not attempt a read on it).
 */
typedef struct {
    int                fd;          /**< File descriptor (-1 for timeout-only)  */
    io_op_type_t       op_type;     /**< Which io_uring opcode to use            */
    size_t             buf_size;    /**< Read buffer size in bytes               */
    io_completion_cb_t callback;    /**< Invoked on SQE completion               */
    void              *user_data;   /**< Passed verbatim to callback             */
    unsigned int       timeout_ms;  /**< Milliseconds per interval (IO_OP_TIMEOUT) */
} io_op_desc_t;

/* ── loop configuration ───────────────────────────────────────────────── */

/**
 * @brief Configuration for io_loop_create().
 */
typedef struct {
    unsigned int    queue_depth;  /**< SQ/CQ depth; must be a power of 2 (≥8) */
    int             use_sqpoll;   /**< 1 = IORING_SETUP_SQPOLL (CPU-bound)      */
    memory_pool_t  *buf_pool;     /**< Optional pool for read buffers           */
    const char     *name;         /**< Human-readable name for logging          */
} io_loop_config_t;

/* ── opaque handle ────────────────────────────────────────────────────── */

/** @brief Opaque io_uring-backed event loop handle. */
typedef struct io_uring_loop io_uring_loop_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a new event loop.
 *
 * @param config  Pointer to a filled io_loop_config_t; not stored after return.
 * @return        New loop handle, or NULL on allocation / io_uring setup failure.
 */
io_uring_loop_t *io_loop_create(const io_loop_config_t *config);

/**
 * @brief Drain in-flight requests and free all resources.
 *
 * Must be called from the same thread that called io_loop_create().
 * It is safe to call with NULL.
 *
 * @param loop  Loop to destroy.
 */
void io_loop_destroy(io_uring_loop_t *loop);

/**
 * @brief Register an I/O operation to be multiplexed by the loop.
 *
 * Operations are submitted when io_loop_run() is first entered.  May be
 * called again between successive io_loop_run() calls (e.g. after reconnect).
 *
 * @param loop  Event loop.
 * @param desc  Descriptor to register (copied internally).
 * @return      Non-negative slot index on success, -1 on overflow.
 */
int io_loop_register_op(io_uring_loop_t *loop, const io_op_desc_t *desc);

/**
 * @brief Mark the registered operation for @p fd as inactive.
 *
 * The slot is freed for reuse.  Any in-flight SQE for that fd will still
 * complete but the callback will not be called.
 *
 * @param loop  Event loop.
 * @param fd    File descriptor to deregister.
 * @return      0 if found and cleared, -1 if not found.
 */
int io_loop_unregister_fd(io_uring_loop_t *loop, int fd);

/**
 * @brief Run the I/O event loop until *stop_flag becomes 0.
 *
 * Submits one SQE per registered active operation, then enters a
 * wait/dispatch cycle.  Timeout ops are re-armed every timeout_ms ms.
 * Read ops are re-armed each time their callback returns 0.
 *
 * @param loop       Event loop.
 * @param stop_flag  Pointer to a flag checked at least once per second.
 *                   Set to 0 to cause io_loop_run() to return.
 * @return           SVC_OK (0) or SVC_ERR_GENERIC on internal error.
 */
int io_loop_run(io_uring_loop_t *loop, volatile sig_atomic_t *stop_flag);

/**
 * @brief Submit an asynchronous write and invoke @p cb on completion.
 *
 * The @p buf contents are copied into an internal buffer; the caller does
 * not need to keep @p buf live after this call returns.
 *
 * @param loop      Event loop.
 * @param fd        Destination file descriptor.
 * @param buf       Data to write.
 * @param len       Number of bytes.
 * @param cb        Completion callback (may be NULL).
 * @param user_data Passed verbatim to @p cb.
 * @return          0 on successful SQE submission, negative on error.
 */
int io_loop_submit_write(io_uring_loop_t *loop, int fd,
                         const void *buf, size_t len,
                         io_completion_cb_t cb, void *user_data);

/**
 * @brief Cause io_loop_run() to return after processing the current CQE.
 *
 * Writes a non-blocking value to the internal wakeup eventfd so that a
 * pending io_uring_wait_cqe() returns.  Safe to call from a different
 * thread or a signal handler.
 *
 * @param loop  Event loop.
 * @return      0 on success, -1 on write failure.
 */
int io_loop_wakeup(io_uring_loop_t *loop);

/**
 * @brief Reset the registered op table so the loop can be re-used.
 *
 * Clears all registered op descriptors (sets op_count = 0).  Must only be
 * called when io_loop_run() is NOT executing (i.e. between loop iterations
 * in the outer while(g_running) reconnect loop).  Any previously in-flight
 * SQEs will complete and their CQEs will be drained silently at the next
 * io_loop_run() entry.
 *
 * Typical usage after reconnect:
 * @code
 *   io_loop_run(loop, &lctx.loop_running);   // exits on disconnect
 *   io_loop_clear_ops(loop);                 // reset
 *   reconnect_sm(ipc, loop, old_fd);         // new fd
 *   io_loop_register_op(loop, &sm_op);       // re-register with new fd
 *   io_loop_run(loop, &lctx.loop_running);   // re-enter
 * @endcode
 *
 * @param loop  Event loop (NULL is a no-op).
 */
void io_loop_clear_ops(io_uring_loop_t *loop);

#ifdef __cplusplus
}
#endif

#endif /* IO_URING_LOOP_H */
