#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "sm_security.h"
#include "sm_logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <linux/capability.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <sys/syscall.h>

// ── DROP PRIVILEGES ───────────────────────────────────────────────────────────

int sm_drop_privileges(void)
{
    // Only drop if running as root
    if (getuid() != 0) {
        sm_log(SM_LOG_INFO, "not running as root, skipping privilege drop");
        return 0;
    }

    struct passwd* pwd = getpwnam(SM_USERNAME);
    if (!pwd) {
        sm_log(SM_LOG_ERROR, "user '%s' not found", SM_USERNAME);
        return -1;
    }

    struct group* grp = getgrnam(SM_GROUPNAME);
    if (!grp) {
        sm_log(SM_LOG_ERROR, "group '%s' not found", SM_GROUPNAME);
        return -1;
    }

    // Change to service manager user (must drop group before user for security)
    if (setgid(grp->gr_gid) < 0) {
        sm_log(SM_LOG_ERROR, "setgid failed: %m");
        return -1;
    }

    if (setuid(pwd->pw_uid) < 0) {
        sm_log(SM_LOG_ERROR, "setuid failed: %m");
        return -1;
    }

    sm_log(SM_LOG_INFO, "dropped privileges to %s:%s (%d:%d)",
           SM_USERNAME, SM_GROUPNAME, pwd->pw_uid, grp->gr_gid);

    return 0;
}

// ── RESOURCE LIMITS ────────────────────────────────────────────────────────────

int sm_set_resource_limits(void)
{
    // Prevent file descriptor exhaustion
    struct rlimit rl_nofile = {
        .rlim_cur = 256,
        .rlim_max = 256,
    };
    if (setrlimit(RLIMIT_NOFILE, &rl_nofile) < 0) {
        sm_log(SM_LOG_ERROR, "setrlimit NOFILE failed: %m");
        return -1;
    }

    // Prevent fork bombs (max processes)
    struct rlimit rl_nproc = {
        .rlim_cur = 64,
        .rlim_max = 64,
    };
    if (setrlimit(RLIMIT_NPROC, &rl_nproc) < 0) {
        sm_log(SM_LOG_ERROR, "setrlimit NPROC failed: %m");
        return -1;
    }

    // Limit memory
    struct rlimit rl_as = {
        .rlim_cur = 256 * 1024 * 1024,  // 256 MB
        .rlim_max = 256 * 1024 * 1024,
    };
    if (setrlimit(RLIMIT_AS, &rl_as) < 0) {
        sm_log(SM_LOG_ERROR, "setrlimit AS failed: %m");
        return -1;
    }

    // Prevent core dumps (security + disk space)
    struct rlimit rl_core = {
        .rlim_cur = 0,
        .rlim_max = 0,
    };
    if (setrlimit(RLIMIT_CORE, &rl_core) < 0) {
        sm_log(SM_LOG_ERROR, "setrlimit CORE failed: %m");
        return -1;
    }

    sm_log(SM_LOG_INFO, "resource limits applied");
    return 0;
}

// ── SECCOMP FILTER ─────────────────────────────────────────────────────────────
// Whitelist ONLY safe syscalls

int sm_setup_seccomp(void)
{
    // BPF filter: allow safe syscalls only
    struct sock_filter filter[] = {
        // Load syscall number
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

        // Allow no-op syscalls
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit_group,        0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit,              0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_rt_sigaction,      0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_rt_sigprocmask,    0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sigaltstack,       0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        // Socket operations
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket,            0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_bind,              0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_listen,            0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_accept4,           0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_accept,            0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sendto,            0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_recvfrom,          0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sendmsg,           0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_recvmsg,           0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_connect,           0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getsockname,       0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getsockopt,        0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_setsockopt,        0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        // File/epoll operations (read only for most)
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_read,              0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write,             0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_close,             0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_epoll_create1,     0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_epoll_ctl,         0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_epoll_wait,        0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_epoll_pwait,       0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_fcntl,             0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        // Process control (kill, wait)
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_kill,              0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_wait4,             0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        // Time
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_clock_gettime,     0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_gettimeofday,      0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_nanosleep,         0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        // Default: KILL process if syscall not whitelisted
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
    };

    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        sm_log(SM_LOG_ERROR, "prctl NO_NEW_PRIVS failed: %m");
        return -1;
    }

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
        sm_log(SM_LOG_ERROR, "prctl SET_SECCOMP failed: %m");
        return -1;
    }

    sm_log(SM_LOG_INFO, "seccomp filter installed");
    return 0;
}

// ── FILESYSTEM SANDBOX ─────────────────────────────────────────────────────────

int sm_setup_sandbox(void)
{
    // For now, optional - can be enhanced with mount namespaces
    // If needed, do chroot to /var/empty (or similar secure location)
    sm_log(SM_LOG_INFO, "sandbox setup (chroot not enabled - consider namespace)");
    return 0;
}

// ── PEER CREDENTIALS ───────────────────────────────────────────────────────────

uid_t sm_get_peer_uid(int fd)
{
    struct ucred cred = {0};
    socklen_t len = sizeof(cred);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        sm_log(SM_LOG_ERROR, "getsockopt SO_PEERCRED failed: %m");
        return (uid_t)-1;
    }

    return cred.uid;
}

gid_t sm_get_peer_gid(int fd)
{
    struct ucred cred = {0};
    socklen_t len = sizeof(cred);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        return (gid_t)-1;
    }

    return cred.gid;
}

pid_t sm_get_peer_pid(int fd)
{
    struct ucred cred = {0};
    socklen_t len = sizeof(cred);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        return (pid_t)-1;
    }

    return cred.pid;
}
