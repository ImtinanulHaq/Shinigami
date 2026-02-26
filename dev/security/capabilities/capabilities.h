#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <sys/types.h>

typedef enum {
    MCAP_NONE           = 0,
    MCAP_SYS_RAWIO      = 1 << 0,
    MCAP_NET_BIND       = 1 << 1,
    MCAP_SYS_ADMIN      = 1 << 2,
    MCAP_SETUID         = 1 << 3,
    MCAP_KILL           = 1 << 4,
    MCAP_NET_RAW        = 1 << 5,
} cap_flags_t;

typedef struct {
    cap_flags_t effective;
    cap_flags_t permitted;
    cap_flags_t inheritable;
    cap_flags_t bounding;
    int enable_auditing;
    uid_t target_uid;
    gid_t target_gid;
} capabilities_config_t;

typedef struct {
    const char* service_name;
    cap_flags_t required_caps;
    cap_flags_t optional_caps;
    uid_t recommended_uid;
    gid_t recommended_gid;
} service_capabilities_t;

int capabilities_apply_config(const capabilities_config_t* config);

capabilities_config_t capabilities_get_service_config(const char* service_name);

int capabilities_enable_auditing(const char* log_path);

int capabilities_check(cap_flags_t cap);

const char* capabilities_name(cap_flags_t cap);

int capabilities_drop_except(cap_flags_t keep_flags)
    __attribute__((deprecated("Use capabilities_apply_config instead")));

int capabilities_drop_all(void)
    __attribute__((deprecated("Use capabilities_apply_config instead")));

#endif
