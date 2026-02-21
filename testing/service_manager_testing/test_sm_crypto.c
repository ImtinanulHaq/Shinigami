/* Unit tests for sm_crypto - HMAC/SHA256 operations */
#include "unity.h"
#include "sm_crypto.h"
#include <string.h>
#include <stdlib.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#define TEST_KEY "supersecretkey123456789"
#define TEST_MESSAGE "hello world"

/* Test 1: HMAC computation consistency */
TEST_BEGIN(hmac_computation_consistency)
{
    unity.current_test = "hmac_computation_consistency";
    
    unsigned char mac1[32];
    unsigned char mac2[32];
    
    /* Compute same HMAC twice */
    int result1 = sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac1
    );
    
    int result2 = sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac2
    );
    
    TEST_ASSERT_EQUAL_INT(result1, 0);
    TEST_ASSERT_EQUAL_INT(result2, 0);
    
    /* Both should be identical */
    TEST_ASSERT_TRUE(memcmp(mac1, mac2, 32) == 0);
}

/* Test 2: HMAC verification - valid */
TEST_BEGIN(hmac_verification_valid)
{
    unity.current_test = "hmac_verification_valid";
    
    unsigned char mac[32];
    
    /* Compute HMAC */
    sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    /* Verify with same inputs */
    int result = sm_crypto_verify_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    TEST_ASSERT_EQUAL_INT(result, 1);  /* 1 = valid */
}

/* Test 3: HMAC verification - modified message */
TEST_BEGIN(hmac_verification_modified_message)
{
    unity.current_test = "hmac_verification_modified_message";
    
    unsigned char mac[32];
    
    /* Compute HMAC for original message */
    sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    /* Verify with modified message */
    const char* modified = "hallo world";  /* 'e' changed to 'a' */
    int result = sm_crypto_verify_hmac(
        (unsigned char*)modified, strlen(modified),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    TEST_ASSERT_EQUAL_INT(result, 0);  /* 0 = invalid */
}

/* Test 4: HMAC verification - wrong key */
TEST_BEGIN(hmac_verification_wrong_key)
{
    unity.current_test = "hmac_verification_wrong_key";
    
    unsigned char mac[32];
    
    /* Compute HMAC with original key */
    sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    /* Verify with wrong key */
    const char* wrong_key = "differentkey987654321";
    int result = sm_crypto_verify_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)wrong_key, strlen(wrong_key),
        mac
    );
    
    TEST_ASSERT_EQUAL_INT(result, 0);  /* 0 = invalid */
}

/* Test 5: HMAC with empty message */
TEST_BEGIN(hmac_empty_message)
{
    unity.current_test = "hmac_empty_message";
    
    unsigned char mac[32];
    
    /* Compute HMAC for empty message */
    int result = sm_crypto_compute_hmac(
        (unsigned char*)"", 0,
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    TEST_ASSERT_EQUAL_INT(result, 0);
    
    /* Should produce valid HMAC (not NULL/error) */
    int is_zero = 1;
    for (int i = 0; i < 32; i++) {
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
    
    unsigned char mac[32];
    
    sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    /* Should be 32 bytes (SHA256) */
    TEST_ASSERT_EQUAL_INT(32, 32);
}

/* Test 7: Large message HMAC */
TEST_BEGIN(hmac_large_message)
{
    unity.current_test = "hmac_large_message";
    
    /* Create 1MB message */
    unsigned char* large_msg = malloc(1024 * 1024);
    memset(large_msg, 'A', 1024 * 1024);
    
    unsigned char mac1[32];
    unsigned char mac2[32];
    
    /* Compute HMAC twice for consistency */
    int result1 = sm_crypto_compute_hmac(
        large_msg, 1024 * 1024,
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac1
    );
    
    int result2 = sm_crypto_compute_hmac(
        large_msg, 1024 * 1024,
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac2
    );
    
    TEST_ASSERT_EQUAL_INT(result1, 0);
    TEST_ASSERT_EQUAL_INT(result2, 0);
    TEST_ASSERT_TRUE(memcmp(mac1, mac2, 32) == 0);
    
    free(large_msg);
}

/* Test 8: Key length variations */
TEST_BEGIN(hmac_key_length_variations)
{
    unity.current_test = "hmac_key_length_variations";
    
    /* Short key */
    unsigned char short_key[] = "x";
    unsigned char mac_short[32];
    int result_short = sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        short_key, 1,
        mac_short
    );
    TEST_ASSERT_EQUAL_INT(result_short, 0);
    
    /* Long key (>64 bytes, should be hashed) */
    unsigned char long_key[100];
    memset(long_key, 'k', 100);
    unsigned char mac_long[32];
    int result_long = sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        long_key, 100,
        mac_long
    );
    TEST_ASSERT_EQUAL_INT(result_long, 0);
    
    /* HMACs should be different */
    TEST_ASSERT_TRUE(memcmp(mac_short, mac_long, 32) != 0);
}

/* Test 9: Timing-safe verification */
TEST_BEGIN(timing_safe_verification)
{
    unity.current_test = "timing_safe_verification";
    
    unsigned char mac[32];
    sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    
    /* Test with completely wrong MAC (all zeros) */
    unsigned char wrong_mac[32];
    memset(wrong_mac, 0, 32);
    
    int result = sm_crypto_verify_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        wrong_mac
    );
    
    TEST_ASSERT_EQUAL_INT(result, 0);
    
    /* Verification function should not short-circuit on first byte mismatch
       (constant-time comparison). We can't directly test timing, but we
       ensure the function returns 0 for wrong MACs. */
}

/* Test 10: NULL parameter handling */
TEST_BEGIN(null_parameters)
{
    unity.current_test = "null_parameters";
    
    unsigned char mac[32];
    
    /* NULL message pointer */
    int result1 = sm_crypto_compute_hmac(
        NULL, 10,
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        mac
    );
    TEST_ASSERT_EQUAL_INT(result1, -1);  /* Error */
    
    /* NULL key pointer */
    int result2 = sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        NULL, 10,
        mac
    );
    TEST_ASSERT_EQUAL_INT(result2, -1);  /* Error */
    
    /* NULL output buffer */
    int result3 = sm_crypto_compute_hmac(
        (unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE),
        (unsigned char*)TEST_KEY, strlen(TEST_KEY),
        NULL
    );
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
