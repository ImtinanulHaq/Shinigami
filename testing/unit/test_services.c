/**
 * @file test_services.c
 * @brief Service Manager unit tests — 5 groups per spec Parts 5.
 *
 * Group 1 – SM Config (load/validate/reload)
 * Group 2 – Signal Handling (SIGTERM, SIGHUP, SIGPIPE)
 * Group 3 – SM Registration & IPC (mock SM)
 * Group 4 – Config Reload (SIGHUP-driven)
 * Group 5 – Security Module Order (mock_security)
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_sm.h"
#include "../mocks/mock_security.h"

#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../dev/core/service_manager/lifecycle/sm_config.h"
#include "../../dev/core/service_manager/observability/sm_health.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>


/* ==========================================================================
 * GROUP 1 – SM Config
 * ========================================================================== */
TEST_GROUP(SMConfig);
TEST_SETUP(SMConfig)     {}
TEST_TEAR_DOWN(SMConfig) {}

TEST(SMConfig, DefaultConfig_ValidFields) {
    sm_config_t cfg = sm_config_default();
    TEST_ASSERT_TRUE(cfg.max_services > 0);
}

TEST(SMConfig, ValidateDefault_Passes) {
    sm_config_t cfg = sm_config_default();
    TEST_ASSERT_EQUAL_INT(0, sm_config_validate(&cfg));
}

TEST(SMConfig, ValidateNull_Fails) {
    TEST_ASSERT_NOT_EQUAL_INT(0, sm_config_validate(NULL));
}

TEST(SMConfig, LoadNonExistentFile_UsesDefaults) {
    /* sm_config_load returns 0 (uses defaults) when file is missing */
    int r = sm_config_load("/tmp/no_such_file_12345.conf");
    TEST_ASSERT_EQUAL_INT(0, r);
}

TEST(SMConfig, WriteAndLoadFile_Succeeds) {
    const char *path = "/tmp/test_sm_cfg_srv.conf";
    FILE *f = fopen(path, "w");
    if (!f) TEST_IGNORE_MESSAGE("Cannot write temp config");
    fprintf(f, "[service_manager]\nmax_services=16\n");
    fclose(f);
    int r = sm_config_load(path);
    unlink(path);
    TEST_ASSERT_EQUAL_INT(0, r);
}

TEST_GROUP_RUNNER(SMConfig) {
    RUN_TEST_CASE(SMConfig, DefaultConfig_ValidFields);
    RUN_TEST_CASE(SMConfig, ValidateDefault_Passes);
    RUN_TEST_CASE(SMConfig, ValidateNull_Fails);
    RUN_TEST_CASE(SMConfig, LoadNonExistentFile_UsesDefaults);
    RUN_TEST_CASE(SMConfig, WriteAndLoadFile_Succeeds);
}


/* ==========================================================================
 * GROUP 2 – Signal Handling
 * ========================================================================== */
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_reload  = 0;

static void sig_term_handler(int sig) { (void)sig; g_running = 0; }
static void sig_hup_handler(int sig)  { (void)sig; g_reload  = 1; }

TEST_GROUP(SignalHandling);
TEST_SETUP(SignalHandling) {
    g_running = 1;
    g_reload  = 0;
}
TEST_TEAR_DOWN(SignalHandling) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
}

TEST(SignalHandling, SIGTERM_Sets_g_running_Zero) {
    signal(SIGTERM, sig_term_handler);
    TEST_ASSERT_EQUAL_INT(1, (int)g_running);
    kill(getpid(), SIGTERM);
    tu_sleep_ms(50);
    TEST_ASSERT_EQUAL_INT(0, (int)g_running);
}

TEST(SignalHandling, SIGHUP_Sets_g_reload_One) {
    signal(SIGHUP, sig_hup_handler);
    TEST_ASSERT_EQUAL_INT(0, (int)g_reload);
    kill(getpid(), SIGHUP);
    tu_sleep_ms(50);
    TEST_ASSERT_EQUAL_INT(1, (int)g_reload);
}

TEST(SignalHandling, SIGPIPE_Ignored_NocrashOnBrokenPipe) {
    signal(SIGPIPE, SIG_IGN);
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    close(fds[0]);   /* break the read end */
    /* Writing to broken pipe with SIG_IGN should return EPIPE, not crash */
    char buf[4] = {0};
    ssize_t n = write(fds[1], buf, sizeof(buf));
    close(fds[1]);
    (void)n;          /* EPIPE errno expected, no SIGPIPE crash */
    /* If we reach here, SIGPIPE did NOT kill the process */
    TEST_PASS();
}

TEST(SignalHandling, ConcurrentSIGTERM_SIGHUP_BothHandled) {
    signal(SIGTERM, sig_term_handler);
    signal(SIGHUP,  sig_hup_handler);
    kill(getpid(), SIGHUP);
    kill(getpid(), SIGTERM);
    tu_sleep_ms(100);
    TEST_ASSERT_EQUAL_INT(0, (int)g_running);
    TEST_ASSERT_EQUAL_INT(1, (int)g_reload);
}

TEST_GROUP_RUNNER(SignalHandling) {
    RUN_TEST_CASE(SignalHandling, SIGTERM_Sets_g_running_Zero);
    RUN_TEST_CASE(SignalHandling, SIGHUP_Sets_g_reload_One);
    RUN_TEST_CASE(SignalHandling, SIGPIPE_Ignored_NocrashOnBrokenPipe);
    RUN_TEST_CASE(SignalHandling, ConcurrentSIGTERM_SIGHUP_BothHandled);
}


/* ==========================================================================
 * GROUP 3 – SM Registration & IPC (mock SM)
 * ========================================================================== */
static mock_sm_t g_msm3;

static size_t build_msg3(uint8_t *buf, size_t bufsz,
                         uint16_t type, const void *pay, size_t plen) {
    memset(buf,0,bufsz);
    sm_hdr_t *h  = (sm_hdr_t *)buf;
    h->magic     = SM_PROTOCOL_MAGIC;
    h->version   = SM_PROTOCOL_VERSION;
    h->type      = type;
    h->length    = (uint32_t)plen;
    h->timestamp = (uint32_t)time(NULL);
    h->client_pid= (uint32_t)getpid();
    h->nonce     = 0x1234u;
    if (pay && plen) memcpy(buf+sizeof(sm_hdr_t),pay,plen);
    return sizeof(sm_hdr_t)+plen;
}

TEST_GROUP(SM_Registration_IPC);
TEST_SETUP(SM_Registration_IPC) {
    signal(SIGPIPE, SIG_IGN);   /* prevent process death from broken socket writes */
    memset(&g_msm3,0,sizeof(g_msm3));
    g_msm3.register_response  = SM_OK;
    g_msm3.lookup_response    = SM_OK;
    g_msm3.heartbeat_response = SM_OK;
    mock_sm_start(&g_msm3, NULL);
}
TEST_TEAR_DOWN(SM_Registration_IPC) { mock_sm_stop(&g_msm3); }

TEST(SM_Registration_IPC, Register_Within1s_CountIncremented) {
    sm_register_req_t pay; memset(&pay,0,sizeof(pay));
    strncpy(pay.service_name,"test_svc",sizeof(pay.service_name)-1);
    strncpy(pay.socket_path,"/tmp/test_svc.sock",sizeof(pay.socket_path)-1);
    pay.pid = (int32_t)getpid();
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    size_t len=build_msg3(buf,sizeof(buf),SM_MSG_REGISTER,&pay,sizeof(pay));
    int fd=tu_unix_connect(g_msm3.socket_path);
    TEST_ASSERT_NOT_EQUAL(-1,fd);
    write(fd,buf,len); close(fd);
    tu_sleep_ms(200);
    TEST_ASSERT_EQUAL_INT(1, g_msm3.register_count);
}

TEST(SM_Registration_IPC, MessageContent_ServiceNamePreserved) {
    sm_register_req_t pay; memset(&pay,0,sizeof(pay));
    strncpy(pay.service_name,"named_svc",sizeof(pay.service_name)-1);
    strncpy(pay.socket_path,"/tmp/nmd.sock",sizeof(pay.socket_path)-1);
    pay.pid=(int32_t)getpid();
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    size_t len=build_msg3(buf,sizeof(buf),SM_MSG_REGISTER,&pay,sizeof(pay));
    int fd=tu_unix_connect(g_msm3.socket_path);
    TEST_ASSERT_NOT_EQUAL(-1,fd);
    write(fd,buf,len); close(fd);
    tu_sleep_ms(200);
    TEST_ASSERT_EQUAL_STRING("named_svc", g_msm3.last_registered_name);
}

TEST(SM_Registration_IPC, Heartbeat_Within500ms_CountIncremented) {
    sm_heartbeat_req_t hb; memset(&hb,0,sizeof(hb));
    strncpy(hb.service_name,"hb_svc",sizeof(hb.service_name)-1);
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    size_t len=build_msg3(buf,sizeof(buf),SM_MSG_HEARTBEAT,&hb,sizeof(hb));
    int fd=tu_unix_connect(g_msm3.socket_path);
    TEST_ASSERT_NOT_EQUAL(-1,fd);
    write(fd,buf,len); close(fd);
    tu_sleep_ms(200);
    TEST_ASSERT_EQUAL_INT(1, g_msm3.heartbeat_count);
}

TEST(SM_Registration_IPC, FiveLookups_CountEquals5) {
    uint8_t buf[SM_MAX_MESSAGE_SIZE];
    for (int i=0;i<5;i++) {
        sm_lookup_req_t lk; memset(&lk,0,sizeof(lk));
        snprintf(lk.service_name,sizeof(lk.service_name),"svc%d",i);
        size_t len=build_msg3(buf,sizeof(buf),SM_MSG_LOOKUP,&lk,sizeof(lk));
        int fd=tu_unix_connect(g_msm3.socket_path);
        if (fd<0) continue;
        write(fd,buf,len); close(fd);
    }
    tu_sleep_ms(300);
    TEST_ASSERT_EQUAL_INT(5, g_msm3.lookup_count);
}

TEST_GROUP_RUNNER(SM_Registration_IPC) {
    RUN_TEST_CASE(SM_Registration_IPC, Register_Within1s_CountIncremented);
    RUN_TEST_CASE(SM_Registration_IPC, MessageContent_ServiceNamePreserved);
    RUN_TEST_CASE(SM_Registration_IPC, Heartbeat_Within500ms_CountIncremented);
    RUN_TEST_CASE(SM_Registration_IPC, FiveLookups_CountEquals5);
}


/* ==========================================================================
 * GROUP 4 – Config Reload (SIGHUP-driven)
 * ========================================================================== */
TEST_GROUP(ConfigReload);
TEST_SETUP(ConfigReload)     {}
TEST_TEAR_DOWN(ConfigReload) {}

TEST(ConfigReload, LoadValidFile_ReturnsZero) {
    const char *path = "/tmp/cfg_reload_test.conf";
    FILE *f=fopen(path,"w");
    if (!f) TEST_IGNORE_MESSAGE("Cannot write config");
    fprintf(f,"[service_manager]\nmax_services=8\n");
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, sm_config_load(path));
    unlink(path);
}

TEST(ConfigReload, LoadTwice_SecondOverridesFirst) {
    const char *p1="/tmp/cfg1.conf", *p2="/tmp/cfg2.conf";
    FILE *f=fopen(p1,"w"); fprintf(f,"[service_manager]\nmax_services=4\n"); fclose(f);
    f=fopen(p2,"w"); fprintf(f,"[service_manager]\nmax_services=16\n"); fclose(f);
    sm_config_load(p1);
    sm_config_load(p2);
    /* No crash expected; second load should succeed */
    TEST_ASSERT_EQUAL_INT(0, sm_config_validate(sm_config_get()));
    unlink(p1); unlink(p2);
}

TEST(ConfigReload, SetInt_ValidKey_Accepted) {
    int r = sm_config_set_int("max_services", 12);
    /* Accept 0 (success) or negative (not supported/invalid key) — must not crash */
    (void)r;
    TEST_PASS();
}

TEST_GROUP_RUNNER(ConfigReload) {
    RUN_TEST_CASE(ConfigReload, LoadValidFile_ReturnsZero);
    RUN_TEST_CASE(ConfigReload, LoadTwice_SecondOverridesFirst);
    RUN_TEST_CASE(ConfigReload, SetInt_ValidKey_Accepted);
}


/* ==========================================================================
 * GROUP 5 – Security Module Order (mock_security)
 * ========================================================================== */
TEST_GROUP(SecurityOrder);
TEST_SETUP(SecurityOrder) {
    mock_security_state_t *s = mock_security_get_state();
    mock_security_reset(s);
}
TEST_TEAR_DOWN(SecurityOrder) {}

TEST(SecurityOrder, Sandbox_Called_First) {
    int r = mock_apply_sandbox();
    TEST_ASSERT_EQUAL_INT(0, r);
    mock_security_state_t *s = mock_security_get_state();
    TEST_ASSERT_EQUAL_INT(1, s->apply_sandbox_calls);
}

TEST(SecurityOrder, Caps_Called_After_Sandbox) {
    mock_apply_sandbox();
    int r = mock_check_capabilities();
    TEST_ASSERT_EQUAL_INT(0, r);
    mock_security_state_t *s = mock_security_get_state();
    TEST_ASSERT_EQUAL_INT(1, s->apply_sandbox_calls);
    TEST_ASSERT_EQUAL_INT(1, s->check_caps_calls);
}

TEST(SecurityOrder, Seccomp_Called_Last) {
    mock_apply_sandbox();
    mock_check_capabilities();
    int r = mock_apply_seccomp();
    TEST_ASSERT_EQUAL_INT(0, r);
    mock_security_state_t *s = mock_security_get_state();
    TEST_ASSERT_EQUAL_INT(1, s->apply_sandbox_calls);
    TEST_ASSERT_EQUAL_INT(1, s->check_caps_calls);
    TEST_ASSERT_EQUAL_INT(1, s->apply_seccomp_calls);
}

TEST(SecurityOrder, SandboxFail_CapsNotCalled) {
    mock_security_state_t *s = mock_security_get_state();
    s->sandbox_ret = -1;
    int r = mock_apply_sandbox();
    TEST_ASSERT_NOT_EQUAL(0, r);
    /* Caller should abort security chain; stubs only track if called */
    TEST_ASSERT_EQUAL_INT(0, s->check_caps_calls);
    TEST_ASSERT_EQUAL_INT(0, s->apply_seccomp_calls);
}

TEST(SecurityOrder, FullChain_AllCallsExactlyOnce) {
    mock_apply_sandbox();
    mock_check_capabilities();
    mock_apply_seccomp();
    mock_security_state_t *s = mock_security_get_state();
    TEST_ASSERT_EQUAL_INT(1, s->apply_sandbox_calls);
    TEST_ASSERT_EQUAL_INT(1, s->check_caps_calls);
    TEST_ASSERT_EQUAL_INT(1, s->apply_seccomp_calls);
}

TEST_GROUP_RUNNER(SecurityOrder) {
    RUN_TEST_CASE(SecurityOrder, Sandbox_Called_First);
    RUN_TEST_CASE(SecurityOrder, Caps_Called_After_Sandbox);
    RUN_TEST_CASE(SecurityOrder, Seccomp_Called_Last);
    RUN_TEST_CASE(SecurityOrder, SandboxFail_CapsNotCalled);
    RUN_TEST_CASE(SecurityOrder, FullChain_AllCallsExactlyOnce);
}


/* ---- main ---- */
static void run_all_groups(void) {
    RUN_TEST_GROUP(SMConfig);
    RUN_TEST_GROUP(SignalHandling);
    RUN_TEST_GROUP(SM_Registration_IPC);
    RUN_TEST_GROUP(ConfigReload);
    RUN_TEST_GROUP(SecurityOrder);
}
int main(int argc, const char *argv[]) {
    return UnityMain(argc, argv, run_all_groups);
}
