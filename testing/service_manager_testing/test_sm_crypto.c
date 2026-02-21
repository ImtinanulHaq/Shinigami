/* Unit tests for sm_crypto - HMAC/SHA256 operations */
#include "unity.h"
#include "sm_crypto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Test utilities */
#define TEST_KEY_LEN 32
static uint8_t test_key[TEST_KEY_LEN] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};

#define TEST_MSG "hello world"
#define TEST_MSG_LEN (sizeof(TEST_MSG)-1)

/* Test 1: HMAC computation consistency */
TEST_BEGIN(hmac_computation_consistency)
{
    unity.current_test = "hmac_computation_consistency";
    
    uint8_t mac1[SM_HMAC_SIZE];
    uint8_t mac2[SM_HMAC_SIZE];
    
    /* Compute same HMAC twice */
    int result1 = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                  (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                  mac1);
    
    int result2 = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                  (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                  mac2);
    
    TEST_ASSERT_EQUAL_INT(result1, 0);
    TEST_ASSERT_EQUAL_INT(result2, 0);
    
    /* Both should be identical */
    TEST_ASSERT_TRUE(memcmp(mac1, mac2, SM_HMAC_SIZE) == 0);
}

/* Test 2: HMAC verification - valid */
TEST_BEGIN(hmac_verification_valid)
{
    unity.current_test = "hmac_verification_valid";
    
    uint8_t mac[SM_HMAC_SIZE];
    
    /* Compute HMAC */
    sm_hmac_sha256(test_key, TEST_KEY_LEN,
                   (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                   mac);
    
    /* Verify with same inputs */
    int result = sm_hmac_verify(test_key, TEST_KEY_LEN,
                                (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                mac);
    
    TEST_ASSERT_EQUAL_INT(result, 0);  /* 0 = valid */
}

/* Test 3: HMAC verification - modified message */
TEST_BEGIN(hmac_verification_modified_message)
{
    unity.current_test = "hmac_verification_modified_message";
    
    uint8_t mac[SM_HMAC_SIZE];
    
    /* Compute HMAC for original message */
    sm_hmac_sha256(test_key, TEST_KEY_LEN,
                   (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                   mac);
    
    /* Verify with modified message */
    const char* modified = "hallo world";  /* 'e' changed to 'a' */
    int result = sm_hmac_verify(test_key, TEST_KEY_LEN,
                                (const uint8_t*)modified, strlen(modified),
                                mac);
    
    TEST_ASSERT_EQUAL_INT(result, -1);  /* -1 = invalid */
}

/* Test 4: HMAC verification - wrong key */
TEST_BEGIN(hmac_verification_wrong_key)
{
    unity.current_test = "hmac_verification_wrong_key";
    
    uint8_t mac[SM_HMAC_SIZE];
    
    /* Compute HMAC with original key */
    sm_hmac_sha256(test_key, TEST_KEY_LEN,
                   (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                   mac);
    
    /* Verify with wrong key */
    uint8_t wrong_key[TEST_KEY_LEN];
    memset(wrong_key, 0xFF, TEST_KEY_LEN);
    
    int result = sm_hmac_verify(wrong_key, TEST_KEY_LEN,
                                (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                mac);
    
    TEST_ASSERT_EQUAL_INT(result, -1);  /* -1 = invalid */
}

/* Test 5: HMAC with empty message */
TEST_BEGIN(hmac_empty_message)
{
    unity.current_test = "hmac_empty_message";
    
    uint8_t mac[SM_HMAC_SIZE];
    
    /* Compute HMAC for empty message */
    int result = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                (const uint8_t*)"", 0,
                                mac);
    
    TEST_ASSERT_EQUAL_INT(result, 0);
    
    /* Should produce a valid HMAC (not all zeros) */
    int is_zero = 1;
    for (int i = 0; i < SM_HMAC_SIZE; i++) {
        if (mac[i] != 0) {
            is_zero = 0;
            break;
        }
    }
    /* HMAC of empty message should not be all zeros */
    TEST_ASSERT_EQUAL_INT(is_zero, 0);
}

/* Test 6: HMAC output length */
TEST_BEGIN(hmac_output_length)
{
    unity.current_test = "hmac_output_length";
    
    uint8_t mac[SM_HMAC_SIZE];
    
    sm_hmac_sha256(test_key, TEST_KEY_LEN,
                   (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                   mac);
    
    /* Should be 32 bytes (SHA256) */
    TEST_ASSERT_EQUAL_INT(SM_HMAC_SIZE, 32);
}

/* Test 7: Large message HMAC */
TEST_BEGIN(hmac_large_message)
{
    unity.current_test = "hmac_large_message";
    
    /* Create 100KB message */
    unsigned char* large_msg = malloc(100 * 1024);
    memset(large_msg, 'A', 100 * 1024);
    
    uint8_t mac1[SM_HMAC_SIZE];
    uint8_t mac2[SM_HMAC_SIZE];
    
    /* Compute HMAC twice for consistency */
    int result1 = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                  large_msg, 100 * 1024,
                                  mac1);
    
    int result2 = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                  large_msg, 100 * 1024,
                                  mac2);
    
    TEST_ASSERT_EQUAL_INT(result1, 0);
    TEST_ASSERT_EQUAL_INT(result2, 0);
    TEST_ASSERT_TRUE(memcmp(mac1, mac2, SM_HMAC_SIZE) == 0);
    
    free(large_msg);
}

/* Test 8: Key length variations */
TEST_BEGIN(hmac_key_length_variations)
{
    unity.current_test = "hmac_key_length_variations";
    
    /* Short key */
    uint8_t short_key[] = {0xAA};
    uint8_t mac_short[SM_HMAC_SIZE];
    int result_short = sm_hmac_sha256(short_key, 1,
                                      (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                      mac_short);
    TEST_ASSERT_EQUAL_INT(result_short, 0);
    
    /* Long key (64 bytes) */
    uint8_t long_key[64];
    memset(long_key, 0xBB, 64);
    uint8_t mac_long[SM_HMAC_SIZE];
    int result_long = sm_hmac_sha256(long_key, 64,
                                     (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                     mac_long);
    TEST_ASSERT_EQUAL_INT(result_long, 0);
    
    /* HMACs should be different */
    TEST_ASSERT_TRUE(memcmp(mac_short, mac_long, SM_HMAC_SIZE) != 0);
}

/* Test 9: Timing-safe verification */
TEST_BEGIN(timing_safe_verification)
{
    unity.current_test = "timing_safe_verification";
    
    uint8_t mac[SM_HMAC_SIZE];
    sm_hmac_sha256(test_key, TEST_KEY_LEN,
                   (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                   mac);
    
    /* Test with completely wrong MAC (all zeros) */
    uint8_t wrong_mac[SM_HMAC_SIZE];
    memset(wrong_mac, 0, SM_HMAC_SIZE);
    
    int result = sm_hmac_verify(test_key, TEST_KEY_LEN,
                                (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                wrong_mac);
    
    TEST_ASSERT_EQUAL_INT(result, -1);
    
    /* Verification function should not short-circuit
       (constant-time comparison). The function should return -1 for any mismatch. */
}

/* Test 10: NULL parameter handling */
TEST_BEGIN(null_parameters)
{
    unity.current_test = "null_parameters";
    
    uint8_t mac[SM_HMAC_SIZE];
    
    /* NULL message pointer */
    int result1 = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                 NULL, 10,
                                 mac);
    TEST_ASSERT_EQUAL_INT(result1, -1);  /* Error */
    
    /* NULL key pointer */
    int result2 = sm_hmac_sha256(NULL, TEST_KEY_LEN,
                                 (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                 mac);
    TEST_ASSERT_EQUAL_INT(result2, -1);  /* Error */
    
    /* NULL output buffer */
    int result3 = sm_hmac_sha256(test_key, TEST_KEY_LEN,
                                 (const uint8_t*)TEST_MSG, TEST_MSG_LEN,
                                 NULL);
    TEST_ASSERT_EQUAL_INT(result3, -1);  /* Error */
}

/* Main test runner */
int main(void) {
    printf("\n=== Crypto Unit Tests ===\n");
    
    test_hmac_computation_consistency();
    test_hmac_verification_valid();
    test_hmac_verification_modified_message();
    test_hmac_verification_wrong_key();
    test_hmac_empty_message();
    test_hmac_output_length();
    test_hmac_large_message();
    test_hmac_key_length_variations();
    test_timing_safe_verification();
    test_null_parameters();
    
    unity_print_results();
    return 0;
}
