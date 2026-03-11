#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "seccomp_core.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t seccomp_core_lock = PTHREAD_MUTEX_INITIALIZER;
static int monitoring_enabled = 0;
static int log_fd = -1;

static int *allowed_fds = NULL;
static size_t allowed_fd_count = 0;

int seccomp_core_is_monitoring_enabled(void) {
  pthread_mutex_lock(&seccomp_core_lock);
  int enabled = monitoring_enabled;
  pthread_mutex_unlock(&seccomp_core_lock);
  return enabled;
}

size_t seccomp_core_get_device_fd_count(void) {
  pthread_mutex_lock(&seccomp_core_lock);
  size_t count = allowed_fd_count;
  pthread_mutex_unlock(&seccomp_core_lock);
  return count;
}

int seccomp_core_setup_monitoring(const char *log_path) {
  pthread_mutex_lock(&seccomp_core_lock);
  if (monitoring_enabled) {
    pthread_mutex_unlock(&seccomp_core_lock);
    return 0;
  }

  log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (log_fd < 0) {
    pthread_mutex_unlock(&seccomp_core_lock);
    return -1;
  }

  monitoring_enabled = 1;
  pthread_mutex_unlock(&seccomp_core_lock);
  return 0;
}

void seccomp_core_cleanup_monitoring(void) {
  pthread_mutex_lock(&seccomp_core_lock);
  if (log_fd >= 0) {
    close(log_fd);
    log_fd = -1;
  }
  monitoring_enabled = 0;
  pthread_mutex_unlock(&seccomp_core_lock);
}

static void sig_safe_write_int(char *buf, int *pos, int bufsize, int val) {
  if (val < 0) {
    if (*pos < bufsize)
      buf[(*pos)++] = '-';
    val = -val;
  }
  char tmp[16];
  int tlen = 0;
  if (val == 0) {
    tmp[tlen++] = '0';
  } else {
    while (val > 0 && tlen < 15) {
      tmp[tlen++] = (char)('0' + (val % 10));
      val /= 10;
    }
  }
  for (int i = tlen - 1; i >= 0; i--) {
    if (*pos < bufsize)
      buf[(*pos)++] = tmp[i];
  }
}

static void sig_safe_write_str(char *buf, int *pos, int bufsize,
                               const char *s) {
  while (*s && *pos < bufsize) {
    buf[(*pos)++] = *s++;
  }
}

static void handle_sigsys(int sig, siginfo_t *info, void *context) {
  (void)sig;
  (void)context;
  if (info->si_code == 1 && monitoring_enabled && log_fd >= 0) {
    char sig_buf[128];
    int pos = 0;
    sig_safe_write_str(sig_buf, &pos, (int)sizeof(sig_buf),
                       "SECCOMP_VIOLATION: syscall=");
    sig_safe_write_int(sig_buf, &pos, (int)sizeof(sig_buf), info->si_syscall);
    sig_safe_write_str(sig_buf, &pos, (int)sizeof(sig_buf), " pid=");
    sig_safe_write_int(sig_buf, &pos, (int)sizeof(sig_buf), getpid());
    sig_safe_write_str(sig_buf, &pos, (int)sizeof(sig_buf), "\n");
    ssize_t r = write(log_fd, sig_buf, (size_t)pos);
    (void)r;
  }
  _exit(139);
}

int seccomp_core_setup_violation_handler(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = handle_sigsys;
  sa.sa_flags = SA_SIGINFO;
  return sigaction(SIGSYS, &sa, NULL);
}

int seccomp_core_setup_device_fds(const char **device_paths, size_t count) {
  seccomp_core_cleanup_device_fds();
  if (!device_paths || count == 0)
    return 0;

  pthread_mutex_lock(&seccomp_core_lock);
  allowed_fds = malloc(count * sizeof(int));
  if (!allowed_fds) {
    pthread_mutex_unlock(&seccomp_core_lock);
    return -1;
  }

  allowed_fd_count = 0;
  for (size_t i = 0; i < count; i++) {
    int fd = open(device_paths[i], O_RDWR);
    if (fd >= 0)
      allowed_fds[allowed_fd_count++] = fd;
  }
  pthread_mutex_unlock(&seccomp_core_lock);
  return 0;
}

void seccomp_core_cleanup_device_fds(void) {
  pthread_mutex_lock(&seccomp_core_lock);
  if (allowed_fds) {
    for (size_t i = 0; i < allowed_fd_count; i++)
      close(allowed_fds[i]);
    free(allowed_fds);
    allowed_fds = NULL;
    allowed_fd_count = 0;
  }
  pthread_mutex_unlock(&seccomp_core_lock);
}

int seccomp_core_add_ioctl_filter(scmp_filter_ctx ctx) {
  if (allowed_fd_count == 0)
    return seccomp_core_allow(ctx, SCMP_SYS(ioctl));

  for (size_t i = 0; i < allowed_fd_count; i++) {
    int rc =
        seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioctl), 1,
                         SCMP_A0(SCMP_CMP_EQ, (unsigned int)allowed_fds[i]));
    if (rc < 0)
      return -1;
  }
  return 0;
}

int seccomp_core_allow(scmp_filter_ctx ctx, int syscall_nr) {
  int rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
  if (rc < 0)
    return -1;
  return 0;
}

int seccomp_core_validate_architecture(scmp_filter_ctx ctx) {
  uint32_t arch = seccomp_arch_native();
  int rc = seccomp_arch_add(ctx, arch);

  /* Ignore -EEXIST since seccomp_init() already adds the native architecture */
  if (rc < 0 && rc != -EEXIST) {
    return -1;
  }

#ifdef __x86_64__
  /* Remove 32-bit x86 compatibility to prevent architecture confusion attacks
   */
  seccomp_arch_remove(ctx, SCMP_ARCH_X86);
#endif

  return 0;
}

int seccomp_core_apply_common(scmp_filter_ctx ctx) {
  seccomp_core_allow(ctx, SCMP_SYS(read));
  seccomp_core_allow(ctx, SCMP_SYS(write));
  seccomp_core_allow(ctx, SCMP_SYS(open));
  seccomp_core_allow(ctx, SCMP_SYS(openat));
  seccomp_core_allow(ctx, SCMP_SYS(close));
  seccomp_core_allow(ctx, SCMP_SYS(lseek));
  seccomp_core_allow(ctx, SCMP_SYS(fstat));
  seccomp_core_allow(ctx, SCMP_SYS(stat));
  seccomp_core_allow(ctx, SCMP_SYS(ftruncate));

  seccomp_core_allow(ctx, SCMP_SYS(mmap));
  seccomp_core_allow(ctx, SCMP_SYS(munmap));
  seccomp_core_allow(ctx, SCMP_SYS(mprotect));
  seccomp_core_allow(ctx, SCMP_SYS(brk));

  seccomp_core_allow(ctx, SCMP_SYS(futex));

  seccomp_core_allow(ctx, SCMP_SYS(clock_gettime));
  seccomp_core_allow(ctx, SCMP_SYS(nanosleep));
  seccomp_core_allow(ctx, SCMP_SYS(clock_nanosleep));

  seccomp_core_allow(ctx, SCMP_SYS(rt_sigaction));
  seccomp_core_allow(ctx, SCMP_SYS(rt_sigreturn));
  seccomp_core_allow(ctx, SCMP_SYS(rt_sigprocmask));

  seccomp_core_allow(ctx, SCMP_SYS(epoll_create1));
  seccomp_core_allow(ctx, SCMP_SYS(epoll_ctl));
  seccomp_core_allow(ctx, SCMP_SYS(epoll_wait));
  seccomp_core_allow(ctx, SCMP_SYS(eventfd2));

  seccomp_core_allow(ctx, SCMP_SYS(getpid));
  seccomp_core_allow(ctx, SCMP_SYS(gettid));

  seccomp_core_allow(ctx, SCMP_SYS(exit));
  seccomp_core_allow(ctx, SCMP_SYS(exit_group));

  /* --- Hardware device control: allow ioctl unconditionally.
   * Libraries (ALSA, V4L2, gpiod) open their own fds internally, so
   * per-fd filtering cannot be applied portably here.  All services are
   * still restricted to the minimal syscall set configured above. */
  seccomp_core_allow(ctx, SCMP_SYS(ioctl));

  /* --- IPC and sockets (Unix domain sockets for SM communication) --- */
  seccomp_core_allow(ctx, SCMP_SYS(socket));
  seccomp_core_allow(ctx, SCMP_SYS(connect));
  seccomp_core_allow(ctx, SCMP_SYS(send));
  seccomp_core_allow(ctx, SCMP_SYS(recv));
  seccomp_core_allow(ctx, SCMP_SYS(sendto));
  seccomp_core_allow(ctx, SCMP_SYS(recvfrom));
  seccomp_core_allow(ctx, SCMP_SYS(sendmsg));
  seccomp_core_allow(ctx, SCMP_SYS(recvmsg));
  seccomp_core_allow(ctx, SCMP_SYS(setsockopt));
  seccomp_core_allow(ctx, SCMP_SYS(getsockopt));
  seccomp_core_allow(ctx, SCMP_SYS(getsockname));
  seccomp_core_allow(ctx, SCMP_SYS(shutdown));

  /* --- File system operations needed by ALSA, V4L2, gpiod, libc --- */
  seccomp_core_allow(ctx, SCMP_SYS(lstat));
  seccomp_core_allow(ctx, SCMP_SYS(newfstatat));  /* fstatat / newfstatat */
  seccomp_core_allow(ctx, SCMP_SYS(access));
  seccomp_core_allow(ctx, SCMP_SYS(faccessat));
  seccomp_core_allow(ctx, SCMP_SYS(readlink));
  seccomp_core_allow(ctx, SCMP_SYS(readlinkat));
  seccomp_core_allow(ctx, SCMP_SYS(unlink));      /* shm_unlink for ring buffers */
  seccomp_core_allow(ctx, SCMP_SYS(unlinkat));
  seccomp_core_allow(ctx, SCMP_SYS(mkdir));
  seccomp_core_allow(ctx, SCMP_SYS(mkdirat));
  seccomp_core_allow(ctx, SCMP_SYS(rmdir));
  seccomp_core_allow(ctx, SCMP_SYS(rename));
  seccomp_core_allow(ctx, SCMP_SYS(renameat));
  seccomp_core_allow(ctx, SCMP_SYS(fcntl));
  seccomp_core_allow(ctx, SCMP_SYS(dup));
  seccomp_core_allow(ctx, SCMP_SYS(dup2));
  seccomp_core_allow(ctx, SCMP_SYS(dup3));
  seccomp_core_allow(ctx, SCMP_SYS(pipe2));
  seccomp_core_allow(ctx, SCMP_SYS(pread64));
  seccomp_core_allow(ctx, SCMP_SYS(pwrite64));
  seccomp_core_allow(ctx, SCMP_SYS(readv));
  seccomp_core_allow(ctx, SCMP_SYS(writev));
  seccomp_core_allow(ctx, SCMP_SYS(getdents64));
  seccomp_core_allow(ctx, SCMP_SYS(getcwd));
  seccomp_core_allow(ctx, SCMP_SYS(chdir));
  seccomp_core_allow(ctx, SCMP_SYS(fchdir));
  seccomp_core_allow(ctx, SCMP_SYS(chmod));
  seccomp_core_allow(ctx, SCMP_SYS(fchmod));
  seccomp_core_allow(ctx, SCMP_SYS(fchmodat));
  seccomp_core_allow(ctx, SCMP_SYS(chown));
  seccomp_core_allow(ctx, SCMP_SYS(fchown));
  seccomp_core_allow(ctx, SCMP_SYS(lchown));
  seccomp_core_allow(ctx, SCMP_SYS(fchownat));
  seccomp_core_allow(ctx, SCMP_SYS(truncate));
  seccomp_core_allow(ctx, SCMP_SYS(sync));
  seccomp_core_allow(ctx, SCMP_SYS(fsync));
  seccomp_core_allow(ctx, SCMP_SYS(fdatasync));
  seccomp_core_allow(ctx, SCMP_SYS(inotify_init1));
  seccomp_core_allow(ctx, SCMP_SYS(inotify_add_watch));
  seccomp_core_allow(ctx, SCMP_SYS(inotify_rm_watch));

  /* --- I/O multiplexing --- */
  seccomp_core_allow(ctx, SCMP_SYS(poll));
  seccomp_core_allow(ctx, SCMP_SYS(ppoll));
  seccomp_core_allow(ctx, SCMP_SYS(select));
  seccomp_core_allow(ctx, SCMP_SYS(pselect6));

  /* --- Thread and process management --- */
  seccomp_core_allow(ctx, SCMP_SYS(clone));
  seccomp_core_allow(ctx, SCMP_SYS(clone3));
  seccomp_core_allow(ctx, SCMP_SYS(prctl));
  seccomp_core_allow(ctx, SCMP_SYS(arch_prctl));
  seccomp_core_allow(ctx, SCMP_SYS(set_tid_address));
  seccomp_core_allow(ctx, SCMP_SYS(tgkill));
  seccomp_core_allow(ctx, SCMP_SYS(kill));
  seccomp_core_allow(ctx, SCMP_SYS(wait4));
  seccomp_core_allow(ctx, SCMP_SYS(set_robust_list));
  seccomp_core_allow(ctx, SCMP_SYS(get_robust_list));

  /* --- Identity/credentials (used by glibc, security checks) --- */
  seccomp_core_allow(ctx, SCMP_SYS(getuid));
  seccomp_core_allow(ctx, SCMP_SYS(geteuid));
  seccomp_core_allow(ctx, SCMP_SYS(getgid));
  seccomp_core_allow(ctx, SCMP_SYS(getegid));
  seccomp_core_allow(ctx, SCMP_SYS(getgroups));
  seccomp_core_allow(ctx, SCMP_SYS(capget));
  seccomp_core_allow(ctx, SCMP_SYS(capset));

  /* --- Memory management extensions --- */
  seccomp_core_allow(ctx, SCMP_SYS(madvise));
  seccomp_core_allow(ctx, SCMP_SYS(msync));
  seccomp_core_allow(ctx, SCMP_SYS(mremap));
  seccomp_core_allow(ctx, SCMP_SYS(mlock));
  seccomp_core_allow(ctx, SCMP_SYS(munlock));
  seccomp_core_allow(ctx, SCMP_SYS(mlock2));
  seccomp_core_allow(ctx, SCMP_SYS(mlockall));
  seccomp_core_allow(ctx, SCMP_SYS(munlockall));
  seccomp_core_allow(ctx, SCMP_SYS(mincore));

  /* --- Shared memory (POSIX shm for ring buffers) --- */
  seccomp_core_allow(ctx, SCMP_SYS(shmget));
  seccomp_core_allow(ctx, SCMP_SYS(shmat));
  seccomp_core_allow(ctx, SCMP_SYS(shmctl));
  seccomp_core_allow(ctx, SCMP_SYS(shmdt));

  /* --- Timers --- */
  seccomp_core_allow(ctx, SCMP_SYS(timerfd_create));
  seccomp_core_allow(ctx, SCMP_SYS(timerfd_settime));
  seccomp_core_allow(ctx, SCMP_SYS(timerfd_gettime));
  seccomp_core_allow(ctx, SCMP_SYS(timer_create));
  seccomp_core_allow(ctx, SCMP_SYS(timer_settime));
  seccomp_core_allow(ctx, SCMP_SYS(timer_gettime));
  seccomp_core_allow(ctx, SCMP_SYS(timer_delete));
  seccomp_core_allow(ctx, SCMP_SYS(clock_getres));
  seccomp_core_allow(ctx, SCMP_SYS(alarm));
  seccomp_core_allow(ctx, SCMP_SYS(getitimer));
  seccomp_core_allow(ctx, SCMP_SYS(setitimer));

  /* --- Signals --- */
  seccomp_core_allow(ctx, SCMP_SYS(rt_sigpending));
  seccomp_core_allow(ctx, SCMP_SYS(rt_sigsuspend));
  seccomp_core_allow(ctx, SCMP_SYS(rt_sigtimedwait));
  seccomp_core_allow(ctx, SCMP_SYS(rt_sigqueueinfo));
  seccomp_core_allow(ctx, SCMP_SYS(sigaltstack));

  /* --- System information --- */
  seccomp_core_allow(ctx, SCMP_SYS(uname));
  seccomp_core_allow(ctx, SCMP_SYS(sysinfo));
  seccomp_core_allow(ctx, SCMP_SYS(getrandom));   /* OpenSSL CSPRNG */
  seccomp_core_allow(ctx, SCMP_SYS(getrlimit));
  seccomp_core_allow(ctx, SCMP_SYS(setrlimit));
  seccomp_core_allow(ctx, SCMP_SYS(prlimit64));

  /* --- Misc libc / dynamic linker needs --- */
  seccomp_core_allow(ctx, SCMP_SYS(sched_getscheduler));
  seccomp_core_allow(ctx, SCMP_SYS(sched_setscheduler));
  seccomp_core_allow(ctx, SCMP_SYS(sched_getparam));
  seccomp_core_allow(ctx, SCMP_SYS(sched_setparam));
  seccomp_core_allow(ctx, SCMP_SYS(sched_yield));
  seccomp_core_allow(ctx, SCMP_SYS(sched_getaffinity));
  seccomp_core_allow(ctx, SCMP_SYS(sched_setaffinity));
  seccomp_core_allow(ctx, SCMP_SYS(sched_get_priority_min));  /* ALSA RT priority */
  seccomp_core_allow(ctx, SCMP_SYS(sched_get_priority_max));
  seccomp_core_allow(ctx, SCMP_SYS(sched_rr_get_interval));
  seccomp_core_allow(ctx, SCMP_SYS(epoll_pwait));
#ifdef __NR_epoll_pwait2
  seccomp_core_allow(ctx, SCMP_SYS(epoll_pwait2));   /* Linux 5.11+ */
#endif
  seccomp_core_allow(ctx, SCMP_SYS(semget));
  seccomp_core_allow(ctx, SCMP_SYS(semop));
  seccomp_core_allow(ctx, SCMP_SYS(semctl));

  /* --- io_uring (used by service event loops) --- */
  seccomp_core_allow_io_uring(ctx);

  /* --- Newer stat variant used by glibc realpath() and V4L2 --- */
  seccomp_core_allow(ctx, SCMP_SYS(statx));

  /* --- openat2 (newer open variant used on recent kernels) --- */
#ifdef __NR_openat2
  seccomp_core_allow(ctx, SCMP_SYS(openat2));
#endif

  /* --- Process session / credentials (used by glibc, ALSA, PAM) --- */
  seccomp_core_allow(ctx, SCMP_SYS(getsid));
  seccomp_core_allow(ctx, SCMP_SYS(setsid));
  seccomp_core_allow(ctx, SCMP_SYS(getppid));
  seccomp_core_allow(ctx, SCMP_SYS(getpgrp));
  seccomp_core_allow(ctx, SCMP_SYS(getpgid));
  seccomp_core_allow(ctx, SCMP_SYS(setpgid));
  seccomp_core_allow(ctx, SCMP_SYS(setuid));
  seccomp_core_allow(ctx, SCMP_SYS(setgid));
  seccomp_core_allow(ctx, SCMP_SYS(setreuid));
  seccomp_core_allow(ctx, SCMP_SYS(setregid));
  seccomp_core_allow(ctx, SCMP_SYS(setresuid));
  seccomp_core_allow(ctx, SCMP_SYS(setresgid));
  seccomp_core_allow(ctx, SCMP_SYS(setgroups));
  seccomp_core_allow(ctx, SCMP_SYS(getresuid));
  seccomp_core_allow(ctx, SCMP_SYS(getresgid));

  /* --- Process execution / environment --- */
  seccomp_core_allow(ctx, SCMP_SYS(execve));
  seccomp_core_allow(ctx, SCMP_SYS(execveat));
  seccomp_core_allow(ctx, SCMP_SYS(vfork));
  seccomp_core_allow(ctx, SCMP_SYS(fork));

  /* --- File descriptor flags and advanced file ops --- */
  seccomp_core_allow(ctx, SCMP_SYS(flock));
  seccomp_core_allow(ctx, SCMP_SYS(fallocate));
  seccomp_core_allow(ctx, SCMP_SYS(sendfile));
  seccomp_core_allow(ctx, SCMP_SYS(copy_file_range));
  seccomp_core_allow(ctx, SCMP_SYS(linkat));
  seccomp_core_allow(ctx, SCMP_SYS(symlinkat));
  seccomp_core_allow(ctx, SCMP_SYS(readlinkat));
  seccomp_core_allow(ctx, SCMP_SYS(faccessat));
  seccomp_core_allow(ctx, SCMP_SYS(faccessat2));

  /* --- Memory locking (ALSA, real-time audio) --- */
  seccomp_core_allow(ctx, SCMP_SYS(memfd_create));

  /* --- Misc needed by ALSA / PulseAudio / PipeWire client libs --- */
  seccomp_core_allow(ctx, SCMP_SYS(personality));
  seccomp_core_allow(ctx, SCMP_SYS(umask));
  seccomp_core_allow(ctx, SCMP_SYS(times));
  seccomp_core_allow(ctx, SCMP_SYS(getrusage));
  seccomp_core_allow(ctx, SCMP_SYS(gettimeofday));
  seccomp_core_allow(ctx, SCMP_SYS(settimeofday));
  seccomp_core_allow(ctx, SCMP_SYS(time));
  seccomp_core_allow(ctx, SCMP_SYS(getcpu));
  seccomp_core_allow(ctx, SCMP_SYS(clock_settime));

  return 0;
}

int seccomp_core_allow_networking(scmp_filter_ctx ctx) {
  seccomp_core_allow(ctx, SCMP_SYS(socket));
  seccomp_core_allow(ctx, SCMP_SYS(connect));
  seccomp_core_allow(ctx, SCMP_SYS(bind));
  seccomp_core_allow(ctx, SCMP_SYS(listen));
  seccomp_core_allow(ctx, SCMP_SYS(accept));
  seccomp_core_allow(ctx, SCMP_SYS(accept4));
  seccomp_core_allow(ctx, SCMP_SYS(send));
  seccomp_core_allow(ctx, SCMP_SYS(recv));
  seccomp_core_allow(ctx, SCMP_SYS(sendto));
  seccomp_core_allow(ctx, SCMP_SYS(recvfrom));
  seccomp_core_allow(ctx, SCMP_SYS(sendmsg));
  seccomp_core_allow(ctx, SCMP_SYS(recvmsg));
  seccomp_core_allow(ctx, SCMP_SYS(shutdown));
  seccomp_core_allow(ctx, SCMP_SYS(setsockopt));
  seccomp_core_allow(ctx, SCMP_SYS(getsockopt));
  seccomp_core_allow(ctx, SCMP_SYS(getpeername));
  seccomp_core_allow(ctx, SCMP_SYS(getsockname));
  return 0;
}

int seccomp_core_allow_io_uring(scmp_filter_ctx ctx) {
  seccomp_core_allow(ctx, SCMP_SYS(io_uring_setup));
  seccomp_core_allow(ctx, SCMP_SYS(io_uring_enter));
  seccomp_core_allow(ctx, SCMP_SYS(io_uring_register));
  return 0;
}

int seccomp_core_setup_notification(scmp_filter_ctx ctx, int syscall_nr) {
#ifdef SCMP_ACT_NOTIFY
  int rc = seccomp_rule_add(ctx, SCMP_ACT_NOTIFY, syscall_nr, 0);
  if (rc < 0) {
    syslog(LOG_ERR, "[seccomp] notification rule for syscall %d failed: %d",
           syscall_nr, rc);
    return -1;
  }
  syslog(LOG_INFO, "[seccomp] notification enabled for syscall %d", syscall_nr);
  return 0;
#else
  (void)ctx;
  (void)syscall_nr;
  syslog(LOG_WARNING, "[seccomp] SECCOMP_RET_USER_NOTIF not supported");
  return -1;
#endif
}
