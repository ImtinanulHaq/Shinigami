#ifndef SM_SECURITY_H
#define SM_SECURITY_H

#include <sys/types.h>

// ── SERVICE MANAGER USER/GROUP ────────────────────────────────────────────────
// Should be created during system setup:
// addgroup servicemanager
// adduser servicemanager --ingroup servicemanager --system

#define SM_USERNAME     "servicemanager"
#define SM_GROUPNAME    "servicemanager"

// ── SECURITY FUNCTIONS ────────────────────────────────────────────────────────

// Drop privileges: set UID/GID after startup
int  sm_drop_privileges(void);

// Set resource limits (prevent DoS)
int  sm_set_resource_limits(void);

// Setup seccomp filter (syscall whitelist)
// Only allows safe syscalls, blocks execve, ptrace, mount, etc.
int  sm_setup_seccomp(void);

// Setup filesystem sandbox with chroot
int  sm_setup_sandbox(void);

// Setup Linux capabilities (CAP_KILL, CAP_SYS_RESOURCE)
// All other capabilities are dropped
int  sm_setup_capabilities(void);

// Validate peer credentials on socket connection
uid_t sm_get_peer_uid(int fd);
gid_t sm_get_peer_gid(int fd);
pid_t sm_get_peer_pid(int fd);

#endif // SM_SECURITY_H
