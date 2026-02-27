#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sandbox_mount.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>

#define SANDBOX_ROOT  "/tmp/sandbox_root"
#define OLD_ROOT_NAME ".old_root"

static int create_minimal_dev(const char* root)
{
    char dev_path[PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", root);

    if (mkdir(dev_path, 0755) < 0 && errno != EEXIST) return -1;

    if (mount("tmpfs", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=0755") < 0)
        return -1;

    char path[PATH_MAX];
    size_t dev_len = strlen(dev_path);
    if (dev_len > PATH_MAX - 10) return -1;

    snprintf(path, sizeof(path), "%s/null", dev_path);
    if (mknod(path, S_IFCHR | 0666, makedev(1, 3)) < 0 && errno != EEXIST)
        return -1;

    snprintf(path, sizeof(path), "%s/zero", dev_path);
    if (mknod(path, S_IFCHR | 0666, makedev(1, 5)) < 0 && errno != EEXIST)
        return -1;

    snprintf(path, sizeof(path), "%s/urandom", dev_path);
    if (mknod(path, S_IFCHR | 0666, makedev(1, 9)) < 0 && errno != EEXIST)
        return -1;

    return 0;
}

static int apply_pivot_root(const char* new_root)
{
    char old_root[PATH_MAX];
    snprintf(old_root, sizeof(old_root), "%s/%s", new_root, OLD_ROOT_NAME);

    if (mkdir(old_root, 0700) < 0 && errno != EEXIST) return -1;

    if (syscall(SYS_pivot_root, new_root, old_root) < 0) return -1;
    if (chdir("/") < 0) return -1;

    char old_inside[PATH_MAX];
    snprintf(old_inside, sizeof(old_inside), "/%s", OLD_ROOT_NAME);
    umount2(old_inside, MNT_DETACH);
    rmdir(old_inside);

    return 0;
}

int sandbox_mount_setup_filesystem(const char* requested_root)
{
    const char* root = requested_root ? requested_root : SANDBOX_ROOT;

    if (mkdir(root, 0755) < 0 && errno != EEXIST) return -1;

    if (mount("tmpfs", root, "tmpfs", MS_NODEV | MS_NOSUID, "mode=0755") < 0)
        return -1;

    char path[PATH_MAX];
    const char* dirs[] = {"tmp", "proc", "sys", NULL};
    for (int i = 0; dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        mkdir(path, 0755);
    }

    if (create_minimal_dev(root) < 0) return -1;
    if (apply_pivot_root(root) < 0) return -1;

    mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, "hidepid=2");
    return 0;
}

int sandbox_mount_add_binding(const filesystem_binding_t* binding)
{
    if (!binding || !binding->host_path || !binding->container_path) return -1;

    if (access(binding->host_path, F_OK) != 0) {
        if (binding->optional) return 0;
        return -1;
    }

    if (mkdir(binding->container_path, 0755) < 0 && errno != EEXIST) return -1;

    if (mount(binding->host_path, binding->container_path, NULL, MS_BIND, NULL) < 0)
        return -1;

    if (binding->read_only) {
        if (mount(NULL, binding->container_path, NULL,
                  MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV, NULL) < 0)
            return -1;
    }

    return 0;
}

int sandbox_mount_remove_binding(const char* container_path)
{
    if (!container_path) return -1;
    if (umount2(container_path, MNT_DETACH) < 0) return -1;
    return 0;
}
