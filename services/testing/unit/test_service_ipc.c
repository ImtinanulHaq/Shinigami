/**
 * @file test_service_ipc.c
 * @brief Unit tests for common/service_ipc using mock_sm.
 *
 * Tests covered:
 *   1. service_ipc_init() — null guards, default field values.
 *   2. service_ipc_connect() — succeeds against a running mock_sm;
 *      fails gracefully when no server is listening.
 *   3. service_ipc_register() — sends a valid REGISTER message and the mock
 *      SM records the service as registered.
 *   4. service_ipc_heartbeat() — mock SM records a heartbeat message.
 *   5. service_ipc_ping() — mock SM responds with PONG; function returns SVC_OK.
 *   6. service_ipc_is_connected() — returns 1 after connect, 0 after disconnect.
 *   7. service_ipc_unregister() — SM marks the service as unregistered.
 *   8. MSG_HEALTH_OK response — service_ipc_send_health() sends a well-formed
 *      SVC_MSG_HEALTH_OK frame.
 *   9. service_ipc_connect() with no server — must return SVC_ERR_IPC.
 *
 * Compile:
 *   gcc -Wall -Wextra -Werror -Wshadow -Wformat=2 \
 *       test_service_ipc.c \
 *       ../../common/service_base.c \
 *       ../../common/service_ipc.c  \
 *       ../mocks/mock_sm.c          \
 *       -I../../common -I../mocks   \
 *       -lpthread -lssl -lcrypto    \
 *       -o test_service_ipc
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../../common/service_ipc.h"
#include "../mocks/mock_sm.h"

/* ── micro test framework ─────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)  static void test_##name(void)
#define RUN(name)   do {                                            \
    g_tests_run++;                                                  \
    printf("  [RUN ]  test_" #name "\n");                          \
    test_##name();                                                  \
    g_tests_passed++;                                               \
    printf("  [ OK ]  test_" #name "\n");                          \
} while (0)

/* ── shared mock SM for all tests ────────────────────────────────────── */

#define MOCK_SOCK  "/tmp/test_svc_ipc.sock"

static mock_sm_t g_sm;

static void setup_mock_sm(void)
{
    int rc = mock_sm_start(&g_sm, MOCK_SOCK);
    assert(rc == 0);
}

static void teardown_mock_sm(void)
{
    mock_sm_stop(&g_sm);
}

/* Build a connected + registered IPC context for use in sub-tests.
 * Caller must call service_ipc_disconnect() when done. */
static void make_connected_ipc(svc_ipc_t *ipc)
{
    int rc = service_ipc_init(ipc, "test_svc", NULL);
    assert(rc == SVC_OK);

    snprintf(ipc->socket_path, sizeof(ipc->socket_path), "%s", MOCK_SOCK);

    rc = service_ipc_connect(ipc);
    assert(rc == SVC_OK);
    assert(service_ipc_is_connected(ipc) == 1);
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1a. Init basic */
TEST(ipc_init_basic)
{
    svc_ipc_t ipc;
    int rc = service_ipc_init(&ipc, "test_svc", NULL);
    assert(rc == SVC_OK);
    assert(ipc.fd == -1);
    assert(strcmp(ipc.service_name, "test_svc") == 0);
    assert(service_ipc_is_connected(&ipc) == 0);
    assert(ipc.reconnect_backoff_sec == 1);
}

/* 1b. Null name guard */
TEST(ipc_init_null_name)
{
    svc_ipc_t ipc;
    int rc = service_ipc_init(&ipc, NULL, NULL);
    assert(rc == SVC_ERR_INVALID);
}

/* 1c. Null ctx guard */
TEST(ipc_init_null_ctx)
{
    int rc = service_ipc_init(NULL, "test_svc", NULL);
    assert(rc == SVC_ERR_INVALID);
}

/* 2. Connect succeeds against running mock SM */
TEST(ipc_connect_ok)
{
    svc_ipc_t ipc;
    service_ipc_init(&ipc, "test_svc", NULL);
    snprintf(ipc.socket_path, sizeof(ipc.socket_path), "%s", MOCK_SOCK);

    int rc = service_ipc_connect(&ipc);
    assert(rc == SVC_OK);
    assert(service_ipc_is_connected(&ipc) == 1);

    service_ipc_disconnect(&ipc);
    assert(service_ipc_is_connected(&ipc) == 0);
}

/* 9. Connect with no server — must fail */
TEST(ipc_connect_no_server)
{
    svc_ipc_t ipc;
    service_ipc_init(&ipc, "test_svc", NULL);
    snprintf(ipc.socket_path, sizeof(ipc.socket_path),
             "/tmp/nonexistent_really_doesnt_exist_12345.sock");

    int rc = service_ipc_connect(&ipc);
    assert(rc != SVC_OK);
    assert(service_ipc_is_connected(&ipc) == 0);
}

/* 3. Register — SM records the service */
TEST(ipc_register_ok)
{
    mock_sm_reset(&g_sm);

    svc_ipc_t ipc;
    make_connected_ipc(&ipc);

    int rc = service_ipc_register(&ipc, "/usr/local/bin/test_svc",
                                  "2.0.0", getpid());
    assert(rc == SVC_OK);

    /* Wait for mock SM to process the REGISTER message */
    assert(mock_sm_wait_request(&g_sm, 2000) == 0);
    assert(mock_sm_is_registered(&g_sm, "test_svc") == 1);
    assert(g_sm.register_count >= 1);

    service_ipc_disconnect(&ipc);
}

/* 4. Heartbeat — SM records the heartbeat */
TEST(ipc_heartbeat_ok)
{
    mock_sm_reset(&g_sm);

    svc_ipc_t ipc;
    make_connected_ipc(&ipc);
    service_ipc_register(&ipc, "/usr/local/bin/test_svc", "2.0.0", getpid());
    mock_sm_wait_request(&g_sm, 2000);

    int rc = service_ipc_heartbeat(&ipc);
    assert(rc == SVC_OK);

    assert(mock_sm_wait_request(&g_sm, 2000) == 0);
    assert(g_sm.heartbeat_count >= 1);

    service_ipc_disconnect(&ipc);
}

/* 5. Ping — SM should respond with PONG */
TEST(ipc_ping_ok)
{
    mock_sm_reset(&g_sm);

    svc_ipc_t ipc;
    make_connected_ipc(&ipc);
    service_ipc_register(&ipc, "/usr/local/bin/test_svc", "2.0.0", getpid());
    mock_sm_wait_request(&g_sm, 2000);

    int rc = service_ipc_ping(&ipc);
    assert(rc == SVC_OK);

    service_ipc_disconnect(&ipc);
}

/* 6. is_connected transitions */
TEST(ipc_is_connected)
{
    svc_ipc_t ipc;
    service_ipc_init(&ipc, "test_svc", NULL);

    /* Not connected initially */
    assert(service_ipc_is_connected(&ipc) == 0);

    snprintf(ipc.socket_path, sizeof(ipc.socket_path), "%s", MOCK_SOCK);
    service_ipc_connect(&ipc);
    assert(service_ipc_is_connected(&ipc) == 1);

    service_ipc_disconnect(&ipc);
    assert(service_ipc_is_connected(&ipc) == 0);
}

/* 7. Unregister — SM marks service as unregistered */
TEST(ipc_unregister_ok)
{
    mock_sm_reset(&g_sm);

    svc_ipc_t ipc;
    make_connected_ipc(&ipc);
    service_ipc_register(&ipc, "/usr/local/bin/test_svc", "2.0.0", getpid());
    mock_sm_wait_request(&g_sm, 2000);

    int rc = service_ipc_unregister(&ipc);
    assert(rc == SVC_OK);
    assert(mock_sm_wait_request(&g_sm, 2000) == 0);
    assert(g_sm.unregister_count >= 1);
    /* Service should no longer appear registered */
    assert(mock_sm_is_registered(&g_sm, "test_svc") == 0);

    service_ipc_disconnect(&ipc);
}

/* 8. service_ipc_send_health() sends SVC_MSG_HEALTH_OK */
TEST(ipc_send_health_ok)
{
    mock_sm_reset(&g_sm);

    svc_ipc_t ipc;
    make_connected_ipc(&ipc);
    service_ipc_register(&ipc, "/usr/local/bin/test_svc", "2.0.0", getpid());
    mock_sm_wait_request(&g_sm, 2000);

    svc_health_status_t st;
    memset(&st, 0, sizeof(st));
    st.uptime_sec  = 123;
    st.error_count = 0;
    st.hal_state   = 2; /* HAL_STATE_ACTIVE */
    st.svc_state   = SVC_STATE_RUNNING;

    int rc = service_ipc_send_health(&ipc, &st);
    assert(rc == SVC_OK);

    /* Wait for mock SM to receive and process the health reply */
    mock_sm_wait_request(&g_sm, 2000);
    assert(g_sm.health_ok_count >= 1);

    service_ipc_disconnect(&ipc);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n=== test_service_ipc ===\n\n");

    /* Start mock SM once for all tests that need it */
    setup_mock_sm();

    RUN(ipc_init_basic);
    RUN(ipc_init_null_name);
    RUN(ipc_init_null_ctx);
    RUN(ipc_connect_no_server);   /* must be before connect_ok */
    RUN(ipc_connect_ok);
    RUN(ipc_register_ok);
    RUN(ipc_heartbeat_ok);
    RUN(ipc_ping_ok);
    RUN(ipc_is_connected);
    RUN(ipc_unregister_ok);
    RUN(ipc_send_health_ok);

    teardown_mock_sm();

    printf("\n=== Results: %d/%d passed ===\n\n",
           g_tests_passed, g_tests_run);

    return (g_tests_failed > 0) ? 1 : 0;
}
