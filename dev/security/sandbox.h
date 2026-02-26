#ifndef SANDBOX_H
#define SANDBOX_H

#include <sys/types.h>
#include <stdint.h>

// resource limits for cgroup integration
typedef struct {
    uint64_t memory_limit_mb;    // memory limit in megabytes (0 = no limit)
    uint32_t cpu_quota_percent;  // CPU quota as percentage (0-100, 0 = no limit)
    uint32_t cpu_weight;         // CPU weight (1-10000, default 100)
    uint32_t io_weight;          // I/O weight (1-10000, default 100)
    uint32_t pids_limit;         // maximum processes (0 = no limit)
} resource_limits_t;

// network configuration for selective access
typedef struct {
    int enable_loopback;         // allow 127.0.0.1 access
    int enable_internet;         // allow internet access
    const char** allowed_hosts;  // specific hosts to allow (NULL-terminated)
    uint16_t* allowed_ports;     // specific ports to allow (0-terminated)
} network_config_t;

// filesystem binding for selective host access
typedef struct {
    const char* host_path;       // path on host system
    const char* container_path;  // path inside container
    int read_only;               // mount as read-only
    int optional;                // don't fail if host_path doesn't exist
} filesystem_binding_t;

// enhanced sandbox configuration
typedef struct {
    int  enable_pid_ns;      // isolate process tree
    int  enable_net_ns;      // isolate network
    int  enable_mount_ns;    // isolate filesystem view
    int  enable_ipc_ns;      // isolate IPC (shared memory, semaphores)
    int  enable_user_ns;     // map uid/gid (run as fake root inside)
    int  enable_uts_ns;      // isolate hostname
    
    uid_t real_uid;          // actual uid to run as (outside namespace)
    gid_t real_gid;          // actual gid to run as
    
    const char* chroot_path; // restrict filesystem access to this path (NULL = skip)
    
    // resource control
    resource_limits_t limits;        // cgroup resource limits
    int enable_cgroups;              // enable cgroup integration
    const char* cgroup_name;         // cgroup name (auto-generated if NULL)
    
    // network control  
    network_config_t network;        // network isolation settings
    
    // filesystem access
    filesystem_binding_t* bindings;  // host filesystem bindings (NULL-terminated)
    size_t binding_count;            // number of bindings
    
    // persistence (experimental)
    int persistent_namespaces;       // keep namespaces alive after process dies
    const char* namespace_name;      // name for persistent namespaces
} sandbox_config_t;

// apply enhanced sandbox configuration
int sandbox_apply_config(const sandbox_config_t* cfg);

// apply namespace isolation - call BEFORE dropping privileges (legacy)
int sandbox_apply(const sandbox_config_t* cfg);

// helper: get default sandbox config for a service type
sandbox_config_t sandbox_default_config(void);

// get service-specific sandbox configuration
sandbox_config_t sandbox_get_service_config(const char* service_name);

// cgroup management functions
int sandbox_create_cgroup(const char* cgroup_name, const resource_limits_t* limits);
int sandbox_add_to_cgroup(const char* cgroup_name, pid_t pid);
int sandbox_destroy_cgroup(const char* cgroup_name);

// network management functions
int sandbox_setup_network_isolation(const network_config_t* net_config);
int sandbox_allow_network_host(const char* hostname);
int sandbox_allow_network_port(uint16_t port);

// filesystem binding functions
int sandbox_add_filesystem_binding(const filesystem_binding_t* binding);
int sandbox_remove_filesystem_binding(const char* container_path);

// namespace persistence functions (experimental)
int sandbox_create_persistent_namespace(const char* name, int ns_types);
int sandbox_join_persistent_namespace(const char* name);
int sandbox_destroy_persistent_namespace(const char* name);

#endif