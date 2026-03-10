#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * sm_security.c - Privilege drop, resource limits, seccomp filter, peer
 * credentials.
 *
 * Fixes applied:
 *   - sm_drop_privileges() verifies the drop succeeded and confirms root cannot
 *     be regained.
 *   - Seccomp whitelist extended with syscalls needed by pthreads and malloc
 *     (futex, mmap, munmap, mprotect, brk, clone, getrandom, getpid, gettid).
 *   - openat/fstat/lseek added for log file and key file access.
 *   - SYS_kill retained but documented; it is needed for the health monitor.
 *     If tighter confinement is required, replace kill() with a private signal
 *     mechanism and remove it from the whitelist.
 */

#include "sm_security.h"
#include "../observability/sm_logging.h"

#include <errno.h>
#include <grp.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

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

/*
 * Helper macro: allow one syscall number and fall through to the next rule.
 * Each BPF_JUMP + BPF_STMT pair occupies two filter slots.
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

      /* Threading (pthreads: mutex/condvar/rwlock use futex internally) */
      ALLOW_SYSCALL(SYS_futex),
      ALLOW_SYSCALL(SYS_clone), /* pthread_create */
      ALLOW_SYSCALL(SYS_getpid),
      ALLOW_SYSCALL(SYS_gettid),
      ALLOW_SYSCALL(SYS_set_robust_list),

      /* Memory (malloc, thread stacks) */
      ALLOW_SYSCALL(SYS_brk),
      ALLOW_SYSCALL(SYS_mmap),
      ALLOW_SYSCALL(SYS_munmap),
      ALLOW_SYSCALL(SYS_mprotect),
      ALLOW_SYSCALL(SYS_mremap),

      /* Time */
      ALLOW_SYSCALL(SYS_clock_gettime),
      ALLOW_SYSCALL(SYS_gettimeofday),
      ALLOW_SYSCALL(SYS_nanosleep),
      ALLOW_SYSCALL(SYS_clock_nanosleep),

      /* Randomness - used for nonce generation in client API */
      ALLOW_SYSCALL(SYS_getrandom),

      /* Process control - used by health monitor to signal crashed services */
      ALLOW_SYSCALL(SYS_kill),
      ALLOW_SYSCALL(SYS_wait4),

      /* chmod - used by sm_socket_setup for socket permissions */
      ALLOW_SYSCALL(SYS_chmod),
      ALLOW_SYSCALL(SYS_fchmod),

      /* signalfd4 — used by sm_run() for signal-safe event loop */
      ALLOW_SYSCALL(SYS_signalfd4),

      /* glibc 2.35+: rseq registered on every thread start.
       * Without this, pthread_create is killed immediately. */
      ALLOW_SYSCALL(SYS_rseq),

      /* clone3 — newer glibc uses clone3 instead of clone for pthread_create */
      ALLOW_SYSCALL(SYS_clone3),

      /* madvise — glibc malloc uses MADV_DONTNEED on free */
      ALLOW_SYSCALL(SYS_madvise),

      /* pipe2 — used by glibc internals and pthread */
      ALLOW_SYSCALL(SYS_pipe2),

      /* pread64 / pwrite64 — glibc internals and log rotation */
      ALLOW_SYSCALL(SYS_pread64),
      ALLOW_SYSCALL(SYS_pwrite64),

      /* getpeername — SO_PEERCRED peer credential checks */
      ALLOW_SYSCALL(SYS_getpeername),

      /* uid/gid getters — verified after privilege drop */
      ALLOW_SYSCALL(SYS_getuid),
      ALLOW_SYSCALL(SYS_geteuid),
      ALLOW_SYSCALL(SYS_getgid),
      ALLOW_SYSCALL(SYS_getegid),

      /* ioctl — terminal and socket operations */
      ALLOW_SYSCALL(SYS_ioctl),

      /* prctl — NO_NEW_PRIVS, seccomp itself, thread name */
      ALLOW_SYSCALL(SYS_prctl),

      /* tgkill — pthread_cancel and glibc internal signal delivery */
      ALLOW_SYSCALL(SYS_tgkill),

      /* newfstatat — glibc stat() wrapper on newer kernels */
      ALLOW_SYSCALL(SYS_newfstatat),

      /* Default: kill process on any unlisted syscall */
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
  };

  struct sock_fprog prog = {
      .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
      .filter = filter,
  };

  /* Prevent privilege escalation via execve+setuid */
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

/* ── FILESYSTEM SANDBOX ───────────────────────────────────────────────────────
 */

int sm_setup_sandbox(void) {
  /*
   * A full chroot or mount-namespace sandbox can be added here.
   * For the current deployment model (single host, dedicated user),
   * privilege drop + seccomp provide sufficient containment.
   */
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
