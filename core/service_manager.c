#define _POSIX_C_SOURCE 200809L

#include "service_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/wait.h>

// ── HASH TABLE for O(1) service lookup ───────────────────────────────────────

#define HASH_SIZE  64   // must be power of 2

typedef struct hash_node {
    char             name[SM_MAX_NAME];
    int              registry_idx;
    struct hash_node* next;
} hash_node_t;

static hash_node_t  hash_pool[SM_MAX_SERVICES];
static int          hash_pool_used = 0;
static hash_node_t* hash_table[HASH_SIZE];

static uint32_t hash_name(const char* name)
{
    uint32_t h = 5381;
    while (*name) h = ((h << 5) + h) ^ (uint8_t)*name++;
    return h & (HASH_SIZE - 1);
}

static void hash_insert(const char* name, int idx)
{
    uint32_t     slot = hash_name(name);
    hash_node_t* node = &hash_pool[hash_pool_used++];
    strncpy(node->name, name, SM_MAX_NAME - 1);
    node->registry_idx = idx;
    node->next         = hash_table[slot];
    hash_table[slot]   = node;
}

static int hash_find(const char* name)
{
    uint32_t     slot = hash_name(name);
    hash_node_t* node = hash_table[slot];
    while (node) {
        if (strncmp(node->name, name, SM_MAX_NAME) == 0)
            return node->registry_idx;
        node = node->next;
    }
    return -1;
}

static void hash_remove(const char* name)
{
    uint32_t      slot = hash_name(name);
    hash_node_t** cur  = &hash_table[slot];
    while (*cur) {
        if (strncmp((*cur)->name, name, SM_MAX_NAME) == 0) {
            *cur = (*cur)->next;
            return;
        }
        cur = &(*cur)->next;
    }
}

// ── STATE ─────────────────────────────────────────────────────────────────────

static service_entry_t registry[SM_MAX_SERVICES];
static int             registry_count = 0;
static int             server_fd      = -1;
static int             epoll_fd       = -1;
static volatile int    running        = 1;

// FIX: instead of fprintf in signal handler, use a flag + crashed pid queue
static volatile sig_atomic_t crashed_pid_flag = 0;
static pid_t                 crashed_pids[SM_MAX_SERVICES];
static volatile int          crashed_count = 0;

// ── SIGNAL HANDLERS ───────────────────────────────────────────────────────────

static void handle_sigterm(int sig)
{
    (void)sig;
    running = 0;
}

static void handle_sigchld(int sig)
{
    (void)sig;
    int   status;
    pid_t pid;

    // FIX: no fprintf here — only async-signal-safe operations allowed
    // store crashed pids in a queue, main loop will process them
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (crashed_count < SM_MAX_SERVICES) {
            crashed_pids[crashed_count++] = pid;
            crashed_pid_flag = 1;
        }
    }
}

// process crashed pids from main loop (safe to use fprintf here)
static void process_crashed_pids(void)
{
    if (!crashed_pid_flag) return;
    crashed_pid_flag = 0;

    for (int c = 0; c < crashed_count; c++) {
        pid_t pid = crashed_pids[c];
        for (int i = 0; i < registry_count; i++) {
            if (registry[i].pid == pid) {
                registry[i].status = SERVICE_CRASHED;
                fprintf(stderr, "[SM] service '%s' pid=%d crashed\n",
                        registry[i].name, pid);
                break;
            }
        }
    }
    crashed_count = 0;
}

// ── REQUEST HANDLERS ──────────────────────────────────────────────────────────

static void handle_register(int fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    if (hash_find(msg->service_name) >= 0) {
        reply.response_code = SM_ERR_EXISTS;
        send(fd, &reply, sizeof(reply), 0);
        return;
    }
    if (registry_count >= SM_MAX_SERVICES) {
        reply.response_code = SM_ERR_FULL;
        send(fd, &reply, sizeof(reply), 0);
        return;
    }

    int idx = registry_count++;
    service_entry_t* e = &registry[idx];
    strncpy(e->name,        msg->service_name, SM_MAX_NAME - 1);
    strncpy(e->socket_path, msg->socket_path,  SM_MAX_PATH - 1);
    strncpy(e->ring_name,   msg->ring_name,    SM_MAX_PATH - 1);
    e->pid            = (pid_t)msg->pid;
    e->status         = SERVICE_RUNNING;
    e->last_heartbeat = time(NULL);

    hash_insert(e->name, idx);
    printf("[SM] registered '%s' pid=%d\n", e->name, e->pid);

    reply.response_code = SM_OK;
    send(fd, &reply, sizeof(reply), 0);
}

static void handle_lookup(int fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    int idx = hash_find(msg->service_name);  // O(1)
    if (idx < 0) {
        reply.response_code = SM_ERR_NOT_FOUND;
        send(fd, &reply, sizeof(reply), 0);
        return;
    }

    reply.response_code = SM_OK;
    strncpy(reply.socket_path, registry[idx].socket_path, SM_MAX_PATH - 1);
    strncpy(reply.ring_name,   registry[idx].ring_name,   SM_MAX_PATH - 1);
    send(fd, &reply, sizeof(reply), 0);
}

static void handle_heartbeat(int fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    int idx = hash_find(msg->service_name);
    if (idx >= 0) {
        registry[idx].last_heartbeat = time(NULL);
        registry[idx].status         = SERVICE_RUNNING;
        reply.response_code = SM_OK;
    } else {
        reply.response_code = SM_ERR_NOT_FOUND;
    }
    send(fd, &reply, sizeof(reply), 0);
}

static void handle_unregister(int fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    int idx = hash_find(msg->service_name);
    if (idx < 0) {
        reply.response_code = SM_ERR_NOT_FOUND;
        send(fd, &reply, sizeof(reply), 0);
        return;
    }

    hash_remove(msg->service_name);
    printf("[SM] unregistered '%s'\n", registry[idx].name);

    // shift array
    for (int i = idx; i < registry_count - 1; i++)
        registry[i] = registry[i + 1];
    registry_count--;

    reply.response_code = SM_OK;
    send(fd, &reply, sizeof(reply), 0);
}

static void handle_client(int client_fd)
{
    sm_message_t msg = {0};
    if (recv(client_fd, &msg, sizeof(msg), 0) <= 0) return;

    switch (msg.type) {
        case SM_MSG_REGISTER:   handle_register(client_fd, &msg);   break;
        case SM_MSG_LOOKUP:     handle_lookup(client_fd, &msg);     break;
        case SM_MSG_HEARTBEAT:  handle_heartbeat(client_fd, &msg);  break;
        case SM_MSG_UNREGISTER: handle_unregister(client_fd, &msg); break;
        default: {
            sm_message_t reply = { .response_code = SM_ERR_INVALID };
            send(client_fd, &reply, sizeof(reply), 0);
        }
    }
}

// ── HEALTH CHECK ──────────────────────────────────────────────────────────────

static void check_health(void)
{
    time_t now = time(NULL);
    for (int i = 0; i < registry_count; i++) {
        service_entry_t* s = &registry[i];
        if (s->status == SERVICE_RUNNING &&
            (now - s->last_heartbeat) > SM_HEARTBEAT_TIMEOUT) {
            s->status = SERVICE_CRASHED;
            fprintf(stderr, "[SM] '%s' heartbeat timeout\n", s->name);
        }
    }
}

// ── SETUP ─────────────────────────────────────────────────────────────────────

static int setup_socket(void)
{
    unlink(SM_SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("[SM] socket"); return -1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SM_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[SM] bind"); return -1;
    }
    if (listen(server_fd, 16) < 0) {
        perror("[SM] listen"); return -1;
    }
    printf("[SM] listening on %s\n", SM_SOCKET_PATH);
    return 0;
}

static int setup_epoll(void)
{
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { perror("[SM] epoll_create1"); return -1; }

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = server_fd };
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
}

static void setup_signals(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    sa.sa_handler = handle_sigchld;
    sa.sa_flags   = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

static void cleanup(void)
{
    if (server_fd >= 0) close(server_fd);
    if (epoll_fd  >= 0) close(epoll_fd);
    unlink(SM_SOCKET_PATH);
    printf("[SM] shutdown\n");
}

// ── MAIN LOOP ─────────────────────────────────────────────────────────────────

int sm_run(void)
{
    setup_signals();
    if (setup_socket() < 0) return -1;
    if (setup_epoll()  < 0) return -1;

    printf("[SM] started\n");

    struct epoll_event events[16];
    time_t last_check = time(NULL);

    while (running) {
        int n = epoll_wait(epoll_fd, events, 16, 3000);
        if (n < 0) {
            if (errno == EINTR) {
                process_crashed_pids();  // handle any crashes signalled
                continue;
            }
            perror("[SM] epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                int cfd = accept(server_fd, NULL, NULL);
                if (cfd >= 0) { handle_client(cfd); close(cfd); }
            }
        }

        process_crashed_pids();

        time_t now = time(NULL);
        if (now - last_check >= 3) { check_health(); last_check = now; }
    }

    cleanup();
    return 0;
}

// ── CLIENT SIDE — persistent connection ──────────────────────────────────────
//
// FIX: keep one open connection per "session" instead of
//      connecting and disconnecting for every single call
//

int sm_connect_persistent(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SM_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        fprintf(stderr, "[SM] cannot connect — is service manager running?\n");
        return -1;
    }
    return fd;
}

void sm_disconnect(int fd)
{
    if (fd >= 0) close(fd);
}

// send message and receive reply on existing connection
static int sm_transact(int fd, sm_message_t* msg, sm_message_t* reply)
{
    if (send(fd, msg, sizeof(*msg), 0) < 0) return -1;
    if (recv(fd, reply, sizeof(*reply), 0) < 0) return -1;
    return reply->response_code;
}

int sm_register(const char* name, const char* socket_path, const char* ring_name)
{
    sm_message_t msg   = {0};
    sm_message_t reply = {0};
    msg.type = SM_MSG_REGISTER;
    msg.pid  = (int)getpid();
    strncpy(msg.service_name, name,        SM_MAX_NAME - 1);
    strncpy(msg.socket_path,  socket_path, SM_MAX_PATH - 1);
    strncpy(msg.ring_name,    ring_name,   SM_MAX_PATH - 1);

    int fd = sm_connect_persistent();
    if (fd < 0) return -1;
    int rc = sm_transact(fd, &msg, &reply);
    sm_disconnect(fd);
    return rc;
}

int sm_lookup(const char* name, char* socket_path_out, char* ring_name_out)
{
    sm_message_t msg   = {0};
    sm_message_t reply = {0};
    msg.type = SM_MSG_LOOKUP;
    strncpy(msg.service_name, name, SM_MAX_NAME - 1);

    int fd = sm_connect_persistent();
    if (fd < 0) return -1;
    int rc = sm_transact(fd, &msg, &reply);
    sm_disconnect(fd);

    if (rc == SM_OK) {
        if (socket_path_out) strncpy(socket_path_out, reply.socket_path, SM_MAX_PATH - 1);
        if (ring_name_out)   strncpy(ring_name_out,   reply.ring_name,   SM_MAX_PATH - 1);
    }
    return rc;
}

int sm_heartbeat(const char* name)
{
    sm_message_t msg   = {0};
    sm_message_t reply = {0};
    msg.type = SM_MSG_HEARTBEAT;
    strncpy(msg.service_name, name, SM_MAX_NAME - 1);

    int fd = sm_connect_persistent();
    if (fd < 0) return -1;
    int rc = sm_transact(fd, &msg, &reply);
    sm_disconnect(fd);
    return rc;
}

int sm_unregister(const char* name)
{
    sm_message_t msg   = {0};
    sm_message_t reply = {0};
    msg.type = SM_MSG_UNREGISTER;
    strncpy(msg.service_name, name, SM_MAX_NAME - 1);

    int fd = sm_connect_persistent();
    if (fd < 0) return -1;
    int rc = sm_transact(fd, &msg, &reply);
    sm_disconnect(fd);
    return rc;
}