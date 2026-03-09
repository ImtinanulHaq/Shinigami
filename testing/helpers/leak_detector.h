/**
 * @file leak_detector.h
 * @brief Lightweight malloc/free tracking for test suites.
 *
 * Replaces malloc/calloc/realloc/free with tracking wrappers using
 * __attribute__((weak)) aliases so that the tracked functions are
 * only active in test binaries that link this translation unit.
 *
 * Usage:
 *   ld_reset();                    // clear counters before test
 *   // ... code under test ...
 *   ld_stats_t s = ld_get_stats();
 *   TEST_ASSERT_EQUAL_INT(0, s.live_bytes); // no leak
 */
#ifndef LEAK_DETECTOR_H
#define LEAK_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t total_allocs;   /**< Calls to malloc/calloc/realloc */
    size_t total_frees;    /**< Calls to free (non-NULL ptr)   */
    size_t live_allocs;    /**< Allocs without matching free   */
    size_t live_bytes;     /**< Bytes currently allocated      */
    size_t peak_bytes;     /**< High-water mark of live_bytes  */
    size_t double_frees;   /**< Attempted free of unknown ptr  */
} ld_stats_t;

/** Reset all counters. Call at the start of each test. */
void ld_reset(void);

/** Return a snapshot of current statistics. */
ld_stats_t ld_get_stats(void);

/** Print statistics to stderr (for debugging). */
void ld_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* LEAK_DETECTOR_H */
