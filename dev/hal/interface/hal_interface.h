/**
 * @file hal_interface.h
 * @brief Hardware Abstraction Layer — common device interface.
 *
 * Every hardware subsystem (audio, camera, sensor, GPIO) is represented
 * as an @ref hw_device_t.  Callers interact exclusively through the
 * @ref hw_device_ops_t vtable, the registry API, and the lifecycle
 * helpers declared here.  No subsystem-specific header needs to be
 * included by code that only consumes devices.
 */

#ifndef HAL_INTERFACE_H
#define HAL_INTERFACE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* ── version ──────────────────────────────────────────────────────────── */

#define HAL_VERSION_MAJOR 2
#define HAL_VERSION_MINOR 0
#define HAL_VERSION_PATCH 0

/** Packs major/minor/patch into a single uint32 for fast comparison. */
#define HAL_VERSION_PACK(maj, min, pat)                                        \
  (((uint32_t)(maj) << 16) | ((uint32_t)(min) << 8) | (uint32_t)(pat))

#define HAL_CURRENT_VERSION                                                    \
  HAL_VERSION_PACK(HAL_VERSION_MAJOR, HAL_VERSION_MINOR, HAL_VERSION_PATCH)

/* ── limits ───────────────────────────────────────────────────────────── */

/** Maximum length of a device name, including the null terminator. */
#define HAL_MAX_NAME_LEN 64U

/** Convenience: number of elements in a statically sized array. */
#define HAL_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ── error codes ──────────────────────────────────────────────────────── */

/**
 * @brief Unified error codes returned by all HAL functions.
 *
 * Negative values signal errors; zero means success.  Positive return
 * values from read/write operations carry the byte count.
 */
typedef enum {
  HAL_SUCCESS = 0,
  HAL_ERROR_GENERIC = -1,
  HAL_ERROR_NO_DEVICE = -2,
  HAL_ERROR_BUSY = -3,
  HAL_ERROR_IO = -4,
  HAL_ERROR_INVALID = -5,
  HAL_ERROR_NO_MEMORY = -6,
  HAL_ERROR_TIMEOUT = -7,
  HAL_ERROR_NOT_SUPPORT = -8,
  HAL_ERROR_PERMISSION = -9,
} hal_error_t;

/* ── device classification ────────────────────────────────────────────── */

/**
 * @brief Broad category of a hardware device.
 */
typedef enum {
  HAL_DEVICE_TYPE_AUDIO = 0x01,
  HAL_DEVICE_TYPE_SENSOR = 0x02,
  HAL_DEVICE_TYPE_CAMERA = 0x03,
  HAL_DEVICE_TYPE_GPIO = 0x04,
  HAL_DEVICE_TYPE_DISPLAY = 0x05,
} hal_device_type_t;

/**
 * @brief Lifecycle state of a hardware device.
 *
 * Valid transitions:
 *   CLOSED → OPEN → ACTIVE → OPEN → CLOSED
 * Any state may transition to ERROR on hardware fault.
 */
typedef enum {
  HAL_STATE_CLOSED = 0,
  HAL_STATE_OPEN = 1,
  HAL_STATE_ACTIVE = 2,
  HAL_STATE_ERROR = 3,
} hal_device_state_t;

/* ── forward declaration ──────────────────────────────────────────────── */

struct hw_device;

/* ── vtable ───────────────────────────────────────────────────────────── */

/**
 * @brief Function-pointer table that defines device behaviour.
 *
 * Every HAL implementation populates one static instance of this struct
 * and stores its address in @ref hw_device_t::ops.  This is the C
 * equivalent of a C++ virtual-method table.
 *
 * Return conventions:
 *   - open / close / start / stop / control / get_info: 0 on success,
 *     negative HAL_ERROR_* on failure.
 *   - read / write: bytes transferred (>0) on success, negative
 *     HAL_ERROR_* on failure.
 */
typedef struct {
  int (*open)(struct hw_device *device_ptr);
  int (*close)(struct hw_device *device_ptr);
  int (*start)(struct hw_device *device_ptr);
  int (*stop)(struct hw_device *device_ptr);
  ssize_t (*read)(struct hw_device *device_ptr, void *data_buffer,
                  size_t buffer_size);
  ssize_t (*write)(struct hw_device *device_ptr, const void *data_buffer,
                   size_t data_size);
  int (*control)(struct hw_device *device_ptr, uint32_t control_command,
                 void *command_arg);
  int (*get_info)(struct hw_device *device_ptr, void *info_out);
} hw_device_ops_t;

/* ── core device struct ───────────────────────────────────────────────── */

/**
 * @brief Base descriptor shared by every hardware device in the HAL.
 *
 * Each subsystem-specific HAL allocates one of these, fills the common
 * fields via @ref hal_device_init, then attaches its own private data
 * through @ref priv and its vtable through @ref ops.
 *
 * Thread safety:
 *   @ref lock is a reader-writer lock.  Data-path operations (read,
 *   write) should acquire the read side; state-changing operations
 *   (control, open, close) must acquire the write side.
 *
 *   @ref ref_count is managed with C11 atomics and must never be
 *   manipulated directly — use @ref hal_device_ref / @ref hal_device_unref.
 */
typedef struct hw_device {
  char name[HAL_MAX_NAME_LEN];
  hal_device_type_t type;
  uint32_t version;
  hal_device_state_t state;
  int fd;
  pthread_rwlock_t lock;
  const hw_device_ops_t *ops;
  void (*cleanup)(struct hw_device *device_ptr);
  void *priv;
  atomic_int ref_count;
} hw_device_t;

/* ── lifecycle ────────────────────────────────────────────────────────── */

/**
 * @brief Zero-initialise and populate the common fields of a device struct.
 *
 * Must be called once after the caller allocates a @ref hw_device_t.
 * Initialises the embedded reader-writer lock and sets ref_count to 1.
 *
 * @param device_ptr   Pointer to caller-allocated hw_device_t.
 * @param device_name  Null-terminated name string (max HAL_MAX_NAME_LEN-1).
 * @param device_type  Category classification.
 * @return HAL_SUCCESS, or HAL_ERROR_INVALID if any pointer is NULL,
 *         or HAL_ERROR_GENERIC if pthread_rwlock_init fails.
 */
int hal_device_init(hw_device_t *device_ptr, const char *device_name,
                    hal_device_type_t device_type);

/**
 * @brief Destroy the reader-writer lock embedded in a device struct.
 *
 * Does NOT free @ref hw_device_t::priv — that is the responsibility of
 * the @ref hw_device_t::cleanup callback set by each HAL implementation.
 * Does NOT call free() on @p device_ptr itself.
 *
 * @param device_ptr  Device whose lock is to be destroyed.
 */
void hal_device_destroy(hw_device_t *device_ptr);

/**
 * @brief Increment the reference count of a device (thread-safe).
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_ref(hw_device_t *device_ptr);

/**
 * @brief Decrement the reference count; destroy device when count reaches 0.
 *
 * When the count hits zero this function:
 *   1. Stops and closes the device if still active.
 *   2. Calls @ref hw_device_t::cleanup to free private data.
 *   3. Calls @ref hal_device_destroy to release the rwlock.
 *   4. Calls free() on @p device_ptr.
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_unref(hw_device_t *device_ptr);

/**
 * @brief Acquire the write (exclusive) side of the device lock.
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_lock(hw_device_t *device_ptr);

/**
 * @brief Acquire the read (shared) side of the device lock.
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_rdlock(hw_device_t *device_ptr);

/**
 * @brief Release whichever side of the device lock is held.
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_unlock(hw_device_t *device_ptr);

/* ── utilities ────────────────────────────────────────────────────────── */

/**
 * @brief Return a human-readable string for a HAL error code.
 * @param error_code  One of the @ref hal_error_t values.
 * @return Static string; never NULL.
 */
const char *hal_error_string(hal_error_t error_code);

/**
 * @brief Fill caller-supplied integers with the HAL version numbers.
 * @param major_out  Receives major version (may be NULL).
 * @param minor_out  Receives minor version (may be NULL).
 * @param patch_out  Receives patch version (may be NULL).
 */
void hal_get_version(int *major_out, int *minor_out, int *patch_out);

/* ── device registry ──────────────────────────────────────────────────── */

/**
 * @brief Add a device to the global name registry.
 *
 * Increments @p device_ptr's reference count on success so the registry
 * holds its own reference.  Fails with HAL_ERROR_BUSY if a device with
 * the same name is already registered.
 *
 * @param device_ptr  Fully initialised device to register.
 * @return HAL_SUCCESS, HAL_ERROR_INVALID, HAL_ERROR_BUSY, or
 *         HAL_ERROR_NO_MEMORY.
 */
int hal_device_register(hw_device_t *device_ptr);

/**
 * @brief Remove a device from the global name registry.
 *
 * Decrements the registry's reference; the device is destroyed if the
 * count reaches zero.
 *
 * @param device_ptr  Device to unregister; no-op if not found.
 */
void hal_device_unregister(hw_device_t *device_ptr);

/**
 * @brief Look up a device by name and return it with an incremented refcount.
 *
 * Caller must call @ref hal_device_unref when finished with the returned
 * pointer.
 *
 * @param device_name  Null-terminated name to search for.
 * @return Pointer to device (refcount incremented), or NULL if not found.
 */
hw_device_t *hal_device_find(const char *device_name);

/**
 * @brief Populate an array with pointers to all registered devices.
 *
 * Each returned pointer has its reference count incremented.  Callers
 * must call @ref hal_device_unref on every returned pointer when done.
 *
 * @param devices_out  Caller-allocated array to fill.
 * @param max_devices  Capacity of @p devices_out.
 * @return Number of devices written (may be less than total if
 *         @p max_devices is too small).
 */
int hal_device_list(hw_device_t **devices_out, int max_devices);

#endif /* HAL_INTERFACE_H */
