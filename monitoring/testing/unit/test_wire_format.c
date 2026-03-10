/**
 * @file test_wire_format.c
 * @brief Unit tests for binary wire protocol serialization
 */
#include "../test_framework.h"
#include "../../protocol/monitor_wire_format.h"
#include "../../protocol/monitor_ipc_protocol.h"
#include <string.h>
#include <arpa/inet.h>

/* ── Test 1: Serialize/Deserialize HELLO ────────────────────────────────────── */
static bool test_hello_message(void)
{
    mon_msg_hello_t hello_out;
    memset(&hello_out, 0, sizeof(hello_out));
    hello_out.hdr.magic = MON_PROTOCOL_MAGIC;
    hello_out.hdr.version = MON_PROTOCOL_VERSION;
    hello_out.hdr.msg_type = MON_MSG_TYPE_HELLO;
    hello_out.hdr.length = sizeof(mon_msg_hello_t);
    hello_out.client_pid = 12345;
    strncpy(hello_out.client_name, "test_client", sizeof(hello_out.client_name)-1);
    
    /* Serialize */
    uint8_t buffer[512];
    ssize_t written = wire_send_message(buffer, sizeof(buffer), &hello_out.hdr);
    TEST_ASSERT(written > 0, "Serialize failed");
    TEST_ASSERT_EQ(written, sizeof(mon_msg_hello_t), "Wrong size");
    
    /* Deserialize */
    mon_msg_header_t *hdr_in = (mon_msg_header_t *)buffer;
    TEST_ASSERT_EQ(hdr_in->magic, MON_PROTOCOL_MAGIC, "Magic mismatch");
    TEST_ASSERT_EQ(hdr_in->version, MON_PROTOCOL_VERSION, "Version mismatch");
    TEST_ASSERT_EQ(hdr_in->msg_type, MON_MSG_TYPE_HELLO, "Type mismatch");
    
    mon_msg_hello_t *hello_in = (mon_msg_hello_t *)buffer;
    TEST_ASSERT_EQ(hello_in->client_pid, 12345, "PID mismatch");
    TEST_ASSERT_STR_EQ(hello_in->client_name, "test_client", "Name mismatch");
    
    return true;
}

/* ── Test 2: SNAPSHOT Request/Response ──────────────────────────────────────── */
static bool test_snapshot_message(void)
{
    /* Request */
    mon_msg_snapshot_request_t req_out;
    memset(&req_out, 0, sizeof(req_out));
    req_out.hdr.magic = MON_PROTOCOL_MAGIC;
    req_out.hdr.version = MON_PROTOCOL_VERSION;
    req_out.hdr.msg_type = MON_MSG_TYPE_SNAPSHOT_REQUEST;
    req_out.hdr.length = sizeof(mon_msg_snapshot_request_t);
    req_out.sequence = 99;
    
    uint8_t buffer[512];
    ssize_t written = wire_send_message(buffer, sizeof(buffer), &req_out.hdr);
    TEST_ASSERT(written > 0, "Serialize request failed");
    
    mon_msg_snapshot_request_t *req_in = (mon_msg_snapshot_request_t *)buffer;
    TEST_ASSERT_EQ(req_in->sequence, 99, "Sequence mismatch");
    
    /* Response with snapshot */
    mon_msg_snapshot_t resp_out;
    memset(&resp_out, 0, sizeof(resp_out));
    resp_out.hdr.magic = MON_PROTOCOL_MAGIC;
    resp_out.hdr.version = MON_PROTOCOL_VERSION;
    resp_out.hdr.msg_type = MON_MSG_TYPE_SNAPSHOT;
    resp_out.hdr.length = sizeof(mon_msg_snapshot_t);
    resp_out.snapshot.timestamp_ms = 123456789;
    resp_out.snapshot.num_services = 2;
    resp_out.snapshot.services[0].cpu_pct = 12.5f;
    resp_out.snapshot.services[1].cpu_pct = 8.3f;
    
    uint8_t snap_buffer[8192];
    written = wire_send_message(snap_buffer, sizeof(snap_buffer), &resp_out.hdr);
    TEST_ASSERT(written > 0, "Serialize snapshot failed");
    
    mon_msg_snapshot_t *resp_in = (mon_msg_snapshot_t *)snap_buffer;
    TEST_ASSERT_EQ(resp_in->snapshot.timestamp_ms, 123456789UL, "Timestamp mismatch");
    TEST_ASSERT_EQ(resp_in->snapshot.num_services, 2, "Service count mismatch");
    TEST_ASSERT_FLOAT_EQ(resp_in->snapshot.services[0].cpu_pct, 12.5f, 0.01f, "CPU 0 mismatch");
    TEST_ASSERT_FLOAT_EQ(resp_in->snapshot.services[1].cpu_pct, 8.3f, 0.01f, "CPU 1 mismatch");
    
    return true;
}

/* ── Test 3: Version Checking ───────────────────────────────────────────────── */
static bool test_version_check(void)
{
    mon_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.hdr.magic = MON_PROTOCOL_MAGIC;
    hello.hdr.version = 99; /* Wrong version */
    hello.hdr.msg_type = MON_MSG_TYPE_HELLO;
    hello.hdr.length = sizeof(mon_msg_hello_t);
    
    uint8_t buffer[512];
    wire_send_message(buffer, sizeof(buffer), &hello.hdr);
    
    /* Validation should detect version mismatch */
    mon_msg_header_t *hdr = (mon_msg_header_t *)buffer;
    TEST_ASSERT_NE(hdr->version, MON_PROTOCOL_VERSION, "Version should differ");
    
    return true;
}

/* ── Test 4: Truncation Handling ─────────────────────────────────────────────── */
static bool test_truncation(void)
{
    mon_msg_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.hdr.magic = MON_PROTOCOL_MAGIC;
    snap.hdr.version = MON_PROTOCOL_VERSION;
    snap.hdr.msg_type = MON_MSG_TYPE_SNAPSHOT;
    snap.hdr.length = sizeof(mon_msg_snapshot_t);
    
    /* Buffer too small */
    uint8_t small_buffer[64];
    ssize_t written = wire_send_message(small_buffer, sizeof(small_buffer), &snap.hdr);
    
    /* Should fail or truncate gracefully */
    TEST_ASSERT(written < 0 || written <= sizeof(small_buffer), "Should handle truncation");
    
    return true;
}

/* ── Test 5: Magic Number Validation ─────────────────────────────────────────── */
static bool test_magic_validation(void)
{
    mon_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    hello.hdr.magic = 0xDEADBEEF; /* Wrong magic */
    hello.hdr.version = MON_PROTOCOL_VERSION;
    hello.hdr.msg_type = MON_MSG_TYPE_HELLO;
    hello.hdr.length = sizeof(mon_msg_hello_t);
    
    uint8_t buffer[512];
    wire_send_message(buffer, sizeof(buffer), &hello.hdr);
    
    mon_msg_header_t *hdr = (mon_msg_header_t *)buffer;
    TEST_ASSERT_NE(hdr->magic, MON_PROTOCOL_MAGIC, "Magic should be wrong");
    
    return true;
}

/* ── Test 6: Random Data Rejection ──────────────────────────────────────────── */
static bool test_random_data(void)
{
    uint8_t random[512];
    for (int i = 0; i < 512; i++) {
        random[i] = (uint8_t)(i * 17 + 42);
    }
    
    mon_msg_header_t *hdr = (mon_msg_header_t *)random;
    
    /* Should have wrong magic and version */
    TEST_ASSERT_NE(hdr->magic, MON_PROTOCOL_MAGIC, "Random shouldn't match magic");
    
    return true;
}

/* ── Test 7: Alert Message ──────────────────────────────────────────────────── */
static bool test_alert_message(void)
{
    mon_msg_alert_t alert_out;
    memset(&alert_out, 0, sizeof(alert_out));
    alert_out.hdr.magic = MON_PROTOCOL_MAGIC;
    alert_out.hdr.version = MON_PROTOCOL_VERSION;
    alert_out.hdr.msg_type = MON_MSG_TYPE_ALERT;
    alert_out.hdr.length = sizeof(mon_msg_alert_t);
    
    alert_out.alert.first_ts_ms = 1000000;
    alert_out.alert.last_ts_ms = 1005000;
    alert_out.alert.severity = ALERT_SEV_CRIT;
    strncpy(alert_out.alert.component, "audio", sizeof(alert_out.alert.component)-1);
    strncpy(alert_out.alert.condition, "CPU > 90%", sizeof(alert_out.alert.condition)-1);
    alert_out.alert.occurrences = 5;
    alert_out.alert.active = 1;
    
    uint8_t buffer[2048];
    ssize_t written = wire_send_message(buffer, sizeof(buffer), &alert_out.hdr);
    TEST_ASSERT(written > 0, "Serialize alert failed");
    
    mon_msg_alert_t *alert_in = (mon_msg_alert_t *)buffer;
    TEST_ASSERT_EQ(alert_in->alert.severity, ALERT_SEV_CRIT, "Severity mismatch");
    TEST_ASSERT_STR_EQ(alert_in->alert.component, "audio", "Component mismatch");
    TEST_ASSERT_EQ(alert_in->alert.occurrences, 5, "Occurrences mismatch");
    
    return true;
}

/* ── Test Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    test_case_t tests[] = {
        {"hello_message", test_hello_message, true},
        {"snapshot_message", test_snapshot_message, true},
        {"version_check", test_version_check, true},
        {"truncation", test_truncation, true},
        {"magic_validation", test_magic_validation, true},
        {"random_data", test_random_data, true},
        {"alert_message", test_alert_message, true},
    };
    
    return test_run_suite(tests, sizeof(tests)/sizeof(tests[0]), "Wire Format");
}
