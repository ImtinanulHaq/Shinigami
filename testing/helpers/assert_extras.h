/**
 * @file assert_extras.h
 * @brief Extended assertion macros built on top of Unity.
 *
 * Provides higher-level assertions with richer output for common
 * middleware testing patterns: file descriptor validation, error code
 * checking, buffer content comparison, timing assertions, and
 * thread-safety verifications.
 */
#ifndef ASSERT_EXTRAS_H
#define ASSERT_EXTRAS_H

#include "../framework/unity.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/* ── Error code assertion ─────────────────────────────────────────── */

/** Assert rc == 0, printing the errno string on failure. */
#define TEST_ASSERT_OK(rc) \
    do { \
        int _rc = (int)(rc); \
        if (_rc != 0) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), \
                     "Expected OK(0) but got %d (errno=%d: %s)", \
                     _rc, errno, strerror(errno)); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/** Assert rc < 0 (any negative = error). */
#define TEST_ASSERT_ERROR(rc) \
    do { \
        int _rc = (int)(rc); \
        if (_rc >= 0) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), \
                     "Expected error (<0) but got %d", _rc); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/** Assert rc == expected_code. */
#define TEST_ASSERT_ERRCODE(expected, rc) \
    do { \
        int _exp = (int)(expected); \
        int _rc  = (int)(rc); \
        if (_rc != _exp) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), \
                     "Expected error code %d but got %d", _exp, _rc); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/* ── File descriptor assertion ────────────────────────────────────── */

/** Assert that fd is a valid open file descriptor. */
#define TEST_ASSERT_VALID_FD(fd) \
    do { \
        int _fd = (int)(fd); \
        if (_fd < 0 || fcntl(_fd, F_GETFD) < 0) { \
            char _buf[64]; \
            snprintf(_buf, sizeof(_buf), "fd %d is not valid (errno=%d)", \
                     _fd, errno); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/** Assert that fd is closed (invalid). */
#define TEST_ASSERT_CLOSED_FD(fd) \
    do { \
        int _fd = (int)(fd); \
        if (_fd >= 0 && fcntl(_fd, F_GETFD) == 0) { \
            char _buf[64]; \
            snprintf(_buf, sizeof(_buf), "fd %d should be closed but is open", _fd); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/* ── Buffer comparison ────────────────────────────────────────────── */

/** Assert two memory buffers are byte-for-byte equal. */
#define TEST_ASSERT_BUF_EQUAL(expected, actual, len) \
    do { \
        if (memcmp((expected), (actual), (len)) != 0) { \
            char _buf[256]; \
            size_t _i; \
            for (_i = 0; _i < (size_t)(len); _i++) { \
                if (((uint8_t*)(expected))[_i] != ((uint8_t*)(actual))[_i]) { \
                    snprintf(_buf, sizeof(_buf), \
                             "Buffer mismatch at byte %zu: expected 0x%02x got 0x%02x", \
                             _i, \
                             ((uint8_t*)(expected))[_i], \
                             ((uint8_t*)(actual))[_i]); \
                    break; \
                } \
            } \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/** Assert a buffer is all zeros. */
#define TEST_ASSERT_BUF_ZERO(buf, len) \
    do { \
        size_t _i; \
        for (_i = 0; _i < (size_t)(len); _i++) { \
            if (((uint8_t*)(buf))[_i] != 0) { \
                char _b[64]; \
                snprintf(_b, sizeof(_b), \
                         "Buffer not zero at byte %zu: 0x%02x", \
                         _i, ((uint8_t*)(buf))[_i]); \
                TEST_FAIL_MESSAGE(_b); \
            } \
        } \
    } while (0)

/* ── Timing assertions ────────────────────────────────────────────── */

/** Assert elapsed_ms is within [lo, hi] milliseconds. */
#define TEST_ASSERT_TIMING_MS(elapsed_ms, lo_ms, hi_ms) \
    do { \
        long _e = (long)(elapsed_ms); \
        if (_e < (long)(lo_ms) || _e > (long)(hi_ms)) { \
            char _buf[128]; \
            snprintf(_buf, sizeof(_buf), \
                     "Timing out of range: got %ld ms, expected [%ld, %ld]", \
                     _e, (long)(lo_ms), (long)(hi_ms)); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/* ── HAL error assertion ──────────────────────────────────────────── */

#define TEST_ASSERT_HAL_OK(rc) \
    do { \
        int _rc = (int)(rc); \
        if (_rc < 0) { \
            char _buf[64]; \
            snprintf(_buf, sizeof(_buf), "HAL error %d", _rc); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/* ── /proc/self/fd leak guard ─────────────────────────────────────── */

/** Count currently open file descriptors via /proc/self/fd. */
static inline int count_open_fds(void)
{
    int count = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') count++;
    }
    closedir(d);
    return count - 1; /* subtract the opendir fd itself */
}

#define TEST_ASSERT_NO_FD_LEAK(before, after) \
    do { \
        int _delta = (int)(after) - (int)(before); \
        if (_delta != 0) { \
            char _buf[64]; \
            snprintf(_buf, sizeof(_buf), \
                     "FD leak: %d fd(s) not closed (before=%d after=%d)", \
                     _delta, (int)(before), (int)(after)); \
            TEST_FAIL_MESSAGE(_buf); \
        } \
    } while (0)

/* Bring in DIR / dirent for count_open_fds */
#include <dirent.h>
#include <fcntl.h>

#endif /* ASSERT_EXTRAS_H */
