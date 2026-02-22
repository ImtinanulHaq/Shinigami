#define _POSIX_C_SOURCE 200809L

/*
 * sm_registry.c - Thread-safe service registry.
 *
 * Fixes applied:
 *   - sm_registry_remove() rebuilds hash table after array shift to prevent
 *     stale index references causing silent data corruption.
 *   - sm_registry_get_all() returns a heap-allocated copy; caller frees it.
 *     This eliminates the lock-release-then-use race condition.
 *   - SERVICE_DEAD status supported.
 */

#include "../infrastructure/sm_registry.h"
#include "../observability/sm_logging.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/* ── STATE ──────────────────────────────────────────────────────────────────── */

static service_entry_t  registry[SM_MAX_SERVICES];
static int              registry_count = 0;
static pthread_rwlock_t registry_lock  = PTHREAD_RWLOCK_INITIALIZER;

/* ── HASH TABLE (name -> array index) ──────────────────────────────────────── */

/*
 * Hash table size must be a power of 2 and at least 2x SM_MAX_SERVICES
 * to keep load factor below 0.5 and minimise collisions.
 */
#define HASH_SIZE 64

typedef struct hash_node {
    int               registry_idx;
    struct hash_node* next;
} hash_node_t;

static hash_node_t  hash_pool[SM_MAX_SERVICES];
static int          hash_pool_used = 0;
static hash_node_t* hash_table[HASH_SIZE];

/* DJB2 hash, result masked to table size */
static uint32_t hash_name(const char* name)
{
    uint32_t h = 5381;
    while (*name) h = ((h << 5) + h) ^ (uint8_t)*name++;
    return h & (HASH_SIZE - 1);
}

static void hash_insert(const char* name, int idx)
{
    uint32_t     slot = hash_name(name);
    hash_node_t* node = &hash_pool[hash_pool_used++];
    node->registry_idx = idx;
    node->next         = hash_table[slot];
    hash_table[slot]   = node;
}

/* Returns registry index or -1 if not found */
static int hash_find(const char* name)
{
    uint32_t     slot = hash_name(name);
    hash_node_t* node = hash_table[slot];
    while (node) {
        if (strncmp(registry[node->registry_idx].name, name, SM_MAX_NAME) == 0)
            return node->registry_idx;
        node = node->next;
    }
    return -1;
}

static void hash_remove(const char* name)
{
    uint32_t      slot = hash_name(name);
    hash_node_t** cur  = &hash_table[slot];
    while (*cur) {
        if (strncmp(registry[(*cur)->registry_idx].name, name, SM_MAX_NAME) == 0) {
            *cur = (*cur)->next;
            return;
        }
        cur = &(*cur)->next;
    }
}

/*
 * Rebuild the entire hash table from the current array contents.
 * Must be called after any array element is moved (i.e. after a shift).
 * Caller must hold the write lock.
 */
static void hash_rebuild(void)
{
    int i;
    memset(hash_pool,  0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    hash_pool_used = 0;
    for (i = 0; i < registry_count; i++) {
        hash_insert(registry[i].name, i);
    }
}

/* ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────── */

int sm_registry_init(void)
{
    pthread_rwlock_wrlock(&registry_lock);
    memset(registry,   0, sizeof(registry));
    memset(hash_pool,  0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    registry_count = 0;
    hash_pool_used = 0;
    pthread_rwlock_unlock(&registry_lock);
    return 0;
}

int sm_registry_add(const service_entry_t* entry)
{
    int idx;

    if (!entry) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    if (hash_find(entry->name) >= 0) {
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR, "registry: '%s' already registered", entry->name);
        return SM_ERR_EXISTS;
    }

    if (registry_count >= SM_MAX_SERVICES) {
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR, "registry: full (max %d)", SM_MAX_SERVICES);
        return SM_ERR_FULL;
    }

    idx = registry_count++;
    registry[idx]              = *entry;
    registry[idx].registered_at = time(NULL);
    registry[idx].restart_count = 0;
    registry[idx].status        = SERVICE_RUNNING;

    hash_insert(entry->name, idx);

    sm_log(SM_LOG_INFO, "registry: registered '%s' pid=%d uid=%d",
           entry->name, (int)entry->pid, (int)entry->uid);

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

service_entry_t* sm_registry_find(const char* name)
{
    int idx;

    if (!name) return NULL;

    pthread_rwlock_rdlock(&registry_lock);
    idx = hash_find(name);
    pthread_rwlock_unlock(&registry_lock);

    return (idx < 0) ? NULL : &registry[idx];
}

int sm_registry_update_status(const char* name, service_status_t status)
{
    int              idx;
    service_status_t old;

    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    old = registry[idx].status;
    registry[idx].status = status;

    if (status == SERVICE_CRASHED) {
        registry[idx].last_crash_time = time(NULL);
        registry[idx].restart_count++;
    }

    sm_log(SM_LOG_WARN, "registry: '%s' status %d -> %d", name, old, status);

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_update_heartbeat(const char* name)
{
    int idx;

    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    registry[idx].last_heartbeat = time(NULL);
    registry[idx].status         = SERVICE_RUNNING;

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_remove(const char* name)
{
    int idx;

    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    sm_log(SM_LOG_INFO, "registry: unregistered '%s'", registry[idx].name);

    /* Remove from hash table BEFORE shifting the array */
    hash_remove(name);

    /* Compact the array by shifting remaining entries left */
    for (int i = idx; i < registry_count - 1; i++) {
        registry[i] = registry[i + 1];
    }
    memset(&registry[registry_count - 1], 0, sizeof(service_entry_t));
    registry_count--;

    /*
     * Rebuild hash table completely after shifting.
     * This is the critical fix: after shifting, any existing hash nodes that
     * stored indices > idx now point to wrong slots, causing silent corruption.
     */
    hash_rebuild();

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_get_all(service_entry_t** out, int* count)
{
    service_entry_t* copy;
    size_t           copy_size;

    if (!out || !count) return SM_ERR_INVALID;

    pthread_rwlock_rdlock(&registry_lock);

    if (registry_count == 0) {
        *out   = NULL;
        *count = 0;
        pthread_rwlock_unlock(&registry_lock);
        return SM_OK;
    }

    copy_size = (size_t)registry_count * sizeof(service_entry_t);
    copy = malloc(copy_size);
    if (!copy) {
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR, "registry: get_all malloc failed");
        return SM_ERR_INVALID;
    }

    memcpy(copy, registry, copy_size);
    *out   = copy;
    *count = registry_count;

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

void sm_registry_free_copy(service_entry_t* copy)
{
    free(copy);
}

int sm_registry_count(void)
{
    int n;
    pthread_rwlock_rdlock(&registry_lock);
    n = registry_count;
    pthread_rwlock_unlock(&registry_lock);
    return n;
}

void sm_registry_cleanup(void)
{
    pthread_rwlock_wrlock(&registry_lock);
    memset(registry,   0, sizeof(registry));
    memset(hash_pool,  0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    registry_count = 0;
    hash_pool_used = 0;
    pthread_rwlock_unlock(&registry_lock);
}
/* ── SAFE CONCURRENT ACCESS FUNCTIONS ──────────────────────────────────────── */

/*
 * sm_registry_find_copy() - Copy entry while holding read lock (TOCTOU fix).
 *
 * Unlike sm_registry_find() which returns a raw pointer after releasing the
 * lock, this function copies the entry into caller-provided storage while the
 * read lock is held.  The caller therefore always reads consistent data even
 * if another thread modifies the registry concurrently.
 */
int sm_registry_find_copy(const char* name, service_entry_t* out)
{
    int idx;

    if (!name || !out) return SM_ERR_INVALID;

    pthread_rwlock_rdlock(&registry_lock);

    idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    *out = registry[idx];          /* copy while lock is held */

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

/*
 * sm_registry_remove_if_owner() - Atomic PID check + remove (TOCTOU fix).
 *
 * The previous pattern in sm_handle_unregister was:
 *   entry = sm_registry_find(name);  // rdlock acquired then released
 *   if (entry->pid != peer_pid) ...  // ← window: array could be shifted here
 *   sm_registry_remove(name);        // another window
 *
 * This function performs the ownership check and the removal in a single
 * write-lock section, closing both TOCTOU windows.
 */
int sm_registry_remove_if_owner(const char* name, pid_t owner_pid)
{
    int idx;

    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    /* Authorization: kernel-verified PID from SO_PEERCRED must match */
    if (registry[idx].pid != owner_pid) {
        sm_log(SM_LOG_ERROR,
               "registry: remove_if_owner denied - owner_pid=%d, requesting_pid=%d, service='%s'",
               (int)registry[idx].pid, (int)owner_pid, name);
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_PERMISSION;
    }

    sm_log(SM_LOG_INFO, "registry: unregistered '%s' pid=%d",
           registry[idx].name, (int)owner_pid);

    hash_remove(name);

    for (int i = idx; i < registry_count - 1; i++) {
        registry[i] = registry[i + 1];
    }
    memset(&registry[registry_count - 1], 0, sizeof(service_entry_t));
    registry_count--;

    hash_rebuild();

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}