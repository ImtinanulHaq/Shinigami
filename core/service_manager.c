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

// ── INTERNAL STATE ─────────────────────────────────────────────────────────────

static service_entry_t registry[SM_MAX_SERVICES];  // all registered services
static int             registry_count = 0;
static int             server_fd      = -1;         // main unix socket
static int             epoll_fd       = -1;
static volatile int    running        = 1;          // set to 0 on SIGTERM

// ══════════════════════════════════════════════════════════════════════════════
// INTERNAL HELPERS
// ══════════════════════════════════════════════════════════════════════════════

// find service by name, returns index or -1
static int find_service(const char* name)
{
    for (int i = 0; i < registry_count; i++) {
        if (strncmp(registry[i].name, name, SM_MAX_NAME) == 0)
            return i;
    }
    return -1;
}

// add epoll watch on a file descriptor
static int epoll_add(int efd, int fd)
{
    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    return epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
}

// ══════════════════════════════════════════════════════════════════════════════
// SIGNAL HANDLERS
// ══════════════════════════════════════════════════════════════════════════════

// graceful shutdown on SIGTERM or SIGINT
static void handle_sigterm(int sig)
{
    (void)sig;
    running = 0;
}

// detect crashed services on SIGCHLD
static void handle_sigchld(int sig)
{
    (void)sig;
    int   status;
    pid_t pid;

    // collect all dead children (WNOHANG = don't block)
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < registry_count; i++) {
            if (registry[i].pid == pid) {
                registry[i].status = SERVICE_CRASHED;
                fprintf(stderr, "[SM] service '%s' (pid %d) crashed!\n",
                        registry[i].name, pid);
                break;
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// REQUEST HANDLERS
// ══════════════════════════════════════════════════════════════════════════════

static void handle_register(int client_fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    // check if already registered
    if (find_service(msg->service_name) >= 0) {
        reply.response_code = SM_ERR_EXISTS;
        send(client_fd, &reply, sizeof(reply), 0);
        return;
    }

    // check if registry is full
    if (registry_count >= SM_MAX_SERVICES) {
        reply.response_code = SM_ERR_FULL;
        send(client_fd, &reply, sizeof(reply), 0);
        return;
    }

    // add to registry
    service_entry_t* entry = &registry[registry_count++];
    strncpy(entry->name,        msg->service_name, SM_MAX_NAME - 1);
    strncpy(entry->socket_path, msg->socket_path,  SM_MAX_PATH - 1);
    strncpy(entry->ring_name,   msg->ring_name,    SM_MAX_PATH - 1);
    entry->pid            = (pid_t)msg->pid;
    entry->status         = SERVICE_RUNNING;
    entry->last_heartbeat = time(NULL);

    printf("[SM] registered: '%s' pid=%d\n", entry->name, entry->pid);

    reply.response_code = SM_OK;
    send(client_fd, &reply, sizeof(reply), 0);
}

static void handle_lookup(int client_fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    int idx = find_service(msg->service_name);
    if (idx < 0) {
        reply.response_code = SM_ERR_NOT_FOUND;
        send(client_fd, &reply, sizeof(reply), 0);
        return;
    }

    reply.response_code = SM_OK;
    strncpy(reply.socket_path, registry[idx].socket_path, SM_MAX_PATH - 1);
    strncpy(reply.ring_name,   registry[idx].ring_name,   SM_MAX_PATH - 1);
    send(client_fd, &reply, sizeof(reply), 0);
}

static void handle_heartbeat(int client_fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    int idx = find_service(msg->service_name);
    if (idx >= 0) {
        registry[idx].last_heartbeat = time(NULL);
        registry[idx].status         = SERVICE_RUNNING;
        reply.response_code = SM_OK;
    } else {
        reply.response_code = SM_ERR_NOT_FOUND;
    }
    send(client_fd, &reply, sizeof(reply), 0);
}

static void handle_unregister(int client_fd, sm_message_t* msg)
{
    sm_message_t reply = {0};

    int idx = find_service(msg->service_name);
    if (idx < 0) {
        reply.response_code = SM_ERR_NOT_FOUND;
        send(client_fd, &reply, sizeof(reply), 0);
        return;
    }

    // remove by shifting array left
    printf("[SM] unregistered: '%s'\n", registry[idx].name);
    for (int i = idx; i < registry_count - 1; i++)
        registry[i] = registry[i + 1];
    registry_count--;

    reply.response_code = SM_OK;
    send(client_fd, &reply, sizeof(reply), 0);
}

// dispatch incoming message to correct handler
static void handle_client(int client_fd)
{
    sm_message_t msg = {0};

    ssize_t n = recv(client_fd, &msg, sizeof(msg), 0);
    if (n <= 0) return;

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

// ══════════════════════════════════════════════════════════════════════════════
// HEALTH CHECK — restart crashed services
// ══════════════════════════════════════════════════════════════════════════════

static void check_service_health(void)
{
    time_t now = time(NULL);

    for (int i = 0; i < registry_count; i++) {
        service_entry_t* s = &registry[i];

        // check heartbeat timeout
        if (s->status == SERVICE_RUNNING &&
            (now - s->last_heartbeat) > SM_HEARTBEAT_TIMEOUT) {
            fprintf(stderr, "[SM] '%s' heartbeat timeout — marking crashed\n", s->name);
            s->status = SERVICE_CRASHED;
        }

        // TODO: add auto-restart logic here in future
        if (s->status == SERVICE_CRASHED) {
            fprintf(stderr, "[SM] '%s' needs restart (not implemented yet)\n", s->name);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP — socket, epoll, signals
// ══════════════════════════════════════════════════════════════════════════════

static int setup_socket(void)
{
    // remove old socket file if left from previous crash
    unlink(SM_SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[SM] socket()");
        return -1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SM_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[SM] bind()");
        return -1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("[SM] listen()");
        return -1;
    }

    printf("[SM] listening on %s\n", SM_SOCKET_PATH);
    return 0;
}

static int setup_epoll(void)
{
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[SM] epoll_create1()");
        return -1;
    }
    return epoll_add(epoll_fd, server_fd);
}

static void setup_signals(void)
{
    struct sigaction sa = {0};

    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    sa.sa_handler = handle_sigchld;
    sa.sa_flags   = SA_NOCLDSTOP;   // only on exit, not stop/continue
    sigaction(SIGCHLD, &sa, NULL);
}

// ══════════════════════════════════════════════════════════════════════════════
// CLEANUP
// ══════════════════════════════════════════════════════════════════════════════

static void cleanup(void)
{
    if (server_fd >= 0) close(server_fd);
    if (epoll_fd  >= 0) close(epoll_fd);
    unlink(SM_SOCKET_PATH);
    printf("[SM] shutdown complete\n");
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN RUN LOOP
// ══════════════════════════════════════════════════════════════════════════════

int sm_run(void)
{
    setup_signals();

    if (setup_socket() < 0) return -1;
    if (setup_epoll()  < 0) return -1;

    printf("[SM] service manager started\n");

    struct epoll_event events[16];
    time_t last_health_check = time(NULL);

    while (running) {
        // wait up to 3 seconds for events
        int n = epoll_wait(epoll_fd, events, 16, 3000);

        if (n < 0) {
            if (errno == EINTR) continue;  // signal interrupted — loop again
            perror("[SM] epoll_wait()");
            break;
        }

        // handle each active fd
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                // new client connecting
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd >= 0) {
                    handle_client(client_fd);
                    close(client_fd);  // one request per connection
                }
            }
        }

        // health check every 3 seconds
        time_t now = time(NULL);
        if (now - last_health_check >= 3) {
            check_service_health();
            last_health_check = now;
        }
    }

    cleanup();
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// CLIENT-SIDE HELPERS (apps and services call these)
// ══════════════════════════════════════════════════════════════════════════════

// open a connection to service manager, returns fd or -1
static int sm_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SM_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// send a message and receive reply, returns response_code
static int sm_send_recv(sm_message_t* msg, sm_message_t* reply)
{
    int fd = sm_connect();
    if (fd < 0) {
        fprintf(stderr, "[SM client] cannot connect — is service manager running?\n");
        return -1;
    }

    send(fd, msg, sizeof(*msg), 0);
    recv(fd, reply, sizeof(*reply), 0);
    close(fd);
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

    return sm_send_recv(&msg, &reply);
}

int sm_lookup(const char* name, char* socket_path_out, char* ring_name_out)
{
    sm_message_t msg   = {0};
    sm_message_t reply = {0};

    msg.type = SM_MSG_LOOKUP;
    strncpy(msg.service_name, name, SM_MAX_NAME - 1);

    int rc = sm_send_recv(&msg, &reply);
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

    return sm_send_recv(&msg, &reply);
}

int sm_unregister(const char* name)
{
    sm_message_t msg   = {0};
    sm_message_t reply = {0};

    msg.type = SM_MSG_UNREGISTER;
    strncpy(msg.service_name, name, SM_MAX_NAME - 1);

    return sm_send_recv(&msg, &reply);
}