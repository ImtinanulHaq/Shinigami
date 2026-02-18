#define _GNU_SOURCE
#include "capabilities.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>
#include <grp.h>            // for setgroups
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/capability.h>
#include <errno.h>

// capability auditing state
static int auditing_enabled = 0;
static int audit_fd = -1;
static char audit_buffer[512];

// service capability database
static const service_capabilities_t service_db[] = {
    {"audio", CAP_SYS_RAWIO, CAP_NONE, 1001, 1001},
    {"camera", CAP_SYS_RAWIO, CAP_NONE, 1002, 1002}, 
    {"sensor", CAP_NONE, CAP_SYS_RAWIO, 1003, 1003},
    {"network", CAP_NET_BIND | CAP_NET_RAW, CAP_SYS_ADMIN, 1004, 1004},
    {NULL, 0, 0, 0, 0} // terminator
};

// forward declarations
static int set_caps(uint32_t keep_bits);

// capability structures for kernel syscalls
struct cap_data {
    __u32 effective;
    __u32 permitted;
    __u32 inheritable;
};

struct cap_header {
    __u32 version;
    int   pid;
};

#define CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3

// Linux capability numbers
#define CAP_KILL_NUM         5
#define CAP_SETUID_NUM       7
#define CAP_NET_BIND_NUM    10
#define CAP_NET_RAW_NUM     13
#define CAP_SYS_RAWIO_NUM   17
#define CAP_SYS_ADMIN_NUM   21

// ══════════════════════════════════════════════════════════════════════════════
// CAPABILITY AUDITING - Log capability usage and changes
// ══════════════════════════════════════════════════════════════════════════════

int capabilities_enable_auditing(const char* log_path)
{
    if (auditing_enabled) return 0; // already enabled
    
    audit_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (audit_fd < 0) {
        perror("[cap] open audit log");
        return -1;
    }
    
    auditing_enabled = 1;
    printf("[cap] auditing enabled - capability changes logged to %s\n", log_path);
    return 0;
}

static void audit_capability_change(const char* action, cap_flags_t caps, const char* details)
{
    if (!auditing_enabled || audit_fd < 0) return;
    
    time_t now = time(NULL);
    snprintf(audit_buffer, sizeof(audit_buffer),
        "[%ld] CAP_%s: flags=0x%x pid=%d %s\n",
        now, action, caps, getpid(), details ? details : "");
    
    write(audit_fd, audit_buffer, strlen(audit_buffer));
}

// ══════════════════════════════════════════════════════════════════════════════ 
// CAPABILITY UTILITIES
// ══════════════════════════════════════════════════════════════════════════════

const char* capabilities_name(cap_flags_t cap)
{
    switch (cap) {
        case CAP_SYS_RAWIO: return "SYS_RAWIO";
        case CAP_NET_BIND:  return "NET_BIND_SERVICE"; 
        case CAP_SYS_ADMIN: return "SYS_ADMIN";
        case CAP_SETUID:    return "SETUID";
        case CAP_KILL:      return "KILL";
        case CAP_NET_RAW:   return "NET_RAW";
        case CAP_NONE:      return "NONE";
        default:            return "UNKNOWN";
    }
}

capabilities_config_t capabilities_get_service_config(const char* service_name)
{
    capabilities_config_t config = {0};
    
    // find service in database
    const service_capabilities_t* svc = NULL;
    for (int i = 0; service_db[i].service_name; i++) {
        if (strcmp(service_db[i].service_name, service_name) == 0) {
            svc = &service_db[i];
            break;
        }
    }
    
    if (svc) {
        config.effective = svc->required_caps;
        config.permitted = svc->required_caps | svc->optional_caps;
        config.inheritable = CAP_NONE;  // don't inherit by default
        config.bounding = config.permitted;  // can't exceed permitted
        config.target_uid = svc->recommended_uid;
        config.target_gid = svc->recommended_gid;
        config.enable_auditing = 1;
    } else {
        // default safe configuration for unknown services
        config.effective = CAP_NONE;
        config.permitted = CAP_NONE;
        config.inheritable = CAP_NONE;
        config.bounding = CAP_NONE;
        config.target_uid = 1000;  // unprivileged user
        config.target_gid = 1000;
        config.enable_auditing = 1;
    }
    
    return config;
}

// convert flags to capability bits
static __u32 flags_to_bits(cap_flags_t flags)
{
    __u32 bits = 0;
    if (flags & CAP_SYS_RAWIO) bits |= (1U << CAP_SYS_RAWIO_NUM);
    if (flags & CAP_NET_BIND)  bits |= (1U << CAP_NET_BIND_NUM);
    if (flags & CAP_SYS_ADMIN) bits |= (1U << CAP_SYS_ADMIN_NUM);
    if (flags & CAP_SETUID)    bits |= (1U << CAP_SETUID_NUM);
    if (flags & CAP_KILL)      bits |= (1U << CAP_KILL_NUM);
    if (flags & CAP_NET_RAW)   bits |= (1U << CAP_NET_RAW_NUM);
    return bits;
}

// set process capabilities via capset syscall with full control
static int set_caps_full(const capabilities_config_t* config)
{
    struct cap_header hdr;
    struct cap_data data[2];
    
    memset(&hdr, 0, sizeof(hdr));
    memset(data, 0, sizeof(data));
    
    hdr.version = CAPABILITY_VERSION;
    hdr.pid = 0;
    
    __u32 eff = flags_to_bits(config->effective);
    __u32 perm = flags_to_bits(config->permitted);
    __u32 inh = flags_to_bits(config->inheritable);
    
    data[0].effective   = eff;
    data[0].permitted   = perm;
    data[0].inheritable = inh;
    data[1].effective   = 0;
    data[1].permitted   = 0;
    data[1].inheritable = 0;
    
    if (syscall(SYS_capset, &hdr, data) < 0) {
        perror("[cap] capset");
        return -1;
    }
    
    audit_capability_change("SET", config->effective, "applied configuration");
    return 0;
}

// set capability bounding set
static int set_capability_bounding_set(cap_flags_t bounding)
{
    // drop capabilities not in bounding set
    for (int cap = 0; cap <= 63; cap++) {
        __u32 cap_bit = (1U << cap);
        __u32 bounding_bits = flags_to_bits(bounding);
        
        // if capability is not in our bounding set, drop it
        if (!(bounding_bits & cap_bit)) {
            if (prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) < 0) {
                // ignore errors for capabilities we don't have
                if (errno != EINVAL && errno != EPERM) {
                    perror("[cap] bounding set drop");
                }
            }
        }
    }
    
    audit_capability_change("BOUND", bounding, "bounding set applied");
    return 0;
}

int capabilities_check(cap_flags_t cap)
{
    struct cap_header hdr;
    struct cap_data data[2];
    
    memset(&hdr, 0, sizeof(hdr));
    hdr.version = CAPABILITY_VERSION;
    hdr.pid = 0;
    
    if (syscall(SYS_capget, &hdr, data) < 0) {
        return 0; // assume no capability on error
    }
    
    __u32 effective = data[0].effective;
    __u32 check_bits = flags_to_bits(cap);
    
    return (effective & check_bits) == check_bits;
}

// ══════════════════════════════════════════════════════════════════════════════
// ENHANCED CAPABILITY APPLICATION
// ══════════════════════════════════════════════════════════════════════════════

int capabilities_apply_config(const capabilities_config_t* config)
{
    if (!config) return -1;
    
    audit_capability_change("START", CAP_NONE, "beginning capability configuration");
    
    // step 1: set capability bounding set first (most restrictive)
    if (set_capability_bounding_set(config->bounding) < 0) {
        fprintf(stderr, "[cap] bounding set configuration failed\n");
        return -1;
    }
    
    // step 2: lock privilege escalation
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("[cap] PR_SET_NO_NEW_PRIVS");
        return -1;
    }
    audit_capability_change("LOCKED", CAP_NONE, "privilege escalation disabled");
    
    // step 3: drop to target user/group if specified  
    if (config->target_uid > 0 && config->target_gid > 0) {
        if (getuid() == 0) {
            setgroups(0, NULL);
            if (setgid(config->target_gid) < 0 || setuid(config->target_uid) < 0) {
                perror("[cap] user/group change");
                return -1;
            }
            
            // verify cannot regain root
            if (setuid(0) == 0) {
                fprintf(stderr, "[cap] ERROR: regained root privileges\n");
                return -1;
            }
            
            audit_capability_change("USER", CAP_NONE, 
                                  audit_buffer + snprintf(audit_buffer, sizeof(audit_buffer),
                                                          "changed to uid=%u gid=%u", 
                                                          config->target_uid, config->target_gid));
        }
    }
    
    // step 4: set final capabilities
    if (set_caps_full(config) < 0) return -1;
    
    // step 5: verify configuration
    printf("[cap] configuration applied successfully:\n");
    printf("[cap]   effective: %s (0x%x)\n", 
           capabilities_name(config->effective), config->effective);
    printf("[cap]   permitted: %s (0x%x)\n",
           capabilities_name(config->permitted), config->permitted);
    printf("[cap]   bounding: %s (0x%x)\n",
           capabilities_name(config->bounding), config->bounding);
    if (config->target_uid > 0) {
        printf("[cap]   user: %u:%u\n", config->target_uid, config->target_gid);
    }
    
    audit_capability_change("COMPLETE", config->effective, "configuration successful");
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// LEGACY FUNCTIONS - For backward compatibility
// ══════════════════════════════════════════════════════════════════════════════

// set process capabilities via capset syscall (legacy version)
static int set_caps(uint32_t keep_bits)
{
    struct cap_header hdr;
    struct cap_data data[2];
    
    memset(&hdr, 0, sizeof(hdr));
    memset(data, 0, sizeof(data));
    
    hdr.version = CAPABILITY_VERSION;
    hdr.pid = 0;
    
    data[0].permitted   = keep_bits;
    data[0].effective   = keep_bits;
    data[0].inheritable = 0;
    data[1].permitted   = 0;
    data[1].effective   = 0;
    data[1].inheritable = 0;
    
    if (syscall(SYS_capset, &hdr, data) < 0) {
        perror("[cap] capset");
        return -1;
    }
    return 0;
}

int capabilities_drop_except(cap_flags_t keep_flags)
{
    // step 1: lock privilege escalation - critical security
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("[cap] PR_SET_NO_NEW_PRIVS");
        return -1;
    }
    printf("[cap] NO_NEW_PRIVS set - escalation locked\n");
    
    // step 2: drop to unprivileged user if root
    if (getuid() == 0) {
        uid_t uid = 1000, gid = 1000;
        
        setgroups(0, NULL);
        if (setgid(gid) < 0 || setuid(uid) < 0) {
            perror("[cap] drop root");
            return -1;
        }
        
        // verify cannot regain root
        if (setuid(0) == 0) {
            fprintf(stderr, "[cap] ERROR: regained root\n");
            return -1;
        }
        printf("[cap] dropped root → uid=%u\n", uid);
    }
    
    // step 3: set capabilities
    __u32 bits = flags_to_bits(keep_flags);
    if (set_caps(bits) < 0) return -1;
    
    printf("[cap] %s\n", keep_flags ? "kept specific caps" : "all caps dropped");
    return 0;
}

int capabilities_drop_all(void)
{
    return capabilities_drop_except(CAP_NONE);
}