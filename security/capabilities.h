#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <sys/types.h>

// well-known Linux capabilities that services might need
typedef enum {
    CAP_NONE           = 0,
    CAP_SYS_RAWIO      = 1 << 0,  // direct hardware I/O (audio, camera)
    CAP_NET_BIND       = 1 << 1,  // bind to ports < 1024
    CAP_SYS_ADMIN      = 1 << 2,  // various admin operations (use sparingly)
    CAP_SETUID         = 1 << 3,  // change uid/gid
    CAP_KILL           = 1 << 4,  // send signals to other processes
    CAP_NET_RAW        = 1 << 5,  // raw network sockets
} cap_flags_t;

// capability configuration for fine-grained control
typedef struct {
    cap_flags_t effective;      // capabilities currently in effect
    cap_flags_t permitted;      // capabilities that can be enabled
    cap_flags_t inheritable;    // capabilities inherited by child processes
    cap_flags_t bounding;       // maximum capabilities (cannot be exceeded)
    int enable_auditing;        // log capability usage
    uid_t target_uid;           // user to drop to (0 = don't change)
    gid_t target_gid;           // group to drop to (0 = don't change)
} capabilities_config_t;

// service-specific capability requirements
typedef struct {
    const char* service_name;
    cap_flags_t required_caps;  // minimum capabilities needed
    cap_flags_t optional_caps;  // nice-to-have capabilities
    uid_t recommended_uid;      // recommended user id
    gid_t recommended_gid;      // recommended group id
} service_capabilities_t;

// apply capability configuration (replaces drop_except function)
int capabilities_apply_config(const capabilities_config_t* config);

// drop ALL capabilities except those in keep_flags (legacy interface)
int capabilities_drop_except(cap_flags_t keep_flags);

// drop all capabilities completely - service runs with zero privileges
int capabilities_drop_all(void);

// get recommended configuration for a service type
capabilities_config_t capabilities_get_service_config(const char* service_name);

// enable capability auditing (must be called before applying capabilities)
int capabilities_enable_auditing(const char* log_path);

// check if process currently has specific capability
int capabilities_check(cap_flags_t cap);

// utility: get human-readable capability name
const char* capabilities_name(cap_flags_t cap);

#endif