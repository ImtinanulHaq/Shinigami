#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sandbox_cgroup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#define CGROUP_ROOT "/sys/fs/cgroup"

static int write_cgroup_file(const char* cgroup_name, const char* file, const char* value)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/%s", CGROUP_ROOT, cgroup_name, file);

    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;

    ssize_t written = write(fd, value, strlen(value));
    close(fd);

    if (written != (ssize_t)strlen(value)) return -1;
    return 0;
}

int sandbox_cgroup_create(const char* cgroup_name, const resource_limits_t* limits)
{
    if (!cgroup_name || !limits) return -1;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", CGROUP_ROOT, cgroup_name);

    if (mkdir(path, 0755) < 0 && errno != EEXIST) return -1;

    char value[128];

    if (limits->memory_limit_mb > 0) {
        snprintf(value, sizeof(value), "%lu",
                 (unsigned long)limits->memory_limit_mb * 1024 * 1024);
        write_cgroup_file(cgroup_name, "memory.max", value);
    }

    if (limits->cpu_quota_percent > 0 && limits->cpu_quota_percent <= 100) {
        snprintf(value, sizeof(value), "%d 100000", limits->cpu_quota_percent * 1000);
        write_cgroup_file(cgroup_name, "cpu.max", value);
    }

    if (limits->cpu_weight > 0) {
        snprintf(value, sizeof(value), "%u", limits->cpu_weight);
        write_cgroup_file(cgroup_name, "cpu.weight", value);
    }

    if (limits->io_weight > 0) {
        snprintf(value, sizeof(value), "%u", limits->io_weight);
        write_cgroup_file(cgroup_name, "io.weight", value);
    }

    if (limits->pids_limit > 0) {
        snprintf(value, sizeof(value), "%u", limits->pids_limit);
        write_cgroup_file(cgroup_name, "pids.max", value);
    }

    return 0;
}

int sandbox_cgroup_add_process(const char* cgroup_name, pid_t pid)
{
    if (!cgroup_name) return -1;

    char value[32];
    snprintf(value, sizeof(value), "%d", pid);
    return write_cgroup_file(cgroup_name, "cgroup.procs", value);
}

int sandbox_cgroup_destroy(const char* cgroup_name)
{
    if (!cgroup_name) return -1;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", CGROUP_ROOT, cgroup_name);

    write_cgroup_file(cgroup_name, "cgroup.kill", "1");
    usleep(100000);

    if (rmdir(path) < 0) return -1;
    return 0;
}
