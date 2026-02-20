#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <stdint.h>
#include <sys/types.h>
#include "service_manager/sm_protocol.h"

// ========== SERVICE MANAGER — PROFESSIONAL ARCHITECTURE ==========
//
// MODULAR DESIGN:
// ├── sm_protocol.{h,c}     Protocol definition, validation (strong typing)
// ├── sm_logging.{h,c}      Centralized logging (syslog + file rotation)
// ├── sm_security.{h,c}     Privilege drop, capabilities, seccomp, sandbox
// ├── sm_registry.{h,c}     Service registry with hash table (thread-safe)
// ├── sm_socket.{h,c}       Socket setup and management
// ├── sm_handlers.{h,c}     Message handlers with validation
// ├── sm_rate_limit.{h,c}   DoS protection (per-PID + global limits)
// ├── sm_health.{h,c}       Health checks, restart with exponential backoff
// └── sm_main.c             Main loop and client API

// ── CONSTANTS ──────────────────────────────────────────────────────────────────

#define SM_SOCKET_PATH      "/run/servicemanager.sock"   // FHS compliant
#define SM_MAX_SERVICES     32
#define SM_MAX_NAME         64
#define SM_MAX_PATH         256

// ── RESPONSE CODES ────────────────────────────────────────────────────────────

#define SM_OK               0
#define SM_ERR_NOT_FOUND   -1
#define SM_ERR_FULL        -2
#define SM_ERR_EXISTS      -3
#define SM_ERR_INVALID     -4
#define SM_ERR_PROTOCOL    -5
#define SM_ERR_PERMISSION  -6
#define SM_ERR_RATELIMIT   -7

// ── SERVER API ─────────────────────────────────────────────────────────────────

// Start service manager daemon (blocks forever)
int  sm_run(void);

// ── CLIENT API ─────────────────────────────────────────────────────────────────

// One-shot registration
int  sm_register(const char* name, const char* socket_path, const char* ring_name);

// Lookup service location
int  sm_lookup(const char* name, char* socket_path_out, char* ring_name_out);

// Send heartbeat
int  sm_heartbeat(const char* name);

// Unregister service
int  sm_unregister(const char* name);

// Persistent connection API (for multiple requests)
int  sm_connect_persistent(void);
void sm_disconnect(int fd);

#endif // SERVICE_MANAGER_H