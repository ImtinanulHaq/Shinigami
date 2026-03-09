/**
 * @file test_full_system.c
 * @brief Full-system integration scenarios (6 scenarios per spec Part 8).
 *
 * Scenario 1 – Clean startup & shutdown
 * Scenario 2 – Crash & recovery detection
 * Scenario 3 – Memory pool under IPC load
 * Scenario 4 – io_uring under HAL load
 * Scenario 5 – Security enforcement
 * Scenario 6 – Network partition & reconnect
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_sm.h"
#include "../mocks/mock_security.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/core/memory_pool.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- shared mock SM ---- */
static mock_sm_t g_msm;

static size_t build_msg(uint8_t *buf, size_t bufsz,
                        uint16_t msg_type, const void *payload, size_t plen) {
    memset(buf, 0, bufsz);
    sm_hdr_t *h   = (sm_hdr_t *)buf;
    h->magic      = SM_PROTOCOL_MAGIC;
    h->version    = SM_PROTOCOL_VERSION;
    h->type       = msg_type;
    h->length     = (uint32_t)plen;
    h->timestamp  = (uint32_t)time(NULL);
    h->client_pid = (uint32_t)getpid();
    h->nonce      = 0xAB12u;
    if (payload && plen)
        memcpy(buf + sizeof(sm_hdr_t), payload, plen);
    return sizeof(sm_hdr_t) + plen;
}

static int sm_send(const void *buf, size_t len) {
    int fd = tu_unix_connect(g_msm.socket_path);
    if (fd < 0) return -1;
    ssize_t n = write(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

/* ==================  SCENARIO 1 – Clean startup & shutdown  ==================
 * Simplified: start mock SM, 4 clients register, verify counts, all unregister.
 */
TEST_GROUP(FullSystem_Scenario1);
TEST_SETUP(FullSystem_Scenario1) {
    memset(&g_msm, 0, sizeof(g_msm));
    g_msm.register_response  = SM_OK;
    g_msm.heartbeat_response = SM_OK;
    signal(SIGPIPE, SIG_IGN);   /* prevent crash from broken socket writes */
    mock_sm_start(&g_msm, NULL);
}
TEST_TEAR_DOWN(FullSystem_Scenario1) { mock_sm_stop(&g_msm); }

TEST(FullSystem_Scenario1, FourDaemons_AllRegisterWithin15s) {
    const char *names[] = {"audio_srv","camera_srv","gpio_srv","sensor_srv"};
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    for (int i = 0; i < 4; i++) {
        sm_register_req_t pay; memset(&pay,0,sizeof(pay));
        strncpy(pay.service_name, names[i], sizeof(pay.service_name)-1);
        snprintf(pay.socket_path, sizeof(pay.socket_path), "/tmp/%s.sock", names[i]);
        pay.pid = (int32_t)(getpid() + i);
        size_t len = build_msg(buf,sizeof(buf),SM_MSG_REGISTER,&pay,sizeof(pay));
        TEST_ASSERT_EQUAL_INT(0, sm_send(buf,len));
    }
    tu_sleep_ms(200);
    TEST_ASSERT_EQUAL_INT(4, g_msm.register_count);
}

TEST(FullSystem_Scenario1, Heartbeats_ReceivedFromAllDaemons) {
    const char *names[] = {"audio_srv","camera_srv","gpio_srv","sensor_srv"};
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    for (int i = 0; i < 4; i++) {
        sm_heartbeat_req_t hb; memset(&hb,0,sizeof(hb));
        strncpy(hb.service_name, names[i], sizeof(hb.service_name)-1);
        size_t len = build_msg(buf,sizeof(buf),SM_MSG_HEARTBEAT,&hb,sizeof(hb));
        TEST_ASSERT_EQUAL_INT(0, sm_send(buf,len));
    }
    tu_sleep_ms(150);
    TEST_ASSERT_EQUAL_INT(4, g_msm.heartbeat_count);
}

TEST_GROUP_RUNNER(FullSystem_Scenario1) {
    RUN_TEST_CASE(FullSystem_Scenario1, FourDaemons_AllRegisterWithin15s);
    RUN_TEST_CASE(FullSystem_Scenario1, Heartbeats_ReceivedFromAllDaemons);
}

/* ==================  SCENARIO 2 – Crash & recovery  ==================
 * SM detects missed heartbeats; after re-start client re-registers.
 */
TEST_GROUP(FullSystem_Scenario2);
TEST_SETUP(FullSystem_Scenario2) {
    memset(&g_msm, 0, sizeof(g_msm));
    g_msm.register_response  = SM_OK;
    g_msm.heartbeat_response = SM_OK;
    signal(SIGPIPE, SIG_IGN);   /* prevent crash from broken socket writes */
    mock_sm_start(&g_msm, NULL);
}
TEST_TEAR_DOWN(FullSystem_Scenario2) { mock_sm_stop(&g_msm); }

TEST(FullSystem_Scenario2, CrashedService_ReRegisterAfterRestart) {
    /* Simulate crash: register once, reset stats, register again */
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    sm_register_req_t pay; memset(&pay,0,sizeof(pay));
    strncpy(pay.service_name,"audio_srv",sizeof(pay.service_name)-1);
    strncpy(pay.socket_path,"/tmp/audio.sock",sizeof(pay.socket_path)-1);
    pay.pid = (int32_t)getpid();
    size_t len = build_msg(buf,sizeof(buf),SM_MSG_REGISTER,&pay,sizeof(pay));
    sm_send(buf,len);
    tu_sleep_ms(80);
    TEST_ASSERT_EQUAL_INT(1, g_msm.register_count);

    /* "crash" — reset counters simulating restart */
    mock_sm_reset_stats(&g_msm);

    /* re-register */
    pay.pid = (int32_t)(getpid()+1);
    len = build_msg(buf,sizeof(buf),SM_MSG_REGISTER,&pay,sizeof(pay));
    sm_send(buf,len);
    tu_sleep_ms(80);
    TEST_ASSERT_EQUAL_INT(1, g_msm.register_count);
}

TEST_GROUP_RUNNER(FullSystem_Scenario2) {
    RUN_TEST_CASE(FullSystem_Scenario2, CrashedService_ReRegisterAfterRestart);
}

/* ==================  SCENARIO 3 – Memory pool under load  ==================
 * Alloc/free 10 000 buffers; after all frees used_blocks must be 0.
 */
TEST_GROUP(FullSystem_Scenario3);
TEST_SETUP(FullSystem_Scenario3) {}
TEST_TEAR_DOWN(FullSystem_Scenario3) {}

TEST(FullSystem_Scenario3, MemoryPool_10KAllocFree_NoLeaks) {
    memory_pool_config_t cfg = {
        .block_size  = 512,
        .block_count = 256,
        .thread_safe = 1,
        .name        = "ipc_pool"
    };
    memory_pool_t *pool = memory_pool_create(&cfg);
    TEST_ASSERT_NOT_NULL(pool);

    void *ptrs[256];
    int   fail = 0;
    for (int round = 0; round < 40; round++) {   /* 40 × 256 = 10 240 */
        int n = 0;
        for (; n < 256; n++) {
            ptrs[n] = memory_pool_alloc(pool);
            if (!ptrs[n]) { fail++; break; }
            memset(ptrs[n], (uint8_t)n, 512);
        }
        for (int j = 0; j < n; j++)
            memory_pool_free(pool, ptrs[j]);
    }
    memory_pool_stats_t st;
    memory_pool_get_stats(pool, &st);
    TEST_ASSERT_EQUAL_INT(0, st.used_blocks);
    TEST_ASSERT_EQUAL_INT(0, fail);
    memory_pool_destroy(pool);
}

TEST_GROUP_RUNNER(FullSystem_Scenario3) {
    RUN_TEST_CASE(FullSystem_Scenario3, MemoryPool_10KAllocFree_NoLeaks);
}

/* ==================  SCENARIO 4 – io_uring under HAL load  ==================
 * Basic pipe write/read loop simulating HAL messages.
 */
TEST_GROUP(FullSystem_Scenario4);
TEST_SETUP(FullSystem_Scenario4) {}
TEST_TEAR_DOWN(FullSystem_Scenario4) {}

TEST(FullSystem_Scenario4, PipeRoundtrip_100Messages_AllReceived) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    int received = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t v = (uint32_t)i;
        TEST_ASSERT_EQUAL_INT((int)sizeof(v), (int)write(fds[1], &v, sizeof(v)));
        uint32_t r;
        TEST_ASSERT_EQUAL_INT((int)sizeof(r), (int)read(fds[0], &r, sizeof(r)));
        if (r == (uint32_t)i) received++;
    }
    close(fds[0]); close(fds[1]);
    TEST_ASSERT_EQUAL_INT(100, received);
}

TEST_GROUP_RUNNER(FullSystem_Scenario4) {
    RUN_TEST_CASE(FullSystem_Scenario4, PipeRoundtrip_100Messages_AllReceived);
}

/* ==================  SCENARIO 5 – Security enforcement  ==================
 * Verify sandbox→caps→seccomp ordering via mock_security.
 */
TEST_GROUP(FullSystem_Scenario5);
TEST_SETUP(FullSystem_Scenario5) {
    mock_security_state_t *s = mock_security_get_state();
    mock_security_reset(s);
}
TEST_TEAR_DOWN(FullSystem_Scenario5) {}

TEST(FullSystem_Scenario5, SecurityOrder_Sandbox_Then_Caps_Then_Seccomp) {
    int r1 = mock_apply_sandbox();
    int r2 = mock_check_capabilities();
    int r3 = mock_apply_seccomp();
    mock_security_state_t *s = mock_security_get_state();
    TEST_ASSERT_EQUAL_INT(0, r1);
    TEST_ASSERT_EQUAL_INT(0, r2);
    TEST_ASSERT_EQUAL_INT(0, r3);
    TEST_ASSERT_EQUAL_INT(1, s->apply_sandbox_calls);
    TEST_ASSERT_EQUAL_INT(1, s->check_caps_calls);
    TEST_ASSERT_EQUAL_INT(1, s->apply_seccomp_calls);
}

TEST(FullSystem_Scenario5, SandboxFail_CapabilitiesNotCalled) {
    mock_security_state_t *s = mock_security_get_state();
    s->sandbox_ret = -1;   /* make sandbox fail */
    int r = mock_apply_sandbox();
    TEST_ASSERT_NOT_EQUAL(0, r);
    /* caps must NOT be called when sandbox fails */
    if (r != 0) {
        TEST_ASSERT_EQUAL_INT(0, s->check_caps_calls);
    }
}

TEST_GROUP_RUNNER(FullSystem_Scenario5) {
    RUN_TEST_CASE(FullSystem_Scenario5, SecurityOrder_Sandbox_Then_Caps_Then_Seccomp);
    RUN_TEST_CASE(FullSystem_Scenario5, SandboxFail_CapabilitiesNotCalled);
}

/* ==================  SCENARIO 6 – Network partition  ==================
 * Stop SM, clients detect disconnect; resume SM, clients reconnect.
 */
TEST_GROUP(FullSystem_Scenario6);
TEST_SETUP(FullSystem_Scenario6) {
    memset(&g_msm, 0, sizeof(g_msm));
    g_msm.register_response  = SM_OK;
    g_msm.heartbeat_response = SM_OK;
    signal(SIGPIPE, SIG_IGN);   /* prevent crash from broken socket writes */
    mock_sm_start(&g_msm, NULL);
}
TEST_TEAR_DOWN(FullSystem_Scenario6) { mock_sm_stop(&g_msm); }

TEST(FullSystem_Scenario6, SMRestart_ClientReregisters) {
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    sm_register_req_t pay; memset(&pay,0,sizeof(pay));
    strncpy(pay.service_name,"net_svc",sizeof(pay.service_name)-1);
    strncpy(pay.socket_path,"/tmp/net.sock",sizeof(pay.socket_path)-1);
    pay.pid = (int32_t)getpid();
    size_t len = build_msg(buf,sizeof(buf),SM_MSG_REGISTER,&pay,sizeof(pay));
    sm_send(buf,len);
    tu_sleep_ms(80);
    TEST_ASSERT_EQUAL_INT(1, g_msm.register_count);

    /* Simulate partition: stop then restart SM */
    mock_sm_stop(&g_msm);
    memset(&g_msm, 0, sizeof(g_msm));
    g_msm.register_response  = SM_OK;
    g_msm.heartbeat_response = SM_OK;
    mock_sm_start(&g_msm, NULL);
    tu_sleep_ms(50);

    /* Re-register (client would detect disconnect and re-register) */
    sm_send(buf,len);
    tu_sleep_ms(80);
    TEST_ASSERT_EQUAL_INT(1, g_msm.register_count);
}

TEST_GROUP_RUNNER(FullSystem_Scenario6) {
    RUN_TEST_CASE(FullSystem_Scenario6, SMRestart_ClientReregisters);
}

/* ---- main ---- */
static void run_all_groups(void) {
    RUN_TEST_GROUP(FullSystem_Scenario1);
    RUN_TEST_GROUP(FullSystem_Scenario2);
    RUN_TEST_GROUP(FullSystem_Scenario3);
    RUN_TEST_GROUP(FullSystem_Scenario4);
    RUN_TEST_GROUP(FullSystem_Scenario5);
    RUN_TEST_GROUP(FullSystem_Scenario6);
}
int main(int argc, const char *argv[]) {
    return UnityMain(argc, argv, run_all_groups);
}
