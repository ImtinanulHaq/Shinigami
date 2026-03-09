/**
 * @file mock_sm.c
 * @brief Mock Service Manager implementation.
 *
 * Implements the mock_sm_t server described in mock_sm.h.
 *
 * Wire format: sm_hdr_t (56 bytes) + variable-length payload.
 * HMAC verification is intentionally skipped in the mock to keep
 * tests fast and dependency-free (the real service_ipc.c handles that).
 * The mock simply echoes back sm_reply_t with a configurable code.
 */

#define _GNU_SOURCE
#include "mock_sm.h"
#include "../../services/common/service_ipc.h"   /* SVC_MSG_* extended types */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Forward declaration for the service_ipc.h extended types used in push fns */
#ifndef SVC_MSG_HEALTH_CHECK
#define SVC_MSG_HEALTH_CHECK  5
#define SVC_MSG_SHUTDOWN      7
#define SVC_MSG_RELOAD_CONFIG 8
#endif

/* ── helpers ──────────────────────────────────────────────────────────── */

#define LOCK(sm)   pthread_mutex_lock(&(sm)->lock)
#define UNLOCK(sm) pthread_mutex_unlock(&(sm)->lock)

/**
 * @brief Build a minimal sm_hdr_t for mock-originated messages (no HMAC).
 */
static void build_hdr(sm_hdr_t *hdr, uint16_t type, uint32_t payload_len)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic      = SM_PROTOCOL_MAGIC;
    hdr->version    = SM_PROTOCOL_VERSION;
    hdr->type       = type;
    hdr->length     = payload_len;
    hdr->timestamp  = (uint32_t)time(NULL);
    hdr->nonce      = (uint32_t)((uintptr_t)hdr ^ (uintptr_t)hdr->timestamp);
}

/**
 * @brief Append a message type to the circular log. Caller holds lock.
 */
static void log_msg(mock_sm_t *sm, uint16_t type)
{
    int idx = (sm->msg_log_head + sm->msg_log_count) % MOCK_SM_MAX_MESSAGES;
    sm->msg_log[idx] = type;
    if (sm->msg_log_count < MOCK_SM_MAX_MESSAGES) {
        sm->msg_log_count++;
    } else {
        /* Overwrite oldest entry */
        sm->msg_log_head = (sm->msg_log_head + 1) % MOCK_SM_MAX_MESSAGES;
    }
}

/* ── message dispatcher (called from worker, holds no lock) ───────────── */

static void dispatch(mock_sm_t *sm, int client_fd,
                     const sm_hdr_t *hdr, const uint8_t *payload)
{
    sm_reply_t reply;
    reply.response_code = SM_OK;

    LOCK(sm);

    log_msg(sm, hdr->type);

    switch (hdr->type) {

    /* ── REGISTER ─────────────────────────────────────────────────────── */
    case SM_MSG_REGISTER: {
        reply.response_code = sm->register_reply;
        if (reply.response_code == SM_OK &&
            sm->entry_count < MOCK_SM_MAX_SERVICES &&
            payload != NULL) {
            const sm_register_req_t *req = (const sm_register_req_t *)payload;
            mock_sm_entry_t *e = &sm->entries[sm->entry_count++];
            strncpy(e->service_name, req->service_name, SM_MAX_NAME - 1);
            strncpy(e->socket_path,  req->socket_path,  SM_MAX_PATH - 1);
            e->pid        = (pid_t)req->pid;
            e->registered = 1;
        }
        sm->register_count++;
        break;
    }

    /* ── HEARTBEAT ────────────────────────────────────────────────────── */
    case SM_MSG_HEARTBEAT:
        reply.response_code = sm->heartbeat_reply;
        sm->heartbeat_count++;
        break;

    /* ── UNREGISTER ───────────────────────────────────────────────────── */
    case SM_MSG_UNREGISTER: {
        reply.response_code = sm->unregister_reply;
        if (payload != NULL) {
            const sm_unregister_req_t *req = (const sm_unregister_req_t *)payload;
            for (int i = 0; i < sm->entry_count; i++) {
                if (strncmp(sm->entries[i].service_name,
                            req->service_name, SM_MAX_NAME) == 0) {
                    sm->entries[i].registered = 0;
                }
            }
        }
        sm->unregister_count++;
        break;
    }

    /* ── Extended service-layer types (received from the service) ─────── */
    case SVC_MSG_HEALTH_CHECK:   /* service should not send this — ignore  */
        break;
    case 6: /* SVC_MSG_HEALTH_OK — service responding to our push          */
        sm->health_ok_count++;
        /* No sm_reply_t sent back for health OK responses */
        UNLOCK(sm);
        pthread_cond_broadcast(&sm->cond);
        return;  /* early return — no send */
    case 9: /* SVC_MSG_PING — service pinging us                          */
        sm->ping_count++;
        reply.response_code = SM_OK;  /* PONG */
        break;

    default:
        fprintf(stderr, "[mock_sm] unknown message type %u — rejecting\n",
                hdr->type);
        reply.response_code = SM_ERR_INVALID;
        break;
    }

    pthread_cond_broadcast(&sm->cond);
    UNLOCK(sm);

    /* Send reply.  Use send() with MSG_NOSIGNAL so a closed peer doesn't kill us. */
    send(client_fd, &reply, sizeof(reply), MSG_NOSIGNAL);
}

/* ── worker thread ────────────────────────────────────────────────────── */

static void *worker_thread(void *arg)
{
    mock_sm_t *sm = (mock_sm_t *)arg;

    while (atomic_load(&sm->running)) {
        /* Accept next client */
        int cfd = accept(sm->listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (atomic_load(&sm->running))
                perror("[mock_sm] accept");
            break;
        }

        LOCK(sm);
        sm->client_fd = cfd;
        UNLOCK(sm);

        /* Service messages until the client disconnects */
        while (atomic_load(&sm->running)) {
            sm_hdr_t hdr;
            ssize_t n = recv(cfd, &hdr, sizeof(hdr), MSG_WAITALL);
            if (n <= 0) break;
            if ((size_t)n < sizeof(hdr)) break;
            if (hdr.magic != SM_PROTOCOL_MAGIC) break;

            uint8_t *payload = NULL;
            size_t plen = hdr.length;
            if (plen > SM_MAX_PAYLOAD_SIZE) plen = SM_MAX_PAYLOAD_SIZE;
            if (plen > 0) {
                payload = malloc(plen);
                if (!payload) break;
                ssize_t got = recv(cfd, payload, plen, MSG_WAITALL);
                if (got < (ssize_t)plen) {
                    free(payload);
                    break;
                }
            }

            dispatch(sm, cfd, &hdr, payload);
            free(payload);
        }

        LOCK(sm);
        sm->client_fd = -1;
        UNLOCK(sm);
        close(cfd);
    }

    return NULL;
}

/* ── push helper (lock must NOT be held by caller) ────────────────────── */

static int push_msg(mock_sm_t *sm, uint16_t type)
{
    LOCK(sm);
    int cfd = sm->client_fd;
    UNLOCK(sm);

    if (cfd < 0) return -1;

    sm_hdr_t hdr;
    build_hdr(&hdr, type, 0);
    ssize_t n = send(cfd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
    return (n == (ssize_t)sizeof(hdr)) ? 0 : -1;
}

/* ── public API ───────────────────────────────────────────────────────── */

int mock_sm_start(mock_sm_t *sm, const char *path)
{
    if (!sm || !path) return -1;

    memset(sm, 0, sizeof(*sm));
    sm->client_fd        = -1;
    sm->listen_fd        = -1;
    sm->register_reply   = SM_OK;
    sm->heartbeat_reply  = SM_OK;
    sm->unregister_reply = SM_OK;
    sm->auth_token_len   = 32;
    memset(sm->auth_token, 0xAA, sm->auth_token_len);

    if (pthread_mutex_init(&sm->lock, NULL) != 0) return -1;
    if (pthread_cond_init(&sm->cond, NULL)  != 0) {
        pthread_mutex_destroy(&sm->lock);
        return -1;
    }

    snprintf(sm->socket_path, sizeof(sm->socket_path), "%s", path);

    /* Remove stale socket file */
    unlink(path);

    sm->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sm->listen_fd < 0) goto fail;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (bind(sm->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) goto fail;
    if (listen(sm->listen_fd, 8) < 0) goto fail;

    atomic_store(&sm->running, 1);
    if (pthread_create(&sm->thread, NULL, worker_thread, sm) != 0) goto fail;

    return 0;

fail:
    perror("[mock_sm] start failed");
    if (sm->listen_fd >= 0) { close(sm->listen_fd); sm->listen_fd = -1; }
    pthread_cond_destroy(&sm->cond);
    pthread_mutex_destroy(&sm->lock);
    return -1;
}

void mock_sm_stop(mock_sm_t *sm)
{
    if (!sm) return;

    atomic_store(&sm->running, 0);

    /* Unblock accept() in worker */
    if (sm->listen_fd >= 0) {
        shutdown(sm->listen_fd, SHUT_RDWR);
        close(sm->listen_fd);
        sm->listen_fd = -1;
    }
    /* Close active client if any */
    LOCK(sm);
    if (sm->client_fd >= 0) {
        close(sm->client_fd);
        sm->client_fd = -1;
    }
    UNLOCK(sm);

    pthread_join(sm->thread, NULL);
    unlink(sm->socket_path);

    pthread_cond_destroy(&sm->cond);
    pthread_mutex_destroy(&sm->lock);
}

int mock_sm_wait_request(mock_sm_t *sm, int timeout_ms)
{
    if (!sm) return -1;

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec  += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    LOCK(sm);
    int initial_count = sm->msg_log_count + sm->register_count +
                        sm->heartbeat_count + sm->health_ok_count +
                        sm->unregister_count;

    int rc = 0;
    while (rc == 0) {
        int new_count = sm->msg_log_count;
        if (new_count > initial_count) break;
        rc = pthread_cond_timedwait(&sm->cond, &sm->lock, &deadline);
    }
    UNLOCK(sm);
    return (rc == ETIMEDOUT) ? -1 : 0;
}

int mock_sm_is_registered(const mock_sm_t *sm, const char *service_name)
{
    if (!sm || !service_name) return 0;
    /* const_cast via cast to non-const for mutex — safe since we read only */
    mock_sm_t *mutable_sm = (mock_sm_t *)(uintptr_t)sm;
    LOCK(mutable_sm);
    int found = 0;
    for (int i = 0; i < sm->entry_count; i++) {
        if (sm->entries[i].registered &&
            strncmp(sm->entries[i].service_name, service_name, SM_MAX_NAME) == 0) {
            found = 1;
            break;
        }
    }
    UNLOCK(mutable_sm);
    return found;
}

int mock_sm_send_health_check(mock_sm_t *sm)
{
    return push_msg(sm, (uint16_t)SVC_MSG_HEALTH_CHECK);
}

int mock_sm_send_shutdown(mock_sm_t *sm)
{
    return push_msg(sm, (uint16_t)SVC_MSG_SHUTDOWN);
}

int mock_sm_send_reload(mock_sm_t *sm)
{
    return push_msg(sm, (uint16_t)SVC_MSG_RELOAD_CONFIG);
}

void mock_sm_reset(mock_sm_t *sm)
{
    if (!sm) return;
    LOCK(sm);
    sm->entry_count      = 0;
    sm->register_count   = 0;
    sm->unregister_count = 0;
    sm->heartbeat_count  = 0;
    sm->health_ok_count  = 0;
    sm->ping_count       = 0;
    sm->msg_log_head     = 0;
    sm->msg_log_count    = 0;
    memset(sm->entries,  0, sizeof(sm->entries));
    memset(sm->msg_log,  0, sizeof(sm->msg_log));
    UNLOCK(sm);
}

int mock_sm_count_msg(const mock_sm_t *sm, uint16_t type)
{
    if (!sm) return 0;
    mock_sm_t *mutable_sm = (mock_sm_t *)(uintptr_t)sm;
    LOCK(mutable_sm);
    int count = 0;
    for (int i = 0; i < sm->msg_log_count; i++) {
        int idx = (sm->msg_log_head + i) % MOCK_SM_MAX_MESSAGES;
        if (sm->msg_log[idx] == type) count++;
    }
    UNLOCK(mutable_sm);
    return count;
}
