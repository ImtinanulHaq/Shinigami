#ifndef SANDBOX_H
#define SANDBOX_H

#include <sys/types.h>

// sandbox configuration for a service
typedef struct {
    int  enable_pid_ns;      // isolate process tree
    int  enable_net_ns;      // isolate network
    int  enable_mount_ns;    // isolate filesystem view
    int  enable_ipc_ns;      // isolate IPC (shared memory, semaphores)
    int  enable_user_ns;     // map uid/gid (run as fake root inside)
    
    uid_t real_uid;          // actual uid to run as (outside namespace)
    gid_t real_gid;          // actual gid to run as
    
    const char* chroot_path; // restrict filesystem access to this path (NULL = skip)
} sandbox_config_t;

// apply namespace isolation - call BEFORE dropping privileges
// returns 0 on success, -1 on failure
int sandbox_apply(const sandbox_config_t* cfg);

// helper: get default sandbox config for a service type
sandbox_config_t sandbox_default_config(void);

#endif