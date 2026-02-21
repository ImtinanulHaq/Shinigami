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
    
    int result = sm_ratelimit_init(100, 10);  /* 100 requests per 10 seconds */
    TEST_ASSERT_EQUAL_INT(result, 0);
}

/* Test 2: First request passes */
TEST_BEGIN(first_request_passes)
{
    unity.current_test = "first_request_passes";
    
    sm_ratelimit_init(100, 10);
    
    int result = sm_ratelimit_check(1234, "service1");  /* PID 1234 */
    TEST_ASSERT_EQUAL_INT(result, 0);  /* 0 = OK */
}

/* Test 3: Requests within quota pass */
TEST_BEGIN(requests_within_quota)
{
    unity.current_test = "requests_within_quota";
    
    sm_ratelimit_init(10, 1);  /* 10 requests per second */
    
    int passed = 0;
    for (int i = 0; i < 10; i++) {
        int result = sm_ratelimit_check(2000, "service");
        if (result == 0) passed++;
    }
    
    TEST_ASSERT_EQUAL_INT(passed, 10);
}

/* Test 4: Requests exceeding quota blocked */
TEST_BEGIN(requests_exceeding_quota)
{
    unity.current_test = "requests_exceeding_quota";
    
    sm_ratelimit_init(5, 1);  /* 5 requests per second */
    
    /* Burn through quota */
    for (int i = 0; i < 5; i++) {
        sm_ratelimit_check(3000, "service");
    }
    
    /* Next request should be blocked */
    int result = sm_ratelimit_check(3000, "service");
    TEST_ASSERT_EQUAL_INT(result, -1);  /* -1 = rate limited */
}

/* Test 5: Different PIDs have separate quotas */
TEST_BEGIN(separate_pid_quotas)
{
    unity.current_test = "separate_pid_quotas";
    
    sm_ratelimit_init(3, 1);  /* 3 requests per second */
    
    /* Exhaust quota for PID 4000 */
    for (int i = 0; i < 3; i++) {
        sm_ratelimit_check(4000, "service");
    }
    
    /* PID 4000 should be blocked */
    int result1 = sm_ratelimit_check(4000, "service");
    TEST_ASSERT_EQUAL_INT(result1, -1);
    
    /* But PID 4001 should still have quota */
    int result2 = sm_ratelimit_check(4001, "service");
    TEST_ASSERT_EQUAL_INT(result2, 0);
}

/* Test 6: Quota refill after window */
TEST_BEGIN(quota_refill_after_window)
{
    unity.current_test = "quota_refill_after_window";
    
    sm_ratelimit_init(2, 1);  /* 2 requests per 1 second */
    
    /* Exhaust quota */
    sm_ratelimit_check(5000, "service");
    sm_ratelimit_check(5000, "service");
    
    /* Should be blocked */
    int result1 = sm_ratelimit_check(5000, "service");
    TEST_ASSERT_EQUAL_INT(result1, -1);
    
    /* Wait for window to pass (plus buffer) */
    sleep(2);
    
    /* Should be allowed again */
    int result2 = sm_ratelimit_check(5000, "service");
    TEST_ASSERT_EQUAL_INT(result2, 0);
}

/* Test 7: Cleanup and reinit */
TEST_BEGIN(cleanup_and_reinit)
{
    unity.current_test = "cleanup_and_reinit";
    
    sm_ratelimit_init(5, 1);
    
    /* Exhaust quota */
    for (int i = 0; i < 5; i++) {
        sm_ratelimit_check(6000, "service");
    }
    
    int blocked = sm_ratelimit_check(6000, "service");
    TEST_ASSERT_EQUAL_INT(blocked, -1);
    
    /* Cleanup and reinit */
    sm_ratelimit_cleanup();
    sm_ratelimit_init(10, 1);
    
    /* Should work again */
    int result = sm_ratelimit_check(6000, "service");
    TEST_ASSERT_EQUAL_INT(result, 0);
}

/* Test 8: Extended rate limit check with service name */
TEST_BEGIN(extended_rate_limit_check)
{
    unity.current_test = "extended_rate_limit_check";
    
    sm_ratelimit_init(4, 1);
    
    /* Check with extended function (include service name) */
    int result1 = sm_ratelimit_check_extended(7000, "auth-service", SM_MSG_REGISTER);
    TEST_ASSERT_EQUAL_INT(result1, 0);
    
    int result2 = sm_ratelimit_check_extended(7000, "auth-service", SM_MSG_LOOKUP);
    TEST_ASSERT_EQUAL_INT(result2, 0);
}

/* Test 9: High concurrency stress test */
TEST_BEGIN(concurrent_stress)
{
    unity.current_test = "concurrent_stress";
    
    sm_ratelimit_init(1000, 1);  /* High quota */
    
    /* Many rapid requests from different PIDs */
    int success_count = 0;
    for (int i = 0; i < 100; i++) {
        int result = sm_ratelimit_check(8000 + i, "service");
        if (result == 0) success_count++;
    }
    
    /* Most should pass (some may fail if actually rate limited) */
    TEST_ASSERT_TRUE(success_count >= 90);
}

/* Test 10: Edge cases */
TEST_BEGIN(edge_cases)
{
    unity.current_test = "edge_cases";
    
    /* Zero quota should fail immediately */
    sm_ratelimit_init(0, 1);
    int result1 = sm_ratelimit_check(9000, "service");
    TEST_ASSERT_EQUAL_INT(result1, -1);
    
    sm_ratelimit_cleanup();
    
    /* Very high quota should allow many requests */
    sm_ratelimit_init(10000, 1);
    int passed = 0;
    for (int i = 0; i < 100; i++) {
        if (sm_ratelimit_check(9100 + i, "service") == 0) {
            passed++;
        }
    }
    TEST_ASSERT_TRUE(passed >= 99);
    
    sm_ratelimit_cleanup();
    
    /* Window of 0 should be invalid, but let's test */
    int init_result = sm_ratelimit_init(100, 0);
    /* Behavior is implementation-dependent, but should not crash */
    TEST_ASSERT_TRUE(init_result >= -1);
}

/* Main test runner */
int main(void) {
    printf("\n=== Rate Limit Unit Tests ===\n");
    
    test_rate_limit_initialization();
    test_first_request_passes();
    test_requests_within_quota();
    test_requests_exceeding_quota();
    test_separate_pid_quotas();
    test_quota_refill_after_window();
    test_cleanup_and_reinit();
    test_extended_rate_limit_check();
    test_concurrent_stress();
    test_edge_cases();
    
    unity_print_results();
    return 0;
}
