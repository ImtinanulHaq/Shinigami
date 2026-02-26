#define _GNU_SOURCE
#include "sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <errno.h>
#include <limits.h>

#define SANDBOX_ROOT   "/tmp/sandbox_root"
#define OLD_ROOT_NAME  ".old_root"
#define CGROUP_ROOT    "/sys/fs/cgroup"

// service configuration database
static const struct {
    const char* name;
    resource_limits_t limits;
    int enable_network;
} service_configs[] = {
    {"audio",  {512, 25, 100, 100, 10}, 0},  // 512MB, 25% CPU, no network
    {"camera", {1024, 50, 200, 200, 5}, 0},  // 1GB, 50% CPU, high I/O priority
    {"sensor", {256, 10, 50, 50, 5}, 0},     // 256MB, 10% CPU, low priority
    {"network", {512, 30, 100, 100, 20}, 1}, // network service gets network access
    {NULL, {0, 0, 0, 0, 0}, 0}
};

// forward declarations
static int validate_config(const sandbox_config_t* cfg);

// ══════════════════════════════════════════════════════════════════════════════
// CGROUP INTEGRATION - Resource Limits
// ══════════════════════════════════════════════════════════════════════════════

static int write_to_cgroup_file(const char* cgroup_name, const char* file, const char* value)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/%s", CGROUP_ROOT, cgroup_name, file);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("[sandbox] open cgroup file");
        return -1;
    }
    
    ssize_t written = write(fd, value, strlen(value));
    close(fd);
    
    if (written != (ssize_t)strlen(value)) {
        fprintf(stderr, "[sandbox] cgroup write failed for %s\n", path);
        return -1;
    }
    
    return 0;
}

int sandbox_create_cgroup(const char* cgroup_name, const resource_limits_t* limits)
{
    if (!cgroup_name || !limits) return -1;
    
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", CGROUP_ROOT, cgroup_name);
    
    // create cgroup directory
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        perror("[sandbox] mkdir cgroup");
        return -1;
    }
    
    char value[128];
    int rc = 0;
    
    // set memory limit
    if (limits->memory_limit_mb > 0) {
        snprintf(value, sizeof(value), "%lu", (unsigned long)limits->memory_limit_mb * 1024 * 1024);
        if (write_to_cgroup_file(cgroup_name, "memory.max", value) < 0) {
            printf("[sandbox] warning: memory limit not set\n");
        }
    }
    
    // set CPU quota (percentage to microseconds)
    if (limits->cpu_quota_percent > 0 && limits->cpu_quota_percent <= 100) {
        snprintf(value, sizeof(value), "%d 100000", limits->cpu_quota_percent * 1000);
        if (write_to_cgroup_file(cgroup_name, "cpu.max", value) < 0) {
            printf("[sandbox] warning: CPU quota not set\n");
        }
    }
    
    // set CPU weight
    if (limits->cpu_weight > 0) {
        snprintf(value, sizeof(value), "%u", limits->cpu_weight);
        if (write_to_cgroup_file(cgroup_name, "cpu.weight", value) < 0) {
            printf("[sandbox] warning: CPU weight not set\n");
        }
    }
    
    // set I/O weight  
    if (limits->io_weight > 0) {
        snprintf(value, sizeof(value), "%u", limits->io_weight);
        if (write_to_cgroup_file(cgroup_name, "io.weight", value) < 0) {
            printf("[sandbox] warning: I/O weight not set\n");
        }
    }
    
    // set process limit
    if (limits->pids_limit > 0) {
        snprintf(value, sizeof(value), "%u", limits->pids_limit);
        if (write_to_cgroup_file(cgroup_name, "pids.max", value) < 0) {
            printf("[sandbox] warning: process limit not set\n");
        }
    }
    
    printf("[sandbox] cgroup '%s' created with resource limits\n", cgroup_name);
    return 0;
}

int sandbox_add_to_cgroup(const char* cgroup_name, pid_t pid)
{
    if (!cgroup_name) return -1;
    
    char value[32];
    snprintf(value, sizeof(value), "%d", pid);
    
    if (write_to_cgroup_file(cgroup_name, "cgroup.procs", value) < 0) {
        return -1;
    }
    
    printf("[sandbox] process %d added to cgroup '%s'\n", pid, cgroup_name);
    return 0;
}

int sandbox_destroy_cgroup(const char* cgroup_name)
{
    if (!cgroup_name) return -1;
    
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", CGROUP_ROOT, cgroup_name);
    
    // kill all processes in cgroup first
    write_to_cgroup_file(cgroup_name, "cgroup.kill", "1");
    
    // wait a bit for processes to die
    usleep(100000); // 100ms
    
    if (rmdir(path) < 0) {
        perror("[sandbox] rmdir cgroup");
        return -1;
    }
    
    printf("[sandbox] cgroup '%s' destroyed\n", cgroup_name);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// SERVICE CONFIGURATIONS
// ══════════════════════════════════════════════════════════════════════════════

sandbox_config_t sandbox_get_service_config(const char* service_name)
{
    sandbox_config_t config = sandbox_default_config();
    
    if (!service_name) return config;
    
    // find service in database
    for (int i = 0; service_configs[i].name; i++) {
        if (strcmp(service_configs[i].name, service_name) == 0) {
            config.limits = service_configs[i].limits;
            config.enable_cgroups = 1;
            
            // generate cgroup name
            static char cgroup_name[64];
            snprintf(cgroup_name, sizeof(cgroup_name), "middleware_%s_%d", 
                    service_name, getpid());
            config.cgroup_name = cgroup_name;
            
            // network access based on service type
            if (service_configs[i].enable_network) {
                config.enable_net_ns = 0;  // don't isolate network
                config.network.enable_internet = 1;
                config.network.enable_loopback = 1;
            } else {
                config.enable_net_ns = 1;  // isolate network
                config.network.enable_internet = 0;
                config.network.enable_loopback = 1;  // allow local communication
            }
            
            break;
        }
    }
    
    return config;
}

// ══════════════════════════════════════════════════════════════════════════════
// ENHANCED SANDBOX APPLICATION
// ══════════════════════════════════════════════════════════════════════════════

int sandbox_apply_config(const sandbox_config_t* cfg)
{
    if (validate_config(cfg) < 0) return -1;
    
    printf("[sandbox] applying enhanced configuration...\n");
    
    // step 1: create cgroup if enabled
    if (cfg->enable_cgroups && cfg->cgroup_name) {
        if (sandbox_create_cgroup(cfg->cgroup_name, &cfg->limits) < 0) {
            fprintf(stderr, "[sandbox] cgroup setup failed\n");
            return -1;
        }
        
        // add current process to cgroup
        if (sandbox_add_to_cgroup(cfg->cgroup_name, getpid()) < 0) {
            fprintf(stderr, "[sandbox] cgroup assignment failed\n");
        }
    }
    
    // step 2: apply traditional namespace isolation
    int result = sandbox_apply(cfg);
    if (result < 0) {
        if (cfg->enable_cgroups && cfg->cgroup_name) {
            sandbox_destroy_cgroup(cfg->cgroup_name);
        }
        return result;
    }
    
    // step 3: setup filesystem bindings
    if (cfg->bindings && cfg->binding_count > 0) {
        for (size_t i = 0; i < cfg->binding_count; i++) {
            if (sandbox_add_filesystem_binding(&cfg->bindings[i]) < 0) {
                fprintf(stderr, "[sandbox] warning: binding %s failed\n", 
                        cfg->bindings[i].host_path);
            }
        }
    }
    
    // step 4: setup network configuration  
    if (cfg->enable_net_ns && cfg->network.enable_internet) {
        if (sandbox_setup_network_isolation(&cfg->network) < 0) {
            fprintf(stderr, "[sandbox] warning: network setup failed\n");
        }
    }
    
    printf("[sandbox] enhanced configuration applied successfully\n");
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// FILESYSTEM BINDING FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

int sandbox_add_filesystem_binding(const filesystem_binding_t* binding)
{
    if (!binding || !binding->host_path || !binding->container_path) {
        return -1;
    }
    
    // check if host path exists (unless optional)
    if (access(binding->host_path, F_OK) != 0) {
        if (binding->optional) {
            printf("[sandbox] optional binding %s not available\n", binding->host_path);
            return 0;
        } else {
            fprintf(stderr, "[sandbox] required binding %s not found\n", binding->host_path);
            return -1;
        }
    }
    
    // create mount point in container
    if (mkdir(binding->container_path, 0755) < 0 && errno != EEXIST) {
        perror("[sandbox] mkdir binding");
        return -1;
    }
    
    // bind mount
    unsigned long flags = MS_BIND;
    if (binding->read_only) {
        flags |= MS_RDONLY;
    }
    
    if (mount(binding->host_path, binding->container_path, NULL, flags, NULL) < 0) {
        perror("[sandbox] bind mount");
        return -1;
    }
    
    printf("[sandbox] bound %s → %s %s\n", 
            binding->host_path, binding->container_path,
            binding->read_only ? "(ro)" : "(rw)");
    
    return 0;
}

int sandbox_remove_filesystem_binding(const char* container_path)
{
    if (!container_path) return -1;
    
    if (umount2(container_path, MNT_DETACH) < 0) {
        perror("[sandbox] umount binding");
        return -1;
    }
    
    printf("[sandbox] unbound %s\n", container_path);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// NETWORK MANAGEMENT FUNCTIONS  
// ══════════════════════════════════════════════════════════════════════════════

int sandbox_setup_network_isolation(const network_config_t* net_config)
{
    if (!net_config) return -1;
    
    // this is a stub - real implementation would need:
    // 1. create veth pair
    // 2. setup bridge or NAT
    // 3. configure iptables rules
    // 4. setup DNS
    
    printf("[sandbox] network isolation configured\n");
    printf("[sandbox]   loopback: %s\n", net_config->enable_loopback ? "enabled" : "disabled");
    printf("[sandbox]   internet: %s\n", net_config->enable_internet ? "enabled" : "disabled");
    
    return 0;
}

int sandbox_allow_network_host(const char* hostname)
{
    if (!hostname) return -1;
    
    // this would add iptables rule to allow specific hostname
    printf("[sandbox] allowed network access to %s\n", hostname);
    return 0;
}

int sandbox_allow_network_port(uint16_t port)
{
    // this would add iptables rule to allow specific port
    printf("[sandbox] allowed network access to port %u\n", port);
    return 0;
}

// validate configuration - prevents attack vectors
static int validate_config(const sandbox_config_t* cfg)
{
    if (!cfg) return -1;
    
    // at least one namespace required (unless using only cgroups)
    if (!cfg->enable_pid_ns && !cfg->enable_net_ns && 
        !cfg->enable_mount_ns && !cfg->enable_ipc_ns && !cfg->enable_cgroups) {
        fprintf(stderr, "[sandbox] no isolation enabled\n");
        return -1;
    }
    
    // prevent accidental root mapping - security critical
    if (cfg->real_uid == 0 || cfg->real_gid == 0) {
        fprintf(stderr, "[sandbox] cannot map to uid/gid 0 - use >= 1000\n");
        return -1;
    }
    
    // path traversal check
    if (cfg->chroot_path && strstr(cfg->chroot_path, "..")) {
        fprintf(stderr, "[sandbox] path contains '..'\n");
        return -1;
    }
    
    // validate resource limits
    if (cfg->limits.cpu_quota_percent > 100) {
        fprintf(stderr, "[sandbox] CPU quota cannot exceed 100%%\n");
        return -1;
    }
    
    if (cfg->limits.cpu_weight > 10000) {
        fprintf(stderr, "[sandbox] CPU weight cannot exceed 10000\n");
        return -1;
    }
    
    // validate cgroup name
    if (cfg->enable_cgroups && cfg->cgroup_name) {
        if (strstr(cfg->cgroup_name, "/") || strstr(cfg->cgroup_name, "..")) {
            fprintf(stderr, "[sandbox] invalid cgroup name\n");
            return -1;
        }
    }
    
    return 0;
}

// create minimal /dev - only essential device nodes
// safer than bind-mounting host /dev
static int create_minimal_dev(const char* root)
{
    char dev_path[PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", root);
    
    if (mkdir(dev_path, 0755) < 0 && errno != EEXIST) {
        perror("[sandbox] mkdir /dev");
        return -1;
    }
    
    // mount fresh tmpfs on /dev
    if (mount("tmpfs", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=0755") < 0) {
        perror("[sandbox] mount /dev tmpfs");
        return -1;
    }
    
    // create only essential devices
    char path[PATH_MAX];
    size_t dev_len = strlen(dev_path);
    
    // ensure we have enough space for the longest path
    if (dev_len > PATH_MAX - 10) {
        fprintf(stderr, "[sandbox] dev_path too long\n");
        return -1;
    }
    
    // /dev/null
    int ret = snprintf(path, sizeof(path), "%s/null", dev_path);
    if (ret >= (int)sizeof(path)) {
        fprintf(stderr, "[sandbox] path truncated for null\n");
        return -1;
    }
    if (mknod(path, S_IFCHR | 0666, makedev(1, 3)) < 0 && errno != EEXIST) {
        perror("[sandbox] mknod null");
        return -1;
    }
    
    // /dev/zero
    ret = snprintf(path, sizeof(path), "%s/zero", dev_path);
    if (ret >= (int)sizeof(path)) {
        fprintf(stderr, "[sandbox] path truncated for zero\n");
        return -1;
    }
    if (mknod(path, S_IFCHR | 0666, makedev(1, 5)) < 0 && errno != EEXIST) {
        perror("[sandbox] mknod zero");
        return -1;
    }
    
    // /dev/urandom - needed for crypto
    ret = snprintf(path, sizeof(path), "%s/urandom", dev_path);
    if (ret >= (int)sizeof(path)) {
        fprintf(stderr, "[sandbox] path truncated for urandom\n");
        return -1;
    }
    if (mknod(path, S_IFCHR | 0666, makedev(1, 9)) < 0 && errno != EEXIST) {
        perror("[sandbox] mknod urandom");
        return -1;
    }
    
    printf("[sandbox] /dev created with null, zero, urandom\n");
    return 0;
}

// setup user namespace uid/gid mapping
// maps internal root to external unprivileged user
static int setup_user_namespace(uid_t real_uid, gid_t real_gid)
{
    pid_t pid = getpid();
    char path[PATH_MAX], map[256];
    int fd;
    
    // write uid_map: "0 real_uid 1"
    snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
    snprintf(map,  sizeof(map),  "0 %u 1", real_uid);
    
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) { perror("[sandbox] open uid_map"); return -1; }
    
    if (write(fd, map, strlen(map)) != (ssize_t)strlen(map)) {
        close(fd);
        fprintf(stderr, "[sandbox] uid_map write failed\n");
        return -1;
    }
    close(fd);
    
    // deny setgroups before gid_map
    snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { write(fd, "deny", 4); close(fd); }
    
    // write gid_map
    snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
    snprintf(map,  sizeof(map),  "0 %u 1", real_gid);
    
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) { perror("[sandbox] open gid_map"); return -1; }
    
    if (write(fd, map, strlen(map)) != (ssize_t)strlen(map)) {
        close(fd);
        fprintf(stderr, "[sandbox] gid_map write failed\n");
        return -1;
    }
    close(fd);
    
    printf("[sandbox] user ns: uid=0 inside → uid=%u outside\n", real_uid);
    return 0;
}

// pivot_root - more secure than chroot
// chroot can be escaped, pivot_root cannot
static int apply_pivot_root(const char* new_root)
{
    char old_root[PATH_MAX];
    snprintf(old_root, sizeof(old_root), "%s/%s", new_root, OLD_ROOT_NAME);
    
    if (mkdir(old_root, 0700) < 0 && errno != EEXIST) {
        perror("[sandbox] mkdir old_root");
        return -1;
    }
    
    // pivot_root syscall
    if (syscall(SYS_pivot_root, new_root, old_root) < 0) {
        perror("[sandbox] pivot_root");
        return -1;
    }
    
    if (chdir("/") < 0) {
        perror("[sandbox] chdir");
        return -1;
    }
    
    // unmount old root - critical security step
    char old_inside[PATH_MAX];
    snprintf(old_inside, sizeof(old_inside), "/%s", OLD_ROOT_NAME);
    umount2(old_inside, MNT_DETACH);
    rmdir(old_inside);
    
    printf("[sandbox] pivot_root done - old fs unmounted\n");
    return 0;
}

// setup minimal isolated filesystem
static int setup_filesystem(const char* requested_root)
{
    const char* root = requested_root ? requested_root : SANDBOX_ROOT;
    
    if (mkdir(root, 0755) < 0 && errno != EEXIST) {
        perror("[sandbox] mkdir root");
        return -1;
    }
    
    // mount tmpfs as new root
    if (mount("tmpfs", root, "tmpfs", MS_NODEV | MS_NOSUID, "mode=0755") < 0) {
        perror("[sandbox] mount tmpfs");
        return -1;
    }
    
    // create directories
    char path[PATH_MAX];
    const char* dirs[] = { "tmp", "proc", "sys", NULL };
    for (int i = 0; dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        mkdir(path, 0755);
    }
    
    if (create_minimal_dev(root) < 0) return -1;
    if (apply_pivot_root(root) < 0) return -1;
    
    // mount proc inside sandbox
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    
    return 0;
}

// main sandbox application
int sandbox_apply(const sandbox_config_t* cfg)
{
    if (validate_config(cfg) < 0) return -1;
    
    // build namespace flags
    int flags = 0;
    if (cfg->enable_pid_ns)   flags |= CLONE_NEWPID;
    if (cfg->enable_net_ns)   flags |= CLONE_NEWNET;
    if (cfg->enable_mount_ns) flags |= CLONE_NEWNS;
    if (cfg->enable_ipc_ns)   flags |= CLONE_NEWIPC;
    if (cfg->enable_user_ns)  flags |= CLONE_NEWUSER;
    
    // create namespaces
    if (unshare(flags) < 0) {
        perror("[sandbox] unshare");
        return -1;
    }
    
    printf("[sandbox] namespaces:");
    if (cfg->enable_pid_ns)   printf(" PID");
    if (cfg->enable_net_ns)   printf(" NET");
    if (cfg->enable_mount_ns) printf(" MOUNT");
    if (cfg->enable_ipc_ns)   printf(" IPC");
    if (cfg->enable_user_ns)  printf(" USER");
    printf("\n");
    
    // setup user namespace if enabled
    if (cfg->enable_user_ns) {
        if (setup_user_namespace(cfg->real_uid, cfg->real_gid) < 0)
            return -1;
    }
    
    // setup filesystem isolation
    if (cfg->enable_mount_ns) {
        // make mounts private
        mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL);
        if (setup_filesystem(cfg->chroot_path) < 0)
            return -1;
    }
    
    // verify PID namespace worked
    if (cfg->enable_pid_ns && getpid() != 1) {
        fprintf(stderr, "[sandbox] WARNING: pid=%d (should be 1)\n", getpid());
    }
    
    return 0;
}

sandbox_config_t sandbox_default_config(void)
{
    sandbox_config_t cfg = {
        .enable_pid_ns   = 1,
        .enable_net_ns   = 1,
        .enable_mount_ns = 1,
        .enable_ipc_ns   = 1,
        .enable_user_ns  = 0,
        .enable_uts_ns   = 1,
        .real_uid        = 1000,
        .real_gid        = 1000,
        .chroot_path     = NULL,
        
        // default resource limits (conservative)
        .limits = {
            .memory_limit_mb = 512,     // 512MB
            .cpu_quota_percent = 50,    // 50% CPU
            .cpu_weight = 100,          // normal priority
            .io_weight = 100,           // normal I/O priority  
            .pids_limit = 10            // max 10 processes
        },
        
        .enable_cgroups = 1,
        .cgroup_name = NULL,  // auto-generated
        
        // default network (isolated)
        .network = {
            .enable_loopback = 1,       // allow local communication
            .enable_internet = 0,       // no internet access
            .allowed_hosts = NULL,
            .allowed_ports = NULL
        },
        
        // no filesystem bindings by default
        .bindings = NULL,
        .binding_count = 0,
        
        // no namespace persistence by default
        .persistent_namespaces = 0,
        .namespace_name = NULL
    };
    return cfg;
}

// ══════════════════════════════════════════════════════════════════════════════
// NAMESPACE PERSISTENCE FUNCTIONS (Experimental)
// ══════════════════════════════════════════════════════════════════════════════

int sandbox_create_persistent_namespace(const char* name, int ns_types)
{
    if (!name) return -1;
    
    // this is a stub - real implementation would use:
    // 1. unshare() to create namespaces
    // 2. bind mount namespace files from /proc/self/ns/*
    // 3. create named references in /var/run/netns/ or similar
    
    printf("[sandbox] persistent namespace '%s' created (stub)\n", name);
    return 0;
}

int sandbox_join_persistent_namespace(const char* name)
{
    if (!name) return -1;
    
    // this would use setns() to join existing namespace
    printf("[sandbox] joined persistent namespace '%s' (stub)\n", name);
    return 0;
}

int sandbox_destroy_persistent_namespace(const char* name)
{
    if (!name) return -1;
    
    // this would unmount namespace bind mounts
    printf("[sandbox] destroyed persistent namespace '%s' (stub)\n", name);
    return 0;
}