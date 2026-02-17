#define _POSIX_C_SOURCE 200809L

#include "seccomp_filter.h"

#include <stdio.h>
#include <seccomp.h>        // libseccomp — easy seccomp API
#include <sys/prctl.h>      // prctl() — process control

// ── HELPER — add one allowed syscall to the filter ────────────────────────────

static int allow(scmp_filter_ctx ctx, int syscall_nr)
{
    return seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
}

// ── COMMON SYSCALLS — every service needs these ───────────────────────────────

static int apply_common(scmp_filter_ctx ctx)
{
    // reading and writing data
    allow(ctx, SCMP_SYS(read));
    allow(ctx, SCMP_SYS(write));

    // open and close files
    allow(ctx, SCMP_SYS(open));
    allow(ctx, SCMP_SYS(openat));
    allow(ctx, SCMP_SYS(close));

    // memory management
    allow(ctx, SCMP_SYS(mmap));
    allow(ctx, SCMP_SYS(munmap));
    allow(ctx, SCMP_SYS(mprotect));
    allow(ctx, SCMP_SYS(brk));

    // threads and locks
    allow(ctx, SCMP_SYS(futex));

    // clean exit — without this process cant even quit
    allow(ctx, SCMP_SYS(exit_group));
    allow(ctx, SCMP_SYS(exit));

    // get own process id
    allow(ctx, SCMP_SYS(getpid));

    // sleep
    allow(ctx, SCMP_SYS(nanosleep));
    allow(ctx, SCMP_SYS(clock_nanosleep));

    // check time
    allow(ctx, SCMP_SYS(clock_gettime));

    // signal handling
    allow(ctx, SCMP_SYS(rt_sigaction));
    allow(ctx, SCMP_SYS(rt_sigreturn));
    allow(ctx, SCMP_SYS(rt_sigprocmask));

    // epoll and eventfd — needed for our event loop
    allow(ctx, SCMP_SYS(epoll_create1));
    allow(ctx, SCMP_SYS(epoll_ctl));
    allow(ctx, SCMP_SYS(epoll_wait));
    allow(ctx, SCMP_SYS(eventfd2));

    // io_uring — our fast async I/O
    allow(ctx, SCMP_SYS(io_uring_setup));
    allow(ctx, SCMP_SYS(io_uring_enter));
    allow(ctx, SCMP_SYS(io_uring_register));

    return 0;
}

// ── AUDIO SERVICE WHITELIST ───────────────────────────────────────────────────

static int apply_audio(scmp_filter_ctx ctx)
{
    apply_common(ctx);

    // audio needs ioctl to talk to /dev/snd (ALSA sound driver)
    allow(ctx, SCMP_SYS(ioctl));

    // unix socket — to register with service manager
    allow(ctx, SCMP_SYS(socket));
    allow(ctx, SCMP_SYS(connect));
    allow(ctx, SCMP_SYS(send));
    allow(ctx, SCMP_SYS(recv));
    allow(ctx, SCMP_SYS(sendto));
    allow(ctx, SCMP_SYS(recvfrom));

    // shared memory — for ring buffer (shm_open uses open() internally)
    allow(ctx, SCMP_SYS(ftruncate));

    // file info
    allow(ctx, SCMP_SYS(fstat));
    allow(ctx, SCMP_SYS(stat));

    // audio service does NOT get: fork, exec, network, ptrace, kill
    // if hacked — attacker cannot spawn new processes or reach internet

    return 0;
}

// ── SENSOR SERVICE WHITELIST ──────────────────────────────────────────────────

static int apply_sensor(scmp_filter_ctx ctx)
{
    apply_common(ctx);

    // sensors are read via sysfs — just need read() and open()
    // already in common — nothing extra needed

    // ioctl for some sensor drivers
    allow(ctx, SCMP_SYS(ioctl));

    // unix socket — to register with service manager
    allow(ctx, SCMP_SYS(socket));
    allow(ctx, SCMP_SYS(connect));
    allow(ctx, SCMP_SYS(send));
    allow(ctx, SCMP_SYS(recv));
    allow(ctx, SCMP_SYS(sendto));
    allow(ctx, SCMP_SYS(recvfrom));

    // shared memory — for ring buffer
    allow(ctx, SCMP_SYS(ftruncate));
    allow(ctx, SCMP_SYS(fstat));

    // sensor service does NOT get: network, fork, exec, audio ioctls

    return 0;
}

// ── CAMERA SERVICE WHITELIST ──────────────────────────────────────────────────

static int apply_camera(scmp_filter_ctx ctx)
{
    apply_common(ctx);

    // camera uses V4L2 driver — needs ioctl heavily
    allow(ctx, SCMP_SYS(ioctl));

    // mmap for camera frame buffers (DMA buffers)
    allow(ctx, SCMP_SYS(mmap));

    // unix socket — to register with service manager
    allow(ctx, SCMP_SYS(socket));
    allow(ctx, SCMP_SYS(connect));
    allow(ctx, SCMP_SYS(send));
    allow(ctx, SCMP_SYS(recv));
    allow(ctx, SCMP_SYS(sendto));
    allow(ctx, SCMP_SYS(recvfrom));

    // shared memory — for ring buffer
    allow(ctx, SCMP_SYS(ftruncate));
    allow(ctx, SCMP_SYS(fstat));
    allow(ctx, SCMP_SYS(stat));

    return 0;
}

// ── MAIN FUNCTION — apply filter for given service type ───────────────────────

int seccomp_apply(service_type_t type)
{
    // SCMP_ACT_KILL = if process calls any syscall NOT in whitelist → kill it
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
    if (ctx == NULL) {
        fprintf(stderr, "[seccomp] init failed\n");
        return -1;
    }

    // apply the right whitelist
    int rc = 0;
    switch (type) {
        case SERVICE_TYPE_AUDIO:  rc = apply_audio(ctx);  break;
        case SERVICE_TYPE_SENSOR: rc = apply_sensor(ctx); break;
        case SERVICE_TYPE_CAMERA: rc = apply_camera(ctx); break;
        default:
            fprintf(stderr, "[seccomp] unknown service type\n");
            seccomp_release(ctx);
            return -1;
    }

    if (rc != 0) {
        fprintf(stderr, "[seccomp] adding rules failed\n");
        seccomp_release(ctx);
        return -1;
    }

    // prctl — even if service calls execve() later, no new privileges
    // this closes a common privilege escalation attack
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    // load the filter into the kernel — from this point it is ACTIVE
    rc = seccomp_load(ctx);
    if (rc != 0) {
        fprintf(stderr, "[seccomp] load failed\n");
        seccomp_release(ctx);
        return -1;
    }

    // free the context object — filter stays active in kernel
    seccomp_release(ctx);

    printf("[seccomp] filter active for service type %d\n", type);
    return 0;
}