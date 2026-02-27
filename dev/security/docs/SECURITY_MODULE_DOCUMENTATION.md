# 🔐 Security Module - Complete Documentation

## Overview

یہ middleware کا security module ہے جو multiple layers of defense in depth فراہم کرتا ہے۔ اس میں پانچ major components ہیں:

1. **Security Manager** - Central orchestrator جو سب security layers کو manage کرتا ہے
2. **Capabilities** - Linux capabilities کا management (privilege control)
3. **Sandbox** - Process isolation using namespaces اور cgroups
4. **Seccomp** - System call filtering
5. **Verify** - Message authentication اور replay protection

---

## 📁 Directory Structure

```
security/
├── core/                    # Core security manager
│   ├── security_manager.c   # Main orchestrator
│   ├── security_manager.h   # Public API
│   └── sec_error.h          # Error codes
├── capabilities/            # Linux capabilities management
│   ├── capabilities.c       # Public API wrapper
│   ├── capabilities.h       # Public interface
│   ├── capabilities_core.c  # Core implementation
│   ├── capabilities_core.h  # Core functions
│   ├── capabilities_audit.c # Audit logging
│   ├── capabilities_audit.h # Audit API
│   ├── capabilities_policy.c# Service policies
│   └── capabilities_policy.h# Policy API
├── sandbox/                 # Process sandboxing
│   ├── sandbox.c            # Public API
│   ├── sandbox.h            # Public interface
│   ├── sandbox_core.c       # Namespace operations
│   ├── sandbox_core.h       # Core functions
│   ├── sandbox_cgroup.c     # cgroup management
│   ├── sandbox_cgroup.h     # cgroup API
│   ├── sandbox_mount.c      # Filesystem isolation
│   ├── sandbox_mount.h      # Mount API
│   ├── sandbox_network.c    # Network isolation
│   └── sandbox_network.h    # Network API
├── seccomp/                 # System call filtering
│   ├── seccomp_filter.c     # Main filter logic
│   ├── seccomp_filter.h     # Public API
│   ├── seccomp_core.c       # Core functionality
│   ├── seccomp_core.h       # Core functions
│   ├── seccomp_policy_*.c   # Service-specific policies
│   └── seccomp_policy_*.h   # Policy headers
└── verify/                  # Message verification
    ├── verify.c             # Main verification logic
    ├── verify.h             # Public API
    ├── verify_hmac.c        # HMAC operations
    ├── verify_hmac.h        # HMAC API
    ├── verify_replay.c      # Replay attack prevention
    ├── verify_replay.h      # Replay API
    ├── verify_token.c       # Service token management
    └── verify_token.h       # Token API
```

---

## 🎯 1. Security Manager (Core)

### Purpose / مقصد
Security Manager ایک central orchestrator ہے جو تمام security components کو ایک unified interface سے control کرتا ہے۔

### Files

#### `security_manager.h`
```c
// Security states - process کس level پر secure ہے
typedef enum {
    SEC_STATE_UNINITIALIZED = 0,      // کچھ apply نہیں ہوا
    SEC_STATE_SANDBOX_APPLIED,         // Sandbox active
    SEC_STATE_CAPABILITIES_APPLIED,    // Capabilities set
    SEC_STATE_SECCOMP_APPLIED,         // Syscall filter on
    SEC_STATE_VERIFY_INITIALIZED,      // Message auth ready
    SEC_STATE_FULLY_SECURED            // سب کچھ apply ہوگیا
} security_state_t;
```

### Key Concepts

**1. State Machine**
- Security layer by layer apply ہوتی ہے
- Seccomp سب سے آخر میں کیونکہ یہ irreversible ہے
- Order important ہے: Sandbox → Capabilities → Verify → Seccomp

**2. Configuration Structure**
```c
typedef struct {
    sandbox_config_t sandbox;           // Sandbox settings
    capabilities_config_t capabilities; // Capability settings
    seccomp_config_t seccomp;           // Seccomp policy
    const char* verify_key_file;        // HMAC key path
    int skip_sandbox;                   // Skip flags
    int skip_capabilities;
    int skip_seccomp;
    int skip_verify;
} security_full_config_t;
```

**3. Rollback Mechanism**
- اگر کوئی step fail ہو تو automatic rollback
- cgroups destroy ہوتے ہیں
- verify context cleanup
- Partial security state prevent

### Usage Example
```c
security_manager_t mgr;
security_manager_init(&mgr);

// Get default config for audio service
security_full_config_t config = security_manager_default_config("audio");

// Apply all security layers
if (security_manager_apply_all(&mgr, &config) < 0) {
    // Handle error
}

// Get current state
security_state_t state = security_manager_get_state(&mgr);

// Cleanup
security_manager_cleanup(&mgr);
```

### Error Handling (`sec_error.h`)
```c
typedef enum {
    SEC_OK                  =  0,   // Success
    SEC_ERR_INVALID_CONFIG  = -1,   // Bad configuration
    SEC_ERR_PERMISSION      = -2,   // Permission denied
    SEC_ERR_SYSCALL         = -3,   // System call failed
    SEC_ERR_MEMORY          = -4,   // Memory allocation failed
    SEC_ERR_NAMESPACE_FAIL  = -14,  // Namespace creation failed
    SEC_ERR_CGROUP_FAIL     = -15,  // cgroup operation failed
    SEC_ERR_SECCOMP_FAIL    = -16,  // Seccomp filter failed
    // ... more errors
} sec_error_t;
```

---

## 🔑 2. Capabilities Module

### Purpose / مقصد
Linux capabilities کا management۔ یہ root privileges کو fine-grained pieces میں توڑتا ہے۔

### Key Concept: Linux Capabilities کیا ہیں؟

Traditional Unix میں root vs non-root ہوتا ہے۔ Capabilities اس binary division کو توڑتی ہیں:

```
Root privileges کو pieces میں divide کیا گیا:
- CAP_SYS_RAWIO    : Raw I/O operations (hardware access)
- CAP_NET_BIND     : Bind to port < 1024
- CAP_SYS_ADMIN    : Many admin operations
- CAP_SETUID       : Change user ID
- CAP_KILL         : Send signals
- CAP_NET_RAW      : Raw sockets (ping etc)
```

### Capability Sets

```c
typedef enum {
    MCAP_NONE           = 0,        // No capabilities
    MCAP_SYS_RAWIO      = 1 << 0,   // Hardware access
    MCAP_NET_BIND       = 1 << 1,   // Privileged ports
    MCAP_SYS_ADMIN      = 1 << 2,   // Admin operations
    MCAP_SETUID         = 1 << 3,   // Change UID
    MCAP_KILL           = 1 << 4,   // Send signals
    MCAP_NET_RAW        = 1 << 5,   // Raw network
} cap_flags_t;
```

### Capability Configuration

```c
typedef struct {
    cap_flags_t effective;    // Currently active capabilities
    cap_flags_t permitted;    // Maximum allowed capabilities
    cap_flags_t inheritable;  // Passed to child processes
    cap_flags_t bounding;     // Hard limit on capabilities
    int enable_auditing;      // Log capability changes
    uid_t target_uid;         // Change to this user
    gid_t target_gid;         // Change to this group
} capabilities_config_t;
```

### Files Explanation

#### `capabilities_core.c` - Core Logic

**1. `capabilities_core_apply_config()`**
```c
// Applies complete capability configuration:
// 1. Set bounding set (hard limit)
// 2. Enable NO_NEW_PRIVS (prevent privilege escalation)
// 3. Drop root if running as root
// 4. Set effective/permitted/inheritable caps
```

**2. `set_capability_bounding_set()`**
```c
// Bounding set drops capabilities from ALL future operations
// یعنی اگر ایک capability bounding set سے drop ہوگئی
// تو وہ کبھی واپس نہیں آ سکتی - even with setuid binaries
```

**3. `PR_SET_NO_NEW_PRIVS`**
```c
// Critical security feature:
// - Process cannot gain new privileges
// - exec() of setuid binaries won't work
// - Seccomp لگانے سے پہلے ضروری ہے
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
```

#### `capabilities_policy.c` - Service Policies

Built-in policies for known services:
```c
// Audio service: needs raw I/O for sound hardware
{
    .service_name = "audio",
    .required_caps = MCAP_SYS_RAWIO,
    .recommended_uid = 1001
}

// Network service: needs network capabilities
{
    .service_name = "network",
    .required_caps = MCAP_NET_BIND | MCAP_NET_RAW,
    .recommended_uid = 1004
}
```

#### `capabilities_audit.c` - Audit Logging

```c
// Thread-safe audit logging for capability changes
// Log format: [timestamp] CAP_ACTION: flags=0x%x pid=%d uid=%d details
capabilities_audit_log("SET", caps, "applied configuration");
```

### Security Logic

```
1. Process starts as root
   ↓
2. Set bounding set (caps کی hard limit)
   ↓
3. Enable NO_NEW_PRIVS (privilege escalation روکیں)
   ↓
4. Change UID/GID (root چھوڑیں)
   ↓
5. Verify root واپس نہیں آ سکتا
   ↓
6. Set effective capabilities (صرف جو چاہیے)
```

---

## 📦 3. Sandbox Module

### Purpose / مقصد
Process isolation - process کو باقی system سے الگ کرنا۔

### Key Concepts

**1. Linux Namespaces**
```
Namespaces process کو اپنی isolated view دیتے ہیں:

PID namespace  → process اپنے PIDs دیکھتا ہے
NET namespace  → separate network stack
MNT namespace  → separate filesystem view
IPC namespace  → separate IPC resources
UTS namespace  → separate hostname
USER namespace → separate user IDs
```

**2. Control Groups (cgroups)**
```
Resource limits لگانے کا mechanism:

memory.max     → Maximum memory usage
cpu.max        → CPU time limit
cpu.weight     → CPU priority
io.weight      → I/O priority
pids.max       → Maximum processes
```

### Configuration Structure

```c
typedef struct {
    // Namespace flags
    int enable_pid_ns;      // Separate PIDs
    int enable_net_ns;      // Separate networking
    int enable_mount_ns;    // Separate filesystem
    int enable_ipc_ns;      // Separate IPC
    int enable_user_ns;     // Separate users
    int enable_uts_ns;      // Separate hostname

    // User mapping
    uid_t real_uid;         // Map to this external UID
    gid_t real_gid;         // Map to this external GID

    // Filesystem
    const char* chroot_path; // Optional chroot

    // Resource limits
    resource_limits_t limits;
    int enable_cgroups;
    const char* cgroup_name;

    // Network config
    network_config_t network;

    // Filesystem bindings
    filesystem_binding_t* bindings;
    size_t binding_count;
} sandbox_config_t;
```

### Files Explanation

#### `sandbox_core.c` - Namespace Operations

**1. `sandbox_core_apply_namespaces()`**
```c
// unshare() syscall creates new namespaces
int flags = 0;
if (cfg->enable_pid_ns)   flags |= CLONE_NEWPID;
if (cfg->enable_net_ns)   flags |= CLONE_NEWNET;
if (cfg->enable_mount_ns) flags |= CLONE_NEWNS;
unshare(flags);
```

**2. `sandbox_core_setup_user_namespace()`**
```c
// User namespace mapping:
// Process کے اندر UID 0 ہے لیکن باہر کوئی اور
// /proc/self/uid_map میں mapping لکھتے ہیں
snprintf(map, "0 %u 1", real_uid);  // Inside:0 → Outside:real_uid
```

**3. Service Configurations**
```c
static const struct service_configs[] = {
    {"audio",   {512MB RAM, 25% CPU, 10 PIDs}, network: off},
    {"camera",  {1GB RAM,   50% CPU, 5 PIDs},  network: off},
    {"network", {512MB RAM, 30% CPU, 20 PIDs}, network: on}
};
```

#### `sandbox_cgroup.c` - Resource Control

```c
// cgroup کے ذریعے resource limits:

// Memory limit set کرنا
snprintf(value, "%lu", limits->memory_limit_mb * 1024 * 1024);
write_cgroup_file(cgroup_name, "memory.max", value);

// CPU quota set کرنا (percentage of one CPU)
snprintf(value, "%d 100000", limits->cpu_quota_percent * 1000);
write_cgroup_file(cgroup_name, "cpu.max", value);

// Process limit
snprintf(value, "%u", limits->pids_limit);
write_cgroup_file(cgroup_name, "pids.max", value);
```

#### `sandbox_mount.c` - Filesystem Isolation

**1. `sandbox_mount_setup_filesystem()`**
```c
// Minimal isolated filesystem create کرتا ہے:
// 1. tmpfs mount کریں
// 2. Essential directories بنائیں (tmp, proc, sys)
// 3. Minimal /dev create کریں (null, zero, urandom)
// 4. pivot_root() call کریں
// 5. Old root unmount کریں
```

**2. `create_minimal_dev()`**
```c
// صرف essential devices:
mknod("/dev/null",    makedev(1, 3));  // Discard output
mknod("/dev/zero",    makedev(1, 5));  // Read zeros
mknod("/dev/urandom", makedev(1, 9));  // Random data
```

**3. `pivot_root()`**
```c
// نئی root کو actual root بنانا:
syscall(SYS_pivot_root, new_root, old_root);
// اب process نئی root میں ہے
// پرانی root unmount کردیں
```

#### `sandbox_network.c` - Network Isolation

```c
// Network namespace میں صرف loopback interface ہوتا ہے
// باقی سب isolated ہے

// Loopback up کرنا
struct ifreq ifr;
strncpy(ifr.ifr_name, "lo", IFNAMSIZ);
ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
ioctl(sock, SIOCSIFFLAGS, &ifr);
```

### Sandbox Security Flow

```
Process Start
     ↓
CGroup Create (resource limits)
     ↓
Add process to cgroup
     ↓
unshare() - create namespaces
     ↓
User namespace mapping (if enabled)
     ↓
Mount namespace setup
     ↓
pivot_root() - change filesystem root
     ↓
Network namespace isolation
     ↓
Process isolated!
```

---

## 🛡️ 4. Seccomp Module

### Purpose / مقصد
System call filtering - process کو صرف specific syscalls کرنے دیں۔

### Key Concept: Seccomp کیا ہے؟

```
Seccomp (Secure Computing Mode):
- Kernel level syscall filter
- Process کے لیے allowlist of syscalls
- غیر allowed syscall → process killed OR logged
- Irreversible - once set, cannot be removed
```

### Configuration

```c
typedef enum {
    SERVICE_TYPE_AUDIO   = 0,  // Audio service policy
    SERVICE_TYPE_SENSOR  = 1,  // Sensor service policy
    SERVICE_TYPE_CAMERA  = 2,  // Camera service policy
    SERVICE_TYPE_NETWORK = 3,  // Network service policy
    SERVICE_TYPE_MINIMAL = 4,  // Most restrictive
} service_type_t;

typedef struct {
    service_type_t type;
    int enable_logging;         // Log violations
    int enable_arg_filtering;   // Filter syscall arguments
    const char** allowed_devices;
    size_t device_count;
} seccomp_config_t;
```

### Files Explanation

#### `seccomp_filter.c` - Main Logic

```c
int seccomp_apply_config(const seccomp_config_t* config) {
    // 1. Setup violation handler (SIGSYS)
    if (config->enable_logging)
        seccomp_core_setup_violation_handler();

    // 2. Setup device FD filtering
    if (config->enable_arg_filtering)
        seccomp_core_setup_device_fds(config->allowed_devices, ...);

    // 3. Create filter context
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);

    // 4. Apply service-specific policy
    switch (config->type) {
        case SERVICE_TYPE_AUDIO:
            seccomp_policy_audio_apply(ctx);
            break;
        // ...
    }

    // 5. Set NO_NEW_PRIVS
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    // 6. Load filter into kernel
    seccomp_load(ctx);
}
```

#### `seccomp_core.c` - Core Functions

**1. `seccomp_core_apply_common()` - Basic syscalls**
```c
// ہر process کو یہ چاہیے:
seccomp_core_allow(ctx, SCMP_SYS(read));
seccomp_core_allow(ctx, SCMP_SYS(write));
seccomp_core_allow(ctx, SCMP_SYS(open));
seccomp_core_allow(ctx, SCMP_SYS(close));
seccomp_core_allow(ctx, SCMP_SYS(mmap));
seccomp_core_allow(ctx, SCMP_SYS(exit));
// ... etc
```

**2. `seccomp_core_allow_networking()` - Network syscalls**
```c
// صرف network services کو چاہیے:
seccomp_core_allow(ctx, SCMP_SYS(socket));
seccomp_core_allow(ctx, SCMP_SYS(connect));
seccomp_core_allow(ctx, SCMP_SYS(bind));
seccomp_core_allow(ctx, SCMP_SYS(listen));
seccomp_core_allow(ctx, SCMP_SYS(accept));
// ... etc
```

**3. Violation Handler**
```c
static void handle_sigsys(int sig, siginfo_t* info, void* context) {
    // Log which syscall was blocked
    // Log format: SECCOMP_VIOLATION: syscall=X pid=Y
    write(log_fd, sig_buf, pos);
    _exit(139);  // Exit with signal indication
}
```

**4. `seccomp_core_add_ioctl_filter()` - Device FD filtering**
```c
// ioctl صرف specific file descriptors پر allow
for (size_t i = 0; i < allowed_fd_count; i++) {
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioctl), 1,
                     SCMP_A0(SCMP_CMP_EQ, allowed_fds[i]));
}
```

### Service-Specific Policies

#### Audio Policy
```c
// Audio service کو:
// - Common syscalls ✓
// - ioctl on audio devices ✓
// Allowed devices: /dev/snd/controlC0, /dev/snd/pcmC0D0p
```

#### Camera Policy
```c
// Camera service کو:
// - Common syscalls ✓
// - ioctl on video devices ✓
// - mlock/munlock (memory pinning for DMA)
// Allowed devices: /dev/video0, /dev/video1
```

#### Network Policy
```c
// Network service کو:
// - Common syscalls ✓
// - All networking syscalls ✓
// - poll, select, ppoll
// Allowed devices: /dev/net/tun
```

#### Minimal Policy
```c
// Most restrictive:
// - صرف common syscalls
// - No device access
// - No networking
```

### Seccomp Security Flow

```
Configuration received
         ↓
Setup SIGSYS handler (violation logging)
         ↓
Open allowed device FDs
         ↓
Create seccomp filter context
         ↓
Add allowed syscalls (whitelist)
         ↓
Add device-specific ioctl rules
         ↓
Set NO_NEW_PRIVS
         ↓
Load filter into kernel
         ↓
Cleanup device FDs
         ↓
Filter active - violations → KILL/TRAP
```

---

## ✅ 5. Verify Module

### Purpose / مقصد
Message authentication - messages کی integrity اور authenticity verify کرنا۔

### Key Concepts

**1. HMAC (Hash-based Message Authentication Code)**
```
HMAC message کی integrity اور authenticity prove کرتا ہے:
- Secret key سے message کا hash بنتا ہے
- Sender اور receiver دونوں کے پاس same key
- Message tamper ہو تو HMAC match نہیں کرتا
```

**2. Replay Attack Prevention**
```
Replay attack: Valid message کو capture کرکے دوبارہ بھیجنا
Prevention mechanisms:
- Timestamp: پرانے messages reject
- Sequence number: Out-of-order rejection
- Nonce: Random value to ensure uniqueness
```

**3. Service Tokens**
```
Service کو permission دینے کا mechanism:
- Service name
- Permissions (bitmap)
- Issue time اور expiry time
- HMAC signature
```

### Data Structures

```c
// Message authentication data
typedef struct {
    uint8_t  hmac[32];      // HMAC-SHA256 signature
    uint64_t timestamp;     // When message was created
    uint32_t nonce;         // Random value
    uint32_t sequence;      // Sequential number
} message_auth_t;

// Service token
typedef struct {
    char     service_name[64];
    uint8_t  token[32];      // HMAC signature
    time_t   issued_at;
    time_t   expires_at;
    uint32_t permissions;    // PERM_* flags
} service_token_t;

// Permissions
typedef enum {
    PERM_NONE           = 0,
    PERM_REGISTER       = 1 << 0,  // Register services
    PERM_LOOKUP         = 1 << 1,  // Lookup services
    PERM_SEND_MSG       = 1 << 2,  // Send messages
    PERM_RECV_MSG       = 1 << 3,  // Receive messages
    PERM_CREATE_BUFFER  = 1 << 4,  // Create shared buffers
    PERM_ADMIN          = 1 << 5,  // Administrative access
} service_permissions_t;
```

### Files Explanation

#### `verify.c` - Main Logic

**1. `verify_init_from_file()`**
```c
// Key file سے initialization:
FILE* f = fopen(key_file, "rb");
key_len = fread(key, 1, MAX_KEY_SIZE, f);
verify_init(ctx, key, key_len);
secure_zero(key, sizeof(key));  // Memory سے key صاف کریں
```

**2. `verify_sign_message()`**
```c
// Message sign کرنا:
auth->timestamp = get_monotonic_time();
auth->nonce = generate_random_nonce();
auth->sequence = last_sequence + 1;

// Sign: timestamp + nonce + sequence + data
hmac_sign(key, key_len, sign_data, total_len, auth->hmac);
```

**3. `verify_check_message()`**
```c
// Message verify کرنا:
// 1. Replay check (timestamp + sequence)
int replay_result = replay_check(&ctx->replay_ctx, timestamp, sequence);
if (replay_result != 1) return 0;  // Replay detected!

// 2. HMAC verify
int result = hmac_verify(key, key_len, sign_data, total_len, auth->hmac);

// 3. Update replay context
if (result == 1)
    replay_update(&ctx->replay_ctx, timestamp, sequence);
```

**4. `verify_rotate_key()`**
```c
// Key rotation:
memcpy(ctx->prev_key, ctx->master_key);  // پرانی key backup
memcpy(ctx->master_key, new_key);         // نئی key set
ctx->key_generation++;                     // Generation increment
// Previous key رکھنے سے in-flight messages handle ہوتے ہیں
```

#### `verify_replay.c` - Replay Protection

```c
int replay_check(replay_context_t* ctx, uint64_t timestamp, uint32_t sequence) {
    uint64_t now = get_monotonic_time();

    // 1. Timestamp check: message زیادہ پرانا نہ ہو
    if ((now - timestamp) > ctx->timestamp_window_sec) {
        return 0;  // Too old!
    }

    // 2. Future check: message future میں نہ ہو
    if (timestamp > now + ctx->timestamp_window_sec) {
        return 0;  // From future!
    }

    // 3. Sequence check: out-of-order نہ ہو
    if (ctx->strict_ordering && sequence <= ctx->last_sequence) {
        return 0;  // Already seen or old!
    }

    return 1;  // OK
}
```

#### `verify_token.c` - Service Tokens

```c
int token_issue(verify_context_t* ctx, const char* service_name,
                uint32_t permissions, uint32_t validity_seconds,
                service_token_t* token) {
    // 1. Fill token fields
    strcpy(token->service_name, service_name);
    token->issued_at = get_monotonic_time();
    token->expires_at = token->issued_at + validity_seconds;
    token->permissions = permissions;

    // 2. Create data buffer: name + issued + expires + permissions
    // 3. Sign with HMAC
    hmac_sign(ctx->master_key, ctx->master_key_len, data, data_len, token->token);
}

int token_verify(verify_context_t* ctx, const service_token_t* token) {
    // 1. Check expiry
    if (now >= token->expires_at)
        return 0;  // Expired!

    // 2. Recreate data buffer
    // 3. Verify HMAC
    return hmac_verify(key, key_len, data, data_len, token->token);
}
```

#### `verify_hmac.c` - HMAC Operations

```c
int hmac_compare(const uint8_t* hmac1, const uint8_t* hmac2) {
    // Constant-time comparison (timing attack prevention)
    uint8_t diff = 0;
    for (int i = 0; i < HMAC_SHA256_SIZE; i++) {
        diff |= (hmac1[i] ^ hmac2[i]);
    }
    return diff == 0 ? 1 : 0;
}
```

### Security Features

**1. Secure Memory Handling**
```c
static void secure_zero(void* ptr, size_t len) {
    volatile unsigned char* p = ptr;  // volatile prevents optimization
    while (len--) *p++ = 0;
}
// Keys اور sensitive data memory سے properly صاف ہوتا ہے
```

**2. Rate Limiting**
```c
// DOS attack prevention:
if (ctx->rate_count >= ctx->rate_limit_per_sec) {
    return 0;  // Rate limit exceeded
}
ctx->rate_count++;
```

**3. Key Rotation**
```c
// Previous key رکھنا allows:
// - Graceful key rotation
// - In-flight messages still work
// Key generation tracking
```

### Verify Security Flow

```
Service wants to send authenticated message
                  ↓
Generate nonce (random value)
                  ↓
Get timestamp (monotonic clock)
                  ↓
Increment sequence number
                  ↓
Create signing data: timestamp + nonce + seq + message
                  ↓
Compute HMAC-SHA256 with master key
                  ↓
Attach auth header to message
                  ↓
                Send
                  ↓
           ================
                  ↓
           Receiver side
                  ↓
Check timestamp (not too old/future)
                  ↓
Check sequence (not replay)
                  ↓
Recompute HMAC with same key
                  ↓
Constant-time compare HMACs
                  ↓
Update replay context on success
                  ↓
Message accepted/rejected
```

---

## 🔄 Complete Security Application Flow

```
                    Service Start
                         ↓
                ┌────────────────────┐
                │ Security Manager   │
                │    Initialize      │
                └────────┬───────────┘
                         ↓
        ┌────────────────────────────────┐
        │  1. SANDBOX APPLICATION        │
        │  - Create cgroup               │
        │  - Set resource limits         │
        │  - Create namespaces           │
        │  - Setup isolated filesystem   │
        │  - Isolate network             │
        └────────────────┬───────────────┘
                         ↓
        ┌────────────────────────────────┐
        │  2. CAPABILITIES APPLICATION   │
        │  - Set bounding set            │
        │  - Enable NO_NEW_PRIVS         │
        │  - Drop root privileges        │
        │  - Set effective caps          │
        │  - Start audit logging         │
        └────────────────┬───────────────┘
                         ↓
        ┌────────────────────────────────┐
        │  3. VERIFY INITIALIZATION      │
        │  - Load HMAC key               │
        │  - Initialize replay context   │
        │  - Setup rate limiting         │
        └────────────────┬───────────────┘
                         ↓
        ┌────────────────────────────────┐
        │  4. SECCOMP APPLICATION        │
        │  - Setup violation handler     │
        │  - Create syscall whitelist    │
        │  - Load filter into kernel     │
        │  - Filter is now ACTIVE        │
        └────────────────┬───────────────┘
                         ↓
                ┌────────────────────┐
                │   FULLY SECURED    │
                │   Service Ready    │
                └────────────────────┘
```

---

## 📊 Service-Specific Security Profiles

| Service | Caps | Sandbox | Seccomp | Network |
|---------|------|---------|---------|---------|
| **Audio** | SYS_RAWIO | 512MB, 25% CPU, 10 PIDs | Common + ioctl(audio) | ❌ |
| **Camera** | SYS_RAWIO | 1GB, 50% CPU, 5 PIDs | Common + ioctl(video) + mlock | ❌ |
| **Sensor** | None/SYS_RAWIO | 256MB, 10% CPU, 5 PIDs | Common + ioctl(iio) | ❌ |
| **Network** | NET_BIND, NET_RAW | 512MB, 30% CPU, 20 PIDs | Common + all networking | ✅ |
| **GPIO** | SYS_RAWIO | Default | Common + ioctl | ❌ |

---

## 🛠️ Best Practices / بہترین طریقے

1. **Order matters**: Sandbox → Caps → Verify → Seccomp
2. **Always use PR_SET_NO_NEW_PRIVS** before seccomp
3. **Drop root as early as possible**
4. **Use service-specific policies** instead of generic
5. **Enable auditing** in production
6. **Rotate keys regularly** 
7. **Set appropriate rate limits**
8. **Always cleanup** on exit

---

## 🚨 Security Warnings

1. **Seccomp is irreversible** - once loaded, cannot be removed
2. **Capability bounding set** - dropped caps cannot return
3. **NO_NEW_PRIVS** - setuid binaries stop working
4. **Key security** - protect key files (0600 permissions)
5. **Replay window** - set appropriate timestamp window
6. **Memory wiping** - always use secure_zero for sensitive data

---

## 📚 References

- Linux Capabilities: `man 7 capabilities`
- Namespaces: `man 7 namespaces`
- cgroups v2: `/sys/fs/cgroup` documentation
- Seccomp: `man 2 seccomp`
- HMAC: RFC 2104

---

*Document generated for middleware security module*
*Last updated: 2026*
