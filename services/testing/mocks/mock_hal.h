/**
 * @file mock_hal.h
 * @brief Thread-safe mock hw_device_t for testing HAL service layers.
 *
 * Provides a fully controllable stub implementation of hw_device_t that:
 *   - Implements the full hw_device_ops_t interface.
 *   - Exposes per-op call counters for lifecycle-order verification.
 *   - Allows configurable return values and errno injection per op.
 *   - Captures data written to the device and supplies pre-loaded read data.
 *   - Protects all shared state with a pthread_mutex_t.
 *   - Provides mock_hal_reset() for clean per-test isolation.
 *
 * Usage:
 * @code
 *   hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);
 *   mock_hal_priv_t *p = mock_hal_get_priv(dev);
 *   p->start_retval = HAL_ERROR_NOT_SUPPORTED; // inject failure
 *   // ... run test ...
 *   assert(p->counts.open_calls == 1);
 *   mock_hal_destroy(dev);
 * @endcode
 */

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Pull in the real hw_device_t / hw_device_ops_t definitions */
#include "../../dev/hal/interface/hal_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── call counters ────────────────────────────────────────────────────── */

/**
 * @brief Per-operation invocation counters.
 *
 * Incremented atomically under mock_hal_priv_t::lock.
 */
typedef struct {
    int open_calls;     /**< Number of times open()     was called. */
    int close_calls;    /**< Number of times close()    was called. */
    int start_calls;    /**< Number of times start()    was called. */
    int stop_calls;     /**< Number of times stop()     was called. */
    int read_calls;     /**< Number of times read()     was called. */
    int write_calls;    /**< Number of times write()    was called. */
    int control_calls;  /**< Number of times control()  was called. */
    int get_info_calls; /**< Number of times get_info() was called. */
    int destroy_calls;  /**< Number of times destroy()  was called. */
} mock_hal_call_counts_t;

/* ── configurable mock state ──────────────────────────────────────────── */

/** Maximum bytes buffered for canned read data and captured write data. */
#define MOCK_HAL_MAX_DATA  65536U

/**
 * @brief Private state block embedded in hw_device_t::priv.
 *
 * Access always through mock_hal_get_priv().
 * All fields protected by @p lock.
 */
typedef struct {
    pthread_mutex_t lock; /**< Guards every other field in this struct. */

    /* ── configurable return values ── */
    int     open_retval;    /**< Returned by mock open().     Default: HAL_SUCCESS. */
    int     close_retval;   /**< Returned by mock close().    Default: HAL_SUCCESS. */
    int     start_retval;   /**< Returned by mock start().    Default: HAL_SUCCESS. */
    int     stop_retval;    /**< Returned by mock stop().     Default: HAL_SUCCESS. */
    ssize_t read_retval;    /**< Bytes returned by read(); -1 = HAL_ERROR_IO.       */
    ssize_t write_retval;   /**< Bytes accepted by write(); -1 = HAL_ERROR_IO.      */
    int     control_retval; /**< Returned by mock control().  Default: HAL_SUCCESS. */

    /* ── canned read payload ── */
    uint8_t read_data[MOCK_HAL_MAX_DATA]; /**< Bytes to copy into caller's buffer.  */
    size_t  read_data_len;                /**< Valid bytes in read_data.             */

    /* ── captured write payload ── */
    uint8_t write_data[MOCK_HAL_MAX_DATA]; /**< Last data written via write().       */
    size_t  write_data_len;                /**< Bytes captured in write_data.        */

    /* ── call counters ── */
    mock_hal_call_counts_t counts; /**< Incremented on every op call.               */
} mock_hal_priv_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a fully-wired mock HAL device.
 *
 * All operations succeed by default (return HAL_SUCCESS / expected byte
 * count).  The device starts in HAL_STATE_CLOSED.
 *
 * @param name  Device name string (copied into hw_device_t::name).
 * @param type  Device type (HAL_DEVICE_TYPE_AUDIO, _CAMERA, etc.).
 * @return Pointer to the new hw_device_t on success, NULL on allocation failure.
 * @note  Caller must call mock_hal_destroy() when done.
 */
hw_device_t *mock_hal_create(const char *name, hal_device_type_t type);

/**
 * @brief Free the mock device and release all resources.
 *
 * Safe to call with dev == NULL (no-op).
 *
 * @param dev  Device returned by mock_hal_create().
 */
void mock_hal_destroy(hw_device_t *dev);

/**
 * @brief Retrieve the private mock state for assertion in tests.
 *
 * @param dev  Device returned by mock_hal_create().
 * @return Pointer to mock_hal_priv_t embedded in dev->priv.
 * @note  Never store the returned pointer across a mock_hal_reset() call.
 */
mock_hal_priv_t *mock_hal_get_priv(hw_device_t *dev);

/**
 * @brief Load data that will be returned by the next read() call.
 *
 * Thread-safe.  Data is copied into the internal buffer.
 *
 * @param dev   Device returned by mock_hal_create().
 * @param data  Bytes to return on the next read.
 * @param len   Number of bytes in @p data (clamped to MOCK_HAL_MAX_DATA).
 */
void mock_hal_set_read_data(hw_device_t *dev, const void *data, size_t len);

/**
 * @brief Reset all call counters, capture buffers, and injected return values.
 *
 * Restores the device to its freshly-created defaults.  Thread-safe.
 *
 * @param dev  Device returned by mock_hal_create().
 */
void mock_hal_reset(hw_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_HAL_H */
