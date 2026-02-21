/* Unit tests for sm_registry - Registry operations */
#include "unity.h"
#include "sm_registry.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* Test 1: Registry initialization and add service */
TEST_BEGIN(register_new_service)
{
    unity.current_test = "register_new_service";
    
    int result = sm_registry_init();
    TEST_ASSERT_EQUAL_INT(result, 0);
    
    /* Create and register a service entry */
    service_entry_t entry = {0};
    strncpy(entry.name, "testservice", SM_MAX_NAME-1);
    strncpy(entry.socket_path, "/tmp/test.sock", SM_MAX_PATH-1);
    entry.pid = 1234;
    entry.uid = 0;
    entry.gid = 0;
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);
    
    int status = sm_registry_add(&entry);
    TEST_ASSERT_TRUE(status == 0 || status == -3);  /* SM_OK or SM_ERR_EXISTS */
}

/* Test 2: Duplicate registration rejection */
TEST_BEGIN(duplicate_registration)
{
    unity.current_test = "duplicate_registration";
    
    sm_registry_init();
    
    /* First registration */
    service_entry_t entry1 = {0};
    strncpy(entry1.name, "dupservice", SM_MAX_NAME-1);
    strncpy(entry1.socket_path, "/tmp/dup.sock", SM_MAX_PATH-1);
    entry1.pid = 2000;
    entry1.status = SERVICE_RUNNING;
    entry1.last_heartbeat = time(NULL);
    
    int status1 = sm_registry_add(&entry1);
    TEST_ASSERT_EQUAL_INT(status1, 0);
    
    /* Try again with same name - should fail */
    service_entry_t entry2 = {0};
    strncpy(entry2.name, "dupservice", SM_MAX_NAME-1);
    strncpy(entry2.socket_path, "/tmp/dup2.sock", SM_MAX_PATH-1);
    entry2.pid = 2001;
    entry2.status = SERVICE_RUNNING;
    entry2.last_heartbeat = time(NULL);
    
    int status2 = sm_registry_add(&entry2);
    TEST_ASSERT_EQUAL_INT(status2, -3);  /* SM_ERR_EXISTS */
}

/* Test 3: Lookup existing service */
TEST_BEGIN(lookup_existing_service)
{
    unity.current_test = "lookup_existing_service";
    
    sm_registry_init();
    
    service_entry_t entry = {0};
    strncpy(entry.name, "lookuptest", SM_MAX_NAME-1);
    strncpy(entry.socket_path, "/tmp/lookup.sock", SM_MAX_PATH-1);
    entry.pid = 3000;
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);
    sm_registry_add(&entry);
    
    /* Thread-safe lookup with copy */
    service_entry_t svc_copy = {0};
    int status = sm_registry_find_copy("lookuptest", &svc_copy);
    TEST_ASSERT_EQUAL_INT(status, 0);  /* SM_OK */
    TEST_ASSERT_EQUAL_STRING(svc_copy.name, "lookuptest");
    TEST_ASSERT_EQUAL_STRING(svc_copy.socket_path, "/tmp/lookup.sock");
    TEST_ASSERT_EQUAL_INT(svc_copy.pid, 3000);
}

/* Test 4: Lookup non-existent service */
TEST_BEGIN(lookup_nonexistent)
{
    unity.current_test = "lookup_nonexistent";
    
    sm_registry_init();
    
    service_entry_t svc_copy = {0};
    int status = sm_registry_find_copy("nonexistent", &svc_copy);
    TEST_ASSERT_EQUAL_INT(status, -1);  /* SM_ERR_NOT_FOUND */
}

/* Test 5: Unregister existing service */
TEST_BEGIN(unregister_service)
{
    unity.current_test = "unregister_service";
    
    sm_registry_init();
    
    service_entry_t entry = {0};
    strncpy(entry.name, "rmtest", SM_MAX_NAME-1);
    strncpy(entry.socket_path, "/tmp/rm.sock", SM_MAX_PATH-1);
    entry.pid = 4000;
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);
    sm_registry_add(&entry);
    
    /* Should find it first */
    service_entry_t found = {0};
    int status1 = sm_registry_find_copy("rmtest", &found);
    TEST_ASSERT_EQUAL_INT(status1, 0);
    
    /* Unregister */
    int status2 = sm_registry_remove("rmtest");
    TEST_ASSERT_EQUAL_INT(status2, 0);
    
    /* Should not find it now */
    int status3 = sm_registry_find_copy("rmtest", &found);
    TEST_ASSERT_EQUAL_INT(status3, -1);  /* SM_ERR_NOT_FOUND */
}

/* Test 6: Registry capacity limits */
TEST_BEGIN(registry_capacity)
{
    unity.current_test = "registry_capacity";
    
    sm_registry_init();
    
    /* Register up to capacity */
    int status = 0;
    for (int i = 0; i < SM_MAX_SERVICES; i++) {
        service_entry_t entry = {0};
        snprintf(entry.name, SM_MAX_NAME-1, "service_%d", i);
        snprintf(entry.socket_path, SM_MAX_PATH-1, "/tmp/svc_%d.sock", i);
        entry.pid = 5000 + i;
        entry.status = SERVICE_RUNNING;
        entry.last_heartbeat = time(NULL);
        
        status = sm_registry_add(&entry);
        if (i < SM_MAX_SERVICES - 1) {
            TEST_ASSERT_EQUAL_INT(status, 0);
        }
    }
    
    /* Next registration should fail (full) */
    service_entry_t overflow = {0};
    strncpy(overflow.name, "overflow", SM_MAX_NAME-1);
    strncpy(overflow.socket_path, "/tmp/overflow.sock", SM_MAX_PATH-1);
    overflow.pid = 9999;
    overflow.status = SERVICE_RUNNING;
    overflow.last_heartbeat = time(NULL);
    
    status = sm_registry_add(&overflow);
    TEST_ASSERT_EQUAL_INT(status, -2);  /* SM_ERR_FULL */
}

/* Test 7: Get all services */
TEST_BEGIN(get_all_services)
{
    unity.current_test = "get_all_services";
    
    sm_registry_init();
    
    /* Register a few services */
    for (int i = 1; i <= 3; i++) {
        service_entry_t entry = {0};
        snprintf(entry.name, SM_MAX_NAME-1, "svc%d", i);
        snprintf(entry.socket_path, SM_MAX_PATH-1, "/tmp/svc%d.sock", i);
        entry.pid = 6000 + i;
        entry.status = SERVICE_RUNNING;
        entry.last_heartbeat = time(NULL);
        sm_registry_add(&entry);
    }
    
    int count = 0;
    service_entry_t* services = NULL;
    int status = sm_registry_get_all(&services, &count);
    
    TEST_ASSERT_EQUAL_INT(status, 0);
    TEST_ASSERT_NOT_NULL(services);
    TEST_ASSERT_EQUAL_INT(count, 3);
    
    /* Verify we have the right services */
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(services[i].name, "svc1") == 0 ||
            strcmp(services[i].name, "svc2") == 0 ||
            strcmp(services[i].name, "svc3") == 0) {
            found++;
        }
    }
    TEST_ASSERT_EQUAL_INT(found, 3);
    
    /* Free the copy */
    sm_registry_free_copy(services);
}

/* Test 8: Update service heartbeat */
TEST_BEGIN(update_heartbeat)
{
    unity.current_test = "update_heartbeat";
    
    sm_registry_init();
    
    /* Register service */
    service_entry_t entry = {0};
    strncpy(entry.name, "hbtest", SM_MAX_NAME-1);
    strncpy(entry.socket_path, "/tmp/hb.sock", SM_MAX_PATH-1);
    entry.pid = 7000;
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);
    sm_registry_add(&entry);
    
    /* Wait a bit then update heartbeat */
    sleep(1);
    int status = sm_registry_update_heartbeat("hbtest");
    TEST_ASSERT_EQUAL_INT(status, 0);
    
    /* Verify heartbeat was updated */
    service_entry_t svc_copy = {0};
    int lookup_status = sm_registry_find_copy("hbtest", &svc_copy);
    TEST_ASSERT_EQUAL_INT(lookup_status, 0);
    TEST_ASSERT_TRUE(svc_copy.last_heartbeat > entry.last_heartbeat);
}

/* Test 9: Update heartbeat for non-existent service */
TEST_BEGIN(heartbeat_nonexistent)
{
    unity.current_test = "heartbeat_nonexistent";
    
    sm_registry_init();
    
    int status = sm_registry_update_heartbeat("nosuchservice");
    TEST_ASSERT_EQUAL_INT(status, -1);  /* SM_ERR_NOT_FOUND */
}

/* Test 10: Long service names */
TEST_BEGIN(long_service_names)
{
    unity.current_test = "long_service_names";
    
    sm_registry_init();
    
    /* Maximum length name (up to SM_MAX_NAME-1) */
    service_entry_t entry = {0};
    memset(entry.name, 'a', SM_MAX_NAME - 1);
    entry.name[SM_MAX_NAME - 1] = '\0';
    
    strncpy(entry.socket_path, "/tmp/long.sock", SM_MAX_PATH-1);
    entry.pid = 8000;
    entry.status = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);
    
    int status = sm_registry_add(&entry);
    TEST_ASSERT_EQUAL_INT(status, 0);
    
    /* Verify we can lookup with exact name */
    service_entry_t svc_copy = {0};
    int lookup_status = sm_registry_find_copy(entry.name, &svc_copy);
    TEST_ASSERT_EQUAL_INT(lookup_status, 0);
    TEST_ASSERT_EQUAL_STRING(svc_copy.name, entry.name);
}

/* Test 11: NULL parameter handling */
TEST_BEGIN(null_parameters)
{
    unity.current_test = "null_parameters";
    
    sm_registry_init();
    
    /* Lookup with NULL name */
    service_entry_t svc_copy = {0};
    int status1 = sm_registry_find_copy(NULL, &svc_copy);
    TEST_ASSERT_EQUAL_INT(status1, SM_ERR_INVALID);  /* Expect -4 for invalid params */
    
    /* Remove with NULL name */
    int status2 = sm_registry_remove(NULL);
    TEST_ASSERT_EQUAL_INT(status2, SM_ERR_INVALID);  /* Expect -4 for invalid params */
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
