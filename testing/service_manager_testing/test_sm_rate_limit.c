/* Unit tests for sm_rate_limit - Rate limiting operations */
#include "unity.h"
#include "sm_rate_limit.h"
#include <unistd.h>
#include <string.h>
#include <time.h>

/* Test 1: Rate limit initialization */
TEST_BEGIN(rate_limit_initialization)
{
    unity.current_test = "rate_limit_initialization";
    
    int result = sm_rate_limit_init();
    TEST_ASSERT_EQUAL_INT(result, 0);
}

/* Test 2: First request passes */
TEST_BEGIN(first_request_passes)
{
    unity.current_test = "first_request_passes";
    
    sm_rate_limit_init();
    
    int result = sm_rate_limit_check(1234);  /* PID 1234 */
    TEST_ASSERT_EQUAL_INT(result, 0);  /* SM_OK */
}

/* Test 3: Requests within quota pass */
TEST_BEGIN(requests_within_quota)
{
    unity.current_test = "requests_within_quota";
    
    sm_rate_limit_init();
    
    /* Try to consume up to PID capacity */
    int passed = 0;
    for (int i = 0; i < SM_RATE_PID_CAPACITY; i++) {
        int result = sm_rate_limit_check(2000);
        if (result == 0) passed++;
    }
    
    TEST_ASSERT_EQUAL_INT(passed, SM_RATE_PID_CAPACITY);
}

/* Test 4: Requests exceeding quota blocked */
TEST_BEGIN(requests_exceeding_quota)
{
    unity.current_test = "requests_exceeding_quota";
    
    sm_rate_limit_init();
    
    /* Burn through quota */
    for (int i = 0; i < SM_RATE_PID_CAPACITY; i++) {
        sm_rate_limit_check(3000);
    }
    
    /* Next request should be blocked */
    int result = sm_rate_limit_check(3000);
    TEST_ASSERT_EQUAL_INT(result, -7);  /* SM_ERR_RATELIMIT */
}

/* Test 5: Different PIDs have separate quotas */
TEST_BEGIN(separate_pid_quotas)
{
    unity.current_test = "separate_pid_quotas";
    
    sm_rate_limit_init();
    
    /* Exhaust quota for PID 4000 */
    for (int i = 0; i < SM_RATE_PID_CAPACITY; i++) {
        sm_rate_limit_check(4000);
    }
    
    /* PID 4000 should be blocked */
    int result1 = sm_rate_limit_check(4000);
    TEST_ASSERT_EQUAL_INT(result1, -7);
    
    /* But PID 4001 should still have quota */
    int result2 = sm_rate_limit_check(4001);
    TEST_ASSERT_EQUAL_INT(result2, 0);
}

/* Test 6: Global rate limit enforcement */
TEST_BEGIN(global_rate_limit)
{
    unity.current_test = "global_rate_limit";
    
    sm_rate_limit_init();
    
    /* Try to exceed global capacity with many PIDs */
    int passed = 0;
    for (int i = 0; i < SM_RATE_GLOBAL_CAPACITY; i++) {
        int result = sm_rate_limit_check(5000 + i);
        if (result == 0) passed++;
    }
    
    /* Should have passed global capacity requests */
    TEST_ASSERT_TRUE(passed >= SM_RATE_GLOBAL_CAPACITY - 1);
}

/* Test 7: Cleanup and reinit */
TEST_BEGIN(cleanup_and_reinit)
{
    unity.current_test = "cleanup_and_reinit";
    
    sm_rate_limit_init();
    
    /* Exhaust quota */
    for (int i = 0; i < SM_RATE_PID_CAPACITY; i++) {
        sm_rate_limit_check(6000);
    }
    
    int blocked = sm_rate_limit_check(6000);
    TEST_ASSERT_EQUAL_INT(blocked, -7);
    
    /* Cleanup and reinit */
    sm_rate_limit_cleanup();
    sm_rate_limit_init();
    
    /* Should work again */
    int result = sm_rate_limit_check(6000);
    TEST_ASSERT_EQUAL_INT(result, 0);
}

/* Test 8: Multiple rapid requests from single PID */
TEST_BEGIN(rapid_requests)
{
    unity.current_test = "rapid_requests";
    
    sm_rate_limit_init();
    
    /* Fire rapid requests from same PID */
    int success_count = 0;
    for (int i = 0; i < SM_RATE_PID_CAPACITY + 5; i++) {
        int result = sm_rate_limit_check(7000);
        if (result == 0) success_count++;
    }
    
    /* Should have succeeded for capacity, failed for rest */
    TEST_ASSERT_EQUAL_INT(success_count, SM_RATE_PID_CAPACITY);
}

/* Test 9: Distributed load across PIDs */
TEST_BEGIN(distributed_load)
{
    unity.current_test = "distributed_load";
    
    sm_rate_limit_init();
    
    /* Distribute requests across many PIDs */
    int total_passed = 0;
    for (int i = 0; i < 50; i++) {
        int result = sm_rate_limit_check(8000 + i);
        if (result == 0) total_passed++;
    }
    
    /* Most should pass (limited by global cap) */
    TEST_ASSERT_TRUE(total_passed > 0);
}

/* Test 10: Constants validation */
TEST_BEGIN(constants_validation)
{
    unity.current_test = "constants_validation";
    
    /* Verify constants are reasonable */
    TEST_ASSERT_TRUE(SM_RATE_PID_CAPACITY > 0);
    TEST_ASSERT_TRUE(SM_RATE_GLOBAL_CAPACITY > 0);
    TEST_ASSERT_TRUE(SM_RATE_TABLE_SIZE > 0);
    TEST_ASSERT_TRUE(SM_RATE_PID_REFILL > 0);
    TEST_ASSERT_TRUE(SM_RATE_GLOBAL_REFILL > 0);
    TEST_ASSERT_TRUE(SM_RATE_GLOBAL_CAPACITY >= SM_RATE_PID_CAPACITY);
}

/* Main test runner */
int main(void) {
    printf("\n=== Rate Limit Unit Tests ===\n");
    
    test_rate_limit_initialization();
    test_first_request_passes();
    test_requests_within_quota();
    test_requests_exceeding_quota();
    test_separate_pid_quotas();
    test_global_rate_limit();
    test_cleanup_and_reinit();
    test_rapid_requests();
    test_distributed_load();
    test_constants_validation();
    
    unity_print_results();
    return 0;
}

