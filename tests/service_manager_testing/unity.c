/* Minimal Unity Framework Implementation */
#include "unity.h"

UnityTestStatus unity = {0, 0, 0, NULL};

void unity_print_results(void) {
    printf("\n========================================\n");
    printf("Tests Run:    %d\n", unity.tests_run);
    printf("Tests Passed: %d\n", unity.tests_passed);
    printf("Tests Failed: %d\n", unity.tests_failed);
    printf("========================================\n");
    
    if (unity.tests_failed == 0) {
        printf("\033[0;32mOK - All tests passed\033[0m\n");
        exit(0);
    } else {
        printf("\033[0;31mFAIL - %d test(s) failed\033[0m\n", unity.tests_failed);
        exit(1);
    }
}
