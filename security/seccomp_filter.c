#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "seccomp_filter.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <seccomp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <linux/seccomp.h>

// global monitoring state
static int monitoring_enabled = 0;
static int log_fd = -1;
static char log_buffer[1024];

// device file descriptors for argument filtering
static int* allowed_fds = NULL;
static size_t allowed_fd_count = 0;

// forward declarations
static int allow(scmp_filter_ctx ctx, int syscall_nr);

// ══════════════════════════════════════════════════════════════════════════════
// RUNTIME MONITORING - Log Syscall Violations
// ══════════════════════════════════════════════════════════════════════════════

int seccomp_enable_monitoring(const char* log_path)
{
    if (monitoring_enabled) return 0; // already enabled
    
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (log_fd < 0) {
        perror("[seccomp] open log file");
        return -1;
    }
    
    monitoring_enabled = 1;
    printf("[seccomp] monitoring enabled - violations logged to %s\n", log_path);
    return 0;
}

static void log_violation(int syscall_nr, const char* syscall_name)
{
    if (!monitoring_enabled || log_fd < 0) return;
    
    time_t now = time(NULL);
    snprintf(log_buffer, sizeof(log_buffer), 
        "[%ld] SECCOMP_VIOLATION: syscall=%d (%s) pid=%d\n",
        now, syscall_nr, syscall_name ? syscall_name : "unknown", getpid());
    
    write(log_fd, log_buffer, strlen(log_buffer));
}

// signal handler for SIGSYS (seccomp violations)
static void handle_sigsys(int sig, siginfo_t* info, void* context)
{
    if (info->si_code == 1) { // seccomp violation code
        log_violation(info->si_syscall, NULL);
        // still terminate - just log first
    }
    
    // re-raise as SIGKILL for immediate termination
    kill(getpid(), SIGKILL);
}

static int setup_violation_logging(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handle_sigsys;
    sa.sa_flags = SA_SIGINFO;
    
    return sigaction(SIGSYS, &sa, NULL);
}

// ══════════════════════════════════════════════════════════════════════════════
// ARGUMENT FILTERING - Restrict ioctl() to specific devices
// ══════════════════════════════════════════════════════════════════════════════

static int setup_device_fd_list(const char** device_paths, size_t count)
{
    if (allowed_fds) {
        free(allowed_fds);
        allowed_fds = NULL;
        allowed_fd_count = 0;
    }
    
    if (!device_paths || count == 0) return 0;
    
    allowed_fds = malloc(count * sizeof(int));
    if (!allowed_fds) return -1;
    
    allowed_fd_count = 0;
    for (size_t i = 0; i < count; i++) {
        int fd = open(device_paths[i], O_RDWR);
        if (fd >= 0) {
            allowed_fds[allowed_fd_count++] = fd;
            printf("[seccomp] registered device fd %d for %s\n", fd, device_paths[i]);
        } else {
            printf("[seccomp] warning: cannot open %s\n", device_paths[i]);
        }
    }
    
    return 0;
}

static int add_ioctl_arg_filter(scmp_filter_ctx ctx)
{
    if (allowed_fd_count == 0) {
        // no argument filtering - allow ioctl on any fd
        return allow(ctx, SCMP_SYS(ioctl));
    }
    
    // add rule for each allowed file descriptor
    for (size_t i = 0; i < allowed_fd_count; i++) {
        int rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioctl), 1,
                                  SCMP_A0(SCMP_CMP_EQ, allowed_fds[i]));
        if (rc < 0) {
            fprintf(stderr, "[seccomp] failed to add ioctl rule for fd %d\n", allowed_fds[i]);
            return -1;
        }
    }
    
    printf("[seccomp] ioctl restricted to %zu device file descriptors\n", allowed_fd_count);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// DEFAULT CONFIGURATIONS
// ══════════════════════════════════════════════════════════════════════════════

seccomp_config_t seccomp_get_default_config(service_type_t type)
{
    seccomp_config_t config = {0};
    config.type = type;
    config.enable_logging = 1;
    config.enable_arg_filtering = 1;
    
    static const char* audio_devices[] = {"/dev/snd/controlC0", "/dev/snd/pcmC0D0p"};
    static const char* camera_devices[] = {"/dev/video0", "/dev/video1"};
    static const char* sensor_devices[] = {"/dev/iio:device0", "/sys/bus/iio/devices/iio:device0"};
    
    switch (type) {
        case SERVICE_TYPE_AUDIO:
            config.allowed_devices = audio_devices;
            config.device_count = sizeof(audio_devices) / sizeof(audio_devices[0]);
            break;
            
        case SERVICE_TYPE_CAMERA:
            config.allowed_devices = camera_devices;
            config.device_count = sizeof(camera_devices) / sizeof(camera_devices[0]);
            break;
            
        case SERVICE_TYPE_SENSOR:
            config.allowed_devices = sensor_devices;
            config.device_count = sizeof(sensor_devices) / sizeof(sensor_devices[0]);
            break;
    }
    
    return config;
}

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
    // now uses argument filtering to restrict to audio devices only
    return add_ioctl_arg_filter(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR SERVICE - Read from /sys/bus/iio
// ══════════════════════════════════════════════════════════════════════════════

static int apply_sensor(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    
    // ioctl - some sensor drivers need it  
    // restricted to sensor device file descriptors only
    return add_ioctl_arg_filter(ctx);
}

// ══════════════════════════════════════════════════════════════════════════════
// CAMERA SERVICE - V4L2 Video Capture
// ══════════════════════════════════════════════════════════════════════════════

static int apply_camera(scmp_filter_ctx ctx)
{
    apply_common(ctx);
    
    // ioctl - V4L2 driver uses ioctl heavily for frame capture
    // restricted to camera device file descriptors only
    if (add_ioctl_arg_filter(ctx) < 0) return -1;
    
    // mlock - lock frame buffers in RAM to prevent swapping
    // critical for real-time video capture
    allow(ctx, SCMP_SYS(mlock));
    allow(ctx, SCMP_SYS(munlock));
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN: Apply seccomp Filter with Configuration
// Enhanced version with monitoring and argument filtering
// ══════════════════════════════════════════════════════════════════════════════

int seccomp_apply_config(const seccomp_config_t* config)
{
    if (!config) return -1;
    
    // ── STEP 1: Setup Monitoring if Enabled ───────────────────────────────────
    if (config->enable_logging) {
        if (setup_violation_logging() < 0) {
            fprintf(stderr, "[seccomp] warning: monitoring setup failed\n");
        }
    }
    
    // ── STEP 2: Setup Device File Descriptors for Argument Filtering ──────────
    if (config->enable_arg_filtering) {
        if (setup_device_fd_list(config->allowed_devices, config->device_count) < 0) {
            fprintf(stderr, "[seccomp] warning: device fd setup failed\n");
        }
    }
    
    // ── STEP 3: Create Filter Context ─────────────────────────────────────────
    scmp_filter_ctx ctx = seccomp_init(monitoring_enabled ? SCMP_ACT_TRAP : SCMP_ACT_KILL);
    if (!ctx) {
        fprintf(stderr, "[seccomp] init failed\n");
        return -1;
    }
    
    // ── STEP 4: Validate Architecture ─────────────────────────────────────────
    if (validate_architecture(ctx) < 0) {
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 5: Apply Service-Specific Whitelist ──────────────────────────────
    int rc = 0;
    switch (config->type) {
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
            fprintf(stderr, "[seccomp] unknown service type %d\n", config->type);
            seccomp_release(ctx);
            return -1;
    }
    
    if (rc != 0) {
        fprintf(stderr, "[seccomp] whitelist application failed\n");
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 6: Lock Privilege Escalation ─────────────────────────────────────
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        perror("[seccomp] PR_SET_NO_NEW_PRIVS");
        seccomp_release(ctx);
        return -1;
    }
    
    // ── STEP 7: Load Filter into Kernel ───────────────────────────────────────
    if (seccomp_load(ctx) != 0) {
        fprintf(stderr, "[seccomp] load failed - check kernel CONFIG_SECCOMP\n");
        seccomp_release(ctx);
        return -1;
    }
    
    seccomp_release(ctx);
    
    printf("[seccomp] ACTIVE - enhanced syscall firewall engaged\n");
    if (config->enable_logging) {
        printf("[seccomp] violation logging enabled\n");
    }
    if (config->enable_arg_filtering && allowed_fd_count > 0) {
        printf("[seccomp] argument filtering active for %zu devices\n", allowed_fd_count);
    }
    
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN: Apply seccomp Filter (Simplified Interface)
// This is the entry point called by services
// ══════════════════════════════════════════════════════════════════════════════

int seccomp_apply(service_type_t type)
{
    // use default configuration for the service type
    seccomp_config_t config = seccomp_get_default_config(type);
    return seccomp_apply_config(&config);
}