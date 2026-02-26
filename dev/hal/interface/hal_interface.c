/**
 * @file hal_interface.c
 * @brief Implementation of the HAL common device interface.
 *
 * Registry: open-addressing hash table, FNV-1a hash, linear probing,
 * tombstone deletion, 128-slot capacity, 75 % max load factor.
 *
 * Concurrency model:
 *   - Registry is protected by a single pthread_rwlock_t: multiple readers
 *     (hal_device_find, hal_device_list) run concurrently; writers
 *     (hal_device_register, hal_device_unregister) are exclusive.
 *   - ref_count is managed with C11 atomic_int; no lock is required for
 *     increment/decrement.
 */

#define _DEFAULT_SOURCE
#include "hal_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── registry configuration ───────────────────────────────────────────── */

#define REGISTRY_SIZE 128U        /* must be a power of 2                 */
#define REGISTRY_MAX_LOAD 96U     /* 75 % of REGISTRY_SIZE                */
#define SLOT_EMPTY 0U             /* hash == 0 → bucket unused            */
#define SLOT_TOMBSTONE UINT32_MAX /* deleted-but-probing-must-continue  */

/**
 * @brief One slot in the open-addressing hash table.
 */
typedef struct {
  uint32_t hash;
  hw_device_t *device;
} registry_slot_t;

static registry_slot_t g_registry[REGISTRY_SIZE];
static unsigned int g_registry_count = 0;
static pthread_rwlock_t g_registry_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ── FNV-1a hash ──────────────────────────────────────────────────────── */

/*
 * FNV-1a over the device name string.  Returns a non-zero uint32 so that
 * hash == 0 always means "empty slot" and hash == UINT32_MAX means
 * tombstone.  The two edge-case inputs that land on those sentinel values
 * are remapped to adjacent integers.
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

  /*
   * Start at 1: the creating HAL holds the first reference.  The registry
   * will add its own reference in hal_device_register().
   */
  atomic_init(&device_ptr->ref_count, 1);

  if (pthread_rwlock_init(&device_ptr->lock, NULL) != 0)
    return HAL_ERROR_GENERIC;

  return HAL_SUCCESS;
}

/* ── hal_device_destroy ───────────────────────────────────────────────── */

void hal_device_destroy(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;
  /*
   * Only destroys the embedded rwlock.  Callers are responsible for
   * freeing priv (via the cleanup callback) and the struct itself.
   */
  pthread_rwlock_destroy(&device_ptr->lock);
}

/* ── reference counting ───────────────────────────────────────────────── */

void hal_device_ref(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;
  /*
   * RELAXED ordering is sufficient for a plain increment: there is no
   * ordering requirement between the increment and any other memory
   * operation — we only need atomicity of the counter itself.
   */
  atomic_fetch_add_explicit(&device_ptr->ref_count, 1, memory_order_relaxed);
}

void hal_device_unref(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;

  /*
   * ACQ_REL ordering on the decrement:
   *   RELEASE — ensures all stores performed before this unref (e.g.,
   *             writes into priv) are visible to the thread that
   *             performs the final cleanup.
   *   ACQUIRE — ensures the thread reaching count == 0 sees all those
   *             prior stores (pairs with the RELEASE of the last unref
   *             that didn't reach zero).
   *
   * atomic_fetch_sub returns the OLD value, so == 1 means "was 1, now 0."
   */
  if (atomic_fetch_sub_explicit(&device_ptr->ref_count, 1,
                                memory_order_acq_rel) != 1)
    return;

  /* We are the last owner — perform full teardown. */
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

void hal_device_lock(hw_device_t *device_ptr) {
  if (device_ptr)
    pthread_rwlock_wrlock(&device_ptr->lock);
}

void hal_device_rdlock(hw_device_t *device_ptr) {
  if (device_ptr)
    pthread_rwlock_rdlock(&device_ptr->lock);
}

void hal_device_unlock(hw_device_t *device_ptr) {
  if (device_ptr)
    pthread_rwlock_unlock(&device_ptr->lock);
}

/* ── utilities ────────────────────────────────────────────────────────── */

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
  default:
    return "Unknown error";
  }
}

void hal_get_version(int *major_out, int *minor_out, int *patch_out) {
  if (major_out)
    *major_out = HAL_VERSION_MAJOR;
  if (minor_out)
    *minor_out = HAL_VERSION_MINOR;
  if (patch_out)
    *patch_out = HAL_VERSION_PATCH;
}

/* ── registry: insert ─────────────────────────────────────────────────── */

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

  for (uint32_t i = 0; i < REGISTRY_SIZE; i++) {
    uint32_t idx = (hash + i) & (REGISTRY_SIZE - 1u);

    if (g_registry[idx].hash == SLOT_EMPTY) {
      /* Use the earlier tombstone slot if we found one. */
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

    /* Occupied — check for duplicate name. */
    if (g_registry[idx].hash == hash &&
        strcmp(g_registry[idx].device->name, device_ptr->name) == 0) {
      pthread_rwlock_unlock(&g_registry_lock);
      return HAL_ERROR_BUSY;
    }
  }

  /* Table full (should not reach here given the load-factor check). */
  pthread_rwlock_unlock(&g_registry_lock);
  return HAL_ERROR_NO_MEMORY;
}

/* ── registry: remove ─────────────────────────────────────────────────── */

void hal_device_unregister(hw_device_t *device_ptr) {
  if (!device_ptr)
    return;

  uint32_t hash = fnv1a_hash(device_ptr->name);

  pthread_rwlock_wrlock(&g_registry_lock);

  for (uint32_t i = 0; i < REGISTRY_SIZE; i++) {
    uint32_t idx = (hash + i) & (REGISTRY_SIZE - 1u);

    if (g_registry[idx].hash == SLOT_EMPTY)
      break;

    if (g_registry[idx].hash == hash && g_registry[idx].device == device_ptr) {
      /*
       * Mark as tombstone rather than EMPTY so that probes for
       * other devices that were displaced past this slot by a
       * previous collision can still find them.
       */
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

hw_device_t *hal_device_find(const char *device_name) {
  if (!device_name)
    return NULL;

  uint32_t hash = fnv1a_hash(device_name);
  hw_device_t *found = NULL;

  /*
   * rdlock allows concurrent lookups from multiple threads without
   * blocking each other.  Only a writer (register/unregister) will
   * stall readers.
   */
  pthread_rwlock_rdlock(&g_registry_lock);

  for (uint32_t i = 0; i < REGISTRY_SIZE; i++) {
    uint32_t idx = (hash + i) & (REGISTRY_SIZE - 1u);

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

/* ── registry: enumerate ──────────────────────────────────────────────── */

int hal_device_list(hw_device_t **devices_out, int max_devices) {
  if (!devices_out || max_devices <= 0)
    return 0;

  int count = 0;

  pthread_rwlock_rdlock(&g_registry_lock);

  for (uint32_t i = 0; i < REGISTRY_SIZE && count < max_devices; i++) {
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
