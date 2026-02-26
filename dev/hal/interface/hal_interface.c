/**
 * @file hal_interface.c
 * @brief Implementation of the HAL common device interface.
 *
 * Registry: open-addressing hash table, FNV-1a hash, linear probing,
 * tombstone deletion, HAL_REGISTRY_SIZE-slot capacity, 75 % max load factor.
 *
 * Concurrency model:
 *   - Registry is protected by a single pthread_rwlock_t: multiple readers
 *     (hal_device_find, hal_device_list, hal_device_iterate) run concurrently;
 *     writers (hal_device_register, hal_device_unregister) are exclusive.
 *   - ref_count is managed with C11 atomic_int; no registry lock is needed
 *     for increment/decrement.
 *
 * PROPOSED CHANGES applied here:
 *   - hal_device_init() sets magic = HAL_DEVICE_MAGIC.
 *   - hal_device_destroy() clears magic to 0 (detects UAF in debug builds).
 *   - hal_device_iterate() added for callback-based enumeration.
 *   - Registry capacity driven by HAL_REGISTRY_SIZE (default 128, power of 2).
 */

#define _DEFAULT_SOURCE
#include "hal_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── registry configuration ───────────────────────────────────────────── */

/** 75 % load factor: max occupied slots before insert refuses. */
#define REGISTRY_MAX_LOAD ((HAL_REGISTRY_SIZE * 3u) / 4u)

/** hash == 0  →  bucket unused */
#define SLOT_EMPTY 0U

/** hash == UINT32_MAX  →  tombstone (deleted, probing continues) */
#define SLOT_TOMBSTONE UINT32_MAX

typedef struct {
  uint32_t hash;
  hw_device_t *device;
} registry_slot_t;

static registry_slot_t g_registry[HAL_REGISTRY_SIZE];
static unsigned int g_registry_count = 0;
static pthread_rwlock_t g_registry_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ── FNV-1a hash ──────────────────────────────────────────────────────── */

/**
 * @brief Compute a non-zero FNV-1a hash for @p str.
 *
 * The two sentinel values (SLOT_EMPTY == 0 and SLOT_TOMBSTONE == UINT32_MAX)
 * are remapped to adjacent safe integers so that the sentinel comparison in
 * every probe loop remains a single integer equality test rather than a range
 * check.
 *
 * @param str  Null-terminated input string.
 * @return Non-zero, non-UINT32_MAX uint32 hash.
 */
static uint32_t fnv1a_hash(const char *str) {
  uint32_t hash = 2166136261u;
  while (*str) {
    hash ^= (uint8_t)(*str++);
    hash *= 16777619u;
  }
  if (hash == SLOT_EMPTY)
    hash = 1u;
  if (hash == SLOT_TOMBSTONE)
    hash = SLOT_TOMBSTONE - 1u;
  return hash;
}

/* ── hal_device_init ──────────────────────────────────────────────────── */

/**
 * @brief Zero-initialise and populate the common fields of a device struct.
 *
 * Performs memset(0) on the entire struct, then fills every named field
 * to a well-known initial value.  The embedded pthread_rwlock_t is
 * initialised with default attributes.  ref_count is set to 1 so the
 * creating HAL holds the first reference before any registry operation.
 *
 * [PROPOSED] Sets magic = HAL_DEVICE_MAGIC after successful lock init so
 * every subsequent entry point can verify the struct has not been freed or
 * corrupted (assert(dev->magic == HAL_DEVICE_MAGIC)).
 *
 * @param device_ptr   Pointer to caller-allocated hw_device_t.
 * @param device_name  Null-terminated name (max HAL_MAX_NAME_LEN-1 chars).
 * @param device_type  Category classification from hal_device_type_t.
 * @return HAL_SUCCESS, HAL_ERROR_INVALID if any pointer is NULL,
 *         or HAL_ERROR_GENERIC if pthread_rwlock_init fails.
 */
int hal_device_init(hw_device_t *device_ptr, const char *device_name,
                    hal_device_type_t device_type) {
  if (!device_ptr || !device_name)
    return HAL_ERROR_INVALID;

  memset(device_ptr, 0, sizeof(hw_device_t));

  strncpy(device_ptr->name, device_name, HAL_MAX_NAME_LEN - 1u);
  device_ptr->name[HAL_MAX_NAME_LEN - 1u] = '\0';

  device_ptr->type = device_type;
  device_ptr->version = HAL_CURRENT_VERSION;
  device_ptr->state = HAL_STATE_CLOSED;
  device_ptr->fd = -1;
  device_ptr->ops = NULL;
  device_ptr->cleanup = NULL;
  device_ptr->priv = NULL;
  device_ptr->capabilities = 0u;
  device_ptr->magic = 0u; /* cleared until lock is ready */

  atomic_init(&device_ptr->ref_count, 1);

  if (pthread_rwlock_init(&device_ptr->lock, NULL) != 0)
    return HAL_ERROR_GENERIC;

  device_ptr->magic = HAL_DEVICE_MAGIC; /* [PROPOSED] arm the sentinel */
  return HAL_SUCCESS;
}

/* ── hal_device_destroy ───────────────────────────────────────────────── */

/**
 * @brief Destroy the reader-writer lock embedded in a device struct.
 *
 * Only destroys the embedded rwlock.  Callers are responsible for freeing
 * priv via the cleanup callback and the struct itself via free().
 *
 * [PROPOSED] Clears magic to 0 before returning so any subsequent access
 * through a stale pointer will fail the HAL_DEVICE_MAGIC check immediately
 * rather than reading stale data silently.
 *
 * @param device_ptr  Device whose lock is to be destroyed.
 */
void hal_device_destroy(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;
  device_ptr->magic = 0u; /* [PROPOSED] poison the sentinel */
  pthread_rwlock_destroy(&device_ptr->lock);
}

/* ── reference counting ───────────────────────────────────────────────── */

/**
 * @brief Increment the reference count of a device (thread-safe).
 *
 * Uses memory_order_relaxed because a plain increment has no ordering
 * requirement with respect to any other memory operation — only atomicity
 * of the counter itself is needed here.
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_ref(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;
  atomic_fetch_add_explicit(&device_ptr->ref_count, 1, memory_order_relaxed);
}

/**
 * @brief Decrement the reference count; destroy the device when it reaches 0.
 *
 * Uses memory_order_acq_rel on the decrement:
 *   RELEASE — ensures all stores before this call are visible to the thread
 *             that performs the final cleanup.
 *   ACQUIRE — the thread that reaches count == 0 sees all prior stores
 *             (pairs with the RELEASE of the last non-zero decrement).
 *
 * When count reaches zero, performs full teardown in order:
 *   1. Stops the device if ACTIVE (calls ops->stop).
 *   2. Closes the device if not CLOSED (calls ops->close).
 *   3. Calls the cleanup callback to free priv.
 *   4. Calls hal_device_destroy() to release the rwlock.
 *   5. Calls free() on device_ptr.
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_unref(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;

  if (atomic_fetch_sub_explicit(&device_ptr->ref_count, 1,
                                memory_order_acq_rel) != 1)
    return;

  /* Last owner — full teardown. */
  if (device_ptr->state == HAL_STATE_ACTIVE && device_ptr->ops &&
      device_ptr->ops->stop)
    device_ptr->ops->stop(device_ptr);

  if (device_ptr->state != HAL_STATE_CLOSED && device_ptr->ops &&
      device_ptr->ops->close)
    device_ptr->ops->close(device_ptr);

  if (device_ptr->cleanup)
    device_ptr->cleanup(device_ptr);

  hal_device_destroy(device_ptr);
  free(device_ptr);
}

/* ── locking helpers ──────────────────────────────────────────────────── */

/**
 * @brief Acquire the write (exclusive) side of the device rwlock.
 *
 * Must be held for any operation that changes device state or the priv
 * struct (open, close, control commands).
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_lock(hw_device_t *device_ptr) {
  if (device_ptr)
    pthread_rwlock_wrlock(&device_ptr->lock);
}

/**
 * @brief Acquire the read (shared) side of the device rwlock.
 *
 * Multiple threads may hold the read side simultaneously.  Suitable for
 * data-path operations (read, write) that do not modify device state.
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_rdlock(hw_device_t *device_ptr) {
  if (device_ptr)
    pthread_rwlock_rdlock(&device_ptr->lock);
}

/**
 * @brief Release whichever side of the device rwlock is currently held.
 *
 * pthread_rwlock_unlock() handles both reader and writer sides with the
 * same call, so callers do not need to track which side they acquired.
 *
 * @param device_ptr  Target device; no-op if NULL.
 */
void hal_device_unlock(hw_device_t *device_ptr) {
  if (device_ptr)
    pthread_rwlock_unlock(&device_ptr->lock);
}

/* ── utilities ────────────────────────────────────────────────────────── */

/**
 * @brief Return a human-readable string for a HAL error code.
 *
 * The returned pointer is a string literal in read-only memory; callers
 * must not modify or free it.  Unknown codes return "Unknown error".
 *
 * @param error_code  One of the hal_error_t values.
 * @return Static string; never NULL.
 */
const char *hal_error_string(hal_error_t error_code) {
  switch (error_code) {
  case HAL_SUCCESS:
    return "Success";
  case HAL_ERROR_GENERIC:
    return "Generic error";
  case HAL_ERROR_NO_DEVICE:
    return "Device not found";
  case HAL_ERROR_BUSY:
    return "Device busy";
  case HAL_ERROR_IO:
    return "I/O error";
  case HAL_ERROR_INVALID:
    return "Invalid parameter";
  case HAL_ERROR_NO_MEMORY:
    return "Out of memory";
  case HAL_ERROR_TIMEOUT:
    return "Operation timeout";
  case HAL_ERROR_NOT_SUPPORT:
    return "Not supported";
  case HAL_ERROR_PERMISSION:
    return "Permission denied";
  case HAL_ERROR_OVERFLOW:
    return "Buffer overflow";
  default:
    return "Unknown error";
  }
}

/**
 * @brief Fill caller-supplied integers with the current HAL version numbers.
 *
 * Any output pointer may be NULL; the corresponding version component is
 * then silently skipped.  Callers that only need the packed version should
 * compare against HAL_CURRENT_VERSION directly.
 *
 * @param major_out  Receives HAL_VERSION_MAJOR (may be NULL).
 * @param minor_out  Receives HAL_VERSION_MINOR (may be NULL).
 * @param patch_out  Receives HAL_VERSION_PATCH (may be NULL).
 */
void hal_get_version(int *major_out, int *minor_out, int *patch_out) {
  if (major_out)
    *major_out = HAL_VERSION_MAJOR;
  if (minor_out)
    *minor_out = HAL_VERSION_MINOR;
  if (patch_out)
    *patch_out = HAL_VERSION_PATCH;
}

/* ── registry: insert ─────────────────────────────────────────────────── */

/**
 * @brief Add a device to the global name registry.
 *
 * Uses FNV-1a hashing with open addressing and linear probing.  Tombstone
 * slots (from prior deletions) are reused to avoid fragmenting the probe
 * sequence.  The registry holds its own reference: hal_device_ref() is
 * called on success so the device is not freed while registered.
 *
 * Fails with HAL_ERROR_BUSY if a device with the same name is already
 * registered (same hash AND same name string — full comparison, not just
 * hash equality).
 *
 * @param device_ptr  Fully initialised device to register.
 * @return HAL_SUCCESS, HAL_ERROR_INVALID, HAL_ERROR_BUSY, or
 *         HAL_ERROR_NO_MEMORY if the load factor is exceeded.
 */
int hal_device_register(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;

  uint32_t hash = fnv1a_hash(device_ptr->name);

  pthread_rwlock_wrlock(&g_registry_lock);

  if (g_registry_count >= REGISTRY_MAX_LOAD) {
    pthread_rwlock_unlock(&g_registry_lock);
    return HAL_ERROR_NO_MEMORY;
  }

  int32_t tombstone_slot = -1;

  for (uint32_t i = 0; i < HAL_REGISTRY_SIZE; i++) {
    uint32_t idx = (hash + i) & (HAL_REGISTRY_SIZE - 1u);

    if (g_registry[idx].hash == SLOT_EMPTY) {
      uint32_t target = (tombstone_slot >= 0) ? (uint32_t)tombstone_slot : idx;
      g_registry[target].hash = hash;
      g_registry[target].device = device_ptr;
      g_registry_count++;
      hal_device_ref(device_ptr);
      pthread_rwlock_unlock(&g_registry_lock);
      return HAL_SUCCESS;
    }

    if (g_registry[idx].hash == SLOT_TOMBSTONE) {
      if (tombstone_slot < 0)
        tombstone_slot = (int32_t)idx;
      continue;
    }

    if (g_registry[idx].hash == hash &&
        strcmp(g_registry[idx].device->name, device_ptr->name) == 0) {
      pthread_rwlock_unlock(&g_registry_lock);
      return HAL_ERROR_BUSY;
    }
  }

  pthread_rwlock_unlock(&g_registry_lock);
  return HAL_ERROR_NO_MEMORY;
}

/* ── registry: remove ─────────────────────────────────────────────────── */

/**
 * @brief Remove a device from the global name registry.
 *
 * Replaces the matched slot with a tombstone rather than SLOT_EMPTY so
 * that probe sequences for other devices that collided at this position
 * are not interrupted.  The registry's reference to the device is released
 * via hal_device_unref(), which may trigger full teardown if this was the
 * last reference.
 *
 * The unref is called AFTER releasing the registry write-lock to avoid
 * holding two locks simultaneously (registry lock + device lock inside
 * the cleanup path).
 *
 * @param device_ptr  Device to unregister; no-op if not found.
 */
void hal_device_unregister(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;

  uint32_t hash = fnv1a_hash(device_ptr->name);

  pthread_rwlock_wrlock(&g_registry_lock);

  for (uint32_t i = 0; i < HAL_REGISTRY_SIZE; i++) {
    uint32_t idx = (hash + i) & (HAL_REGISTRY_SIZE - 1u);

    if (g_registry[idx].hash == SLOT_EMPTY)
      break;

    if (g_registry[idx].hash == hash && g_registry[idx].device == device_ptr) {
      g_registry[idx].hash = SLOT_TOMBSTONE;
      g_registry[idx].device = NULL;
      g_registry_count--;
      pthread_rwlock_unlock(&g_registry_lock);
      hal_device_unref(device_ptr);
      return;
    }
  }

  pthread_rwlock_unlock(&g_registry_lock);
}

/* ── registry: lookup ─────────────────────────────────────────────────── */

/**
 * @brief Look up a device by name and return it with an incremented refcount.
 *
 * Acquires the read side of the registry lock so multiple concurrent lookups
 * do not block each other.  The returned pointer has its refcount bumped;
 * callers MUST call hal_device_unref() when finished.
 *
 * Probe sequence skips tombstones and terminates on SLOT_EMPTY (an empty
 * slot proves no further displacement could have placed the key beyond it).
 *
 * @param device_name  Null-terminated name to search for.
 * @return Pointer with incremented refcount, or NULL if not found.
 */
hw_device_t *hal_device_find(const char *device_name) {
  if (!device_name)
    return NULL;

  uint32_t hash = fnv1a_hash(device_name);
  hw_device_t *found = NULL;

  pthread_rwlock_rdlock(&g_registry_lock);

  for (uint32_t i = 0; i < HAL_REGISTRY_SIZE; i++) {
    uint32_t idx = (hash + i) & (HAL_REGISTRY_SIZE - 1u);

    if (g_registry[idx].hash == SLOT_EMPTY)
      break;
    if (g_registry[idx].hash == SLOT_TOMBSTONE)
      continue;

    if (g_registry[idx].hash == hash &&
        strcmp(g_registry[idx].device->name, device_name) == 0) {
      found = g_registry[idx].device;
      hal_device_ref(found);
      break;
    }
  }

  pthread_rwlock_unlock(&g_registry_lock);
  return found;
}

/* ── registry: enumerate (array) ─────────────────────────────────────── */

/**
 * @brief Populate a caller array with pointers to all registered devices.
 *
 * Each returned pointer has its refcount incremented.  Callers must call
 * hal_device_unref() on every returned pointer when done.  If max_devices
 * is smaller than the total count, the first max_devices entries are
 * returned without error.
 *
 * @param devices_out  Caller-allocated array with room for max_devices.
 * @param max_devices  Capacity of devices_out.
 * @return Number of device pointers written (≤ max_devices).
 */
int hal_device_list(hw_device_t **devices_out, int max_devices) {
  if (!devices_out || max_devices <= 0)
    return 0;

  int count = 0;

  pthread_rwlock_rdlock(&g_registry_lock);

  for (uint32_t i = 0; i < HAL_REGISTRY_SIZE && count < max_devices; i++) {
    if (g_registry[i].hash == SLOT_EMPTY ||
        g_registry[i].hash == SLOT_TOMBSTONE)
      continue;
    devices_out[count] = g_registry[i].device;
    hal_device_ref(devices_out[count]);
    count++;
  }

  pthread_rwlock_unlock(&g_registry_lock);
  return count;
}

/* ── registry: enumerate (callback) [PROPOSED] ───────────────────────── */

/**
 * @brief Invoke a callback for every registered device.
 *
 * [PROPOSED] Avoids the fixed-array limitation of hal_device_list().
 * The registry read-lock is held for the entire iteration, so @p callback
 * must not call any registry write operations (register/unregister) or a
 * deadlock will occur.  Each device's refcount is NOT bumped for the
 * duration of the callback; if the callback needs to retain a pointer
 * beyond the iteration it must call hal_device_ref() itself.
 *
 * @param callback   Function called with each device and @p user_data.
 * @param user_data  Opaque pointer forwarded unchanged to @p callback.
 */
void hal_device_iterate(void (*callback)(hw_device_t *device_ptr,
                                         void *user_data),
                        void *user_data) {
  if (!callback)
    return;

  pthread_rwlock_rdlock(&g_registry_lock);

  for (uint32_t i = 0; i < HAL_REGISTRY_SIZE; i++) {
    if (g_registry[i].hash == SLOT_EMPTY ||
        g_registry[i].hash == SLOT_TOMBSTONE)
      continue;
    callback(g_registry[i].device, user_data);
  }

  pthread_rwlock_unlock(&g_registry_lock);
}
