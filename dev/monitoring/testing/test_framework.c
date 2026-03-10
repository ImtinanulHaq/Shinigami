/**
 * @file test_framework.c
 * @brief Test framework implementation
 */
#define _POSIX_C_SOURCE 200809L
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>
#include <math.h>

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

char *test_create_temp_dir(void)
{
    char *path = malloc(256);
    snprintf(path, 256, "/tmp/monitor_test_%d", getpid());
    mkdir(path, 0755);
    return path;
}

static void remove_dir_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive(full_path);
            } else {
                unlink(full_path);
            }
        }
    }
    closedir(d);
    rmdir(path);
}

void test_cleanup_dir(const char *path)
{
    remove_dir_recursive(path);
}

void test_sleep_ms(uint32_t ms)
{
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);
}

uint64_t test_get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

int test_run_suite(test_case_t *tests, int count, const char *suite_name)
{
    printf("\n=== Running Test Suite: %s ===\n", suite_name);
    
    int suite_passed = 0;
    int suite_failed = 0;
    
    for (int i = 0; i < count; i++) {
        if (!tests[i].enabled) continue;
        
        printf("  [%3d/%-3d] %s...", i+1, count, tests[i].name);
        fflush(stdout);
        
        g_tests_run = 0;
        g_tests_passed = 0;
        g_tests_failed = 0;
        
        bool result = tests[i].func();
        
        if (result && g_tests_failed == 0) {
            printf(" PASS (%d assertions)\n", g_tests_passed);
            suite_passed++;
        } else {
            printf(" FAIL\n");
            suite_failed++;
        }
    }
    
    printf("\n=== Suite Summary: %d passed, %d failed ===\n\n",
           suite_passed, suite_failed);
    
    return suite_failed;
}

bool test_process_running(pid_t pid)
{
    return (kill(pid, 0) == 0);
}

void test_kill_process(pid_t pid)
{
    kill(pid, SIGTERM);
    test_sleep_ms(100);
    if (test_process_running(pid)) {
        kill(pid, SIGKILL);
    }
}

char *test_read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    
    if (size_out) *size_out = size;
    return buf;
}

bool test_write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    
    fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

bool test_socket_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISSOCK(st.st_mode));
}

bool test_wait_for_socket(const char *path, uint32_t timeout_ms)
{
    uint64_t start = test_get_time_ms();
    while (test_get_time_ms() - start < timeout_ms) {
        if (test_socket_exists(path)) {
            return true;
        }
        test_sleep_ms(10);
    }
    return false;
}

int test_count_fds(void)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') {
            count++;
        }
    }
    closedir(d);
    return count;
}

uint64_t test_get_rss_bytes(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    
    char line[256];
    uint64_t rss_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%lu", &rss_kb);
            break;
        }
    }
    fclose(f);
    return rss_kb * 1024;
}
