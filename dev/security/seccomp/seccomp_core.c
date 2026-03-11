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

  /* --- ADDED: Basic IPC and Syslog support for ALL services --- */
  seccomp_core_allow(ctx, SCMP_SYS(socket));
  seccomp_core_allow(ctx, SCMP_SYS(connect));
  seccomp_core_allow(ctx, SCMP_SYS(send));
  seccomp_core_allow(ctx, SCMP_SYS(recv));
  seccomp_core_allow(ctx, SCMP_SYS(sendto));
  seccomp_core_allow(ctx, SCMP_SYS(recvfrom));
  seccomp_core_allow(ctx, SCMP_SYS(sendmsg));
  seccomp_core_allow(ctx, SCMP_SYS(recvmsg));
  /* ------------------------------------------------------------ */

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
