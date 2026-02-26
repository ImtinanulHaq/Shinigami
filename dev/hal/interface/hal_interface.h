/**
 * @file hal_interface.h
 * @brief Hardware Abstraction Layer — common device interface.
 *
 * Every hardware subsystem (audio, camera, sensor, GPIO) is represented
 * as an @ref hw_device_t.  Callers interact exclusively through the
 * @ref hw_device_ops_t vtable, the registry API, and the lifecycle
 * helpers declared here.  No subsystem-specific header needs to be
 * included by code that only consumes devices.
 *
 * PROPOSED CHANGES (marked with [PROPOSED] below):
 *   1. Added @ref HAL_DEVICE_MAGIC and @p magic field to hw_device_t for
 *      use-after-free detection in debug builds.
 *   2. Added @p capabilities bitmask to hw_device_t so callers can query
 *      supported operations without NULL-checking every vtable slot.
 *   3. Added @p reset to hw_device_ops_t for soft-reset without full
 *      close/open cycle — many embedded devices need this.
 *   4. Added HAL_ERROR_OVERFLOW for ring-buffer saturation conditions.
 *   5. Added HAL_DEVICE_TYPE_CUSTOM base for out-of-tree vendor extensions.
 *   6. Added hal_device_iterate() callback enumeration — avoids the fixed
 *      array size limitation of hal_device_list().
 *   7. Made registry capacity configurable via HAL_REGISTRY_SIZE.
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
#define HAL_VERSION_MINOR 1
#define HAL_VERSION_PATCH 0

/** Packs major/minor/patch into a single uint32 for fast comparison. */
#define HAL_VERSION_PACK(maj, min, pat)                                        \
  (((uint32_t)(maj) << 16) | ((uint32_t)(min) << 8) | (uint32_t)(pat))

#define HAL_CURRENT_VERSION                                                    \
  HAL_VERSION_PACK(HAL_VERSION_MAJOR, HAL_VERSION_MINOR, HAL_VERSION_PATCH)

/** [PROPOSED] True if the packed version @p v satisfies minimum maj.min. */
#define HAL_VERSION_AT_LEAST(v, maj, min)                                      \
  ((v) >= HAL_VERSION_PACK((maj), (min), 0))

/* ── limits ───────────────────────────────────────────────────────────── */

/** Maximum length of a device name, including the null terminator. */
#define HAL_MAX_NAME_LEN 64U

/** Convenience: number of elements in a statically sized array. */
#define HAL_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * [PROPOSED] Compile-time registry capacity.
 * Override at build time: -DHAL_REGISTRY_SIZE=256.  Must be a power of 2.
 */
#ifndef HAL_REGISTRY_SIZE
#define HAL_REGISTRY_SIZE 128U
#endif

/* ── sentinel / magic ─────────────────────────────────────────────────── */

/**
 * [PROPOSED] Magic cookie stored in hw_device_t::magic.
 * Set on init, cleared on destroy.  Checked on every public entry point
 * to catch use-after-free in debug builds.
 */
#define HAL_DEVICE_MAGIC 0xD3A1C0DEu

/* ── error codes ──────────────────────────────────────────────────────── */

/**
 * @brief Unified error codes returned by all HAL functions.
 *
 * Negative values signal errors; zero means success.
 * Positive return values from read/write carry the byte count.
 *
 * @p HAL_SUCCESS       Operation completed without error.
 * @p HAL_ERROR_GENERIC Unclassified error.
 * @p HAL_ERROR_NO_DEVICE  Hardware not present or path invalid.
 * @p HAL_ERROR_BUSY    Device is locked by another caller.
 * @p HAL_ERROR_IO      Low-level read/write/ioctl failure.
 * @p HAL_ERROR_INVALID NULL pointer or out-of-range argument.
 * @p HAL_ERROR_NO_MEMORY  malloc/calloc returned NULL.
 * @p HAL_ERROR_TIMEOUT Operation did not complete within the deadline.
 * @p HAL_ERROR_NOT_SUPPORT  Operation not implemented for this device.
 * @p HAL_ERROR_PERMISSION  Insufficient privilege (e.g. no root for sysfs).
 * @p HAL_ERROR_OVERFLOW  [PROPOSED] Ring buffer or FIFO saturated.
 */
typedef enum {
  HAL_SUCCESS          =  0,
  HAL_ERROR_GENERIC    = -1,
  HAL_ERROR_NO_DEVICE  = -2,
  HAL_ERROR_BUSY       = -3,
  HAL_ERROR_IO         = -4,
  HAL_ERROR_INVALID    = -5,
  HAL_ERROR_NO_MEMORY  = -6,
  HAL_ERROR_TIMEOUT    = -7,
  HAL_ERROR_NOT_SUPPORT = -8,
  HAL_ERROR_PERMISSION = -9,
  HAL_ERROR_OVERFLOW   = -10,  /* [PROPOSED] */
} hal_error_t;

/* ── device classification ────────────────────────────────────────────── */

/**
 * @brief Broad category of a hardware device.
 *
 * @p HAL_DEVICE_TYPE_AUDIO    PCM audio playback or capture.
 * @p HAL_DEVICE_TYPE_SENSOR   IIO accelerometer, gyroscope, etc.
 * @p HAL_DEVICE_TYPE_CAMERA   V4L2 video capture.
 * @p HAL_DEVICE_TYPE_GPIO     Sysfs general-purpose I/O pin.
 * @p HAL_DEVICE_TYPE_DISPLAY  Frame-buffer display output.
 * @p HAL_DEVICE_TYPE_CUSTOM   [PROPOSED] Base value for vendor extensions.
 *                              Add custom types as CUSTOM + N.
 */
typedef enum {
  HAL_DEVICE_TYPE_AUDIO   = 0x01,
  HAL_DEVICE_TYPE_SENSOR  = 0x02,
  HAL_DEVICE_TYPE_CAMERA  = 0x03,
  HAL_DEVICE_TYPE_GPIO    = 0x04,
  HAL_DEVICE_TYPE_DISPLAY = 0x05,
  HAL_DEVICE_TYPE_CUSTOM  = 0x80,  /* [PROPOSED] vendor extension base */
} hal_device_type_t;

/**
 * @brief Lifecycle state of a hardware device.
 *
 * Valid transitions:
 *   CLOSED → OPEN → ACTIVE → OPEN → CLOSED
 * Any state may transition to ERROR on hardware fault.
 *
 * @p HAL_STATE_CLOSED  Not opened; no kernel resources held.
 * @p HAL_STATE_OPEN    Opened and configured; not yet streaming.
 * @p HAL_STATE_ACTIVE  Actively streaming or sampling.
 * @p HAL_STATE_ERROR   Unrecoverable hardware fault.
 */
typedef enum {
  HAL_STATE_CLOSED = 0,
  HAL_STATE_OPEN   = 1,
  HAL_STATE_ACTIVE = 2,
  HAL_STATE_ERROR  = 3,
} hal_device_state_t;

/* ── capability bits ──────────────────────────────────────────────────── */

/**
 * [PROPOSED] Bitmask constants for hw_device_t::capabilities.
 *
 * Set by the HAL implementation at create time.  Callers can test these
 * without NULL-checking every vtable slot.
 *
 * @p HAL_CAP_READ    Device supports ops->read().
 * @p HAL_CAP_WRITE   Device supports ops->write().
 * @p HAL_CAP_CONTROL Device supports ops->control().
 * @p HAL_CAP_RESET   Device supports ops->reset().
 */
#define HAL_CAP_READ    (1u << 0)
#define HAL_CAP_WRITE   (1u << 1)
#define HAL_CAP_CONTROL (1u << 2)
#define HAL_CAP_RESET   (1u << 3)  /* [PROPOSED] */

/* ── forward declaration ──────────────────────────────────────────────── */

struct hw_device;

/* ── vtable ───────────────────────────────────────────────────────────── */

/**
 * @brief Function-pointer table that defines device behaviour.
 *
 * Every HAL implementation populates one static instance and stores its
 * address in hw_device_t::ops.  This is the C equivalent of a C++ vtable.
 *
 * Return conventions: open/close/start/stop/control/get_info/reset return
 * 0 on success, negative HAL_ERROR_* on failure.  read/write return bytes
 * transferred (>0) on success, negative HAL_ERROR_* on failure.
 *
 * @p open      Acquire hardware resources and transition to OPEN.
 * @p close     Release hardware resources and transition to CLOSED.
 * @p start     Begin data production/consumption; transition to ACTIVE.
 * @p stop      Pause data flow; transition back to OPEN.
 * @p read      Receive data from the device into a caller buffer.
 * @p write     Send data from a caller buffer to the device.
 * @p control   Execute a device-specific command with an optional argument.
 * @p get_info  Fill a device-specific info struct with runtime state.
 * @p reset     [PROPOSED] Soft-reset without full close/open cycle.
 */
typedef struct {
  int    (*open    )(struct hw_device *device_ptr);
  int    (*close   )(struct hw_device *device_ptr);
  int    (*start   )(struct hw_device *device_ptr);
  int    (*stop    )(struct hw_device *device_ptr);
  ssize_t (*read   )(struct hw_device *device_ptr, void *data_buffer,
                     size_t buffer_size);
  ssize_t (*write  )(struct hw_device *device_ptr, const void *data_buffer,
                     size_t data_size);
  int    (*control )(struct hw_device *device_ptr, uint32_t control_command,
                     void *command_arg);
  int    (*get_info)(struct hw_device *device_ptr, void *info_out);
  int    (*reset   )(struct hw_device *device_ptr);  /* [PROPOSED] */
} hw_device_ops_t;

/* ── core device struct ───────────────────────────────────────────────── */

/**
 * @brief Base descriptor shared by every hardware device in the HAL.
 *
 * Thread safety: @p lock is a reader-writer lock.  Data-path operations
 * (read, write) should acquire the read side; state-changing operations
 * (control, open, close) must acquire the write side.  @p ref_count is
 * managed with C11 atomics; never manipulate it directly.
 *
 * @p name         Human-readable identifier; max HAL_MAX_NAME_LEN-1 chars.
 * @p type         Classification from hal_device_type_t.
 * @p version      HAL_CURRENT_VERSION packed at creation time.
 * @p state        Current lifecycle state from hal_device_state_t.
 * @p fd           Primary kernel file descriptor; -1 when closed.
 * @p lock         Embedded reader-writer lock protecting state and priv.
 * @p ops          Pointer to the implementation's static vtable.
 * @p cleanup      Callback invoked by hal_device_unref() at refcount==0
 *                 to free @p priv and any other implementation resources.
 * @p priv         Opaque pointer to implementation-specific private data.
 * @p ref_count    Atomic reference counter; 1 at creation.
 * @p capabilities [PROPOSED] Bitmask of HAL_CAP_* flags advertised by
 *                 the implementation.
 * @p magic        [PROPOSED] Set to HAL_DEVICE_MAGIC on init, cleared on
 *                 destroy.  Checked on public entry points to catch UAF.
 */
typedef struct hw_device {
  char               name[HAL_MAX_NAME_LEN];
  hal_device_type_t  type;
  uint32_t           version;
  hal_device_state_t state;
  int                fd;
  pthread_rwlock_t   lock;
  const hw_device_ops_t *ops;
  void (*cleanup)(struct hw_device *device_ptr);
  void              *priv;
  atomic_int         ref_count;
  uint32_t           capabilities;  /* [PROPOSED] */
  uint32_t           magic;         /* [PROPOSED] */
} hw_device_t;

/* ── lifecycle ────────────────────────────────────────────────────────── */

/** @brief Zero-initialise and populate the common fields of a device struct. */
int hal_device_init(hw_device_t *device_ptr, const char *device_name,
                    hal_device_type_t device_type);

/** @brief Destroy the reader-writer lock embedded in a device struct. */
void hal_device_destroy(hw_device_t *device_ptr);

/** @brief Increment the reference count of a device (thread-safe). */
void hal_device_ref(hw_device_t *device_ptr);

/** @brief Decrement the reference count; destroy the device when it reaches 0. */
void hal_device_unref(hw_device_t *device_ptr);

/** @brief Acquire the write (exclusive) side of the device lock. */
void hal_device_lock(hw_device_t *device_ptr);

/** @brief Acquire the read (shared) side of the device lock. */
void hal_device_rdlock(hw_device_t *device_ptr);

/** @brief Release whichever side of the device lock is currently held. */
void hal_device_unlock(hw_device_t *device_ptr);

/* ── utilities ────────────────────────────────────────────────────────── */

/** @brief Return a human-readable string for a HAL error code. */
const char *hal_error_string(hal_error_t error_code);

/** @brief Fill caller-supplied integers with the HAL version numbers. */
void hal_get_version(int *major_out, int *minor_out, int *patch_out);

/* ── device registry ──────────────────────────────────────────────────── */

/** @brief Add a device to the global name registry. */
int hal_device_register(hw_device_t *device_ptr);

/** @brief Remove a device from the global name registry. */
void hal_device_unregister(hw_device_t *device_ptr);

/** @brief Look up a device by name and return it with an incremented refcount. */
hw_device_t *hal_device_find(const char *device_name);

/** @brief Populate an array with pointers to all registered devices. */
int hal_device_list(hw_device_t **devices_out, int max_devices);

/**
 * [PROPOSED] Callback-based device enumeration — avoids fixed-array limits.
 * @brief Invoke @p callback once per registered device with @p user_data.
 */
void hal_device_iterate(void (*callback)(hw_device_t *device_ptr,
                                         void *user_data),
                        void *user_data);

#endif /* HAL_INTERFACE_H */
