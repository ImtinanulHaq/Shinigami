#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/*
 * sm_watchdog.c - Watchdog timer implementation
 *
 * System-level self-healing via /dev/watchdog
 */

#include "../enterprise/sm_watchdog.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <time.h>

#define WATCHDOG_DEVICE "/dev/watchdog"
#define WATCHDOG_KEEPALIVE_INTERVAL_MS 5000
#define WATCHDOG_DEFAULT_TIMEOUT 30

typedef struct {
    int watchdog_fd;
    pthread_t keepalive_thread;
    int thread_running;
    int initialized;
    
    uint64_t total_keepalives;
    uint64_t total_failures;
    time_t last_keepalive_time;
    
    pthread_mutex_t lock;
} watchdog_context_t;

static watchdog_context_t g_watchdog = {0};

static void* watchdog_thread_main(void* arg)
{
    (void)arg;
    
    sm_log(SM_LOG_INFO, "watchdog: keepalive thread started (interval=%dms)",
           WATCHDOG_KEEPALIVE_INTERVAL_MS);
    
    /* Attempt to set SCHED_FIFO priority for responsiveness */
    struct sched_param param;
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    if (param.sched_priority >= 0) {
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    }
    
    while (g_watchdog.thread_running) {
        usleep(WATCHDOG_KEEPALIVE_INTERVAL_MS * 1000);
        
        if (sm_watchdog_keepalive() != 0) {
            sm_log(SM_LOG_WARN, "watchdog: keepalive failed");
            pthread_mutex_lock(&g_watchdog.lock);
            g_watchdog.total_failures++;
            pthread_mutex_unlock(&g_watchdog.lock);
        }
    }
    
    sm_log(SM_LOG_INFO, "watchdog: keepalive thread exiting");
    return NULL;
}

int sm_watchdog_init(void)
{
    if (g_watchdog.initialized) {
        sm_log(SM_LOG_WARN, "watchdog: already initialized");
        return 0;
    }
    
    memset(&g_watchdog, 0, sizeof(g_watchdog));
    
    /* Initialize lock */
    if (pthread_mutex_init(&g_watchdog.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "watchdog: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }
    
    /* Try to open watchdog device */
    g_watchdog.watchdog_fd = open(WATCHDOG_DEVICE, O_WRONLY);
    
    if (g_watchdog.watchdog_fd < 0) {
        sm_log(SM_LOG_WARN, "watchdog: %s not available: %s (continuing without watchdog)",
               WATCHDOG_DEVICE, strerror(errno));
        g_watchdog.initialized = 1;
        return 0;  /* Non-fatal, continue without watchdog */
    }
    
    /* Set timeout via ioctl */
    int timeout = WATCHDOG_DEFAULT_TIMEOUT;
    if (ioctl(g_watchdog.watchdog_fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
        sm_log(SM_LOG_WARN, "watchdog: WDIOC_SETTIMEOUT failed: %s", strerror(errno));
    }
    
    /* Get actual timeout */
    int actual_timeout = 0;
    if (ioctl(g_watchdog.watchdog_fd, WDIOC_GETTIMEOUT, &actual_timeout) == 0) {
        sm_log(SM_LOG_INFO, "watchdog: timeout set to %d seconds", actual_timeout);
    }
    
    /* Start keepalive thread */
    g_watchdog.thread_running = 1;
    
    if (pthread_create(&g_watchdog.keepalive_thread, NULL, watchdog_thread_main, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "watchdog: pthread_create failed: %s", strerror(errno));
        close(g_watchdog.watchdog_fd);
        g_watchdog.watchdog_fd = -1;
        pthread_mutex_destroy(&g_watchdog.lock);
        return -1;
    }
    
    g_watchdog.initialized = 1;
    sm_log(SM_LOG_INFO, "watchdog: successfully initialized with %s", WATCHDOG_DEVICE);
    return 0;
}

int sm_watchdog_set_timeout(int timeout_seconds)
{
    if (!g_watchdog.initialized) {
        sm_log(SM_LOG_WARN, "watchdog: not initialized");
        return -1;
    }
    
    if (timeout_seconds <= 0 || timeout_seconds > 3600) {
        sm_log(SM_LOG_ERROR, "watchdog: invalid timeout %d seconds", timeout_seconds);
        return -1;
    }
    
    if (g_watchdog.watchdog_fd < 0) {
        return 0;  /* Watchdog not available, non-fatal */
    }
    
    int timeout = timeout_seconds;
    if (ioctl(g_watchdog.watchdog_fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
        sm_log(SM_LOG_ERROR, "watchdog: WDIOC_SETTIMEOUT failed: %s", strerror(errno));
        return -1;
    }
    
    sm_log(SM_LOG_INFO, "watchdog: timeout set to %d seconds", timeout);
    return 0;
}

int sm_watchdog_keepalive(void)
{
    if (!g_watchdog.initialized || g_watchdog.watchdog_fd < 0) {
        return 0;  /* Watchdog not available, non-fatal */
    }
    
    /* Write to watchdog to refresh timeout */
    if (write(g_watchdog.watchdog_fd, "1", 1) < 0) {
        sm_log(SM_LOG_ERROR, "watchdog: write failed: %s", strerror(errno));
        pthread_mutex_lock(&g_watchdog.lock);
        g_watchdog.total_failures++;
        pthread_mutex_unlock(&g_watchdog.lock);
        return -1;
    }
    
    pthread_mutex_lock(&g_watchdog.lock);
    g_watchdog.total_keepalives++;
    g_watchdog.last_keepalive_time = time(NULL);
    pthread_mutex_unlock(&g_watchdog.lock);
    
    return 0;
}

sm_watchdog_stats_t sm_watchdog_get_stats(void)
{
    sm_watchdog_stats_t stats = {0};
    
    if (!g_watchdog.initialized) {
        return stats;
    }
    
    pthread_mutex_lock(&g_watchdog.lock);
    stats.total_keepalives = g_watchdog.total_keepalives;
    stats.total_failures = g_watchdog.total_failures;
    stats.last_keepalive_time = g_watchdog.last_keepalive_time;
    stats.watchdog_fd = g_watchdog.watchdog_fd;
    stats.is_running = g_watchdog.thread_running;
    pthread_mutex_unlock(&g_watchdog.lock);
    
    return stats;
}

int sm_watchdog_cleanup(void)
{
    if (!g_watchdog.initialized) {
        return 0;
    }
    
    /* Stop keepalive thread */
    g_watchdog.thread_running = 0;
    
    if (pthread_join(g_watchdog.keepalive_thread, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "watchdog: pthread_join failed: %s", strerror(errno));
    }
    
    /* Close watchdog device gracefully (prevents restart) */
    if (g_watchdog.watchdog_fd >= 0) {
        if (write(g_watchdog.watchdog_fd, "V", 1) < 0) {
            sm_log(SM_LOG_WARN, "watchdog: magic close failed (device may not support it)");
        }
        
        close(g_watchdog.watchdog_fd);
        g_watchdog.watchdog_fd = -1;
    }
    
    pthread_mutex_destroy(&g_watchdog.lock);
    memset(&g_watchdog, 0, sizeof(g_watchdog));
    
    sm_log(SM_LOG_INFO, "watchdog: cleanup complete");
    return 0;
}
