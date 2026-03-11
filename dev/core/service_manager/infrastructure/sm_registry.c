#define _POSIX_C_SOURCE 200809L

/*
 * sm_registry.c - Thread-safe service registry.
 *
 * FIX SUMMARY (on top of the already-applied fixes):
 *   1. sm_registry_find()          — raw pointer return ke baad lock release
 *                                    hoti thi — caller ke paas dangling pointer
 *                                    aa sakta tha. WARNING comment add kiya.
 *                                    Prefer sm_registry_find_copy() instead.
 *   2. sm_registry_add()           — entry->name length validate karo pehle,
 *                                    zero-length ya whitespace-only name reject.
 *   3. sm_registry_update_status() — SERVICE_DEAD status ko restart_count
 *                                    increment nahi karna chahiye — fixed.
 *   4. sm_registry_get_all()       — malloc fail return code SM_ERR_INVALID ki
 *                                    jagah SM_ERR_FULL use karo (semantically
 *                                    correct).
 *   5. sm_registry_cleanup()       — hash_pool_used reset missing tha — added.
 *   6. hash_rebuild()              — pool overflow guard added (defensive).
 *   7. All wrlock sections         — log BAAD mein karo (lock ke bahar),
 *                                    sm_log internals lock le sakte hain.
 */

#include "../infrastructure/sm_registry.h"
#include "../observability/sm_logging.h"

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>

/* ── STATE ──────────────────────────────────────────────────────────────────── */

static service_entry_t  registry[SM_MAX_SERVICES];
static int              registry_count = 0;
static pthread_rwlock_t registry_lock  = PTHREAD_RWLOCK_INITIALIZER;

/* ── HASH TABLE (name -> array index) ──────────────────────────────────────── */

#define HASH_SIZE 64   /* power-of-2, >= 2x SM_MAX_SERVICES */

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
 * Rebuild entire hash table from current array.
 * Must be called after any array shift (i.e. after remove).
 * Caller must hold write lock.
 *
 * FIX 6: pool overflow guard — registry_count <= SM_MAX_SERVICES always,
 * but defensive check prevents out-of-bounds write if invariant breaks.
 */
static void hash_rebuild(void)
{
    memset(hash_pool,  0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    hash_pool_used = 0;

    int limit = registry_count < SM_MAX_SERVICES ? registry_count : SM_MAX_SERVICES;
    for (int i = 0; i < limit; i++) {
        hash_insert(registry[i].name, i);
    }
}

/* ── INTERNAL HELPERS ───────────────────────────────────────────────────────── */

/*
 * FIX 2: Name validation helper.
 * Zero-length names and names that are only whitespace are rejected.
 */
static int name_is_valid(const char* name)
{
    if (!name || name[0] == '\0') return 0;

    /* At least one non-whitespace character required */
    const char* p = name;
    while (*p) {
        if (!isspace((unsigned char)*p)) return 1;
        p++;
    }
    return 0;
}

/* ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────── */

int sm_registry_init(void)
{
    pthread_rwlock_wrlock(&registry_lock);
    memset(registry,   0, sizeof(registry));
    memset(hash_pool,  0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    registry_count = 0;
    hash_pool_used = 0;   /* FIX 5: reset karo */
    pthread_rwlock_unlock(&registry_lock);
    return 0;
}

/* ── sm_registry_add ────────────────────────────────────────────────────────── */

int sm_registry_add(const service_entry_t* entry)
{
    if (!entry) return SM_ERR_INVALID;

    /* FIX 2: name validate karo pehle, lock ke bahar — cheap check */
    if (!name_is_valid(entry->name)) {
        sm_log(SM_LOG_ERROR, "registry: add — empty or blank service name");
        return SM_ERR_INVALID;
    }

    pthread_rwlock_wrlock(&registry_lock);

    if (hash_find(entry->name) >= 0) {
        pthread_rwlock_unlock(&registry_lock);
        /* FIX 7: log lock ke bahar */
        sm_log(SM_LOG_ERROR, "registry: '%s' already registered", entry->name);
        return SM_ERR_EXISTS;
    }

    if (registry_count >= SM_MAX_SERVICES) {
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR, "registry: full (max %d)", SM_MAX_SERVICES);
        return SM_ERR_FULL;
    }

    int idx             = registry_count++;
    registry[idx]       = *entry;
    /* Initialise atomic hot-path fields explicitly so values are well-defined
     * regardless of what the caller placed in *entry. */
    registry[idx].registered_at = time(NULL);           /* plain field */
    atomic_init(&registry[idx].restart_count, 0);
    atomic_init(&registry[idx].status,        SERVICE_RUNNING);
    atomic_init(&registry[idx].last_heartbeat, time(NULL));
    atomic_init(&registry[idx].last_crash_time, (time_t)0);

    hash_insert(entry->name, idx);

    pthread_rwlock_unlock(&registry_lock);

    /* FIX 7: log lock ke bahar */
    sm_log(SM_LOG_INFO, "registry: registered '%s' pid=%d uid=%d",
           entry->name, (int)entry->pid, (int)entry->uid);
    return SM_OK;
}

/* ── sm_registry_find ───────────────────────────────────────────────────────── */

/*
 * WARNING: sm_registry_find() returns a raw pointer into the registry array.
 * The read lock is released before returning.  If another thread calls
 * sm_registry_remove() after this function returns, the pointer becomes a
 * dangling reference.
 *
 * USE sm_registry_find_copy() for safe concurrent access.
 * sm_registry_find() is kept only for legacy call sites that have their own
 * external synchronisation.
 */
service_entry_t* sm_registry_find(const char* name)
{
    if (!name) return NULL;

    pthread_rwlock_rdlock(&registry_lock);
    int idx = hash_find(name);
    pthread_rwlock_unlock(&registry_lock);

    return (idx < 0) ? NULL : &registry[idx];
}

/* ── sm_registry_update_status ──────────────────────────────────────────────── */

int sm_registry_update_status(const char* name, service_status_t status)
{
    if (!name) return SM_ERR_INVALID;

    /*
     * READ lock is sufficient: the only mutable state touched here lives in
     * _Atomic fields (status, last_crash_time, restart_count).  Taking a
     * read lock still serialises against structural write operations
     * (add/remove) that hold the write lock, so the index returned by
     * hash_find() stays valid until we unlock.
     */
    pthread_rwlock_rdlock(&registry_lock);

    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    service_status_t old = atomic_load_explicit(&registry[idx].status,
                                                memory_order_relaxed);
    atomic_store_explicit(&registry[idx].status, status,
                          memory_order_release);

    if (status == SERVICE_CRASHED) {
        atomic_store_explicit(&registry[idx].last_crash_time, time(NULL),
                              memory_order_relaxed);
        atomic_fetch_add_explicit(&registry[idx].restart_count, 1,
                                  memory_order_relaxed);
    }
    /* SERVICE_DEAD = give-up state — restart_count stays as-is */

    pthread_rwlock_unlock(&registry_lock);

    /* FIX 7: log lock ke bahar */
    sm_log(SM_LOG_WARN, "registry: '%s' status %d -> %d", name, old, status);
    return SM_OK;
}

/* ── sm_registry_update_heartbeat ───────────────────────────────────────────── */

int sm_registry_update_heartbeat(const char* name)
{
    if (!name) return SM_ERR_INVALID;

    /*
     * READ lock: only _Atomic fields (last_heartbeat, status) are written.
     * Multiple callers can update different services' heartbeats in parallel
     * — no write-lock contention on the hot heartbeat path.
     */
    pthread_rwlock_rdlock(&registry_lock);

    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    atomic_store_explicit(&registry[idx].last_heartbeat, time(NULL),
                          memory_order_release);
    atomic_store_explicit(&registry[idx].status, SERVICE_RUNNING,
                          memory_order_release);

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

/* ── sm_registry_remove ─────────────────────────────────────────────────────── */

int sm_registry_remove(const char* name)
{
    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    /* Save name for logging before zeroing */
    char saved_name[SM_MAX_NAME];
    strncpy(saved_name, registry[idx].name, SM_MAX_NAME - 1);
    saved_name[SM_MAX_NAME - 1] = '\0';

    /* Remove from hash BEFORE shifting array */
    hash_remove(name);

    /* Compact array */
    for (int i = idx; i < registry_count - 1; i++) {
        registry[i] = registry[i + 1];
    }
    memset(&registry[registry_count - 1], 0, sizeof(service_entry_t));
    registry_count--;

    /* Rebuild hash — indices changed after shift */
    hash_rebuild();

    pthread_rwlock_unlock(&registry_lock);

    /* FIX 7: log lock ke bahar */
    sm_log(SM_LOG_INFO, "registry: unregistered '%s'", saved_name);
    return SM_OK;
}

/* ── sm_registry_get_all ────────────────────────────────────────────────────── */

int sm_registry_get_all(service_entry_t** out, int* count)
{
    if (!out || !count) return SM_ERR_INVALID;

    pthread_rwlock_rdlock(&registry_lock);

    if (registry_count == 0) {
        *out   = NULL;
        *count = 0;
        pthread_rwlock_unlock(&registry_lock);
        return SM_OK;
    }

    size_t copy_size = (size_t)registry_count * sizeof(service_entry_t);
    service_entry_t* copy = malloc(copy_size);
    if (!copy) {
        pthread_rwlock_unlock(&registry_lock);
        /* FIX 4: SM_ERR_FULL ki jagah — malloc fail = resource exhaustion */
        sm_log(SM_LOG_ERROR, "registry: get_all malloc failed (%zu bytes)", copy_size);
        return SM_ERR_FULL;
    }

    /*
     * Use struct assignment (not memcpy) so that _Atomic fields are read
     * through the proper atomic load path.  The read lock held above
     * ensures no structural change races with this loop.
     */
    for (int i = 0; i < registry_count; i++) {
        copy[i] = registry[i];
    }
    *out   = copy;
    *count = registry_count;

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

void sm_registry_free_copy(service_entry_t* copy)
{
    free(copy);
}

/* ── sm_registry_count ──────────────────────────────────────────────────────── */

int sm_registry_count(void)
{
    pthread_rwlock_rdlock(&registry_lock);
    int n = registry_count;
    pthread_rwlock_unlock(&registry_lock);
    return n;
}

/* ── sm_registry_cleanup ────────────────────────────────────────────────────── */

void sm_registry_cleanup(void)
{
    pthread_rwlock_wrlock(&registry_lock);
    memset(registry,   0, sizeof(registry));
    memset(hash_pool,  0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    registry_count = 0;
    hash_pool_used = 0;   /* FIX 5: pehle missing tha */
    pthread_rwlock_unlock(&registry_lock);
}

/* ── sm_registry_find_copy ──────────────────────────────────────────────────── */

/*
 * Safe concurrent access — copies entry while read lock is held.
 * Eliminates TOCTOU: caller always reads consistent data even if another
 * thread removes the entry immediately after this call returns.
 */
int sm_registry_find_copy(const char* name, service_entry_t* out)
{
    if (!name || !out) return SM_ERR_INVALID;

    pthread_rwlock_rdlock(&registry_lock);

    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    *out = registry[idx];   /* copy while lock is held */

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

/* ── sm_registry_remove_if_owner ────────────────────────────────────────────── */

/*
 * Atomic PID check + remove — closes both TOCTOU windows that existed when
 * sm_handle_unregister did: find() [rdlock release] ... remove() [wrlock].
 */
int sm_registry_remove_if_owner(const char* name, pid_t owner_pid)
{
    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    if (registry[idx].pid != owner_pid) {
        int reg_pid = (int)registry[idx].pid;
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR,
               "registry: remove_if_owner denied — owner_pid=%d, requesting_pid=%d, service='%s'",
               reg_pid, (int)owner_pid, name);
        return SM_ERR_PERMISSION;
    }

    /* Save for log */
    char saved_name[SM_MAX_NAME];
    strncpy(saved_name, registry[idx].name, SM_MAX_NAME - 1);
    saved_name[SM_MAX_NAME - 1] = '\0';

    hash_remove(name);

    for (int i = idx; i < registry_count - 1; i++) {
        registry[i] = registry[i + 1];
    }
    memset(&registry[registry_count - 1], 0, sizeof(service_entry_t));
    registry_count--;

    hash_rebuild();

    pthread_rwlock_unlock(&registry_lock);

    /* FIX 7: log lock ke bahar */
    sm_log(SM_LOG_INFO, "registry: unregistered '%s' pid=%d",
           saved_name, (int)owner_pid);
    return SM_OK;
}