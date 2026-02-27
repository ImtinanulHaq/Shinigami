#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sandbox_core.h"
#include "sandbox_mount.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <errno.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>

#define PERSISTENT_NS_DIR "/run/middleware/ns"
#define NS_PATH_MAX 256

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static const struct {
    const char* name;
    resource_limits_t limits;
    int enable_network;
} service_configs[] = {
    {"audio",   {512, 25, 100, 100, 10}, 0},
    {"camera",  {1024, 50, 200, 200, 5}, 0},
    {"sensor",  {256, 10, 50, 50, 5},    0},
    {"network", {512, 30, 100, 100, 20}, 1},
    {NULL,      {0, 0, 0, 0, 0},         0}
};

int sandbox_core_validate_config(const sandbox_config_t* cfg)
{
    if (!cfg) return -1;

    if (!cfg->enable_pid_ns && !cfg->enable_net_ns &&
        !cfg->enable_mount_ns && !cfg->enable_ipc_ns && !cfg->enable_cgroups)
        return -1;

    if (cfg->real_uid == 0 || cfg->real_gid == 0) return -1;

    if (cfg->chroot_path && strstr(cfg->chroot_path, "..")) return -1;

    if (cfg->limits.cpu_quota_percent > 100) return -1;
    if (cfg->limits.cpu_weight > 10000) return -1;

    if (cfg->enable_cgroups && cfg->cgroup_name) {
        if (strstr(cfg->cgroup_name, "/") || strstr(cfg->cgroup_name, ".."))
            return -1;
    }

    return 0;
}

int sandbox_core_setup_user_namespace(uid_t real_uid, gid_t real_gid)
{
    pid_t pid = getpid();
    char path[PATH_MAX], map[256];
    int fd;

    snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
    snprintf(map, sizeof(map), "0 %u 1", real_uid);

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (write(fd, map, strlen(map)) != (ssize_t)strlen(map)) {
        close(fd);
        return -1;
    }
    close(fd);

    snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { ssize_t r = write(fd, "deny", 4); (void)r; close(fd); }

    snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
    snprintf(map, sizeof(map), "0 %u 1", real_gid);

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    if (write(fd, map, strlen(map)) != (ssize_t)strlen(map)) {
        close(fd);
        return -1;
    }
    close(fd);

    return 0;
}

int sandbox_core_apply_namespaces(const sandbox_config_t* cfg)
{
    if (sandbox_core_validate_config(cfg) < 0) {
        syslog(LOG_ERR, "[sandbox] invalid configuration");
        return -1;
    }

    int flags = 0;
    if (cfg->enable_pid_ns)   flags |= CLONE_NEWPID;
    if (cfg->enable_net_ns)   flags |= CLONE_NEWNET;
    if (cfg->enable_mount_ns) flags |= CLONE_NEWNS;
    if (cfg->enable_ipc_ns)   flags |= CLONE_NEWIPC;
    if (cfg->enable_user_ns)  flags |= CLONE_NEWUSER;
    if (cfg->enable_uts_ns)   flags |= CLONE_NEWUTS;

    if (unshare(flags) < 0) {
        syslog(LOG_ERR, "[sandbox] unshare(0x%x) failed: %s", flags, strerror(errno));
        return -1;
    }

    if (cfg->enable_user_ns) {
        if (sandbox_core_setup_user_namespace(cfg->real_uid, cfg->real_gid) < 0) {
            syslog(LOG_ERR, "[sandbox] user namespace setup failed, cleaning up");
            return -1;
        }
    }

    if (cfg->enable_mount_ns) {
        mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL);
        if (sandbox_mount_setup_filesystem(cfg->chroot_path) < 0)
            return -1;
    }

    return 0;
}

sandbox_config_t sandbox_core_default_config(void)
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
        .limits = {
            .memory_limit_mb   = 512,
            .cpu_quota_percent = 50,
            .cpu_weight        = 100,
            .io_weight         = 100,
            .pids_limit        = 10
        },
        .enable_cgroups = 1,
        .cgroup_name    = NULL,
        .network = {
            .enable_loopback = 1,
            .enable_internet = 0,
            .allowed_hosts   = NULL,
            .allowed_ports   = NULL
        },
        .bindings      = NULL,
        .binding_count = 0,
        .persistent_namespaces = 0,
        .namespace_name        = NULL
    };
    return cfg;
}

sandbox_config_t sandbox_core_get_service_config(const char* service_name)
{
    sandbox_config_t config = sandbox_core_default_config();
    if (!service_name) return config;

    for (int i = 0; service_configs[i].name; i++) {
        if (strcmp(service_configs[i].name, service_name) == 0) {
            config.limits = service_configs[i].limits;
            config.enable_cgroups = 1;

            static __thread char cgroup_name[64];
            snprintf(cgroup_name, sizeof(cgroup_name), "middleware_%s_%d",
                     service_name, getpid());
            config.cgroup_name = cgroup_name;

            if (service_configs[i].enable_network) {
                config.enable_net_ns = 0;
                config.network.enable_internet = 1;
                config.network.enable_loopback = 1;
            } else {
                config.enable_net_ns = 1;
                config.network.enable_internet = 0;
                config.network.enable_loopback = 1;
            }
            break;
        }
    }
    return config;
}

int sandbox_core_create_persistent_namespace(const char* name, int ns_types)
{
    if (!name || strlen(name) == 0 || strlen(name) > 64) return -1;

    char ns_dir[NS_PATH_MAX];
    snprintf(ns_dir, sizeof(ns_dir), "%s/%s", PERSISTENT_NS_DIR, name);

    if (mkdir(PERSISTENT_NS_DIR, 0755) < 0 && errno != EEXIST) {
        syslog(LOG_ERR, "[sandbox] mkdir %s: %s", PERSISTENT_NS_DIR, strerror(errno));
        return -1;
    }
    if (mkdir(ns_dir, 0755) < 0 && errno != EEXIST) {
        syslog(LOG_ERR, "[sandbox] mkdir %s: %s", ns_dir, strerror(errno));
        return -1;
    }

    static const struct { int flag; const char* file; } ns_map[] = {
        {CLONE_NEWPID,  "pid"},
        {CLONE_NEWNET,  "net"},
        {CLONE_NEWNS,   "mnt"},
        {CLONE_NEWIPC,  "ipc"},
        {CLONE_NEWUTS,  "uts"},
        {CLONE_NEWUSER, "user"},
        {0, NULL}
    };

    for (int i = 0; ns_map[i].file; i++) {
        if (!(ns_types & ns_map[i].flag)) continue;

        char src[NS_PATH_MAX], dst[NS_PATH_MAX];
        snprintf(src, sizeof(src), "/proc/self/ns/%s", ns_map[i].file);
        snprintf(dst, sizeof(dst), "%s/%s", ns_dir, ns_map[i].file);

        int fd = open(dst, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) {
            syslog(LOG_ERR, "[sandbox] create %s: %s", dst, strerror(errno));
            return -1;
        }
        if (fd >= 0) close(fd);

        if (mount(src, dst, NULL, MS_BIND, NULL) < 0) {
            syslog(LOG_ERR, "[sandbox] bind mount %s -> %s: %s", src, dst, strerror(errno));
            return -1;
        }
    }

    syslog(LOG_INFO, "[sandbox] persistent namespace '%s' created (flags=0x%x)", name, ns_types);
    return 0;
}

int sandbox_core_join_persistent_namespace(const char* name)
{
    if (!name || strlen(name) == 0 || strlen(name) > 64) return -1;

    char ns_dir[NS_PATH_MAX];
    snprintf(ns_dir, sizeof(ns_dir), "%s/%s", PERSISTENT_NS_DIR, name);

    static const char* ns_files[] = {"user", "mnt", "pid", "net", "ipc", "uts", NULL};

    for (int i = 0; ns_files[i]; i++) {
        char path[NS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", ns_dir, ns_files[i]);

        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        if (setns(fd, 0) < 0) {
            syslog(LOG_ERR, "[sandbox] setns %s: %s", path, strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }

    syslog(LOG_INFO, "[sandbox] joined persistent namespace '%s'", name);
    return 0;
}

int sandbox_core_destroy_persistent_namespace(const char* name)
{
    if (!name || strlen(name) == 0 || strlen(name) > 64) return -1;

    char ns_dir[NS_PATH_MAX];
    snprintf(ns_dir, sizeof(ns_dir), "%s/%s", PERSISTENT_NS_DIR, name);

    static const char* ns_files[] = {"pid", "net", "mnt", "ipc", "uts", "user", NULL};

    for (int i = 0; ns_files[i]; i++) {
        char path[NS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", ns_dir, ns_files[i]);

        umount2(path, MNT_DETACH);
        unlink(path);
    }

    rmdir(ns_dir);
    syslog(LOG_INFO, "[sandbox] persistent namespace '%s' destroyed", name);
    return 0;
}

#pragma GCC diagnostic pop
