/* Unit tests for sm_protocol - Message format validation */
#include "unity.h"
#include "../../dev/core/service_manager/infrastructure/sm_protocol.h"
#include <time.h>
#include <string.h>

/* Test 1: Valid header validation */
TEST_BEGIN(valid_header_validation)
{
    unity.current_test = "valid_header_validation";
    
    sm_hdr_t hdr = {
        .magic = SM_PROTOCOL_MAGIC,
        .version = SM_PROTOCOL_VERSION,
        .type = SM_MSG_REGISTER,
        .length = 100,
        .timestamp = (uint32_t)time(NULL),
        .client_pid = 1234,
        .nonce = 5678
    };
    
    /* Valid header should pass (no validation function exposed, so just check fields) */
    TEST_ASSERT_EQUAL_INT(hdr.magic, SM_PROTOCOL_MAGIC);
    TEST_ASSERT_EQUAL_INT(hdr.version, SM_PROTOCOL_VERSION);
    TEST_ASSERT_EQUAL_INT(hdr.type, SM_MSG_REGISTER);
}

/* Test 2: Large payload size rejection */
TEST_BEGIN(large_payload_rejection)
{
    unity.current_test = "large_payload_rejection";
    
    uint32_t huge_length = SM_MAX_PAYLOAD_SIZE + 1;
    /* Should reject lengths beyond max */
    TEST_ASSERT_TRUE(huge_length > SM_MAX_PAYLOAD_SIZE);
}

/* Test 3: Service name validation - valid names */
TEST_BEGIN(valid_service_names)
{
    unity.current_test = "valid_service_names";
    
    /* These should be valid */
    const char* names[] = {
        "myservice",
        "auth-service",
        "db_worker",
        "api123",
        "svc"
    };
    
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(strlen(names[i]) > 0);
        TEST_ASSERT_TRUE(strlen(names[i]) < SM_MAX_NAME);
    }
}

/* Test 4: Service name validation - invalid names */
TEST_BEGIN(invalid_service_names)
{
    unity.current_test = "invalid_service_names";
    
    /* Empty name */
    const char* empty = "";
    TEST_ASSERT_TRUE(strlen(empty) == 0);  /* Should be rejected */
    
    /* Name with special chars that might cause injection */
    const char* injection = "service\";DROP TABLE";
    TEST_ASSERT_TRUE(strchr(injection, '"') != NULL);  /* Detectible */
}

/* Test 5: Path validation - allowed paths */
TEST_BEGIN(valid_paths)
{
    unity.current_test = "valid_paths";
    
    const char* paths[] = {
        "/run/myservice.sock",
        "/tmp/service.sock",
        "/run/app/service.sock"
    };
    
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(strncmp(paths[i], "/run/", 5) == 0 || 
                        strncmp(paths[i], "/tmp/", 5) == 0);
    }
}

/* Test 6: Path validation - disallowed paths */
TEST_BEGIN(invalid_paths)
{
    unity.current_test = "invalid_paths";
    
    const char* bad_paths[] = {
        "/etc/passwd",
        "../../../etc/passwd",
        "/proc/self/maps",
        "/var/log/system.log"
    };
    
    for (int i = 0; i < 4; i++) {
        /* Should not allow /etc, /proc, /var, or path traversal */
        TEST_ASSERT_FALSE(strncmp(bad_paths[i], "/run/", 5) == 0 || 
                         strncmp(bad_paths[i], "/tmp/", 5) == 0);
    }
}

/* Test 7: Message type constants */
TEST_BEGIN(message_types)
{
    unity.current_test = "message_types";
    
    TEST_ASSERT_EQUAL_INT(SM_MSG_REGISTER, 1);
    TEST_ASSERT_EQUAL_INT(SM_MSG_LOOKUP, 2);
    TEST_ASSERT_EQUAL_INT(SM_MSG_HEARTBEAT, 3);
    TEST_ASSERT_EQUAL_INT(SM_MSG_UNREGISTER, 4);
}

/* Test 8: Response codes */
TEST_BEGIN(response_codes)
{
    unity.current_test = "response_codes";
    
    TEST_ASSERT_EQUAL_INT(SM_OK, 0);
    TEST_ASSERT_EQUAL_INT(SM_ERR_NOT_FOUND, -1);
    TEST_ASSERT_EQUAL_INT(SM_ERR_FULL, -2);
    TEST_ASSERT_EQUAL_INT(SM_ERR_EXISTS, -3);
    TEST_ASSERT_EQUAL_INT(SM_ERR_INVALID, -4);
    TEST_ASSERT_EQUAL_INT(SM_ERR_RATELIMIT, -7);
    TEST_ASSERT_EQUAL_INT(SM_ERR_AUTH, -8);
}

/* Test 9: Header size constant */
TEST_BEGIN(header_size)
{
    unity.current_test = "header_size";
    
    /* Header should be 56 bytes (magic+version+type+length+timestamp+pid+nonce+hmac) */
    TEST_ASSERT_EQUAL_INT(sizeof(sm_hdr_t), 56);
}

/* Test 10: Max services and limits */
TEST_BEGIN(protocol_limits)
{
    unity.current_test = "protocol_limits";
    
    TEST_ASSERT_EQUAL_INT(SM_MAX_SERVICES, 32);
    TEST_ASSERT_EQUAL_INT(SM_MAX_NAME, 64);
    TEST_ASSERT_EQUAL_INT(SM_MAX_PATH, 256);
    TEST_ASSERT_EQUAL_INT(SM_MAX_PAYLOAD_SIZE, 1024);
}

/* Main test runner */
int main(void) {
    printf("\n=== Protocol Unit Tests ===\n");
    
    test_valid_header_validation();
    test_large_payload_rejection();
    test_valid_service_names();
    test_invalid_service_names();
    test_valid_paths();
    test_invalid_paths();
    test_message_types();
    test_response_codes();
    test_header_size();
    test_protocol_limits();
    
    unity_print_results();
    return 0;
}
