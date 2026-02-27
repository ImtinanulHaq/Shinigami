#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sandbox.h"
#include <unistd.h>
#include "sandbox_core.h"
#include "sandbox_cgroup.h"
#include "sandbox_network.h"
#include "sandbox_mount.h"

int sandbox_apply_config(const sandbox_config_t* cfg)
{
    if (sandbox_core_validate_config(cfg) < 0) return -1;

    if (cfg->enable_cgroups && cfg->cgroup_name) {
        if (sandbox_cgroup_create(cfg->cgroup_name, &cfg->limits) < 0) return -1;
        if (sandbox_cgroup_add_process(cfg->cgroup_name, getpid()) < 0) {
            sandbox_cgroup_destroy(cfg->cgroup_name);
            return -1;
        }
    }

    if (sandbox_core_apply_namespaces(cfg) < 0) {
        if (cfg->enable_cgroups && cfg->cgroup_name)
            sandbox_cgroup_destroy(cfg->cgroup_name);
        return -1;
    }

    if (cfg->bindings && cfg->binding_count > 0) {
        for (size_t i = 0; i < cfg->binding_count; i++)
            sandbox_mount_add_binding(&cfg->bindings[i]);
    }

    if (cfg->enable_net_ns && cfg->network.enable_internet)
        sandbox_network_isolate(&cfg->network);

    return 0;
}

int sandbox_apply(const sandbox_config_t* cfg)
{
    return sandbox_core_apply_namespaces(cfg);
}

sandbox_config_t sandbox_default_config(void)
{
    return sandbox_core_default_config();
}

sandbox_config_t sandbox_get_service_config(const char* service_name)
{
    return sandbox_core_get_service_config(service_name);
}

int sandbox_create_cgroup(const char* cgroup_name, const resource_limits_t* limits)
{
    return sandbox_cgroup_create(cgroup_name, limits);
}

int sandbox_add_to_cgroup(const char* cgroup_name, pid_t pid)
{
    return sandbox_cgroup_add_process(cgroup_name, pid);
}

int sandbox_destroy_cgroup(const char* cgroup_name)
{
    return sandbox_cgroup_destroy(cgroup_name);
}

int sandbox_setup_network_isolation(const network_config_t* net_config)
{
    return sandbox_network_isolate(net_config);
}

int sandbox_allow_network_host(const char* hostname)
{
    return sandbox_network_allow_host(hostname);
}

int sandbox_allow_network_port(uint16_t port)
{
    return sandbox_network_allow_port(port);
}

int sandbox_add_filesystem_binding(const filesystem_binding_t* binding)
{
    return sandbox_mount_add_binding(binding);
}

int sandbox_remove_filesystem_binding(const char* container_path)
{
    return sandbox_mount_remove_binding(container_path);
}

int sandbox_create_persistent_namespace(const char* name, int ns_types)
{
    return sandbox_core_create_persistent_namespace(name, ns_types);
}

int sandbox_join_persistent_namespace(const char* name)
{
    return sandbox_core_join_persistent_namespace(name);
}

int sandbox_destroy_persistent_namespace(const char* name)
{
    return sandbox_core_destroy_persistent_namespace(name);
}
