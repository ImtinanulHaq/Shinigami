#ifndef SM_REGISTRY_H
#define SM_REGISTRY_H

/*
 * sm_registry.h - Service registry: storage and lookup of registered services.
 */

#include <time.h>
#include <sys/types.h>
#include "../infrastructure/sm_protocol.h"
#include "../lifecycle/sm_service_tier.h"

/* ── SERVICE STATUS ─────────────────────────────────────────────────────────── */

typedef enum {
    SERVICE_RUNNING  = 0,   /* healthy and receiving heartbeats */
    SERVICE_STOPPED  = 1,   /* cleanly unregistered */
    SERVICE_CRASHED  = 2,   /* heartbeat timeout - will be restarted */
    SERVICE_DEAD     = 3,   /* exceeded max restarts - no further action */
} service_status_t;

/* ── SERVICE ENTRY ──────────────────────────────────────────────────────────── */

typedef struct {
    char             name[SM_MAX_NAME];
    char             socket_path[SM_MAX_PATH];
    char             ring_name[SM_MAX_PATH];
    pid_t            pid;
    uid_t            uid;
    gid_t            gid;
    service_status_t status;
    time_t           last_heartbeat;
    time_t           registered_at;
    int              restart_count;
    time_t           last_crash_time;
    service_tier_t   tier;              /* NEW: service priority tier */
    int              health_status;     /* NEW: custom health check status */
    uint64_t         request_count;     /* NEW: total requests processed */
} service_entry_t;

/* ── FUNCTIONS ──────────────────────────────────────────────────────────────── */

int  sm_registry_init(void);

int  sm_registry_add(const service_entry_t* entry);

/* Returns a direct pointer into the registry array (valid only while registry
   lock is held by caller - intended for server-side single-threaded use).
   Use sm_registry_find_copy() for safe concurrent access. */
service_entry_t* sm_registry_find(const char* name);

/*
 * sm_registry_find_copy() - Thread-safe copy under lock.
 *
 * Copies the entry for 'name' into '*out' while holding the read lock, then
 * releases the lock.  Eliminates the TOCTOU race in sm_registry_find().
 * Returns SM_OK if found, SM_ERR_NOT_FOUND otherwise.
 */
int sm_registry_find_copy(const char* name, service_entry_t* out);

/*
 * sm_registry_remove_if_owner() - Atomic authorize-and-remove.
 *
 * Acquires the write lock, checks entry->pid == owner_pid, and if so removes
 * the entry.  The entire check+remove happens under the same lock - no TOCTOU
 * window between the PID authorization check and the removal.
 * Returns SM_OK, SM_ERR_NOT_FOUND, or SM_ERR_PERMISSION.
 */
int sm_registry_remove_if_owner(const char* name, pid_t owner_pid);

int  sm_registry_update_status(const char* name, service_status_t status);

int  sm_registry_update_heartbeat(const char* name);

int  sm_registry_remove(const char* name);

/*
 * Get a heap-allocated copy of all service entries.
 * Caller must call sm_registry_free_copy() when done.
 * Returns 0 on success, negative on error.
 */
int  sm_registry_get_all(service_entry_t** out, int* count);

/* Free the copy allocated by sm_registry_get_all() */
void sm_registry_free_copy(service_entry_t* copy);

int  sm_registry_count(void);

void sm_registry_cleanup(void);

#endif /* SM_REGISTRY_H */