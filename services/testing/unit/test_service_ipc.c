/**
 * @file test_service_ipc.c
 * @brief Unit tests for service_ipc: SM register, unregister, heartbeat.
 *
 * Uses mock_sm to create a real Unix socket responder and verifies that
 * svc_ipc_connect / svc_ipc_register / svc_ipc_heartbeat / svc_ipc_unregister
 * produce the correct messages on the wire.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../common/service_ipc.h"
#include "../mocks/mock_sm.h"

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do { printf("  [RUN ]  " #name "\n"); test_##name(); \
                        printf("  [ OK ]  " #name "\n"); } while (0)

#define MOCK_SOCK "/tmp/test_mock_sm.sock"

/* ── tests ────────────────────────────────────────────────────────────── */

TEST(ipc_init_basic)
{
    svc_ipc_t ipc;
    int rc = svc_ipc_init(&ipc, "test_svc", NULL);
    assert(rc == SVC_OK);
    assert(ipc.fd == -1);
    assert(strcmp(ipc.service_name, "test_svc") == 0);
    assert(ipc.authenticated == 0);
}

TEST(ipc_init_null_name)
{
    svc_ipc_t ipc;
    int rc = svc_ipc_init(&ipc, NULL, NULL);
    assert(rc == SVC_ERR_INVALID);
}

TEST(ipc_connect_no_server)
{
    svc_ipc_t ipc;
    svc_ipc_init(&ipc, "test_svc", NULL);
    strncpy(ipc.socket_path, "/tmp/nonexistent_12345.sock",
            sizeof(ipc.socket_path) - 1);
    int rc = svc_ipc_connect(&ipc);
    assert(rc != SVC_OK);
    assert(!svc_ipc_is_connected(&ipc));
}

TEST(ipc_register_and_heartbeat)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);

    svc_ipc_t ipc;
    svc_ipc_init(&ipc, "test_svc", NULL);
    strncpy(ipc.socket_path, MOCK_SOCK, sizeof(ipc.socket_path) - 1);

    int rc = svc_ipc_connect(&ipc);
    assert(rc == SVC_OK);
    assert(svc_ipc_is_connected(&ipc));

    rc = svc_ipc_register(&ipc, "/tmp/test_svc.sock", getpid());
    assert(rc == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(mock_sm_is_registered(&sm, "test_svc") == 1);

    rc = svc_ipc_heartbeat(&ipc);
    assert(rc == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(sm.heartbeat_count >= 1);

    rc = svc_ipc_unregister(&ipc);
    assert(rc == SVC_OK);
    assert(mock_sm_wait_request(&sm, 2000) == 0);
    assert(sm.unregister_count >= 1);

    svc_ipc_close(&ipc);
    assert(!svc_ipc_is_connected(&ipc));

    mock_sm_stop(&sm);
}

TEST(ipc_heartbeat_rejected)
{
    mock_sm_t sm;
    assert(mock_sm_start(&sm, MOCK_SOCK) == 0);
    sm.heartbeat_reply = SM_ERR_NOT_FOUND;  /* inject rejection */

    svc_ipc_t ipc;
    svc_ipc_init(&ipc, "bad_svc", NULL);
    strncpy(ipc.socket_path, MOCK_SOCK, sizeof(ipc.socket_path) - 1);

    svc_ipc_connect(&ipc);
    int rc = svc_ipc_heartbeat(&ipc);
    assert(rc != SVC_OK);

    svc_ipc_close(&ipc);
    mock_sm_stop(&sm);
}

TEST(ipc_is_connected_null)
{
    assert(!svc_ipc_is_connected(NULL));
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== test_service_ipc ===\n");
    RUN(ipc_init_basic);
    RUN(ipc_init_null_name);
    RUN(ipc_connect_no_server);
    RUN(ipc_register_and_heartbeat);
    RUN(ipc_heartbeat_rejected);
    RUN(ipc_is_connected_null);
    printf("All tests passed.\n");
    return EXIT_SUCCESS;
}
