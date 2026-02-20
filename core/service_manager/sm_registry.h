#ifndef SM_REGISTRY_H
#define SM_REGISTRY_H

#include <time.h>
#include <sys/types.h>
#include "sm_protocol.h"

// ── SERVICE STATUS ────────────────────────────────────────────────────────────

typedef enum {
    SERVICE_RUNNING  = 0,
    SERVICE_STOPPED  = 1,
    SERVICE_CRASHED  = 2,
} service_status_t;

// ── SERVICE ENTRY ─────────────────────────────────────────────────────────────

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
    int              restart_count;         // Number of times restarted
    time_t           last_crash_time;       // For exponential backoff
} service_entry_t;

// ── FUNCTIONS ──────────────────────────────────────────────────────────────────

// Initialize registry
int  sm_registry_init(void);

// Add service to registry
int  sm_registry_add(const service_entry_t* entry);

// Find service by name
service_entry_t* sm_registry_find(const char* name);

// Update service status
int  sm_registry_update_status(const char* name, service_status_t status);

// Update heartbeat timestamp
int  sm_registry_update_heartbeat(const char* name);

// Remove service from registry
int  sm_registry_remove(const char* name);

// Get all services
int  sm_registry_get_all(service_entry_t** out, int* count);

// Get service count
int  sm_registry_count(void);

// Cleanup
void sm_registry_cleanup(void);

#endif // SM_REGISTRY_H
