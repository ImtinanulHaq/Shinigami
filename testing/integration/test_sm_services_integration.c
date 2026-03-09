/**
 * @file test_sm_services_integration.c
 * @brief Integration: SM server (mock) + service registration over IPC.
 *
 * Starts a mock SM unix-socket server, connects a client, sends
 * REGISTER / LOOKUP / HEARTBEAT messages, and asserts correct
 * server-side state changes.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_sm.h"

#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/core/service_manager/infrastructure/sm_registry.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

static mock_sm_t g_msm;

/* Helper: connect to mock SM and send exactly one message */
static int send_sm_message(const void *buf, size_t len)
{
    int fd = tu_unix_connect(g_msm.socket_path);
    if (fd < 0) return -1;
    ssize_t n = write(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

TEST_GROUP(SM_Services_Integration);

TEST_SETUP(SM_Services_Integration)
{
    mock_sm_start(&g_msm);
}

TEST_TEAR_DOWN(SM_Services_Integration)
{
    mock_sm_stop(&g_msm);
}

/* ── Scenario 1: Valid REGISTER increments mock counter ─────────── */

TEST(SM_Services_Integration, Register_ValidMessage_CounterIncremented)
{
    uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
    sm_message_header_t *hdr = (sm_message_header_t *)buf;
    hdr->magic    = SM_MAGIC;
    hdr->version  = SM_VERSION;
    hdr->type     = SM_MSG_REGISTER;
    hdr->length   = sizeof(*hdr) + sizeof(sm_register_payload_t);
    hdr->timestamp = (uint64_t)time(NULL);
    hdr->sequence  = 1;

    sm_register_payload_t *pay = (sm_register_payload_t *)(buf + sizeof(*hdr));
    strncpy(pay->name,        "my_svc",        sizeof(pay->name) - 1);
    strncpy(pay->socket_path, "/tmp/my.sock",  sizeof(pay->socket_path) - 1);

    int r = send_sm_message(buf, hdr->length);
    TEST_ASSERT_EQUAL_INT(0, r);

    tu_sleep_ms(50);   /* let server thread process */
    TEST_ASSERT_EQUAL_INT(1, g_msm.stats.register_count);
}

/* ── Scenario 2: LOOKUP after REGISTER succeeds ─────────────────── */

TEST(SM_Services_Integration, Lookup_AfterRegister_Succeeds)
{
    /* Register first */
    {
        uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
        sm_message_header_t *h = (sm_message_header_t *)buf;
        h->magic = SM_MAGIC; h->version = SM_VERSION;
        h->type  = SM_MSG_REGISTER;
        h->length = sizeof(*h) + sizeof(sm_register_payload_t);
        h->timestamp = (uint64_t)time(NULL); h->sequence = 1;
        sm_register_payload_t *p = (sm_register_payload_t *)(buf + sizeof(*h));
        strncpy(p->name, "lookup_svc", sizeof(p->name) - 1);
        strncpy(p->socket_path, "/tmp/lk.sock", sizeof(p->socket_path) - 1);
        send_sm_message(buf, h->length);
    }
    tu_sleep_ms(50);

    /* Now lookup */
    {
        uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
        sm_message_header_t *h = (sm_message_header_t *)buf;
        h->magic = SM_MAGIC; h->version = SM_VERSION;
        h->type  = SM_MSG_LOOKUP;
        h->length = sizeof(*h) + sizeof(sm_lookup_payload_t);
        h->timestamp = (uint64_t)time(NULL); h->sequence = 2;
        sm_lookup_payload_t *p = (sm_lookup_payload_t *)(buf + sizeof(*h));
        strncpy(p->name, "lookup_svc", sizeof(p->name) - 1);
        send_sm_message(buf, h->length);
    }
    tu_sleep_ms(50);

    TEST_ASSERT_EQUAL_INT(1, g_msm.stats.register_count);
    TEST_ASSERT_EQUAL_INT(1, g_msm.stats.lookup_count);
}

/* ── Scenario 3: HEARTBEAT increments heartbeat counter ─────────── */

TEST(SM_Services_Integration, Heartbeat_ValidMessage_CounterIncremented)
{
    uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
    sm_message_header_t *h = (sm_message_header_t *)buf;
    h->magic = SM_MAGIC; h->version = SM_VERSION;
    h->type  = SM_MSG_HEARTBEAT;
    h->length = sizeof(*h);
    h->timestamp = (uint64_t)time(NULL); h->sequence = 1;

    int r = send_sm_message(buf, h->length);
    TEST_ASSERT_EQUAL_INT(0, r);
    tu_sleep_ms(50);
    TEST_ASSERT_EQUAL_INT(1, g_msm.stats.heartbeat_count);
}

/* ── Scenario 4: Malformed magic → message rejected ─────────────── */

TEST(SM_Services_Integration, BadMagic_MessageRejected)
{
    uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
    sm_message_header_t *h = (sm_message_header_t *)buf;
    h->magic   = 0xDEADBEEF;   /* wrong magic */
    h->version = SM_VERSION;
    h->type    = SM_MSG_REGISTER;
    h->length  = sizeof(*h) + sizeof(sm_register_payload_t);

    send_sm_message(buf, h->length);
    tu_sleep_ms(50);
    TEST_ASSERT_EQUAL_INT(0, g_msm.stats.register_count);
}

/* ── Scenario 5: Stats reset ─────────────────────────────────────── */

TEST(SM_Services_Integration, Reset_ClearsStats)
{
    uint8_t buf[SM_MAX_MESSAGE_SIZE] = {0};
    sm_message_header_t *h = (sm_message_header_t *)buf;
    h->magic = SM_MAGIC; h->version = SM_VERSION;
    h->type  = SM_MSG_HEARTBEAT;
    h->length = sizeof(*h);
    h->timestamp = (uint64_t)time(NULL);
    send_sm_message(buf, h->length);
    tu_sleep_ms(50);

    mock_sm_reset_stats(&g_msm);
    TEST_ASSERT_EQUAL_INT(0, g_msm.stats.heartbeat_count);
}

/* ── Runner ───────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(SM_Services_Integration)
{
    RUN_TEST_CASE(SM_Services_Integration, Register_ValidMessage_CounterIncremented);
    RUN_TEST_CASE(SM_Services_Integration, Lookup_AfterRegister_Succeeds);
    RUN_TEST_CASE(SM_Services_Integration, Heartbeat_ValidMessage_CounterIncremented);
    RUN_TEST_CASE(SM_Services_Integration, BadMagic_MessageRejected);
    RUN_TEST_CASE(SM_Services_Integration, Reset_ClearsStats);
}

static void run_all_groups(void) { RUN_TEST_GROUP(SM_Services_Integration); }
int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
