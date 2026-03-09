/**
 * @file mock_sm.c
 * @brief Mock Service Manager — listens on a Unix socket and handles
 *        register / heartbeat / unregister messages per sm_protocol.h.
 *
 * A POSIX thread handles one blocking accept() and services messages
 * from that connection until it closes.  Tests use mock_sm_wait_request()
 * to synchronise.
 */

#include "mock_sm.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ── internal worker ───────────────────────────────────────────────────── */

static void dispatch_message(mock_sm_t *sm, int client_fd,
                             const sm_hdr_t *hdr, const uint8_t *payload)
{
    sm_reply_t reply;
    reply.response_code = SM_OK;

    switch (hdr->type) {
    case SM_MSG_REGISTER: {
        sm_register_req_t *req = (sm_register_req_t *)payload;
        reply.response_code = sm->register_reply;
        if (reply.response_code == SM_OK &&
            sm->entry_count < MOCK_SM_MAX_SERVICES) {
            mock_sm_entry_t *e = &sm->entries[sm->entry_count++];
            strncpy(e->service_name, req->service_name, SM_MAX_NAME - 1);
            strncpy(e->socket_path, req->socket_path, SM_MAX_PATH - 1);
            e->pid        = (pid_t)req->pid;
            e->registered = 1;
        }
        sm->register_count++;
        break;
    }
    case SM_MSG_HEARTBEAT: {
        reply.response_code = sm->heartbeat_reply;
        sm->heartbeat_count++;
        break;
    }
    case SM_MSG_UNREGISTER: {
        sm_unregister_req_t *req = (sm_unregister_req_t *)payload;
        reply.response_code = sm->unregister_reply;
        for (int i = 0; i < sm->entry_count; i++) {
            if (strcmp(sm->entries[i].service_name, req->service_name) == 0)
                sm->entries[i].registered = 0;
        }
        sm->unregister_count++;
        break;
    }
    default:
        reply.response_code = SM_ERR_INVALID;
        break;
    }

    send(client_fd, &reply, sizeof(reply), MSG_NOSIGNAL);
}

static void *worker_thread(void *arg)
{
    mock_sm_t *sm = (mock_sm_t *)arg;

    while (sm->running) {
        int client_fd = accept(sm->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (sm->running)
                perror("[mock_sm] accept");
            break;
        }

        /* Receive messages until connection closes */
        while (1) {
            sm_hdr_t hdr;
            ssize_t n = recv(client_fd, &hdr, sizeof(hdr), MSG_WAITALL);
            if (n <= 0) break;
            if (n < (ssize_t)sizeof(hdr)) break;

            if (hdr.magic != SM_PROTOCOL_MAGIC) break;

            uint8_t *payload = NULL;
            if (hdr.length > 0 && hdr.length <= SM_MAX_PAYLOAD_SIZE) {
                payload = malloc(hdr.length);
                if (payload) {
                    ssize_t got = recv(client_fd, payload,
                                       hdr.length, MSG_WAITALL);
                    if (got < (ssize_t)hdr.length) {
                        free(payload);
                        break;
                    }
                }
            }

            dispatch_message(sm, client_fd, &hdr, payload);
            free(payload);
        }

        close(client_fd);
    }

    return NULL;
}

/* ── public API ─────────────────────────────────────────────────────────── */

int mock_sm_start(mock_sm_t *sm, const char *path)
{
    if (!sm || !path) return -1;

    memset(sm, 0, sizeof(*sm));
    strncpy(sm->socket_path, path, sizeof(sm->socket_path) - 1);
    sm->register_reply   = SM_OK;
    sm->heartbeat_reply  = SM_OK;
    sm->unregister_reply = SM_OK;

    /* Remove stale socket */
    unlink(path);

    sm->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sm->listen_fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(sm->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sm->listen_fd);
        return -1;
    }

    if (listen(sm->listen_fd, MOCK_SM_BACKLOG) < 0) {
        close(sm->listen_fd);
        return -1;
    }

    sm->running = 1;
    pthread_t *tid = malloc(sizeof(pthread_t));
    if (!tid) { close(sm->listen_fd); return -1; }
    sm->thread = tid;

    if (pthread_create(tid, NULL, worker_thread, sm) != 0) {
        free(tid);
        close(sm->listen_fd);
        return -1;
    }

    return 0;
}

void mock_sm_stop(mock_sm_t *sm)
{
    if (!sm) return;

    sm->running = 0;
    if (sm->listen_fd >= 0) {
        shutdown(sm->listen_fd, SHUT_RDWR);
        close(sm->listen_fd);
        sm->listen_fd = -1;
    }

    if (sm->thread) {
        pthread_join(*(pthread_t *)sm->thread, NULL);
        free(sm->thread);
        sm->thread = NULL;
    }

    unlink(sm->socket_path);
}

int mock_sm_wait_request(mock_sm_t *sm, int timeout_ms)
{
    if (!sm) return -1;
    int initial = sm->register_count + sm->heartbeat_count +
                  sm->unregister_count;
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        int now = sm->register_count + sm->heartbeat_count +
                  sm->unregister_count;
        if (now > initial) return 0;
        usleep(10000);   /* 10 ms */
        elapsed += 10;
    }
    return -1;  /* timeout */
}

int mock_sm_is_registered(const mock_sm_t *sm, const char *service_name)
{
    if (!sm || !service_name) return 0;
    for (int i = 0; i < sm->entry_count; i++) {
        if (sm->entries[i].registered &&
            strcmp(sm->entries[i].service_name, service_name) == 0)
            return 1;
    }
    return 0;
}

void mock_sm_reset(mock_sm_t *sm)
{
    if (!sm) return;
    sm->entry_count      = 0;
    sm->register_count   = 0;
    sm->heartbeat_count  = 0;
    sm->unregister_count = 0;
    memset(sm->entries, 0, sizeof(sm->entries));
}
