#define _POSIX_C_SOURCE 200809L

/*
 * sm_connection_pool.c - Unix socket connection pooling.
 */

#include "../infrastructure/sm_connection_pool.h"
#include "../infrastructure/sm_socket.h"
#include "../observability/sm_logging.h"
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

/* Socket path resolved at runtime from sm_socket module — never hardcoded. */

/*
 * Each slot tracks the fd and when it was last used.
 * Connections idle longer than CONN_MAX_IDLE_SECS are discarded on next get().
 */
#define CONN_MAX_IDLE_SECS 30

typedef struct {
    int    fd;
    time_t last_used;
} conn_slot_t;

typedef struct {
    conn_slot_t*    slots;
    int             max;
    int             available;
    int             in_use;
    pthread_mutex_t mutex;
    int             initialized;
} connpool_t;

static connpool_t g_pool = {0};

/* ── Internal helpers ────────────────────────────────────────────────────────── */

static int create_connection(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        sm_log(SM_LOG_ERROR, "connpool: socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sm_socket_get_path(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sm_log(SM_LOG_WARN, "connpool: connect() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * Check whether a pooled connection is still usable.
 * Uses MSG_PEEK | MSG_DONTWAIT: EAGAIN/EWOULDBLOCK means alive (no pending
 * data but socket is open). Any other result means the server closed it.
 * Also rejects connections that have been idle too long.
 */
static int connection_is_alive(const conn_slot_t* slot)
{
    if (slot->fd < 0) return 0;

    /* Discard connections idle for too long. */
    if (time(NULL) - slot->last_used > CONN_MAX_IDLE_SECS) return 0;

    int flags = fcntl(slot->fd, F_GETFL, 0);
    if (flags < 0) return 0;

    /* Temporarily non-blocking for the peek. */
    if (fcntl(slot->fd, F_SETFL, flags | O_NONBLOCK) < 0) return 0;

    char buf;
    ssize_t ret = recv(slot->fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
    int saved_errno = errno;

    /* Restore original flags regardless of result. */
    fcntl(slot->fd, F_SETFL, flags);

    if (ret < 0 && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK))
        return 1;   /* alive — no data but connection open */

    return 0;       /* ret == 0 means server closed; other errors = broken */
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

int sm_connpool_init(int max_conns)
{
    if (g_pool.initialized) {
        sm_log(SM_LOG_WARN, "connpool: already initialized");
        return 0;
    }

    if (max_conns <= 0) max_conns = 10;

    g_pool.slots = calloc((size_t)max_conns, sizeof(conn_slot_t));
    if (!g_pool.slots) {
        sm_log(SM_LOG_ERROR, "connpool: calloc failed");
        return -1;
    }

    for (int i = 0; i < max_conns; i++)
        g_pool.slots[i].fd = -1;

    g_pool.max         = max_conns;
    g_pool.available   = 0;
    g_pool.in_use      = 0;

    if (pthread_mutex_init(&g_pool.mutex, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "connpool: mutex init failed: %s", strerror(errno));
        free(g_pool.slots);
        g_pool.slots = NULL;
        return -1;
    }

    g_pool.initialized = 1;
    sm_log(SM_LOG_INFO, "connpool: initialized (max=%d)", max_conns);
    return 0;
}

int sm_connpool_get(void)
{
    if (!g_pool.initialized) return -1;

    pthread_mutex_lock(&g_pool.mutex);

    /* Return the first alive pooled connection. */
    for (int i = 0; i < g_pool.max; i++) {
        if (g_pool.slots[i].fd < 0) continue;

        if (connection_is_alive(&g_pool.slots[i])) {
            int fd = g_pool.slots[i].fd;
            g_pool.slots[i].fd = -1;
            g_pool.available--;
            g_pool.in_use++;
            pthread_mutex_unlock(&g_pool.mutex);
            return fd;
        }

        /* Dead slot — close and clear it. */
        close(g_pool.slots[i].fd);
        g_pool.slots[i].fd = -1;
        g_pool.available--;
        sm_log(SM_LOG_DEBUG, "connpool: discarded stale connection");
    }

    /* No pooled connection available; reserve a slot for the new one. */
    g_pool.in_use++;
    pthread_mutex_unlock(&g_pool.mutex);

    int fd = create_connection();
    if (fd < 0) {
        pthread_mutex_lock(&g_pool.mutex);
        g_pool.in_use--;
        pthread_mutex_unlock(&g_pool.mutex);
        return -1;
    }

    return fd;
}

void sm_connpool_put(int fd)
{
    if (fd < 0 || !g_pool.initialized) return;

    pthread_mutex_lock(&g_pool.mutex);

    /* Store in the first empty slot. */
    for (int i = 0; i < g_pool.max; i++) {
        if (g_pool.slots[i].fd < 0) {
            g_pool.slots[i].fd        = fd;
            g_pool.slots[i].last_used = time(NULL);
            g_pool.available++;
            g_pool.in_use--;
            pthread_mutex_unlock(&g_pool.mutex);
            return;
        }
    }

    /* Pool is full — release the in_use count then close outside the lock. */
    g_pool.in_use--;
    pthread_mutex_unlock(&g_pool.mutex);

    close(fd);
}

void sm_connpool_cleanup(void)
{
    if (!g_pool.initialized) return;

    pthread_mutex_lock(&g_pool.mutex);

    for (int i = 0; i < g_pool.max; i++) {
        if (g_pool.slots[i].fd >= 0) {
            close(g_pool.slots[i].fd);
            g_pool.slots[i].fd = -1;
        }
    }

    free(g_pool.slots);
    g_pool.slots       = NULL;
    g_pool.max         = 0;
    g_pool.initialized = 0;

    pthread_mutex_unlock(&g_pool.mutex);
    pthread_mutex_destroy(&g_pool.mutex);

    sm_log(SM_LOG_INFO, "connpool: cleanup complete");
}

void sm_connpool_stats(int* available, int* in_use)
{
    if (!available || !in_use) return;

    pthread_mutex_lock(&g_pool.mutex);
    *available = g_pool.available;
    *in_use    = g_pool.in_use;
    pthread_mutex_unlock(&g_pool.mutex);
}