#define _POSIX_C_SOURCE 200809L

#include "../service_manager.h"
#include "sm_logging.h"
#include "sm_security.h"
#include "sm_socket.h"
#include "sm_registry.h"
#include "sm_handlers.h"
#include "sm_health.h"
#include "sm_rate_limit.h"
#include "sm_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

#pragma GCC diagnostic ignored "-Wstringop-truncation"

// ── CLIENT CONNECTION STATE ────────────────────────────────────────────────────

#define MAX_CLIENTS 32

typedef enum {
    CLIENT_READING_HEADER,
    CLIENT_READING_PAYLOAD,
    CLIENT_SENDING_REPLY,
    CLIENT_CLOSED,
} client_state_t;

typedef struct {
    int client_state;
    int fd;
    int header_received;
    int payload_received;
    sm_hdr_t header;
    char payload[4096];
    char reply[4096];
    int reply_size;
} client_conn_t;

static client_conn_t clients[MAX_CLIENTS];

static int find_free_client_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) {
            return i;
        }
    }
    return -1;
}

static void close_client(int slot)
{
    if (slot >= 0 && slot < MAX_CLIENTS) {
        if (clients[slot].fd >= 0) {
            close(clients[slot].fd);
        }
        clients[slot].fd = -1;
        clients[slot].client_state = CLIENT_CLOSED;
    }
}

static void init_clients(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].client_state = CLIENT_CLOSED;
        clients[i].header_received = 0;
        clients[i].payload_received = 0;
        clients[i].reply_size = 0;
    }
}

// ── STATE ──────────────────────────────────────────────────────────────────────

static volatile int running = 1;
static volatile sig_atomic_t sighup_received = 0;

// ── SIGNAL HANDLERS ────────────────────────────────────────────────────────────

static void handle_sigterm(int sig)
{
    (void)sig;
    running = 0;
}

static void handle_sighup(int sig)
{
    (void)sig;
    sighup_received = 1;
}

static void setup_signals(void)
{
    struct sigaction sa = {0};

    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    sa.sa_handler = handle_sighup;
    sigaction(SIGHUP,  &sa, NULL);

    // Ignore SIGPIPE (when client closes socket)
    signal(SIGPIPE, SIG_IGN);
}

// ── CLEANUP ────────────────────────────────────────────────────────────────────

static void cleanup(void)
{
    sm_log(SM_LOG_INFO, "shutting down...");

    sm_socket_cleanup();
    sm_registry_cleanup();
    sm_rate_limit_cleanup();

    sm_logging_close();
}

// ── MAIN LOOP ──────────────────────────────────────────────────────────────────

int sm_run(void)
{
    // Initialize logging FIRST
    if (sm_logging_init() < 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return -1;
    }

    sm_log(SM_LOG_INFO, "========== Service Manager Starting ==========");

    // Setup security
    if (sm_set_resource_limits() < 0) {
        sm_log(SM_LOG_ERROR, "failed to set resource limits");
        cleanup();
        return -1;
    }

    // Drop privileges (must be done BEFORE creating sockets)
    if (sm_drop_privileges() < 0) {
        sm_log(SM_LOG_ERROR, "failed to drop privileges");
        cleanup();
        return -1;
    }

    // Setup signals
    setup_signals();

    // Initialize modules
    if (sm_registry_init() < 0) {
        sm_log(SM_LOG_ERROR, "failed to initialize registry");
        cleanup();
        return -1;
    }

    if (sm_rate_limit_init() < 0) {
        sm_log(SM_LOG_ERROR, "failed to initialize rate limiter");
        cleanup();
        return -1;
    }

    // Setup socket
    if (sm_socket_setup() < 0) {
        sm_log(SM_LOG_ERROR, "failed to setup socket");
        cleanup();
        return -1;
    }

    // Setup seccomp (restricted syscalls)
    if (sm_setup_seccomp() < 0) {
        sm_log(SM_LOG_ERROR, "failed to setup seccomp");
        cleanup();
        return -1;
    }

    // Initialize client connection tracking
    init_clients();

    // Setup epoll
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        sm_log(SM_LOG_ERROR, "epoll_create1 failed: %m");
        cleanup();
        return -1;
    }

    int server_fd = sm_socket_get_fd();
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = server_fd };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        sm_log(SM_LOG_ERROR, "epoll_ctl failed: %m");
        close(epoll_fd);
        cleanup();
        return -1;
    }

    sm_log(SM_LOG_INFO, "service manager initialized and ready");

    // ── MAIN EVENT LOOP ───────────────────────────────────────────────────────

    struct epoll_event events[MAX_CLIENTS + 1];
    time_t last_health_check = time(NULL);

    while (running) {
        int n = epoll_wait(epoll_fd, events, MAX_CLIENTS + 1, 3000);

        if (n < 0) {
            if (errno == EINTR) {
                // Handle signals
                if (sighup_received) {
                    sighup_received = 0;
                    sm_log(SM_LOG_INFO, "SIGHUP received");
                    // Could reload configuration here
                }
                continue;
            }
            sm_log(SM_LOG_ERROR, "epoll_wait failed: %m");
            break;
        }

        // Process events
        for (int i = 0; i < n; i++) {
            // Accept new connections
            if (events[i].data.fd == server_fd) {
                while (1) {
                    int client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            sm_log(SM_LOG_ERROR, "accept failed: %m");
                        }
                        break;
                    }

                    // Find a free slot
                    int slot = find_free_client_slot();
                    if (slot < 0) {
                        sm_log(SM_LOG_WARN, "max clients reached, dropping connection");
                        close(client_fd);
                        break;
                    }

                    // Set non-blocking
                    fcntl(client_fd, F_SETFL, O_NONBLOCK);

                    // Track client
                    clients[slot].fd = client_fd;
                    clients[slot].client_state = CLIENT_READING_HEADER;
                    clients[slot].header_received = 0;
                    clients[slot].payload_received = 0;

                    // Add to epoll
                    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = &clients[slot] };
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        sm_log(SM_LOG_ERROR, "epoll_ctl add client failed: %m");
                        close(client_fd);
                        clients[slot].fd = -1;
                    }
                }
            } else {
                // Handle client data
                client_conn_t* client = (client_conn_t*)events[i].data.ptr;
                if (!client || client->fd < 0) continue;

                if (events[i].events & EPOLLIN) {
                    // Read header
                    if (client->client_state == CLIENT_READING_HEADER) {
                        int remaining = sizeof(sm_hdr_t) - client->header_received;
                        int nr = recv(client->fd, (char*)&client->header + client->header_received, remaining, 0);
                        if (nr <= 0) {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                            close_client(client - clients);
                            continue;
                        }
                        client->header_received += nr;

                        if (client->header_received == sizeof(sm_hdr_t)) {
                            // Validate and prepare for payload
                            if (sm_validate_header(&client->header, sizeof(sm_hdr_t)) < 0) {
                                sm_log(SM_LOG_WARN, "invalid header from client");
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                                close_client(client - clients);
                                continue;
                            }
                            client->client_state = CLIENT_READING_PAYLOAD;
                            client->payload_received = 0;
                        }
                    }
                    // Read payload
                    else if (client->client_state == CLIENT_READING_PAYLOAD) {
                        int remaining = client->header.length - client->payload_received;
                        if (remaining > 0) {
                            int nr = recv(client->fd, client->payload + client->payload_received, remaining, 0);
                            if (nr <= 0) {
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                                close_client(client - clients);
                                continue;
                            }
                            client->payload_received += nr;
                        }

                        if ((uint32_t)client->payload_received == client->header.length) {
                            // Message complete - handle it
                            sm_handle_client_async(client->fd, &client->header, client->payload, 
                                                  client->reply, sizeof(client->reply), &client->reply_size);
                            client->client_state = CLIENT_SENDING_REPLY;

                            // Switch to write mode
                            struct epoll_event ev = { .events = EPOLLOUT, .data.ptr = client };
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &ev);
                        }
                    }
                }

                if (events[i].events & EPOLLOUT) {
                    // Send reply
                    if (client->client_state == CLIENT_SENDING_REPLY) {
                        int nw = send(client->fd, client->reply, client->reply_size, 0);
                        if (nw < 0) {
                            sm_log(SM_LOG_ERROR, "send reply failed: %m");
                        }
                        // Close after sending reply
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                        close_client(client - clients);
                    }
                }
            }
        }

        // Periodic health check
        time_t now = time(NULL);
        if (now - last_health_check >= SM_HEALTH_CHECK_INTERVAL) {
            sm_health_check();
            last_health_check = now;
        }
    }

    // ── SHUTDOWN ──────────────────────────────────────────────────────────────

    // Close all client connections
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            close(clients[i].fd);
        }
    }

    close(epoll_fd);
    cleanup();

    sm_log(SM_LOG_INFO, "========== Service Manager Stopped ==========");
    return 0;
}

// ── CLIENT-SIDE API ────────────────────────────────────────────────────────────
// These functions are used by services/apps to communicate with SM

// Persistent connection helpers
static int _sm_send_message(int fd, const sm_hdr_t* hdr, const void* payload, size_t payload_size)
{
    // Send header first
    if (send(fd, hdr, sizeof(sm_hdr_t), 0) < 0) return -1;
    // Send payload if provided
    if (payload && payload_size > 0) {
        if (send(fd, payload, payload_size, 0) < 0) return -1;
    }
    return 0;
}

static int _sm_transact(int fd, const sm_hdr_t* hdr, const void* payload, size_t payload_size, void* reply, size_t reply_size)
{
    if (_sm_send_message(fd, hdr, payload, payload_size) < 0) return -1;
    if (recv(fd, reply, reply_size, MSG_WAITALL) < 0) return -1;
    return 0;
}

int sm_connect_persistent(void)
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

int sm_register(const char* name, const char* socket_path, const char* ring_name)
{
    int fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    sm_hdr_t hdr = {
        .magic = SM_PROTOCOL_MAGIC,
        .version = SM_PROTOCOL_VERSION,
        .type = SM_MSG_REGISTER,
        .length = sizeof(sm_register_req_t),
        .timestamp = (uint32_t)time(NULL),
        .client_pid = (uint32_t)getpid(),
    };

    sm_register_req_t req = {0};
    strncpy(req.service_name, name, SM_MAX_NAME - 1);
    strncpy(req.socket_path, socket_path, SM_MAX_PATH - 1);
    strncpy(req.ring_name, ring_name, SM_MAX_PATH - 1);
    req.pid = getpid();

    sm_reply_t reply = {0};
    if (_sm_transact(fd, &hdr, &req, sizeof(req), &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);
    return reply.response_code;
}

int sm_lookup(const char* name, char* socket_path_out, char* ring_name_out)
{
    int fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    sm_hdr_t hdr = {
        .magic = SM_PROTOCOL_MAGIC,
        .version = SM_PROTOCOL_VERSION,
        .type = SM_MSG_LOOKUP,
        .length = sizeof(sm_lookup_req_t),
        .timestamp = (uint32_t)time(NULL),
        .client_pid = (uint32_t)getpid(),
    };

    sm_lookup_req_t req = {0};
    strncpy(req.service_name, name, SM_MAX_NAME - 1);

    sm_lookup_reply_t reply = {0};
    if (_sm_transact(fd, &hdr, &req, sizeof(req), &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    if (socket_path_out) strncpy(socket_path_out, reply.socket_path, SM_MAX_PATH - 1);
    if (ring_name_out) strncpy(ring_name_out, reply.ring_name, SM_MAX_PATH - 1);

    close(fd);
    return 0;
}

int sm_heartbeat(const char* name)
{
    int fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    sm_hdr_t hdr = {
        .magic = SM_PROTOCOL_MAGIC,
        .version = SM_PROTOCOL_VERSION,
        .type = SM_MSG_HEARTBEAT,
        .length = sizeof(sm_heartbeat_req_t),
        .timestamp = (uint32_t)time(NULL),
        .client_pid = (uint32_t)getpid(),
    };

    sm_heartbeat_req_t req = {0};
    strncpy(req.service_name, name, SM_MAX_NAME - 1);

    sm_reply_t reply = {0};
    if (_sm_transact(fd, &hdr, &req, sizeof(req), &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);
    return reply.response_code;
}

int sm_unregister(const char* name)
{
    int fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    sm_hdr_t hdr = {
        .magic = SM_PROTOCOL_MAGIC,
        .version = SM_PROTOCOL_VERSION,
        .type = SM_MSG_UNREGISTER,
        .length = sizeof(sm_unregister_req_t),
        .timestamp = (uint32_t)time(NULL),
        .client_pid = (uint32_t)getpid(),
    };

    sm_unregister_req_t req = {0};
    strncpy(req.service_name, name, SM_MAX_NAME - 1);

    sm_reply_t reply = {0};
    if (_sm_transact(fd, &hdr, &req, sizeof(req), &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);
    return reply.response_code;
}

void sm_disconnect(int fd)
{
    if (fd >= 0) close(fd);
}

// ── ENTRY POINT ────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    return sm_run();
}
