#ifndef SANDBOX_CGROUP_H
#define SANDBOX_CGROUP_H

#include "sandbox.h"

int sandbox_cgroup_create(const char* cgroup_name, const resource_limits_t* limits);
int sandbox_cgroup_add_process(const char* cgroup_name, pid_t pid);
int sandbox_cgroup_destroy(const char* cgroup_name);

#endif
