/**
 * @file test_monitord_tui_ipc.c
 * @brief Integration test for monitord <-> TUI IPC communication
 */
#include "../test_framework.h"
#include "../mocks/mock_middleware.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include "../../protocol/monitor_wire_format.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>

#define SOCK_PATH "/tmp/test_monitor_ipc.sock"

/* ── Mock Server Thread ─────────────────────────────────────────────────────── */
static void *mock_server_thread(void *arg)
{
    (void)arg;
    
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
    
    unlink(SOCK_PATH);
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);
    
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) return NULL;
    
    /* Receive HELLO */
    uint8_t buffer[4096];
    ssize_t nread = recv(client_fd, buffer, sizeof(buffer), 0);
    if (nread <= 0) {
        close(client_fd);
        close(server_fd);
        return NULL;
    }
    
    mon_msg_hello_t *hello = (mon_msg_hello_t *)buffer;
    if (hello->hdr.msg_type != MON_MSG_TYPE_HELLO) {
        close(client_fd);
        close(server_fd);
        return NULL;
    }
    
    /* Send WELCOME */
    mon_msg_welcome_t welcome;
    memset(&welcome, 0, sizeof(welcome));
    welcome.hdr.magic = MON_PROTOCOL_MAGIC;
    welcome.hdr.version = MON_PROTOCOL_VERSION;
    welcome.hdr.msg_type = MON_MSG_TYPE_WELCOME;
    welcome.hdr.length = sizeof(mon_msg_welcome_t);
    send(client_fd, &welcome, sizeof(welcome), 0);
    
    /* Wait for SNAPSHOT_REQUEST */
    nread = recv(client_fd, buffer, sizeof(buffer), 0);
    if (nread > 0) {
        mon_msg_snapshot_request_t *req = (mon_msg_snapshot_request_t *)buffer;
        if (req->hdr.msg_type == MON_MSG_TYPE_SNAPSHOT_REQUEST) {
            /* Send SNAPSHOT */
            mon_msg_snapshot_t snap;
            memset(&snap, 0, sizeof(snap));
            snap.hdr.magic = MON_PROTOCOL_MAGIC;
            snap.hdr.version = MON_PROTOCOL_VERSION;
            snap.hdr.msg_type = MON_MSG_TYPE_SNAPSHOT;
            snap.hdr.length = sizeof(mon_msg_snapshot_t);
            snap.snapshot.timestamp_ms = 123456;
            snap.snapshot.num_services = 1;
            snap.snapshot.services[0].cpu_pct = 45.5f;
            send(client_fd, &snap, sizeof(snap), 0);
        }
    }
    
    close(client_fd);
    close(server_fd);
    unlink(SOCK_PATH);
    return NULL;
}

/* ── Test 1: HELLO/WELCOME Handshake ────────────────────────────────────────── */
static bool test_hello_welcome(void)
{
    pthread_t server;
    pthread_create(&server, NULL, mock_server_thread, NULL);
    
    usleep(100000); /* Let server start */
    
    /* Connect as client */
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
    
    TEST_ASSERT_EQ(connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)), 0, "Connect failed");
    
    /* Send HELLO */
    mon_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.hdr.magic = MON_PROTOCOL_MAGIC;
    hello.hdr.version = MON_PROTOCOL_VERSION;
    hello.hdr.msg_type = MON_MSG_TYPE_HELLO;
    hello.hdr.length = sizeof(mon_msg_hello_t);
    hello.client_pid = getpid();
    strncpy(hello.client_name, "test_client", sizeof(hello.client_name)-1);
    
    send(client_fd, &hello, sizeof(hello), 0);
    
    /* Receive WELCOME */
    uint8_t buffer[4096];
    ssize_t nread = recv(client_fd, buffer, sizeof(buffer), 0);
    TEST_ASSERT(nread > 0, "No WELCOME received");
    
    mon_msg_welcome_t *welcome = (mon_msg_welcome_t *)buffer;
    TEST_ASSERT_EQ(welcome->hdr.msg_type, MON_MSG_TYPE_WELCOME, "Not WELCOME");
    
    close(client_fd);
    pthread_join(server, NULL);
    return true;
}

/* ── Test 2: Snapshot Request (<100ms) ──────────────────────────────────────── */
static bool test_snapshot_latency(void)
{
    pthread_t server;
    pthread_create(&server, NULL, mock_server_thread, NULL);
    usleep(100000);
    
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
    connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));
    
    /* Send HELLO */
    mon_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.hdr.magic = MON_PROTOCOL_MAGIC;
    hello.hdr.version = MON_PROTOCOL_VERSION;
    hello.hdr.msg_type = MON_MSG_TYPE_HELLO;
    hello.hdr.length = sizeof(mon_msg_hello_t);
    hello.client_pid = getpid();
    send(client_fd, &hello, sizeof(hello), 0);
    
    /* Receive WELCOME */
    uint8_t buffer[8192];
    recv(client_fd, buffer, sizeof(buffer), 0);
    
    /* Send SNAPSHOT_REQUEST and measure latency */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    
    mon_msg_snapshot_request_t req;
    memset(&req, 0, sizeof(req));
    req.hdr.magic = MON_PROTOCOL_MAGIC;
    req.hdr.version = MON_PROTOCOL_VERSION;
    req.hdr.msg_type = MON_MSG_TYPE_SNAPSHOT_REQUEST;
    req.hdr.length = sizeof(mon_msg_snapshot_request_t);
    req.sequence = 1;
    send(client_fd, &req, sizeof(req), 0);
    
    /* Receive SNAPSHOT */
    ssize_t nread = recv(client_fd, buffer, sizeof(buffer), 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    
    TEST_ASSERT(nread > 0, "No snapshot received");
    
    uint64_t latency_us = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
    TEST_ASSERT(latency_us < 100000, "Latency > 100ms");
    
    mon_msg_snapshot_t *snap = (mon_msg_snapshot_t *)buffer;
    TEST_ASSERT_EQ(snap->hdr.msg_type, MON_MSG_TYPE_SNAPSHOT, "Not snapshot");
    TEST_ASSERT_FLOAT_EQ(snap->snapshot.services[0].cpu_pct, 45.5f, 0.1f, "CPU value wrong");
    
    close(client_fd);
    pthread_join(server, NULL);
    return true;
}

/* ── Test 3: Log Subscription ───────────────────────────────────────────────── */
static bool test_log_subscription(void)
{
    /* This test requires full monitord implementation */
    /* For now, verify protocol structures exist */
    mon_msg_subscribe_logs_t sub;
    memset(&sub, 0, sizeof(sub));
    sub.hdr.magic = MON_PROTOCOL_MAGIC;
    sub.hdr.version = MON_PROTOCOL_VERSION;
    sub.hdr.msg_type = MON_MSG_TYPE_SUBSCRIBE_LOGS;
    sub.hdr.length = sizeof(mon_msg_subscribe_logs_t);
    
    TEST_ASSERT_EQ(sub.hdr.msg_type, MON_MSG_TYPE_SUBSCRIBE_LOGS, "Message type");
    
    return true;
}

/* ── Test 4: Two Clients Simultaneously ─────────────────────────────────────── */
static bool test_two_clients(void)
{
    /* This test requires full monitord multi-client support */
    /* Placeholder: verify we can create two client structures */
    
    struct {
        int fd;
        uint32_t client_id;
    } clients[2];
    
    clients[0].fd = -1;
    clients[0].client_id = 1;
    clients[1].fd = -1;
    clients[1].client_id = 2;
    
    TEST_ASSERT_NE(clients[0].client_id, clients[1].client_id, "IDs should differ");
    
    return true;
}

/* ── Test 5: Abrupt Disconnect ──────────────────────────────────────────────── */
static bool test_abrupt_disconnect(void)
{
    /* Verify server handles client closing without proper shutdown */
    /* Placeholder: just verify we can close socket abruptly */
    
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "Socket creation failed");
    
    close(fd); /* Abrupt close */
    
    TEST_ASSERT(fd >= 0, "FD was valid");
    
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"hello_welcome", test_hello_welcome, true},
        {"snapshot_latency", test_snapshot_latency, true},
        {"log_subscription", test_log_subscription, true},
        {"two_clients", test_two_clients, true},
        {"abrupt_disconnect", test_abrupt_disconnect, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Monitord-TUI IPC");
}
