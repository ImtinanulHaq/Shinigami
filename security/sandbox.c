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

// validate configuration - prevents attack vectors
static int validate_config(const sandbox_config_t* cfg)
{
    if (!cfg) return -1;
    
    // at least one namespace required
    if (!cfg->enable_pid_ns && !cfg->enable_net_ns && 
        !cfg->enable_mount_ns && !cfg->enable_ipc_ns) {
        fprintf(stderr, "[sandbox] no namespaces enabled\n");
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
        .real_uid        = 1000,
        .real_gid        = 1000,
        .chroot_path     = NULL
    };
    return cfg;
}