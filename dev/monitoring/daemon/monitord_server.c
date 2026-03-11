/**
 * @file    monitord_server.c
 * @brief   Unix domain socket server implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "monitord_server.h"
#include "../protocol/monitor_wire_format.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>

#define MAX_CLIENTS 16

typedef struct {
    int      fd;
    uint64_t last_seq;
    int      active;
} client_conn_t;

struct monitord_server {
    int               listen_fd;
    pthread_t         accept_thread;
    volatile int      stop_flag;

    client_conn_t     clients[MAX_CLIENTS];
    pthread_mutex_t   clients_lock;

    monitord_config_t *config;
    monitord_state_t  *state;
};

static void *accept_thread_fn(void *arg)
{
    monitord_server_t *srv = (monitord_server_t *)arg;

    while (!srv->stop_flag) {
        int client_fd = accept(srv->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("accept");
            break;
        }

        /* Add to client list */
        pthread_mutex_lock(&srv->clients_lock);
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!srv->clients[i].active) {
                srv->clients[i].fd = client_fd;
                srv->clients[i].last_seq = 0;
                srv->clients[i].active = 1;
                added = 1;
                break;
            }
        }
        pthread_mutex_unlock(&srv->clients_lock);

        if (!added) {
            fprintf(stderr, "[monitord_server] Client limit reached, dropping connection\n");
            close(client_fd);
        }
    }

    return NULL;
}

monitord_server_t *monitord_server_start(monitord_config_t *config,
                                          monitord_state_t *state)
{
    monitord_server_t *srv = calloc(1, sizeof(monitord_server_t));
    if (!srv) return NULL;

    srv->config = config;
    srv->state = state;
    pthread_mutex_init(&srv->clients_lock, NULL);

    /* Create Unix socket */
    srv->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        perror("socket");
        free(srv);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.*s",
             (int)(sizeof(addr.sun_path) - 1), config->unix_socket_path);

    unlink(addr.sun_path);  /* Remove stale socket */

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    if (listen(srv->listen_fd, 8) < 0) {
        perror("listen");
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    /* Start accept thread */
    if (pthread_create(&srv->accept_thread, NULL, accept_thread_fn, srv) != 0) {
        perror("pthread_create");
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    return srv;
}

void monitord_server_stop(monitord_server_t *srv)
{
    if (!srv) return;

    srv->stop_flag = 1;
    shutdown(srv->listen_fd, SHUT_RDWR);
    pthread_join(srv->accept_thread, NULL);

    /* Close all client connections */
    pthread_mutex_lock(&srv->clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->clients[i].active) {
            close(srv->clients[i].fd);
            srv->clients[i].active = 0;
        }
    }
    pthread_mutex_unlock(&srv->clients_lock);

    close(srv->listen_fd);
    unlink(srv->config->unix_socket_path);
    pthread_mutex_destroy(&srv->clients_lock);
    free(srv);
}

uint32_t monitord_server_broadcast_snapshot(monitord_server_t *srv,
                                              const mon_snapshot_t *snapshot)
{
    uint32_t sent = 0;

    pthread_mutex_lock(&srv->clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!srv->clients[i].active) continue;

        int rc = mon_send_msg(srv->clients[i].fd, MON_MSG_SNAPSHOT_RESP,
                              0,  /* Simple sequence number */
                              snapshot, sizeof(*snapshot));
        if (rc == 0) {
            srv->clients[i].last_seq = snapshot->snapshot_seq;
            sent++;
        } else {
            /* Client disconnected or error */
            close(srv->clients[i].fd);
            srv->clients[i].active = 0;
        }
    }
    pthread_mutex_unlock(&srv->clients_lock);

    return sent;
}
