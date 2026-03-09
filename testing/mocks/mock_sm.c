/**
 * @file mock_sm.c
 * @brief Mock Service Manager implementation.
 */
#include "mock_sm.h"
#include "../helpers/test_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>
#include <fcntl.h>

/* Minimal protocol constants (must match sm_protocol.h) */
#define SM_PROTOCOL_MAGIC   0x534D4B47
#define SM_PROTOCOL_VERSION 2
#define SM_MSG_REGISTER     1
#define SM_MSG_LOOKUP       2
#define SM_MSG_HEARTBEAT    3
#define SM_MSG_UNREGISTER   4
#define SM_OK               0

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;
    uint32_t timestamp;
    uint32_t client_pid;
    uint32_t nonce;
    uint8_t  hmac[32];
} mock_sm_hdr_t;

typedef struct { int32_t response_code; } mock_sm_reply_t;
#pragma pack(pop)

static void handle_client(mock_sm_t *sm, int client_fd)
{
    if (sm->shutdown_on_connect) {
        close(client_fd);
        return;
    }

    mock_sm_hdr_t hdr;
    uint8_t payload[1024];

    /* Read header */
    ssize_t r = recv(client_fd, &hdr, sizeof(hdr), MSG_WAITALL);
    if (r != (ssize_t)sizeof(hdr)) { close(client_fd); return; }

    uint32_t plen = hdr.length;
    if (plen > sizeof(payload)) plen = (uint32_t)sizeof(payload);
    if (plen > 0) recv(client_fd, payload, plen, MSG_WAITALL);

    /* Record call */
    int resp = SM_OK;
    switch (hdr.type) {
    case SM_MSG_REGISTER:
        sm->register_count++;
        resp = sm->register_response;
        if (plen >= 64)
            memcpy(sm->last_registered_name, payload, 63);
        break;
    case SM_MSG_LOOKUP:
        sm->lookup_count++;
        resp = sm->lookup_response;
        break;
    case SM_MSG_HEARTBEAT:
        sm->heartbeat_count++;
        resp = sm->heartbeat_response;
        break;
    default:
        break;
    }

    /* Send reply */
    mock_sm_hdr_t rh;
    memset(&rh, 0, sizeof(rh));
    rh.magic   = SM_PROTOCOL_MAGIC;
    rh.version = SM_PROTOCOL_VERSION;
    rh.type    = hdr.type;
    rh.length  = sizeof(mock_sm_reply_t);

    mock_sm_reply_t rep = { .response_code = resp };
    send(client_fd, &rh,  sizeof(rh),  0);
    send(client_fd, &rep, sizeof(rep), 0);
    close(client_fd);
}

static void *sm_thread(void *arg)
{
    mock_sm_t *sm = (mock_sm_t *)arg;
    while (sm->running) {
        int client = accept(sm->listen_fd, NULL, NULL);
        if (client < 0) {
            if (!sm->running) break;
            continue;
        }
        sm->connect_count++;
        handle_client(sm, client);
    }
    return NULL;
}

int mock_sm_start(mock_sm_t *sm, char *out_path)
{
    memset(sm, 0, sizeof(*sm));
    sm->register_response   = SM_OK;
    sm->lookup_response     = SM_OK;
    sm->heartbeat_response  = SM_OK;

    tu_tmp_socket_path(sm->socket_path, sizeof(sm->socket_path), "mock_sm");
    if (out_path) strcpy(out_path, sm->socket_path);

    sm->listen_fd = tu_unix_listen(sm->socket_path);
    if (sm->listen_fd < 0) return -1;

    /* Make accept() non-blocking so thread can check sm->running */
    int flags = fcntl(sm->listen_fd, F_GETFL);
    fcntl(sm->listen_fd, F_SETFL, flags | O_NONBLOCK);

    sm->running = 1;
    if (pthread_create(&sm->thread, NULL, sm_thread, sm) != 0) {
        close(sm->listen_fd);
        return -1;
    }
    return 0;
}

void mock_sm_stop(mock_sm_t *sm)
{
    sm->running = 0;
    shutdown(sm->listen_fd, SHUT_RDWR);
    close(sm->listen_fd);
    pthread_join(sm->thread, NULL);
    unlink(sm->socket_path);
}

int mock_sm_broadcast_shutdown(mock_sm_t *sm)
{
    (void)sm;
    /* For a full implementation this would iterate connected clients */
    return 0;
}

void mock_sm_reset_stats(mock_sm_t *sm)
{
    sm->register_count  = 0;
    sm->lookup_count    = 0;
    sm->heartbeat_count = 0;
    sm->connect_count   = 0;
}
