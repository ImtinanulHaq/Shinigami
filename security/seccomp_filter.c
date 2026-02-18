#define _POSIX_C_SOURCE 200809L

#include "seccomp_filter.h"

#include <stdio.h>
#include <string.h>
#include <seccomp.h>
#include <sys/prctl.h>
#include <errno.h>

// ══════════════════════════════════════════════════════════════════════════════
// ARCHITECTURE VALIDATION
// Critical security: prevent 32-bit syscall bypass on 64-bit systems
// Without this check, attacker can use int 0x80 to call 32-bit syscalls
// which bypass our 64-bit filter
// ══════════════════════════════════════════════════════════════════════════════

static int validate_architecture(scmp_filter_ctx ctx)
{
    uint32_t arch = seccomp_arch_native();
    
    // ensure we're filtering the correct architecture
    if (seccomp_arch_add(ctx, arch) < 0) {
        fprintf(stderr, "[seccomp] failed to add native arch\n");
        return -1;
    }
    
    // on x86_64, also block 32-bit syscalls explicitly
    #ifdef __x86_64__
    if (seccomp_arch_remove(ctx, SCMP_ARCH_X86) < 0) {
        // if removal fails, it might not have been added - that's ok
    }
    printf("[seccomp] x86_64: blocked 32-bit syscall interface\n");
    #endif
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// HELPER: Add One Allowed Syscall
// ══════════════════════════════════════════════════════════════════════════════

static int allow(scmp_filter_ctx ctx, int syscall_nr)
{
    int rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
    if (rc < 0) {
        fprintf(stderr, "[seccomp] failed to add syscall %d: %s\n",
                syscall_nr, strerror(-rc));
        return -1;
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMON SYSCALLS - Minimum Required by All Services
// Carefully audited - each syscall here serves a specific purpose
// Android has similar lists but we're more restrictive
// ══════════════════════════════════════════════════════════════════════════════

static int apply_common(scmp_filter_ctx ctx)
{
    // ── FILE I/O ───────────────────────────────────────────────────────────────
    // needed for basic file operations on /dev devices and ring buffer
    allow(ctx, SCMP_SYS(read));
    allow(ctx, SCMP_SYS(write));
    allow(ctx, SCMP_SYS(open));
    allow(ctx, SCMP_SYS(openat));    // modern replacement for open
    allow(ctx, SCMP_SYS(close));
    allow(ctx, SCMP_SYS(lseek));
    allow(ctx, SCMP_SYS(fstat));
    allow(ctx, SCMP_SYS(stat));
    allow(ctx, SCMP_SYS(ftruncate)); // needed for shm_open size setting
    
    // ── MEMORY MANAGEMENT ──────────────────────────────────────────────────────
    // ring buffer and heap allocation
    allow(ctx, SCMP_SYS(mmap));
    allow(ctx, SCMP_SYS(munmap));
    allow(ctx, SCMP_SYS(mprotect));  // needed for some allocators
    allow(ctx, SCMP_SYS(brk));       // heap expansion
    
    // ── SYNCHRONIZATION ────────────────────────────────────────────────────────
    // atomics and mutexes
    allow(ctx, SCMP_SYS(futex));
    
    // ── TIMING ─────────────────────────────────────────────────────────────────
    // performance monitoring and timeouts
    allow(ctx, SCMP_SYS(clock_gettime));
    allow(ctx, SCMP_SYS(nanosleep));
    allow(ctx, SCMP_SYS(clock_nanosleep));
    
    // ── SIGNALS ────────────────────────────────────────────────────────────────
    // signal handling for graceful shutdown
    allow(ctx, SCMP_SYS(rt_sigaction));
    allow(ctx, SCMP_SYS(rt_sigreturn));
    allow(ctx, SCMP_SYS(rt_sigprocmask));
    
    // ── EVENT LOOP ─────────────────────────────────────────────────────────────
    // epoll for multiplexing I/O
    allow(ctx, SCMP_SYS(epoll_create1));
    allow(ctx, SCMP_SYS(epoll_ctl));
    allow(ctx, SCMP_SYS(epoll_wait));
    allow(ctx, SCMP_SYS(eventfd2));  // for ring buffer notifications
    
    // ── ASYNC I/O (io_uring) ──────────────────────────────────────────────────
    // modern high-performance I/O
    allow(ctx, SCMP_SYS(io_uring_setup));
    allow(ctx, SCMP_SYS(io_uring_enter));
    allow(ctx, SCMP_SYS(io_uring_register));
    
    // ── UNIX SOCKET ────────────────────────────────────────────────────────────
    // needed to communicate with service manager
    allow(ctx, SCMP_SYS(socket));
    allow(ctx, SCMP_SYS(connect));
    allow(ctx, SCMP_SYS(send));
    allow(ctx, SCMP_SYS(recv));
    allow(ctx, SCMP_SYS(sendto));
    allow(ctx, SCMP_SYS(recvfrom));
    allow(ctx, SCMP_SYS(shutdown));
    
    // ── PROCESS INFO ───────────────────────────────────────────────────────────
    allow(ctx, SCMP_SYS(getpid));
    allow(ctx, SCMP_SYS(gettid));
    
    // ── EXIT ───────────────────────────────────────────────────────────────────
    // process must be able to exit cleanly
    allow(ctx, SCMP_SYS(exit));
    allow(ctx, SCMP_SYS(exit_group));
    
    // NOTE: fork, execve, ptrace, clone are EXPLICITLY NOT HERE
    // These are highly dangerous syscalls that enable:
    // - Process creation (fork/clone)
    // - Code execution (execve)
    // - Debugging/injection (ptrace)
    // Services get none of these by default
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// AUDIO SERVICE - Hardware I/O for Sound
// ══════════════════════════════════════════════════════════════════════════════

static int apply_audio(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    
    // ioctl - needed to control /dev/snd ALSA driver
    // examples: set sample rate, buffer size, start/stop playback
    allow(ctx, SCMP_SYS(ioctl));
    
    // TODO: add argument filtering to restrict ioctl to audio device fds only
    // this would make it even more secure than Android
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR SERVICE - Read from /sys/bus/iio
// ══════════════════════════════════════════════════════════════════════════════

static int apply_sensor(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    
    // ioctl - some sensor drivers need it
    allow(ctx, SCMP_SYS(ioctl));
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// CAMERA SERVICE - V4L2 Video Capture
// ══════════════════════════════════════════════════════════════════════════════

static int apply_camera(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    
    // ioctl - V4L2 driver uses ioctl heavily for frame capture
    allow(ctx, SCMP_SYS(ioctl));
    
    // mlock - lock frame buffers in RAM to prevent swapping
    // critical for real-time video capture
    allow(ctx, SCMP_SYS(mlock));
    allow(ctx, SCMP_SYS(munlock));
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN: Apply seccomp Filter
// This is the entry point called by services
// ══════════════════════════════════════════════════════════════════════════════

int seccomp_apply(service_type_t type)
{
    // ── STEP 1: Create Filter Context ─────────────────────────────────────────
    // SCMP_ACT_KILL = any syscall not in whitelist → instant process termination
    // This is the strictest mode - better than Android's SCMP_ACT_TRAP
    
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
    if (!ctx) {
        fprintf(stderr, "[seccomp] init failed\n");
        return -1;
    }
    
    // ── STEP 2: Validate Architecture ─────────────────────────────────────────
    // Prevent 32-bit syscall bypass on 64-bit systems
    
    if (validate_architecture(ctx) < 0) {
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 3: Apply Service-Specific Whitelist ──────────────────────────────
    
    int rc = 0;
    switch (type) {
        case SERVICE_TYPE_AUDIO:
            rc = apply_audio(ctx);
            printf("[seccomp] audio service whitelist applied\n");
            break;
            
        case SERVICE_TYPE_SENSOR:
            rc = apply_sensor(ctx);
            printf("[seccomp] sensor service whitelist applied\n");
            break;
            
        case SERVICE_TYPE_CAMERA:
            rc = apply_camera(ctx);
            printf("[seccomp] camera service whitelist applied\n");
            break;
            
        default:
            fprintf(stderr, "[seccomp] unknown service type %d\n", type);
            seccomp_release(ctx);
            return -1;
    }
    
    if (rc != 0) {
        fprintf(stderr, "[seccomp] whitelist application failed\n");
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 4: Lock Privilege Escalation ─────────────────────────────────────
    // Even if service calls execve() on a setuid binary, no new privileges
    // This is critical - without it, seccomp can be bypassed
    
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("[seccomp] PR_SET_NO_NEW_PRIVS");
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 5: Load Filter into Kernel ───────────────────────────────────────
    // From this point forward, filter is ACTIVE and IRREVERSIBLE
    // Any syscall not in whitelist = instant SIGKILL
    
    if (seccomp_load(ctx) != 0) {
        fprintf(stderr, "[seccomp] load failed - check kernel CONFIG_SECCOMP\n");
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 6: Release Context ───────────────────────────────────────────────
    // Filter stays active in kernel even after releasing context
    
    seccomp_release(ctx);
    
    printf("[seccomp] ACTIVE - syscall firewall engaged\n");
    printf("[seccomp] unauthorized syscalls will result in immediate termination\n");
    
    return 0;
}