# Security Folder - Complete Technical Documentation

## Overview: What Is This Security System?

This security folder contains a **3-layer defense system** that protects the middleware services from attacks and limits damage if something goes wrong. Think of it like a fortress with multiple walls:

1. **Sandbox** - Locks each service in its own isolated room
2. **Capabilities** - Takes away dangerous powers from processes 
3. **Seccomp Filter** - Acts like a firewall for system calls

**Why We Need This:**
- Services handle hardware (audio, camera, sensors) - if hacked, attacker could spy on user
- Services run as root initially - need to remove dangerous privileges
- Services should only do their specific job - nothing extra
- If one service gets compromised, it shouldn't affect others

## File-by-File Analysis

### 1. seccomp_filter.h + seccomp_filter.c
**Purpose:** Syscall Firewall - Controls exactly which system calls each service can make

#### What's Inside:
```c
enum service_type_t {
    SERVICE_TYPE_AUDIO,  
    SERVICE_TYPE_SENSOR, 
    SERVICE_TYPE_CAMERA
}
```
- Defines 3 types of services that need different permissions

#### How It Works:
1. **Whitelist Approach:** By default, ALL syscalls are BLOCKED (SCMP_ACT_KILL)
2. **Service-Specific Lists:** Each service type gets only the syscalls it needs:
   - **Audio Service:** Basic syscalls + ioctl() for sound hardware
   - **Sensor Service:** Basic syscalls + ioctl() for sensor data
   - **Camera Service:** Basic syscalls + ioctl() + mlock() for video buffers

3. **Common Syscalls** (all services get these):
   - File I/O: read(), write(), open(), close()
   - Memory: mmap(), munmap() (for ring buffer)
   - Timing: clock_gettime(), nanosleep()
   - Signals: rt_sigaction() (for shutdown)
   - Event Loop: epoll_wait(), eventfd2()
   - Unix Socket: socket(), connect() (talk to service manager)
   - Process: getpid(), exit()

4. **Blocked Syscalls** (NONE of the services can use):
   - fork() - can't create new processes
   - execve() - can't run other programs  
   - ptrace() - can't debug/inject into other processes
   - kill() - can't send signals to other processes

#### What's Implemented: ✅
- Architecture validation (prevents 32-bit syscall bypass)
- Service-specific whitelists
- Proper filter loading with libseccomp
- NO_NEW_PRIVS protection

#### What's Missing: ⚠️
- **Argument filtering** - Currently ioctl() is allowed for ANY file descriptor
- Should restrict ioctl() to only audio/camera device files
- **Runtime syscall monitoring** - no logging of blocked attempts
- **Dynamic filter updates** - once applied, can't be changed

#### Security Level: **High** 
Better than Android because Android uses SCMP_ACT_TRAP (returns error) but we use SCMP_ACT_KILL (terminates process immediately). No second chances.

---

### 2. capabilities.h + capabilities.c  
**Purpose:** Privilege Control - Removes dangerous root powers from processes

#### What's Inside:
```c
typedef enum {
    CAP_NONE           = 0,
    CAP_SYS_RAWIO      = 1 << 0,  // direct hardware I/O
    CAP_NET_BIND       = 1 << 1,  // bind to ports < 1024  
    CAP_SYS_ADMIN      = 1 << 2,  // admin operations
    CAP_SETUID         = 1 << 3,  // change user ID
} cap_flags_t;
```

#### How It Works:
1. **Start as Root:** Services initially start with root privileges (needed to access hardware)
2. **Drop to User:** Changes to uid=1000, gid=1000 (unprivileged user)
3. **Keep Specific Capabilities:** Only keeps the minimum capabilities the service actually needs
4. **Lock Escalation:** Sets PR_SET_NO_NEW_PRIVS so process can never regain privileges

#### Example Flow:
```
Audio Service starts as root → drops to uid=1000 → keeps only CAP_SYS_RAWIO → locks escalation
Result: Can access /dev/snd but cannot read /etc/passwd or kill other processes
```

#### What's Implemented: ✅
- Direct kernel capset() syscalls (not using libcap library)
- Proper capability bit manipulation
- User/group dropping
- NO_NEW_PRIVS protection
- Verification that root cannot be regained

#### What's Missing: ⚠️  
- **Capability bounding set** - should limit inheritable capabilities
- **Individual capability control** - currently uses bitmap approach
- **Capability auditing** - no logging of what capabilities are actually being used
- **Service-specific defaults** - each service type should have predefined capability needs

#### Security Level: **Medium-High**
Better than running as root, but could be more fine-grained. Android's system is similar but more automated.

---

### 3. sandbox.h + sandbox.c
**Purpose:** Process Isolation - Creates isolated environments using Linux namespaces

#### What's Inside:
```c
typedef struct {
    int  enable_pid_ns;      // isolate process tree
    int  enable_net_ns;      // isolate network  
    int  enable_mount_ns;    // isolate filesystem
    int  enable_ipc_ns;      // isolate shared memory
    int  enable_user_ns;     // map uid/gid
    uid_t real_uid;          // external user ID
    gid_t real_gid;          // external group ID  
    const char* chroot_path; // filesystem restriction
} sandbox_config_t;
```

#### How It Works:

**1. Namespace Isolation:**
- **PID Namespace:** Service sees only its own processes (getpid() returns 1)
- **Network Namespace:** Service has no network access (isolated network stack)
- **Mount Namespace:** Service gets its own filesystem view
- **IPC Namespace:** Service can't see shared memory from other processes
- **User Namespace:** Service thinks it's root inside but is unprivileged outside

**2. Filesystem Isolation:**
- Creates tmpfs as new root filesystem
- Only includes essential directories: /tmp, /proc, /sys, /dev
- Minimal /dev with only: /dev/null, /dev/zero, /dev/urandom
- Uses pivot_root() (more secure than chroot - cannot be escaped)

**3. User Mapping:**
- Maps internal uid=0 (root inside container) → external uid=1000 (unprivileged)
- Service feels like root but kernel knows it's not

#### What's Implemented: ✅
- All 5 namespace types
- Minimal filesystem with tmpfs
- Secure device nodes creation
- User/group ID mapping
- pivot_root() for inescapable filesystem jail
- Configuration validation

#### What's Missing: ⚠️
- **Resource limits** - no cgroup integration for CPU/memory limits
- **Network isolation bypass** - no way to selectively allow network access
- **File system binding** - can't mount specific host directories inside sandbox
- **Container runtime integration** - no Docker/containerd compatibility
- **Namespace persistence** - namespaces die when process dies

#### Security Level: **Medium**
Good isolation but missing resource controls. Docker/LXC containers are more complete but this is sufficient for our middleware use case.

#### Real-World Comparison:
- **Docker:** More features but heavier
- **Android:** Similar approach but uses different namespace combinations
- **SystemD:** Uses similar techniques for service isolation  
- **Our System:** Simpler but focused on middleware needs

---

## How Everything Works Together

### Service Startup Security Flow:
```
1. Service starts as root
2. sandbox_apply() → Creates namespaces, isolates filesystem
3. capabilities_drop_except() → Removes dangerous privileges  
4. seccomp_apply() → Activates syscall firewall
5. Service runs in secure environment
```

**Layered Defense:**
- If attacker breaks out of seccomp → still trapped in sandbox
- If attacker breaks out of sandbox → still has no dangerous capabilities
- If attacker gains capabilities → still limited by seccomp syscalls

### Current Status: What's Working vs What's Missing

#### ✅ IMPLEMENTED AND WORKING:
- **Syscall filtering** with service-specific whitelists
- **Privilege dropping** with capability control
- **Namespace isolation** with filesystem jailing
- **Architecture validation** preventing bypass attacks
- **Memory-safe path handling** (fixed truncation warnings)

#### ⚠️ MISSING OR INCOMPLETE:
- **Resource limits** - services could consume all CPU/RAM
- **Audit logging** - no tracking of security events
- **Dynamic reconfiguration** - security settings are permanent
- **Integration testing** - no automated security tests
- **Attack surface analysis** - haven't measured actual attack reduction

#### 🐛 KNOWN ISSUES:
- **ioctl() too permissive** - should be restricted to device-specific file descriptors
- **Error handling incomplete** - some failure paths don't clean up properly  
- **Documentation gaps** - missing deployment and configuration guides
- **Performance impact unknown** - haven't measured syscall filtering overhead

## Security Assessment

### Strengths:
1. **Multiple defense layers** - attacker must break through 3 different systems
2. **Principle of least privilege** - services get minimal permissions needed
3. **Industry-standard techniques** - using same approaches as Docker, Android
4. **Kill-on-violation** - immediate termination on security violation
5. **No privilege escalation** - locked down permanently at startup

### Weaknesses:
1. **Resource exhaustion possible** - no CPU/memory limits
2. **Coarse-grained filtering** - syscall arguments not validated  
3. **No runtime monitoring** - security violations not logged
4. **Configuration complexity** - easy to misconfigure and break security

### Comparison to Real-World Systems:

| Feature | Our System | Android | Docker | SystemD |
|---------|------------|---------|--------|---------|
| Syscall Filtering | ✅ seccomp | ✅ seccomp | ✅ seccomp | ❌ |
| Namespace Isolation | ✅ 5 types | ✅ selected | ✅ all | ✅ some |
| Capability Control | ✅ granular | ✅ predefined | ✅ flexible | ✅ basic |
| Resource Limits | ❌ missing | ✅ cgroups | ✅ cgroups | ✅ cgroups |
| Attack Logging | ❌ missing | ✅ logcat | ✅ auditd | ✅ journald |

**Security Rating: 7/10**
- Strong foundation but missing operational features
- Good for prototype/development, needs hardening for production

## Usage Guide

### For Audio Service:
```c
sandbox_config_t cfg = sandbox_default_config();
cfg.enable_pid_ns = 1;
cfg.enable_mount_ns = 1;
sandbox_apply(&cfg);

capabilities_drop_except(CAP_SYS_RAWIO);
seccomp_apply(SERVICE_TYPE_AUDIO);
// Now service can only access audio hardware
```

### For Sensor Service:
```c
// Similar but different capability needs
capabilities_drop_except(CAP_NONE);  // sensors don't need special caps
seccomp_apply(SERVICE_TYPE_SENSOR);
```

## Future Improvements Needed

### High Priority:
1. **Add cgroup integration** for resource limits
2. **Implement syscall argument filtering** 
3. **Add security event logging**
4. **Create automated security tests**

### Medium Priority:
1. **Performance benchmarking** of security overhead
2. **Configuration management** system
3. **Runtime security monitoring**
4. **Attack simulation testing**

### Low Priority:
1. **Container runtime compatibility**
2. **SELinux/AppArmor integration** 
3. **Hardware security module** support
4. **Formal security verification**

---

*This documentation covers the current state as of the codebase. The security system is functional for development but requires additional hardening for production deployment.*