#include "sm_registry.h"
#include "sm_logging.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

// ── STATE ──────────────────────────────────────────────────────────────────────

static service_entry_t registry[SM_MAX_SERVICES];
static int             registry_count = 0;
static pthread_rwlock_t registry_lock = PTHREAD_RWLOCK_INITIALIZER;

// ── HASH TABLE ─────────────────────────────────────────────────────────────────

#define HASH_SIZE 32  // power of 2

typedef struct hash_node {
    int               registry_idx;
    struct hash_node* next;
} hash_node_t;

static hash_node_t  hash_pool[SM_MAX_SERVICES];
static int          hash_pool_used = 0;
static hash_node_t* hash_table[HASH_SIZE];

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

// ── PUBLIC FUNCTIONS ───────────────────────────────────────────────────────────

int sm_registry_init(void)
{
    pthread_rwlock_wrlock(&registry_lock);
    memset(registry, 0, sizeof(registry));
    memset(hash_pool, 0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    registry_count = 0;
    hash_pool_used = 0;
    pthread_rwlock_unlock(&registry_lock);
    return 0;
}

int sm_registry_add(const service_entry_t* entry)
{
    if (!entry) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    // Check if already exists
    if (hash_find(entry->name) >= 0) {
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR, "service '%s' already registered", entry->name);
        return SM_ERR_EXISTS;
    }

    // Check capacity
    if (registry_count >= SM_MAX_SERVICES) {
        pthread_rwlock_unlock(&registry_lock);
        sm_log(SM_LOG_ERROR, "registry full");
        return SM_ERR_FULL;
    }

    // Add to registry
    int idx = registry_count++;
    registry[idx] = *entry;
    registry[idx].registered_at = time(NULL);
    registry[idx].restart_count = 0;

    // Add to hash table
    hash_insert(entry->name, idx);

    sm_log(SM_LOG_INFO, "registered '%s' pid=%d uid=%d",
           entry->name, entry->pid, entry->uid);

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

service_entry_t* sm_registry_find(const char* name)
{
    if (!name) return NULL;

    pthread_rwlock_rdlock(&registry_lock);
    int idx = hash_find(name);
    pthread_rwlock_unlock(&registry_lock);

    if (idx < 0) return NULL;
    return &registry[idx];
}

int sm_registry_update_status(const char* name, service_status_t status)
{
    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);
    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    service_status_t old_status = registry[idx].status;
    registry[idx].status = status;

    if (status == SERVICE_CRASHED) {
        registry[idx].last_crash_time = time(NULL);
        registry[idx].restart_count++;
    }

    sm_log(SM_LOG_WARN, "service '%s' status change: %d -> %d",
           name, old_status, status);

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_update_heartbeat(const char* name)
{
    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);
    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    registry[idx].last_heartbeat = time(NULL);
    registry[idx].status = SERVICE_RUNNING;

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_remove(const char* name)
{
    if (!name) return SM_ERR_INVALID;

    pthread_rwlock_wrlock(&registry_lock);

    int idx = hash_find(name);
    if (idx < 0) {
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_NOT_FOUND;
    }

    hash_remove(name);
    sm_log(SM_LOG_INFO, "unregistered '%s'", registry[idx].name);

    // Shift array
    for (int i = idx; i < registry_count - 1; i++)
        registry[i] = registry[i + 1];
    registry_count--;

    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_get_all(service_entry_t** out, int* count)
{
    if (!out || !count) return SM_ERR_INVALID;

    pthread_rwlock_rdlock(&registry_lock);
    *count = registry_count;
    *out = registry;
    pthread_rwlock_unlock(&registry_lock);
    return SM_OK;
}

int sm_registry_count(void)
{
    pthread_rwlock_rdlock(&registry_lock);
    int count = registry_count;
    pthread_rwlock_unlock(&registry_lock);
    return count;
}

void sm_registry_cleanup(void)
{
    pthread_rwlock_wrlock(&registry_lock);
    registry_count = 0;
    hash_pool_used = 0;
    memset(registry, 0, sizeof(registry));
    memset(hash_pool, 0, sizeof(hash_pool));
    memset(hash_table, 0, sizeof(hash_table));
    pthread_rwlock_unlock(&registry_lock);
}
