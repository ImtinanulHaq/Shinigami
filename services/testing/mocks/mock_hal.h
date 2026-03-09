/**
 * @file mock_hal.h
 * @brief Mock hw_device_t for testing service HAL layers without real hardware.
 *
 * Provides a controllable stub implementation of hw_device_t that:
 *   - Tracks the sequence of lifecycle calls (open, start, read, write, etc.).
 *   - Lets tests inject return values and errno for each operation.
 *   - Captures data written to the device.
 *   - Supplies pre-loaded data for device reads.
 */

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Pull in the real hw_device_t definition */
#include "../../dev/hal/interface/hal_interface.h"

/* ── call counters ─────────────────────────────────────────────────────── */

typedef struct {
    int open_calls;
    int close_calls;
    int start_calls;
    int stop_calls;
    int read_calls;
    int write_calls;
    int control_calls;
    int get_info_calls;
    int destroy_calls;
} mock_hal_call_counts_t;

/* ── mock configuration ────────────────────────────────────────────────── */

#define MOCK_HAL_MAX_DATA 65536

typedef struct {
    /* Injected return values */
    int     open_retval;
    int     close_retval;
    int     start_retval;
    int     stop_retval;
    ssize_t read_retval;    /**< Bytes to return on read; -1 = error */
    ssize_t write_retval;   /**< Bytes to accept; -1 = error         */
    int     control_retval;

    /* Canned read data */
    uint8_t read_data[MOCK_HAL_MAX_DATA];
    size_t  read_data_len;

    /* Captured write data */
    uint8_t write_data[MOCK_HAL_MAX_DATA];
    size_t  write_data_len;

    /* Call counters */
    mock_hal_call_counts_t counts;
} mock_hal_priv_t;

/* ── public API ─────────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a mock HAL device.
 *
 * The returned device has all ops wired to the mock backend.
 * All operations succeed by default (retval = 0 / bytes).
 *
 * @param name  Device name for the hw_device_t.
 * @param type  Device type (HAL_DEVICE_TYPE_AUDIO, etc.).
 * @return Pointer to the mock hw_device_t, or NULL on allocation failure.
 */
hw_device_t *mock_hal_create(const char *name, hal_device_type_t type);

/**
 * @brief Destroy a mock HAL device and release all resources.
 */
void mock_hal_destroy(hw_device_t *dev);

/**
 * @brief Retrieve the private mock state for assertion in tests.
 */
mock_hal_priv_t *mock_hal_get_priv(hw_device_t *dev);

/**
 * @brief Load data that will be returned by the next read() call.
 */
void mock_hal_set_read_data(hw_device_t *dev, const void *data, size_t len);

/**
 * @brief Reset all call counters and captured write data.
 */
void mock_hal_reset(hw_device_t *dev);

#endif /* MOCK_HAL_H */
