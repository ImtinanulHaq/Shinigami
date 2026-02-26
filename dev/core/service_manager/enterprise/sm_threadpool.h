#ifndef SM_THREADPOOL_H
#define SM_THREADPOOL_H

/*
 * sm_threadpool.h - Managed thread pool for client request processing
 *
 * Provides a fixed number of worker threads that process client connections
 * from an epoll-managed queue. Prevents thread explosion under heavy load and
 * enables graceful degradation when queue is full.
 */

#include <stddef.h>

typedef void (*sm_task_handler_t)(int client_fd, void* context);

typedef struct {
    int pool_size;           /* Number of worker threads */
    int queue_max_size;      /* Maximum pending tasks */
    int current_queue_size;  /* Current pending tasks */
    int tasks_processed;     /* Total tasks completed */
    int tasks_dropped;       /* Tasks dropped due to queue full */
} sm_threadpool_stats_t;

/*
 * sm_threadpool_init() - Initialize thread pool with specified number of workers
 *
 * Returns 0 on success, -1 on failure (will log error internally)
 * Failures are typically: memory allocation, pthread creation, mutex init
 */
int sm_threadpool_init(int worker_count, int queue_max_size);

/*
 * sm_threadpool_submit() - Submit a client connection for processing
 *
 * Returns 0 on success (task queued)
 * Returns -1 if queue is full (task dropped, connection should be closed)
 * Returns -2 if threadpool not initialized
 *
 * The handler will be called in a worker thread context with the client fd.
 * Handler is responsible for: reading request, writing response, closing fd.
 */
int sm_threadpool_submit(int client_fd, sm_task_handler_t handler, void* context);

/*
 * sm_threadpool_get_stats() - Get current thread pool statistics
 *
 * Returns statistics about pool utilization, task counts, and dropped tasks
 * Useful for monitoring and alerting on queue saturation
 */
sm_threadpool_stats_t sm_threadpool_get_stats(void);

/*
 * sm_threadpool_set_max_queue() - Dynamically adjust maximum queue size
 *
 * Returns 0 on success, -1 if new size < current queue size
 * Allows runtime adjustment of backpressure thresholds
 */
int sm_threadpool_set_max_queue(int new_size);

/*
 * sm_threadpool_shutdown() - Gracefully shutdown thread pool
 *
 * Signals all workers to stop after processing current task
 * Waits up to timeout_sec for all threads to complete
 * Cancels any remaining queued tasks
 */
void sm_threadpool_shutdown(int timeout_sec);

/*
 * sm_threadpool_force_shutdown() - Forcefully terminate thread pool
 *
 * Immediately cancels all threads and queued tasks
 * Use only in emergency situations (system shutdown, fatal error)
 */
void sm_threadpool_force_shutdown(void);

#endif /* SM_THREADPOOL_H */
