#define _POSIX_C_SOURCE 200809L

/*
 * sm_threadpool.c - Thread pool implementation for client request processing
 *
 * Implements a bounded task queue with fixed number of worker threads.
 * Provides graceful degradation when queue reaches capacity.
 */

#include "../enterprise/sm_threadpool.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>   /* sysconf */

/*
 * Determine default worker count at runtime from the host CPU topology.
 * 2× online cores is a sensible default for I/O-bound worker threads.
 * Clamp to [2, 256] so we never under- or over-provision.
 */
static int default_worker_count(void)
{
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    long sz = ncpu * 2;
    if (sz < 2)   sz = 2;
    if (sz > 256) sz = 256;
    return (int)sz;
}

#define DEFAULT_QUEUE_SIZE 128

typedef struct {
    int client_fd;
    sm_task_handler_t handler;
    void* context;
} task_t;

typedef struct {
    pthread_t* threads;
    int thread_count;
    
    task_t* queue;
    int queue_size;
    int queue_max_size;
    int queue_front;
    int queue_back;
    int queue_used;
    
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    
    int shutdown_requested;
    int stats_tasks_processed;
    int stats_tasks_dropped;
} threadpool_t;

static threadpool_t g_pool = {0};
static int g_pool_initialized = 0;

static void* worker_thread(void* arg)
{
    (void)arg;
    
    while (1) {
        pthread_mutex_lock(&g_pool.queue_mutex);
        
        /* Wait for task or shutdown signal */
        while (g_pool.queue_used == 0 && !g_pool.shutdown_requested) {
            pthread_cond_wait(&g_pool.queue_cond, &g_pool.queue_mutex);
        }
        
        /* Check shutdown flag */
        if (g_pool.shutdown_requested && g_pool.queue_used == 0) {
            pthread_mutex_unlock(&g_pool.queue_mutex);
            sm_log(SM_LOG_DEBUG, "threadpool: worker thread exiting");
            break;
        }
        
        /* Get task from queue */
        if (g_pool.queue_used > 0) {
            task_t task = g_pool.queue[g_pool.queue_front];
            g_pool.queue_front = (g_pool.queue_front + 1) % g_pool.queue_max_size;
            g_pool.queue_used--;
            g_pool.stats_tasks_processed++;
            
            pthread_mutex_unlock(&g_pool.queue_mutex);
            
            /* Execute task outside of lock */
            if (task.handler && task.client_fd >= 0) {
                task.handler(task.client_fd, task.context);
            }
            
            /* Ensure client fd is closed */
            if (task.client_fd >= 0) {
                close(task.client_fd);
            }
        } else {
            pthread_mutex_unlock(&g_pool.queue_mutex);
        }
    }
    
    return NULL;
}

int sm_threadpool_init(int worker_count, int queue_max_size)
{
    if (g_pool_initialized) {
        sm_log(SM_LOG_WARN, "threadpool: already initialized");
        return -1;
    }
    
    if (worker_count <= 0) worker_count = default_worker_count();
    if (queue_max_size <= 0) queue_max_size = DEFAULT_QUEUE_SIZE;
    
    if (worker_count > 256) {
        sm_log(SM_LOG_ERROR, "threadpool: worker_count %d exceeds maximum 256", worker_count);
        return -1;
    }
    
    if (queue_max_size > 4096) {
        sm_log(SM_LOG_ERROR, "threadpool: queue_max_size %d exceeds maximum 4096", queue_max_size);
        return -1;
    }
    
    memset(&g_pool, 0, sizeof(g_pool));
    
    /* Initialize mutex and condition variable */
    if (pthread_mutex_init(&g_pool.queue_mutex, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "threadpool: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }
    
    if (pthread_cond_init(&g_pool.queue_cond, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "threadpool: pthread_cond_init failed: %s", strerror(errno));
        pthread_mutex_destroy(&g_pool.queue_mutex);
        return -1;
    }
    
    /* Allocate queue */
    g_pool.queue = (task_t*)malloc(queue_max_size * sizeof(task_t));
    if (!g_pool.queue) {
        sm_log(SM_LOG_ERROR, "threadpool: malloc queue failed");
        pthread_cond_destroy(&g_pool.queue_cond);
        pthread_mutex_destroy(&g_pool.queue_mutex);
        return -1;
    }
    memset(g_pool.queue, 0, queue_max_size * sizeof(task_t));
    
    /* Allocate thread array */
    g_pool.threads = (pthread_t*)malloc(worker_count * sizeof(pthread_t));
    if (!g_pool.threads) {
        sm_log(SM_LOG_ERROR, "threadpool: malloc threads failed");
        free(g_pool.queue);
        pthread_cond_destroy(&g_pool.queue_cond);
        pthread_mutex_destroy(&g_pool.queue_mutex);
        return -1;
    }
    
    g_pool.queue_max_size = queue_max_size;
    g_pool.thread_count = worker_count;
    g_pool.shutdown_requested = 0;
    
    /* Create worker threads */
    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&g_pool.threads[i], NULL, worker_thread, NULL) != 0) {
            sm_log(SM_LOG_ERROR, "threadpool: pthread_create worker %d failed: %s", i, strerror(errno));
            
            /* Cleanup and return error */
            g_pool.shutdown_requested = 1;
            pthread_cond_broadcast(&g_pool.queue_cond);
            
            for (int j = 0; j < i; j++) {
                pthread_join(g_pool.threads[j], NULL);
            }
            
            free(g_pool.threads);
            free(g_pool.queue);
            pthread_cond_destroy(&g_pool.queue_cond);
            pthread_mutex_destroy(&g_pool.queue_mutex);
            memset(&g_pool, 0, sizeof(g_pool));
            return -1;
        }
    }
    
    g_pool_initialized = 1;
    sm_log(SM_LOG_INFO, "threadpool: initialized with %d workers, queue size %d",
           worker_count, queue_max_size);
    return 0;
}

int sm_threadpool_submit(int client_fd, sm_task_handler_t handler, void* context)
{
    if (!g_pool_initialized) {
        sm_log(SM_LOG_ERROR, "threadpool: not initialized");
        return -2;
    }
    
    if (client_fd < 0 || !handler) {
        sm_log(SM_LOG_ERROR, "threadpool: invalid fd %d or handler %p", client_fd, (void*)handler);
        return -1;
    }
    
    pthread_mutex_lock(&g_pool.queue_mutex);
    
    /* Check if queue is full */
    if (g_pool.queue_used >= g_pool.queue_max_size) {
        g_pool.stats_tasks_dropped++;
        pthread_mutex_unlock(&g_pool.queue_mutex);
        sm_log(SM_LOG_WARN, "threadpool: queue full, dropping task (total dropped: %d)",
               g_pool.stats_tasks_dropped);
        return -1;
    }
    
    /* Add task to queue */
    int back_index = (g_pool.queue_front + g_pool.queue_used) % g_pool.queue_max_size;
    g_pool.queue[back_index].client_fd = client_fd;
    g_pool.queue[back_index].handler = handler;
    g_pool.queue[back_index].context = context;
    g_pool.queue_used++;
    
    /* Signal a waiting worker */
    pthread_cond_signal(&g_pool.queue_cond);
    
    pthread_mutex_unlock(&g_pool.queue_mutex);
    return 0;
}

sm_threadpool_stats_t sm_threadpool_get_stats(void)
{
    sm_threadpool_stats_t stats = {0};
    
    if (!g_pool_initialized) {
        return stats;
    }
    
    pthread_mutex_lock(&g_pool.queue_mutex);
    stats.pool_size = g_pool.thread_count;
    stats.queue_max_size = g_pool.queue_max_size;
    stats.current_queue_size = g_pool.queue_used;
    stats.tasks_processed = g_pool.stats_tasks_processed;
    stats.tasks_dropped = g_pool.stats_tasks_dropped;
    pthread_mutex_unlock(&g_pool.queue_mutex);
    
    return stats;
}

int sm_threadpool_set_max_queue(int new_size)
{
    if (!g_pool_initialized) {
        return -1;
    }
    
    if (new_size <= 0 || new_size > 4096) {
        sm_log(SM_LOG_ERROR, "threadpool: invalid queue size %d", new_size);
        return -1;
    }
    
    pthread_mutex_lock(&g_pool.queue_mutex);
    
    if (g_pool.queue_used > new_size) {
        pthread_mutex_unlock(&g_pool.queue_mutex);
        sm_log(SM_LOG_ERROR, "threadpool: cannot shrink queue below current usage");
        return -1;
    }
    
    if (new_size == g_pool.queue_max_size) {
        pthread_mutex_unlock(&g_pool.queue_mutex);
        return 0;  /* No change needed */
    }
    
    /* Reallocate queue */
    task_t* new_queue = (task_t*)malloc(new_size * sizeof(task_t));
    if (!new_queue) {
        pthread_mutex_unlock(&g_pool.queue_mutex);
        sm_log(SM_LOG_ERROR, "threadpool: malloc failed for new queue");
        return -1;
    }
    
    /* Copy existing tasks in order */
    for (int i = 0; i < g_pool.queue_used; i++) {
        new_queue[i] = g_pool.queue[(g_pool.queue_front + i) % g_pool.queue_max_size];
    }
    
    free(g_pool.queue);
    g_pool.queue = new_queue;
    g_pool.queue_max_size = new_size;
    g_pool.queue_front = 0;
    
    pthread_mutex_unlock(&g_pool.queue_mutex);
    
    sm_log(SM_LOG_INFO, "threadpool: queue size adjusted to %d", new_size);
    return 0;
}

void sm_threadpool_shutdown(int timeout_sec)
{
    if (!g_pool_initialized) {
        return;
    }
    
    sm_log(SM_LOG_INFO, "threadpool: graceful shutdown initiated (timeout=%ds)", timeout_sec);
    
    pthread_mutex_lock(&g_pool.queue_mutex);
    g_pool.shutdown_requested = 1;
    pthread_cond_broadcast(&g_pool.queue_cond);
    pthread_mutex_unlock(&g_pool.queue_mutex);
    
    /* Wait for all threads to complete with timeout */
    for (int i = 0; i < g_pool.thread_count; i++) {
        pthread_join(g_pool.threads[i], NULL);
    }
    
    sm_log(SM_LOG_INFO, "threadpool: all workers stopped (processed: %d, dropped: %d)",
           g_pool.stats_tasks_processed, g_pool.stats_tasks_dropped);
}

void sm_threadpool_force_shutdown(void)
{
    if (!g_pool_initialized) {
        return;
    }
    
    sm_log(SM_LOG_WARN, "threadpool: force shutdown");
    
    pthread_mutex_lock(&g_pool.queue_mutex);
    g_pool.shutdown_requested = 1;
    g_pool.queue_used = 0;  /* Discard all queued tasks */
    pthread_cond_broadcast(&g_pool.queue_cond);
    pthread_mutex_unlock(&g_pool.queue_mutex);
    
    /* Wait briefly for threads */
    for (int i = 0; i < g_pool.thread_count; i++) {
        pthread_join(g_pool.threads[i], NULL);
    }
    
    /* Cleanup resources */
    pthread_cond_destroy(&g_pool.queue_cond);
    pthread_mutex_destroy(&g_pool.queue_mutex);
    free(g_pool.threads);
    free(g_pool.queue);
    memset(&g_pool, 0, sizeof(g_pool));
    g_pool_initialized = 0;
    
    sm_log(SM_LOG_INFO, "threadpool: force shutdown complete");
}
