#define _POSIX_C_SOURCE 200809L

/*
 * sm_connection_pool.c - Connection pooling
 */

#include "sm_connection_pool.h"
#include "sm_logging.h"
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define SOCKET_PATH "/run/servicemanager.sock"

typedef struct {
    int* fds;
    int  max;
    int  available;
    int  in_use;
    pthread_mutex_t mutex;
} connpool_t;

static connpool_t g_pool;

int sm_connpool_init(int max_conns)
{
    if (max_conns <= 0) max_conns = 10;
    
    g_pool.fds = malloc(max_conns * sizeof(int));
    if (!g_pool.fds) return -1;
    
    for (int i = 0; i < max_conns; i++) {
        g_pool.fds[i] = -1;
    }
    
    g_pool.max = max_conns;
    g_pool.available = 0;
    g_pool.in_use = 0;
    pthread_mutex_init(&g_pool.mutex, NULL);
    
    return 0;
}

static int create_connection(void)
{
    int fd;
    struct sockaddr_un addr;
    
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}

static int verify_connection_alive(int fd)
{
    /* Test if connection is still alive by checking availability without blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    /* Try to peek at data without consuming it */
    char test_byte;
    ssize_t ret = recv(fd, &test_byte, 1, MSG_PEEK | MSG_DONTWAIT);
    
    /* Restore blocking mode */
    fcntl(fd, F_SETFL, flags);
    
    /* Connection is good if: ret == -1 with EAGAIN/EWOULDBLOCK (no data but connected) */
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 1;  /* connection is alive */
    }
    
    /* Any other result indicates connection is dead */
    return 0;
}

int sm_connpool_get(void)
{
    int fd = -1;
    
    pthread_mutex_lock(&g_pool.mutex);
    
    /* Try to get a healthy connection from pool */
    for (int i = 0; i < g_pool.max; i++) {
        if (g_pool.fds[i] >= 0) {
            /* Verify connection is still alive before returning it */
            if (verify_connection_alive(g_pool.fds[i])) {
                fd = g_pool.fds[i];
                g_pool.fds[i] = -1;
                g_pool.available--;
                g_pool.in_use++;
                break;
            } else {
                /* Connection is dead, close and remove from pool */
                close(g_pool.fds[i]);
                g_pool.fds[i] = -1;
                g_pool.available--;
            }
        }
    }
    
    pthread_mutex_unlock(&g_pool.mutex);
    
    /* If no pooled connection available, create new one */
    if (fd < 0) {
        fd = create_connection();
        if (fd >= 0) {
            pthread_mutex_lock(&g_pool.mutex);
            g_pool.in_use++;
            pthread_mutex_unlock(&g_pool.mutex);
        }
    }
    
    return fd;
}

void sm_connpool_put(int fd)
{
    if (fd < 0) return;
    
    pthread_mutex_lock(&g_pool.mutex);
    
    /* Try to store in pool if there's space */
    for (int i = 0; i < g_pool.max; i++) {
        if (g_pool.fds[i] < 0) {
            g_pool.fds[i] = fd;
            g_pool.available++;
            g_pool.in_use--;
            pthread_mutex_unlock(&g_pool.mutex);
            return;
        }
    }
    
    g_pool.in_use--;
    pthread_mutex_unlock(&g_pool.mutex);
    
    /* Pool is full, just close it */
    close(fd);
}

void sm_connpool_cleanup(void)
{
    if (!g_pool.fds) return;
    
    pthread_mutex_lock(&g_pool.mutex);
    
    for (int i = 0; i < g_pool.max; i++) {
        if (g_pool.fds[i] >= 0) {
            close(g_pool.fds[i]);
        }
    }
    
    free(g_pool.fds);
    g_pool.fds = NULL;
    g_pool.max = 0;
    
    pthread_mutex_unlock(&g_pool.mutex);
    pthread_mutex_destroy(&g_pool.mutex);
}

void sm_connpool_stats(int* available, int* in_use)
{
    pthread_mutex_lock(&g_pool.mutex);
    *available = g_pool.available;
    *in_use = g_pool.in_use;
    pthread_mutex_unlock(&g_pool.mutex);
}
