#define _POSIX_C_SOURCE 200809L

#include "seccomp_filter.h"
#include <stdio.h>
#include <seccomp.h>
#include <sys/prctl.h>

static int allow(scmp_filter_ctx ctx, int syscall_nr)
{
    return seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
}

// minimum syscalls every service needs to function
static int apply_common(scmp_filter_ctx ctx)
{
    // file I/O
    allow(ctx, SCMP_SYS(read));
    allow(ctx, SCMP_SYS(write));
    allow(ctx, SCMP_SYS(open));
    allow(ctx, SCMP_SYS(openat));
    allow(ctx, SCMP_SYS(close));
    allow(ctx, SCMP_SYS(fstat));
    allow(ctx, SCMP_SYS(stat));
    allow(ctx, SCMP_SYS(lseek));
    allow(ctx, SCMP_SYS(ftruncate));

    // memory
    allow(ctx, SCMP_SYS(mmap));
    allow(ctx, SCMP_SYS(munmap));
    allow(ctx, SCMP_SYS(mprotect));
    allow(ctx, SCMP_SYS(brk));

    // threading and locks
    allow(ctx, SCMP_SYS(futex));

    // timing
    allow(ctx, SCMP_SYS(clock_gettime));
    allow(ctx, SCMP_SYS(nanosleep));

    // signals
    allow(ctx, SCMP_SYS(rt_sigaction));
    allow(ctx, SCMP_SYS(rt_sigreturn));
    allow(ctx, SCMP_SYS(rt_sigprocmask));

    // event loop
    allow(ctx, SCMP_SYS(epoll_create1));
    allow(ctx, SCMP_SYS(epoll_ctl));
    allow(ctx, SCMP_SYS(epoll_wait));
    allow(ctx, SCMP_SYS(eventfd2));

    // io_uring
    allow(ctx, SCMP_SYS(io_uring_setup));
    allow(ctx, SCMP_SYS(io_uring_enter));
    allow(ctx, SCMP_SYS(io_uring_register));

    // unix socket — needed to talk to service manager
    allow(ctx, SCMP_SYS(socket));
    allow(ctx, SCMP_SYS(connect));
    allow(ctx, SCMP_SYS(send));
    allow(ctx, SCMP_SYS(recv));
    allow(ctx, SCMP_SYS(sendto));
    allow(ctx, SCMP_SYS(recvfrom));

    // process identity
    allow(ctx, SCMP_SYS(getpid));

    // must have — process cannot exit without this
    allow(ctx, SCMP_SYS(exit));
    allow(ctx, SCMP_SYS(exit_group));

    // NOTE: fork, execve, ptrace, clone are NOT here
    // they are dangerous — only add where truly needed
    return 0;
}

// audio: needs ioctl to control /dev/snd ALSA driver
static int apply_audio(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    allow(ctx, SCMP_SYS(ioctl));
    return 0;
}

// sensor: reads from /sys/bus/iio sysfs — common is enough + ioctl
static int apply_sensor(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    allow(ctx, SCMP_SYS(ioctl));
    return 0;
}

// camera: V4L2 driver needs ioctl heavily for frame capture
static int apply_camera(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    allow(ctx, SCMP_SYS(ioctl));
    allow(ctx, SCMP_SYS(mlock));    // lock camera frame buffers in RAM
    return 0;
}

int seccomp_apply(service_type_t type)
{
    // SCMP_ACT_KILL: any syscall not in whitelist = instant process kill
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
    if (!ctx) {
        fprintf(stderr, "[seccomp] init failed\n");
        return -1;
    }

    int rc = 0;
    switch (type) {
        case SERVICE_TYPE_AUDIO:  rc = apply_audio(ctx);  break;
        case SERVICE_TYPE_SENSOR: rc = apply_sensor(ctx); break;
        case SERVICE_TYPE_CAMERA: rc = apply_camera(ctx); break;
        default:
            fprintf(stderr, "[seccomp] unknown type\n");
            seccomp_release(ctx);
            return -1;
    }

    if (rc != 0) {
        fprintf(stderr, "[seccomp] rule add failed\n");
        seccomp_release(ctx);
        return -1;
    }

    // even if execve runs a setuid binary — no new privileges ever
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    // load into kernel — filter is ACTIVE from this point
    if (seccomp_load(ctx) != 0) {
        fprintf(stderr, "[seccomp] load failed\n");
        seccomp_release(ctx);
        return -1;
    }

    seccomp_release(ctx);
    printf("[seccomp] active for service type %d\n", type);
    return 0;
}