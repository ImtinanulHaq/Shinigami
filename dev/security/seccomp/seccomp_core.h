#ifndef SECCOMP_CORE_H
#define SECCOMP_CORE_H

#include "seccomp_filter.h"
#include <seccomp.h>

int seccomp_core_allow(scmp_filter_ctx ctx, int syscall_nr);
int seccomp_core_apply_common(scmp_filter_ctx ctx);
int seccomp_core_allow_networking(scmp_filter_ctx ctx);
int seccomp_core_allow_io_uring(scmp_filter_ctx ctx);
int seccomp_core_validate_architecture(scmp_filter_ctx ctx);
int seccomp_core_setup_monitoring(const char* log_path);
int seccomp_core_setup_violation_handler(void);
int seccomp_core_setup_device_fds(const char** device_paths, size_t count);
int seccomp_core_add_ioctl_filter(scmp_filter_ctx ctx);
void seccomp_core_cleanup_device_fds(void);
void seccomp_core_cleanup_monitoring(void);

int seccomp_core_setup_notification(scmp_filter_ctx ctx, int syscall_nr);

int seccomp_core_is_monitoring_enabled(void);
size_t seccomp_core_get_device_fd_count(void);

#endif
