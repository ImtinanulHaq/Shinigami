/* Unit tests for sm_registry - Registry operations */
#include "unity.h"
#include "sm_registry.h"
#include <string.h>
#include <stdlib.h>

/* Test 1: Register new service */
TEST_BEGIN(register_new_service)
{
    unity.current_test = "register_new_service";
    
    int result = sm_registry_init();
    TEST_ASSERT_EQUAL_INT(result, 0);
    
    /* Register a service */
    int status = sm_registry_register("testservice", "/tmp/test.sock", 1234, 0, 1000000000U);
    TEST_ASSERT_EQUAL_INT(status, SM_OK);
}

/* Test 2: Duplicate registration rejection */
TEST_BEGIN(duplicate_registration)
{
    unity.current_test = "duplicate_registration";
    
    sm_registry_init();
    
    /* Register first time */
    int status1 = sm_registry_register("dupservice", "/tmp/dup.sock", 2000, 0, 1000000000U);
    TEST_ASSERT_EQUAL_INT(status1, SM_OK);
    
    /* Register again with same name - should fail */
    int status2 = sm_registry_register("dupservice", "/tmp/dup2.sock", 2001, 0, 1000000000U);
    TEST_ASSERT_EQUAL_INT(status2, SM_ERR_EXISTS);
}

/* Test 3: Lookup existing service */
TEST_BEGIN(lookup_existing_service)
{
    unity.current_test = "lookup_existing_service";
    
    sm_registry_init();
    sm_registry_register("lookuptest", "/tmp/lookup.sock", 3000, 0, 1000000000U);
    
    sm_service_t* svc = sm_registry_lookup("lookuptest");
    TEST_ASSERT_NOT_NULL(svc);
    TEST_ASSERT_EQUAL_STRING(svc->name, "lookuptest");
    TEST_ASSERT_EQUAL_STRING(svc->path, "/tmp/lookup.sock");
    TEST_ASSERT_EQUAL_INT(svc->pid, 3000);
}

/* Test 4: Lookup non-existent service */
TEST_BEGIN(lookup_nonexistent)
{
    unity.current_test = "lookup_nonexistent";
    
    sm_registry_init();
    
    sm_service_t* svc = sm_registry_lookup("nonexistent");
    TEST_ASSERT_NULL(svc);
}

/* Test 5: Unregister existing service */
TEST_BEGIN(unregister_service)
{
    unity.current_test = "unregister_service";
    
    sm_registry_init();
    sm_registry_register("rmtest", "/tmp/rm.sock", 4000, 0, 1000000000U);
    
    /* Should find it first */
    sm_service_t* found = sm_registry_lookup("rmtest");
    TEST_ASSERT_NOT_NULL(found);
    
    /* Unregister */
    int status = sm_registry_unregister("rmtest");
    TEST_ASSERT_EQUAL_INT(status, SM_OK);
    
    /* Should not find it now */
    found = sm_registry_lookup("rmtest");
    TEST_ASSERT_NULL(found);
}

/* Test 6: Registry capacity limits */
TEST_BEGIN(registry_capacity)
{
    unity.current_test = "registry_capacity";
    
    sm_registry_init();
    
    /* Register up to capacity */
    int status = SM_OK;
    for (int i = 0; i < SM_MAX_SERVICES; i++) {
        char name[64];
        char path[256];
        snprintf(name, sizeof(name), "service_%d", i);
        snprintf(path, sizeof(path), "/tmp/svc_%d.sock", i);
        
        status = sm_registry_register(name, path, 5000+i, 0, 1000000000U);
        if (i < SM_MAX_SERVICES - 1) {
            TEST_ASSERT_EQUAL_INT(status, SM_OK);
        }
    }
    
    /* Next registration should fail (full) */
    status = sm_registry_register("overflow", "/tmp/overflow.sock", 9999, 0, 1000000000U);
    TEST_ASSERT_EQUAL_INT(status, SM_ERR_FULL);
}

/* Test 7: Get all services */
TEST_BEGIN(get_all_services)
{
    unity.current_test = "get_all_services";
    
    sm_registry_init();
    
    /* Register a few services */
    sm_registry_register("svc1", "/tmp/svc1.sock", 6001, 0, 1000000000U);
    sm_registry_register("svc2", "/tmp/svc2.sock", 6002, 0, 1000000000U);
    sm_registry_register("svc3", "/tmp/svc3.sock", 6003, 0, 1000000000U);
    
    int count = 0;
    sm_service_t** services = sm_registry_get_all(&count);
    
    TEST_ASSERT_NOT_NULL(services);
    TEST_ASSERT_EQUAL_INT(count, 3);
    
    /* Verify we have the right services */
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(services[i]->name, "svc1") == 0 ||
            strcmp(services[i]->name, "svc2") == 0 ||
            strcmp(services[i]->name, "svc3") == 0) {
            found++;
        }
    }
    TEST_ASSERT_EQUAL_INT(found, 3);
}

/* Test 8: Update service heartbeat */
TEST_BEGIN(update_heartbeat)
{
    unity.current_test = "update_heartbeat";
    
    sm_registry_init();
    
    uint32_t initial_ts = 1000000000U;
    sm_registry_register("hbtest", "/tmp/hb.sock", 7000, 0, initial_ts);
    
    /* Update heartbeat to later timestamp */
    uint32_t new_ts = 1000000100U;
    int status = sm_registry_heartbeat("hbtest", new_ts);
    TEST_ASSERT_EQUAL_INT(status, SM_OK);
    
    /* Verify timestamp was updated */
    sm_service_t* svc = sm_registry_lookup("hbtest");
    TEST_ASSERT_NOT_NULL(svc);
    TEST_ASSERT_EQUAL_INT(svc->last_heartbeat, new_ts);
}

/* Test 9: Heartbeat non-existent service */
TEST_BEGIN(heartbeat_nonexistent)
{
    unity.current_test = "heartbeat_nonexistent";
    
    sm_registry_init();
    
    uint32_t ts = 1000000000U;
    int status = sm_registry_heartbeat("nosuchservice", ts);
    TEST_ASSERT_EQUAL_INT(status, SM_ERR_NOT_FOUND);
}

/* Test 10: Long service names */
TEST_BEGIN(long_service_names)
{
    unity.current_test = "long_service_names";
    
    sm_registry_init();
    
    /* Maximum length name (63 chars + null terminator) */
    char long_name[SM_MAX_NAME];
    memset(long_name, 'a', SM_MAX_NAME - 1);
    long_name[SM_MAX_NAME - 1] = '\0';
    
    int status = sm_registry_register(long_name, "/tmp/long.sock", 8000, 0, 1000000000U);
    TEST_ASSERT_EQUAL_INT(status, SM_OK);
    
    /* Verify we can lookup with exact name */
    sm_service_t* svc = sm_registry_lookup(long_name);
    TEST_ASSERT_NOT_NULL(svc);
    TEST_ASSERT_EQUAL_STRING(svc->name, long_name);
}

/* Test 11: NULL parameter handling */
TEST_BEGIN(null_parameters)
{
    unity.current_test = "null_parameters";
    
    sm_registry_init();
    
    /* Lookup with NULL name */
    sm_service_t* svc = sm_registry_lookup(NULL);
    TEST_ASSERT_NULL(svc);
    
    /* Unregister with NULL name */
    int status = sm_registry_unregister(NULL);
    TEST_ASSERT_EQUAL_INT(status, SM_ERR_INVALID);
}

/* Main test runner */
int main(void) {
    printf("\n=== Registry Unit Tests ===\n");
    
    test_register_new_service();
    test_duplicate_registration();
    test_lookup_existing_service();
    test_lookup_nonexistent();
    test_unregister_service();
    test_registry_capacity();
    test_get_all_services();
    test_update_heartbeat();
    test_heartbeat_nonexistent();
    test_long_service_names();
    test_null_parameters();
    
    unity_print_results();
    return 0;
}
