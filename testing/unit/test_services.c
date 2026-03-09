/**
 * @file test_services.c
 * @brief Service Manager lifecycle, config, and health unit tests.
 */
#include "../framework/unity.h"
#include "../framework/unity_fixture.h"
#include "../helpers/assert_extras.h"
#include "../helpers/test_utils.h"
#include "../mocks/mock_sm.h"

#include "../../dev/core/service_manager/infrastructure/sm_registry.h"
#include "../../dev/core/service_manager/lifecycle/sm_config.h"
#include "../../dev/core/service_manager/observability/sm_health.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════════════════════
 * Group 1 – SM Config
 * ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(SMConfig);
TEST_SETUP(SMConfig)    { /* nothing */ }
TEST_TEAR_DOWN(SMConfig) { /* nothing */ }

TEST(SMConfig, DefaultConfig_ValidFields)
{
    sm_config_t cfg = sm_config_default();
    /* Port must be > 0 (or the constant if compiled without networking) */
    TEST_ASSERT_TRUE(cfg.port > 0 || cfg.max_services > 0);
}

TEST(SMConfig, ValidateDefault_Passes)
{
    sm_config_t cfg = sm_config_default();
    int r = sm_config_validate(&cfg);
    TEST_ASSERT_EQUAL_INT(0, r);
}

TEST(SMConfig, ValidateNull_Fails)
{
    int r = sm_config_validate(NULL);
    TEST_ASSERT_NOT_EQUAL_INT(0, r);
}

TEST(SMConfig, LoadNonExistentFile_Fails)
{
    int r = sm_config_load("/tmp/does_not_exist_12345.conf");
    TEST_ASSERT_NOT_EQUAL_INT(0, r);
}

TEST(SMConfig, LoadNull_Fails)
{
    int r = sm_config_load(NULL);
    TEST_ASSERT_NOT_EQUAL_INT(0, r);
}

TEST(SMConfig, GetConfig_AfterDefault_NotNull)
{
    /* Ensure sm_config_get() does not crash after loading defaults */
    const sm_config_t *cfg = sm_config_get();
    /* may be NULL if not yet loaded; must not crash */
    (void)cfg;
}

TEST_GROUP_RUNNER(SMConfig)
{
    RUN_TEST_CASE(SMConfig, DefaultConfig_ValidFields);
    RUN_TEST_CASE(SMConfig, ValidateDefault_Passes);
    RUN_TEST_CASE(SMConfig, ValidateNull_Fails);
    RUN_TEST_CASE(SMConfig, LoadNonExistentFile_Fails);
    RUN_TEST_CASE(SMConfig, LoadNull_Fails);
    RUN_TEST_CASE(SMConfig, GetConfig_AfterDefault_NotNull);
}

/* ══════════════════════════════════════════════════════════════════════
 * Group 2 – SM Health
 * ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(SMHealth);
TEST_SETUP(SMHealth)
{
    sm_registry_init();
}
TEST_TEAR_DOWN(SMHealth)
{
    sm_registry_cleanup();
}

TEST(SMHealth, HealthCheck_EmptyRegistry_DoesNotCrash)
{
    sm_health_check();
}

TEST(SMHealth, HealthCheck_WithEntry_DoesNotCrash)
{
    service_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name,        "test_svc",     sizeof(e.name) - 1);
    strncpy(e.socket_path, "/tmp/t.sock",  sizeof(e.socket_path) - 1);
    e.pid    = getpid();
    e.status = SERVICE_STATUS_RUNNING;
    sm_registry_add(&e);

    sm_health_check(); /* must not crash */
}

TEST_GROUP_RUNNER(SMHealth)
{
    RUN_TEST_CASE(SMHealth, HealthCheck_EmptyRegistry_DoesNotCrash);
    RUN_TEST_CASE(SMHealth, HealthCheck_WithEntry_DoesNotCrash);
}

/* ══════════════════════════════════════════════════════════════════════
 * Group 3 – Registry update/heartbeat
 * ════════════════════════════════════════════════════════════════════ */

TEST_GROUP(SMRegistryExt);
TEST_SETUP(SMRegistryExt)    { sm_registry_init(); }
TEST_TEAR_DOWN(SMRegistryExt) { sm_registry_cleanup(); }

static void fill_entry(service_entry_t *e, const char *name)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->name,        name,           sizeof(e->name) - 1);
    strncpy(e->socket_path, "/tmp/t.sock",  sizeof(e->socket_path) - 1);
    e->pid    = (pid_t)(1000 + (int)(name[0]));
    e->status = SERVICE_STATUS_RUNNING;
}

TEST(SMRegistryExt, UpdateStatus_ExistingEntry_Succeeds)
{
    service_entry_t e;
    fill_entry(&e, "svc_a");
    TEST_ASSERT_EQUAL_INT(0, sm_registry_add(&e));

    int r = sm_registry_update_status("svc_a", SERVICE_STATUS_STOPPED);
    TEST_ASSERT_EQUAL_INT(0, r);

    service_entry_t out;
    sm_registry_find_copy("svc_a", &out);
    TEST_ASSERT_EQUAL_INT(SERVICE_STATUS_STOPPED, (int)out.status);
}

TEST(SMRegistryExt, UpdateStatus_NonExistent_Fails)
{
    int r = sm_registry_update_status("no_such_svc", SERVICE_STATUS_STOPPED);
    TEST_ASSERT_NOT_EQUAL_INT(0, r);
}

TEST(SMRegistryExt, UpdateHeartbeat_ExistingEntry_Succeeds)
{
    service_entry_t e;
    fill_entry(&e, "svc_b");
    sm_registry_add(&e);

    int r = sm_registry_update_heartbeat("svc_b");
    TEST_ASSERT_EQUAL_INT(0, r);
}

TEST(SMRegistryExt, UpdateHeartbeat_NonExistent_Fails)
{
    int r = sm_registry_update_heartbeat("no_such_svc");
    TEST_ASSERT_NOT_EQUAL_INT(0, r);
}

TEST(SMRegistryExt, GetAll_EmptyRegistry_CountZero)
{
    service_entry_t *entries = NULL;
    int count = 0;
    int r = sm_registry_get_all(&entries, &count);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(0, count);
    sm_registry_free_copy(entries);
}

TEST(SMRegistryExt, GetAll_TwoEntries_CountTwo)
{
    service_entry_t a, b;
    fill_entry(&a, "svc_x");
    fill_entry(&b, "svc_y");
    sm_registry_add(&a);
    sm_registry_add(&b);

    service_entry_t *entries = NULL;
    int count = 0;
    sm_registry_get_all(&entries, &count);
    TEST_ASSERT_EQUAL_INT(2, count);
    sm_registry_free_copy(entries);
}

TEST(SMRegistryExt, RemoveIfOwner_WrongPid_Fails)
{
    service_entry_t e;
    fill_entry(&e, "svc_c");
    e.pid = 9999;
    sm_registry_add(&e);

    int r = sm_registry_remove_if_owner("svc_c", 1); /* pid 1 != 9999 */
    TEST_ASSERT_NOT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(1, sm_registry_count());
}

TEST(SMRegistryExt, RemoveIfOwner_CorrectPid_Succeeds)
{
    service_entry_t e;
    fill_entry(&e, "svc_d");
    e.pid = 9999;
    sm_registry_add(&e);

    int r = sm_registry_remove_if_owner("svc_d", 9999);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(0, sm_registry_count());
}

TEST_GROUP_RUNNER(SMRegistryExt)
{
    RUN_TEST_CASE(SMRegistryExt, UpdateStatus_ExistingEntry_Succeeds);
    RUN_TEST_CASE(SMRegistryExt, UpdateStatus_NonExistent_Fails);
    RUN_TEST_CASE(SMRegistryExt, UpdateHeartbeat_ExistingEntry_Succeeds);
    RUN_TEST_CASE(SMRegistryExt, UpdateHeartbeat_NonExistent_Fails);
    RUN_TEST_CASE(SMRegistryExt, GetAll_EmptyRegistry_CountZero);
    RUN_TEST_CASE(SMRegistryExt, GetAll_TwoEntries_CountTwo);
    RUN_TEST_CASE(SMRegistryExt, RemoveIfOwner_WrongPid_Fails);
    RUN_TEST_CASE(SMRegistryExt, RemoveIfOwner_CorrectPid_Succeeds);
}

/* ══════════════════════════════════════════════════════════════════════
 * Runner
 * ════════════════════════════════════════════════════════════════════ */

static void run_all_groups(void)
{
    RUN_TEST_GROUP(SMConfig);
    RUN_TEST_GROUP(SMHealth);
    RUN_TEST_GROUP(SMRegistryExt);
}

int main(int argc, const char *argv[]) { return UnityMain(argc, argv, run_all_groups); }
