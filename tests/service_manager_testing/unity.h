/* Minimal Unity Framework for C Testing */
#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    const char* current_test;
} UnityTestStatus;

extern UnityTestStatus unity;

#define TEST_ASSERT_TRUE(condition) \
    do { \
        unity.tests_run++; \
        if (!(condition)) { \
            unity.tests_failed++; \
            printf("FAIL: %s:%d - %s\n", __FILE__, __LINE__, #condition); \
        } else { \
            unity.tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_FALSE(condition) TEST_ASSERT_TRUE(!(condition))

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { \
        unity.tests_run++; \
        if ((expected) != (actual)) { \
            unity.tests_failed++; \
            printf("FAIL: %s:%d - Expected %d but got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        } else { \
            unity.tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    do { \
        unity.tests_run++; \
        if (strcmp((expected), (actual)) != 0) { \
            unity.tests_failed++; \
            printf("FAIL: %s:%d - Expected '%s' but got '%s'\n", __FILE__, __LINE__, (expected), (actual)); \
        } else { \
            unity.tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_NULL(ptr) TEST_ASSERT_TRUE((ptr) == NULL)
#define TEST_ASSERT_NOT_NULL(ptr) TEST_ASSERT_TRUE((ptr) != NULL)

#define TEST_BEGIN(name) \
    void test_##name(void); \
    void test_##name(void)

#define TEST_END() \
    printf("PASS: %s\n", unity.current_test);

void unity_print_results(void);

#endif /* UNITY_H */
