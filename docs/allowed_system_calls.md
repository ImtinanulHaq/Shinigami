Complete System Call List

### Core Essential (tumhari HAL mein zaroor honi chahiye):

**Process Management (~25):**
```
fork, execve, execveat, clone, clone3
exit, exit_group, wait4, waitpid, waitid
getpid, getppid, gettid, getpgid, setpgid
setsid, getsid, kill, tkill, tgkill
prctl, arch_prctl, seccomp, ptrace
```

**Memory Management (~15):**
```
brk, mmap, mmap2, munmap, mremap
mprotect, madvise, mlock, munlock
mlockall, munlockall, mincore
memfd_create, shmat, shmdt
shmget, shmctl
```

**File System (~60):**
```
open, openat, openat2, close, read
write, pread64, pwrite64, readv, writev
lseek, llseek, dup, dup2, dup3
stat, fstat, lstat, statx, newfstatat
access, faccessat, chmod, fchmod, chown
fchown, lchown, mkdir, mkdirat, rmdir
unlink, unlinkat, rename, renameat, link
symlink, readlink, readlinkat, truncate
ftruncate, fallocate, fsync, fdatasync
sync, syncfs, getcwd, chdir, fchdir
getdents, getdents64, inotify_init
inotify_add_watch, inotify_rm_watch
```

**Network/Socket (~35):**
```
socket, bind, listen, accept, accept4
connect, sendto, recvfrom, send, recv
sendmsg, recvmsg, sendmmsg, recvmmsg
getsockname, getpeername, getsockopt
setsockopt, shutdown, socketpair, pipe
pipe2, poll, ppoll, select, pselect6
epoll_create, epoll_create1, epoll_ctl
epoll_wait, epoll_pwait, epoll_pwait2
```

**IPC (~20):**
```
semget, semop, semctl, semtimedop
msgget, msgsnd, msgrcv, msgctl
shmget, shmat, shmdt, shmctl
mq_open, mq_send, mq_receive
mq_unlink, mq_getsetattr
eventfd, eventfd2, signalfd, signalfd4
```

**Security (~15):**
```
capget, capset, getuid, getgid
geteuid, getegid, setuid, setgid
setreuid, setregid, setresuid, setresgid
getresuid, getresgid, setgroups
getgroups, initgroups
```

**Threading (~10):**
```
futex, futex_waitv, set_tid_address
set_robust_list, get_robust_list
gettid, clone, clone3, tgkill, tkill
```

**Time (~10):**
```
clock_gettime, clock_settime, clock_nanosleep
clock_getres, gettimeofday, settimeofday
nanosleep, alarm, timer_create
timer_settime, timer_gettime, timer_delete
```

**Device/Hardware (~15):**
```
ioctl, read, write, open, close
mmap, munmap, poll, select
getrandom, urandom (via open)
perf_event_open, userfaultfd
io_uring_setup, io_uring_enter
```

**Signals (~12):**
```
signal, sigaction, rt_sigaction
sigprocmask, rt_sigprocmask
sigpending, sigsuspend, rt_sigsuspend
sigreturn, rt_sigreturn, raise
kill, sigqueue, sigtimedwait
```

**Miscellaneous (~10):**
```
uname, sysinfo, getrlimit, setrlimit
prlimit64, getrusage, times
syslog, reboot, kexec_load
```

---

## Tumhari HAL Strategy

```
Total ~350 syscalls mein se:
BLOCK completely:    ~150  (dangerous, unused)
ALLOW with check:   ~150  (validate before passing)
ALLOW directly:     ~50   (harmless: getpid, time etc)
```
