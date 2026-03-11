#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * sm_security.c - Privilege drop, resource limits, seccomp filter, peer
 * credentials.
 */

#include "sm_security.h"
#include "../observability/sm_logging.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h> /* SIGTERM, SIGKILL, SIGCHLD */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Linux-specific kernel interfaces — seccomp BPF, capabilities, prctl.
 * These headers are guarded so the file compiles on any POSIX system
 * (cross-compilation validation, static analysis on macOS, etc.).
 * At runtime the daemon requires Linux ≥ 3.17 for the full seccomp API.
 */
#ifdef __linux__
#  include <linux/capability.h>
#  include <linux/filter.h>
#  include <linux/seccomp.h>
#  include <sys/prctl.h>
#  include <sys/syscall.h>
#endif /* __linux__ */

/* ── PRIVILEGE DROP ───────────────────────────────────────────────────────────
 */

int sm_drop_privileges(void) {
  struct passwd *pwd;
  struct group *grp;

  if (getuid() != 0) {
    sm_log(SM_LOG_INFO, "security: not root, skipping privilege drop");
    return 0;
  }

  pwd = getpwnam(SM_USERNAME);
  if (!pwd) {
    sm_log(SM_LOG_ERROR, "security: user '%s' not found", SM_USERNAME);
    return -1;
  }

  grp = getgrnam(SM_GROUPNAME);
  if (!grp) {
    sm_log(SM_LOG_ERROR, "security: group '%s' not found", SM_GROUPNAME);
    return -1;
  }

  /* Drop supplementary groups first */
  if (initgroups(SM_USERNAME, grp->gr_gid) < 0) {
    sm_log(SM_LOG_ERROR, "security: initgroups failed: %s", strerror(errno));
    return -1;
  }

  /* Must drop GID before UID; once UID is dropped we lose CAP_SETGID */
  if (setgid(grp->gr_gid) < 0) {
    sm_log(SM_LOG_ERROR, "security: setgid(%d) failed: %s", (int)grp->gr_gid,
           strerror(errno));
    return -1;
  }

  if (setuid(pwd->pw_uid) < 0) {
    sm_log(SM_LOG_ERROR, "security: setuid(%d) failed: %s", (int)pwd->pw_uid,
           strerror(errno));
    return -1;
  }

  /* Verify drop: both real and effective UIDs must match */
  if (getuid() != pwd->pw_uid || geteuid() != pwd->pw_uid) {
    sm_log(SM_LOG_CRIT, "security: UID mismatch after drop - aborting");
    abort();
  }

  if (getgid() != grp->gr_gid || getegid() != grp->gr_gid) {
    sm_log(SM_LOG_CRIT, "security: GID mismatch after drop - aborting");
    abort();
  }

  /* Verify that root cannot be regained */
  if (setuid(0) == 0) {
    sm_log(SM_LOG_CRIT, "security: was able to regain root - aborting");
    abort();
  }
  errno = 0; /* clear EPERM set by the failed setuid(0) */

  sm_log(SM_LOG_INFO, "security: privileges dropped to %s:%s (uid=%d gid=%d)",
         SM_USERNAME, SM_GROUPNAME, (int)pwd->pw_uid, (int)grp->gr_gid);
  return 0;
}

/* ── RESOURCE LIMITS ──────────────────────────────────────────────────────────
 */

int sm_set_resource_limits(void) {
  /* Limit open file descriptors */
  struct rlimit rl_nofile = {.rlim_cur = 512, .rlim_max = 512};
  if (setrlimit(RLIMIT_NOFILE, &rl_nofile) < 0) {
    sm_log(SM_LOG_ERROR, "security: setrlimit NOFILE: %s", strerror(errno));
    return -1;
  }

  /* Prevent fork bombs */
  struct rlimit rl_nproc = {.rlim_cur = 64, .rlim_max = 64};
  if (setrlimit(RLIMIT_NPROC, &rl_nproc) < 0) {
    sm_log(SM_LOG_ERROR, "security: setrlimit NPROC: %s", strerror(errno));
    return -1;
  }

  /* Limit virtual address space */
  struct rlimit rl_as = {
      .rlim_cur = 256UL * 1024 * 1024,
      .rlim_max = 256UL * 1024 * 1024,
  };
  if (setrlimit(RLIMIT_AS, &rl_as) < 0) {
    sm_log(SM_LOG_ERROR, "security: setrlimit AS: %s", strerror(errno));
    return -1;
  }

  /* Disable core dumps */
  struct rlimit rl_core = {.rlim_cur = 0, .rlim_max = 0};
  if (setrlimit(RLIMIT_CORE, &rl_core) < 0) {
    sm_log(SM_LOG_ERROR, "security: setrlimit CORE: %s", strerror(errno));
    return -1;
  }

  sm_log(SM_LOG_INFO, "security: resource limits applied");
  return 0;
}

/* ── SECCOMP FILTER ───────────────────────────────────────────────────────────
 */

#ifdef __linux__

/*
 * Helper macro: allow one syscall number and fall through to the next rule.
 */
#define ALLOW_SYSCALL(nr)                                                      \
  BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1),                             \
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

int sm_setup_seccomp(void) {
  struct sock_filter filter[] = {
      /* Load the syscall number from the seccomp_data structure */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

      /* Process lifecycle */
      ALLOW_SYSCALL(SYS_exit_group),
      ALLOW_SYSCALL(SYS_exit),

      /* Signal handling */
      ALLOW_SYSCALL(SYS_rt_sigaction),
      ALLOW_SYSCALL(SYS_rt_sigprocmask),
      ALLOW_SYSCALL(SYS_rt_sigreturn),
      ALLOW_SYSCALL(SYS_sigaltstack),

      /* Socket I/O */
      ALLOW_SYSCALL(SYS_socket),
      ALLOW_SYSCALL(SYS_bind),
      ALLOW_SYSCALL(SYS_listen),
      ALLOW_SYSCALL(SYS_accept),
      ALLOW_SYSCALL(SYS_accept4),
      ALLOW_SYSCALL(SYS_connect),
      ALLOW_SYSCALL(SYS_sendto),
      ALLOW_SYSCALL(SYS_recvfrom),
      ALLOW_SYSCALL(SYS_sendmsg),
      ALLOW_SYSCALL(SYS_recvmsg),
      ALLOW_SYSCALL(SYS_getsockname),
      ALLOW_SYSCALL(SYS_getsockopt),
      ALLOW_SYSCALL(SYS_setsockopt),
      ALLOW_SYSCALL(SYS_shutdown),

      /* File descriptor I/O */
      ALLOW_SYSCALL(SYS_read),
      ALLOW_SYSCALL(SYS_write),
      ALLOW_SYSCALL(SYS_close),
      ALLOW_SYSCALL(SYS_fcntl),
      ALLOW_SYSCALL(SYS_fstat),
      ALLOW_SYSCALL(SYS_lseek),
      ALLOW_SYSCALL(SYS_openat), /* log file + key file */
      ALLOW_SYSCALL(SYS_rename), /* log rotation */
      ALLOW_SYSCALL(SYS_unlink), /* socket cleanup */
      ALLOW_SYSCALL(SYS_stat),

      /* epoll */
      ALLOW_SYSCALL(SYS_epoll_create1),
      ALLOW_SYSCALL(SYS_epoll_ctl),
      ALLOW_SYSCALL(SYS_epoll_wait),
      ALLOW_SYSCALL(SYS_epoll_pwait),

      /* Threading */
      ALLOW_SYSCALL(SYS_futex),
      ALLOW_SYSCALL(SYS_clone), /* pthread_create */
      ALLOW_SYSCALL(SYS_getpid),
      ALLOW_SYSCALL(SYS_gettid),
      ALLOW_SYSCALL(SYS_set_robust_list),

      /* Memory */
      ALLOW_SYSCALL(SYS_brk),
      ALLOW_SYSCALL(SYS_mmap),
      ALLOW_SYSCALL(SYS_munmap),
      ALLOW_SYSCALL(SYS_mprotect),
      ALLOW_SYSCALL(SYS_mremap),
      ALLOW_SYSCALL(SYS_mlock), /* Pin key in RAM */
      ALLOW_SYSCALL(SYS_munlock),

      /* Time */
      ALLOW_SYSCALL(SYS_clock_gettime),
      ALLOW_SYSCALL(SYS_gettimeofday),
      ALLOW_SYSCALL(SYS_nanosleep),
      ALLOW_SYSCALL(SYS_clock_nanosleep),

      /* Randomness */
      ALLOW_SYSCALL(SYS_getrandom),

      /* * SYS_kill — RESTRICTED.
       * Only permits signals needed for health monitoring.
       */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_kill, 0, 7),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[1])),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SIGTERM, 4, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SIGKILL, 3, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SIGCHLD, 2, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

      ALLOW_SYSCALL(SYS_wait4),

      /* chmod - used for socket permissions */
      ALLOW_SYSCALL(SYS_chmod),
      ALLOW_SYSCALL(SYS_fchmod),

      /* signalfd4 — signal-safe event loop */
      ALLOW_SYSCALL(SYS_signalfd4),

      /* glibc compatibility */
      ALLOW_SYSCALL(SYS_rseq),
      ALLOW_SYSCALL(SYS_clone3),
      ALLOW_SYSCALL(SYS_madvise),
      ALLOW_SYSCALL(SYS_pipe2),
      ALLOW_SYSCALL(SYS_pread64),
      ALLOW_SYSCALL(SYS_pwrite64),
      ALLOW_SYSCALL(SYS_getpeername),
      ALLOW_SYSCALL(SYS_getuid),
      ALLOW_SYSCALL(SYS_geteuid),
      ALLOW_SYSCALL(SYS_getgid),
      ALLOW_SYSCALL(SYS_getegid),
      ALLOW_SYSCALL(SYS_ioctl),
      ALLOW_SYSCALL(SYS_prctl),
      ALLOW_SYSCALL(SYS_tgkill),
      ALLOW_SYSCALL(SYS_newfstatat),

      /* Default: kill process on any unlisted syscall */
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
  };

  struct sock_fprog prog = {
      .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
      .filter = filter,
  };

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
    sm_log(SM_LOG_ERROR, "security: PR_SET_NO_NEW_PRIVS failed: %s",
           strerror(errno));
    return -1;
  }

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    sm_log(SM_LOG_ERROR, "security: seccomp filter install failed: %s",
           strerror(errno));
    return -1;
  }

  sm_log(SM_LOG_INFO, "security: seccomp filter installed (%zu rules)",
         sizeof(filter) / sizeof(filter[0]));
  return 0;
}

#else  /* !__linux__ */

int sm_setup_seccomp(void)
{
    sm_log(SM_LOG_WARN, "security: seccomp not supported on this platform — skipping");
    return 0;
}

#endif /* __linux__ */

/* ── FILESYSTEM SANDBOX ───────────────────────────────────────────────────────
 */

int sm_setup_sandbox(void) {
  sm_log(SM_LOG_INFO, "security: sandbox: using seccomp + privilege drop");
  return 0;
}

/* ── PEER CREDENTIALS ─────────────────────────────────────────────────────────
 */

uid_t sm_get_peer_uid(int fd) {
  struct ucred cred = {0};
  socklen_t len = sizeof(cred);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
    sm_log(SM_LOG_ERROR, "security: SO_PEERCRED (uid) failed: %s",
           strerror(errno));
    return (uid_t)-1;
  }
  return cred.uid;
}

gid_t sm_get_peer_gid(int fd) {
  struct ucred cred = {0};
  socklen_t len = sizeof(cred);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
    sm_log(SM_LOG_ERROR, "security: SO_PEERCRED (gid) failed: %s",
           strerror(errno));
    return (gid_t)-1;
  }
  return cred.gid;
}

pid_t sm_get_peer_pid(int fd) {
  struct ucred cred = {0};
  socklen_t len = sizeof(cred);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
    sm_log(SM_LOG_ERROR, "security: SO_PEERCRED (pid) failed: %s",
           strerror(errno));
    return (pid_t)-1;
  }
  return cred.pid;
}
