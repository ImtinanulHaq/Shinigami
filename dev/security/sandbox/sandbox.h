#ifndef SANDBOX_H
#define SANDBOX_H

#include <sys/types.h>
#include <stdint.h>

typedef struct {
    uint64_t memory_limit_mb;
    uint32_t cpu_quota_percent;
    uint32_t cpu_weight;
    uint32_t io_weight;
    uint32_t pids_limit;
} resource_limits_t;

typedef struct {
    int enable_loopback;
    int enable_internet;
    const char** allowed_hosts;
    uint16_t* allowed_ports;
} network_config_t;

typedef struct {
    const char* host_path;
    const char* container_path;
    int read_only;
    int optional;
} filesystem_binding_t;

typedef struct {
    int enable_pid_ns;
    int enable_net_ns;
    int enable_mount_ns;
    int enable_ipc_ns;
    int enable_user_ns;
    int enable_uts_ns;

    uid_t real_uid;
    gid_t real_gid;

    const char* chroot_path;

    resource_limits_t limits;
    int enable_cgroups;
    const char* cgroup_name;

    network_config_t network;

    filesystem_binding_t* bindings;
    size_t binding_count;

    int persistent_namespaces;
    const char* namespace_name;
} sandbox_config_t;

int sandbox_apply_config(const sandbox_config_t* cfg);
int sandbox_apply(const sandbox_config_t* cfg);
sandbox_config_t sandbox_default_config(void);
sandbox_config_t sandbox_get_service_config(const char* service_name);

int sandbox_create_cgroup(const char* cgroup_name, const resource_limits_t* limits);
int sandbox_add_to_cgroup(const char* cgroup_name, pid_t pid);
int sandbox_destroy_cgroup(const char* cgroup_name);

int sandbox_setup_network_isolation(const network_config_t* net_config);
int sandbox_allow_network_host(const char* hostname);
int sandbox_allow_network_port(uint16_t port);

int sandbox_add_filesystem_binding(const filesystem_binding_t* binding);
int sandbox_remove_filesystem_binding(const char* container_path);

int sandbox_create_persistent_namespace(const char* name, int ns_types);
int sandbox_join_persistent_namespace(const char* name);
int sandbox_destroy_persistent_namespace(const char* name);

#endif
