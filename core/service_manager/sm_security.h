#ifndef SM_SECURITY_H
#define SM_SECURITY_H

#include <sys/types.h>

/*
 * System user and group that the service manager runs as after startup.
 * Create these before first run:
 *   addgroup --system servicemanager
 *   adduser  --system --ingroup servicemanager servicemanager
 */
#define SM_USERNAME     "servicemanager"
#define SM_GROUPNAME    "servicemanager"

/* Drop root after socket and key file setup is complete */
int  sm_drop_privileges(void);

/* Apply rlimit constraints to prevent resource exhaustion */
int  sm_set_resource_limits(void);

/* Install seccomp-BPF syscall whitelist - call last, after thread pool is started */
int  sm_setup_seccomp(void);

/* Optional chroot / namespace sandbox (stub - extend as needed) */
int  sm_setup_sandbox(void);

/* Read kernel-verified peer credentials from SO_PEERCRED - cannot be spoofed */
uid_t sm_get_peer_uid(int fd);
gid_t sm_get_peer_gid(int fd);
pid_t sm_get_peer_pid(int fd);

#endif /* SM_SECURITY_H */