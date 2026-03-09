/**
 * @file test_service_manager.c
 * @brief Service Manager unit tests.
 *
 * Groups: Registry, Protocol Validation, Health Monitor,
 *         Rate Limiting, Config Loading.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"

#include "../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include "../../dev/core/service_manager/security/sm_rate_limit.h"
#include "../../dev/core/service_manager/lifecycle/sm_config.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

/* ════════════════════════════════════════════════════════════════════
   GROUP 1 — Registry
   ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(SM_Registry);

TEST_SETUP(SM_Registry)
{
    sm_registry_init();
}

TEST_TEAR_DOWN(SM_Registry)
{
    sm_registry_cleanup();
}

static void fill_entry(service_entry_t *e, const char *name)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, SM_MAX_NAME - 1);
    snprintf(e->socket_path, SM_MAX_PATH, "/tmp/%s.sock", name);
    snprintf(e->ring_name, SM_MAX_PATH, "/%s_ring", name);
    e->pid    = (pid_t)(1000 + (int)strlen(name));
    e->uid    = 1000;
    e->status = SERVICE_RUNNING;
    e->tier   = 0;
}

TEST(SM_Registry, Register_ValidEntry_Succeeds)
{
    service_entry_t e;
    fill_entry(&e, "svc_a");
    int r = sm_registry_add(&e);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SM_OK, r, "add should succeed");
}

TEST(SM_Registry, Register_ThenFind_ReturnsMatchingEntry)
{
    service_entry_t e;
    fill_entry(&e, "svc_find");
    sm_registry_add(&e);

    service_entry_t out;
    int r = sm_registry_find_copy("svc_find", &out);
    TEST_ASSERT_EQUAL_INT(SM_OK, r);
    TEST_ASSERT_EQUAL_STRING("svc_find", out.name);
    TEST_ASSERT_EQUAL_STRING(e.socket_path, out.socket_path);
}

TEST(SM_Registry, Register_DuplicateName_ReturnsExists)
{
    service_entry_t e;
    fill_entry(&e, "svc_dup");
    sm_registry_add(&e);
    int r = sm_registry_add(&e);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SM_ERR_EXISTS, r,
        "Second registration should return SM_ERR_EXISTS");
}

TEST(SM_Registry, Register_MaxServices_AllSucceed)
{
    for (int i = 0; i < SM_MAX_SERVICES; i++) {
        char name[SM_MAX_NAME];
        snprintf(name, sizeof(name), "svc_%d", i);
        service_entry_t e;
        fill_entry(&e, name);
        int r = sm_registry_add(&e);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SM_OK, r, "Should accept up to max services");
    }
}

TEST(SM_Registry, Register_BeyondMaxServices_ReturnsFull)
{
    for (int i = 0; i < SM_MAX_SERVICES; i++) {
        char name[SM_MAX_NAME];
        snprintf(name, sizeof(name), "svc_%d", i);
        service_entry_t e;
        fill_entry(&e, name);
        sm_registry_add(&e);
    }
    service_entry_t extra;
    fill_entry(&extra, "svc_overflow");
    int r = sm_registry_add(&extra);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SM_ERR_FULL, r,
        "Should return SM_ERR_FULL when registry is full");
}

TEST(SM_Registry, Deregister_ThenFind_ReturnsNotFound)
{
    service_entry_t e;
    fill_entry(&e, "svc_rem");
    sm_registry_add(&e);
    sm_registry_remove("svc_rem");

    service_entry_t out;
    int r = sm_registry_find_copy("svc_rem", &out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SM_ERR_NOT_FOUND, r,
        "Removed service should not be found");
}

TEST(SM_Registry, Deregister_NonExistent_GracefulError)
{
    int r = sm_registry_remove("svc_ghost");
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Registry, Count_MatchesRegistered)
{
    service_entry_t e;
    fill_entry(&e, "svc_count");
    sm_registry_add(&e);
    TEST_ASSERT_EQUAL_INT(1, sm_registry_count());
}

typedef struct { const char *name; tu_barrier_t *barrier; int ok; } reg_arg_t;

static void *reg_thread(void *a)
{
    reg_arg_t *arg = (reg_arg_t *)a;
    tu_barrier_wait(arg->barrier);
    service_entry_t e;
    fill_entry(&e, arg->name);
    int r = sm_registry_add(&e);
    arg->ok = (r == SM_OK);
    return NULL;
}

TEST(SM_Registry, ConcurrentRegister_8Threads_NoCorruption)
{
    const char *names[] = {
        "tc_a","tc_b","tc_c","tc_d","tc_e","tc_f","tc_g","tc_h"
    };
    int N = 8;
    tu_barrier_t barrier;
    tu_barrier_init(&barrier, N);

    reg_arg_t   args[8];
    pthread_t   tids[8];
    for (int i = 0; i < N; i++) {
        args[i].name    = names[i];
        args[i].barrier = &barrier;
        args[i].ok      = 0;
        pthread_create(&tids[i], NULL, reg_thread, &args[i]);
    }
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);
    tu_barrier_destroy(&barrier);

    for (int i = 0; i < N; i++)
        TEST_ASSERT_TRUE_MESSAGE(args[i].ok, "Concurrent registration failed");
    TEST_ASSERT_EQUAL_INT(N, sm_registry_count());
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 2 — Protocol (Header Validation)
   ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(SM_Protocol);
TEST_SETUP(SM_Protocol) {}
TEST_TEAR_DOWN(SM_Protocol) {}

static void make_valid_hdr(sm_hdr_t *h, uint16_t type, uint32_t plen)
{
    memset(h, 0, sizeof(*h));
    h->magic      = SM_PROTOCOL_MAGIC;
    h->version    = SM_PROTOCOL_VERSION;
    h->type       = type;
    h->length     = plen;
    h->timestamp  = (uint32_t)time(NULL);
    h->client_pid = (uint32_t)getpid();
    h->nonce      = 0xDEAD;
}

TEST(SM_Protocol, ValidHeader_PassesBasicValidation)
{
    sm_hdr_t hdr;
    make_valid_hdr(&hdr, SM_MSG_HEARTBEAT, 0);
    int r = sm_validate_header(&hdr, sizeof(hdr));
    TEST_ASSERT_TRUE_MESSAGE(r == SM_OK || r >= 0,
        "Valid header should pass validation");
}

TEST(SM_Protocol, WrongMagic_FailsValidation)
{
    sm_hdr_t hdr;
    make_valid_hdr(&hdr, SM_MSG_REGISTER, 0);
    hdr.magic = 0xDEADBEEF;
    int r = sm_validate_header(&hdr, sizeof(hdr));
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, TruncatedBuffer_FailsValidation)
{
    sm_hdr_t hdr;
    make_valid_hdr(&hdr, SM_MSG_HEARTBEAT, 0);
    int r = sm_validate_header(&hdr, sizeof(hdr) - 10);
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, FutureTimestamp_OutsideSkewWindow_Fails)
{
    sm_hdr_t hdr;
    make_valid_hdr(&hdr, SM_MSG_HEARTBEAT, 0);
    hdr.timestamp = (uint32_t)(time(NULL) + SM_TIMESTAMP_MAX_SKEW + 60);
    int r = sm_validate_header(&hdr, sizeof(hdr));
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, StaleTimestamp_Fails)
{
    sm_hdr_t hdr;
    make_valid_hdr(&hdr, SM_MSG_HEARTBEAT, 0);
    hdr.timestamp = (uint32_t)(time(NULL) - SM_TIMESTAMP_MAX_AGE - 60);
    int r = sm_validate_header(&hdr, sizeof(hdr));
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, ValidateServiceName_Normal_OK)
{
    int r = sm_validate_service_name("audio_service");
    TEST_ASSERT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, ValidateServiceName_Empty_Fails)
{
    int r = sm_validate_service_name("");
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, ValidateServiceName_TooLong_Fails)
{
    char long_name[SM_MAX_NAME + 10];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    int r = sm_validate_service_name(long_name);
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, ValidateSocketPath_ValidTmp_OK)
{
    int r = sm_validate_socket_path("/tmp/audio.sock");
    TEST_ASSERT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, ValidateSocketPath_BadPrefix_Fails)
{
    int r = sm_validate_socket_path("/etc/shadow");
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

TEST(SM_Protocol, PayloadSize_OverMax_Fails)
{
    int r = sm_validate_message_size(SM_MAX_PAYLOAD_SIZE + 1, SM_MSG_REGISTER);
    TEST_ASSERT_NOT_EQUAL_INT(SM_OK, r);
}

/* ════════════════════════════════════════════════════════════════════
   GROUP 3 — Rate Limiting
   ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(SM_RateLimit);
TEST_SETUP(SM_RateLimit)    { sm_rate_limit_init(); }
TEST_TEAR_DOWN(SM_RateLimit){ sm_rate_limit_cleanup(); }

TEST(SM_RateLimit, BelowLimit_AllRequestsAccepted)
{
    /* PID_CAPACITY default = 10; first 10 from same PID must pass */
    for (int i = 0; i < SM_RATE_PID_CAPACITY; i++) {
        int r = sm_rate_limit_check((pid_t)9999);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SM_OK, r, "Should accept up to pid capacity");
    }
}

TEST(SM_RateLimit, AboveLimit_ExcessRejected)
{
    /* Exhaust PID bucket then verify next is throttled */
    for (int i = 0; i < SM_RATE_PID_CAPACITY; i++)
        sm_rate_limit_check((pid_t)8888);
    int r = sm_rate_limit_check((pid_t)8888);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SM_ERR_RATELIMIT, r,
        "Request beyond pid capacity should be throttled");
}

/* ── Runners ──────────────────────────────────────────────────────── */

TEST_GROUP_RUNNER(SM_Registry)
{
    RUN_TEST_CASE(SM_Registry, Register_ValidEntry_Succeeds);
    RUN_TEST_CASE(SM_Registry, Register_ThenFind_ReturnsMatchingEntry);
    RUN_TEST_CASE(SM_Registry, Register_DuplicateName_ReturnsExists);
    RUN_TEST_CASE(SM_Registry, Register_MaxServices_AllSucceed);
    RUN_TEST_CASE(SM_Registry, Register_BeyondMaxServices_ReturnsFull);
    RUN_TEST_CASE(SM_Registry, Deregister_ThenFind_ReturnsNotFound);
    RUN_TEST_CASE(SM_Registry, Deregister_NonExistent_GracefulError);
    RUN_TEST_CASE(SM_Registry, Count_MatchesRegistered);
    RUN_TEST_CASE(SM_Registry, ConcurrentRegister_8Threads_NoCorruption);
}

TEST_GROUP_RUNNER(SM_Protocol)
{
    RUN_TEST_CASE(SM_Protocol, ValidHeader_PassesBasicValidation);
    RUN_TEST_CASE(SM_Protocol, WrongMagic_FailsValidation);
    RUN_TEST_CASE(SM_Protocol, TruncatedBuffer_FailsValidation);
    RUN_TEST_CASE(SM_Protocol, FutureTimestamp_OutsideSkewWindow_Fails);
    RUN_TEST_CASE(SM_Protocol, StaleTimestamp_Fails);
    RUN_TEST_CASE(SM_Protocol, ValidateServiceName_Normal_OK);
    RUN_TEST_CASE(SM_Protocol, ValidateServiceName_Empty_Fails);
    RUN_TEST_CASE(SM_Protocol, ValidateServiceName_TooLong_Fails);
    RUN_TEST_CASE(SM_Protocol, ValidateSocketPath_ValidTmp_OK);
    RUN_TEST_CASE(SM_Protocol, ValidateSocketPath_BadPrefix_Fails);
    RUN_TEST_CASE(SM_Protocol, PayloadSize_OverMax_Fails);
}

TEST_GROUP_RUNNER(SM_RateLimit)
{
    RUN_TEST_CASE(SM_RateLimit, BelowLimit_AllRequestsAccepted);
    RUN_TEST_CASE(SM_RateLimit, AboveLimit_ExcessRejected);
}

static void run_all_groups(void)
{
    RUN_TEST_GROUP(SM_Registry);
    RUN_TEST_GROUP(SM_Protocol);
    RUN_TEST_GROUP(SM_RateLimit);
}

int main(int argc, const char *argv[])
{
    return UnityMain(argc, argv, run_all_groups);
}
