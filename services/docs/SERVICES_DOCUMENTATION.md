# Services Layer — Complete Technical Documentation

**Project:** Secure Linux Middleware  
**Module:** `services/`  
**Language:** C (C11), POSIX  
**Build System:** CMake 3.28+  
**Version:** 2.0.0  
**Last Updated:** March 2026

---

## Table of Contents

1. [Overview & Architecture](#1-overview--architecture)
2. [Directory Structure](#2-directory-structure)
3. [Common Layer](#3-common-layer)
   - 3.1 [service_base — Daemon Lifecycle](#31-service_base--daemon-lifecycle)
   - 3.2 [service_ipc — Service Manager Communication](#32-service_ipc--service-manager-communication)
   - 3.3 [service_config — INI Configuration Parser](#33-service_config--ini-configuration-parser)
4. [Audio Service](#4-audio-service)
5. [Camera Service](#5-camera-service)
6. [Sensor Service](#6-sensor-service)
7. [GPIO Service](#7-gpio-service)
8. [Testing Infrastructure](#8-testing-infrastructure)
   - 8.1 [Mock HAL](#81-mock-hal)
   - 8.2 [Mock Service Manager](#82-mock-service-manager)
   - 8.3 [Unit Tests](#83-unit-tests)
   - 8.4 [Integration Tests](#84-integration-tests)
9. [Build System](#9-build-system)
10. [Wire Protocol](#10-wire-protocol)
11. [Security Model](#11-security-model)
12. [Signal Handling](#12-signal-handling)
13. [How to Build and Run](#13-how-to-build-and-run)
14. [Error Codes Reference](#14-error-codes-reference)

---

## 1. Overview & Architecture

The `services/` module implements four long-running Linux daemon processes that sit between the hardware (via the HAL layer) and the rest of the system. Each daemon follows an identical life-cycle pattern:

```
┌─────────────────────────────────────────────────────────┐
│                   Service Daemon (e.g. audio_service)   │
│                                                         │
│   main()                                                │
│    │                                                    │
│    ├─► service_base_init()       — context setup        │
│    ├─► service_config_load()     — parse .conf file     │
│    ├─► service_base_install_signals() — SIGTERM/SIGHUP  │
│    ├─► service_base_daemonize()  — double-fork (optional)│
│    ├─► service_base_write_pid()  — /run/<name>.pid      │
│    ├─► service_base_open_log()   — syslog + file sink   │
│    ├─► *_service_apply_security() — capabilities/seccomp│
│    ├─► *_service_hal_init()      — open HAL device      │
│    ├─► service_ipc_connect()     — connect to SM socket │
│    ├─► service_ipc_register()    — send REGISTER + auth │
│    └─► *_service_run_loop()      — epoll event loop     │
│              │                                          │
│              ├─► epoll timeout → heartbeat / ping       │
│              ├─► SM commands (HEALTH_CHECK, SHUTDOWN…)  │
│              └─► HAL work (read/write/interrupt…)       │
│                                                         │
│   shutdown:                                             │
│    ├─► service_ipc_unregister()                         │
│    ├─► *_service_hal_cleanup()  — stop→close→destroy   │
│    ├─► service_base_remove_pid()                        │
│    └─► service_base_close_log()                         │
└─────────────────────────────────────────────────────────┘
```

**Key design principles:**

| Principle | Implementation |
|---|---|
| Single responsibility | Each `.c` file handles one concern (HAL, IPC, security, loop) |
| No global mutable state | Everything threaded through `*_service_ctx_t` |
| Testability | All HAL calls go through the `hw_device_t` vtable (`ops->read/write/control`) so mock devices work identically to real ones |
| Security-first | Capabilities are dropped and seccomp filters applied before any network or HAL operations |
| Crash isolation | Each service is a separate process; one crash cannot affect others |

---

## 2. Directory Structure

```
services/
│
├── CMakeLists.txt               # Root build — defines all library and executable targets
│
├── common/                      # Shared infrastructure (compiled into service_common static lib)
│   ├── service_base.h / .c      # Daemon lifecycle, logging, signal handling
│   ├── service_ipc.h / .c       # Service Manager IPC (register, heartbeat, health, ping)
│   └── service_config.h / .c    # INI file parser (section/key/value)
│
├── audio_service/               # ALSA PCM audio daemon
│   ├── audio_service.h          # Master context struct + lifecycle API
│   ├── audio_service_main.c     # Entry point, arg parsing, startup/shutdown orchestration
│   ├── audio_service_hal.c/.h   # HAL integration (init, read, write, stop, cleanup)
│   ├── audio_service_loop.c/.h  # epoll event loop (heartbeat, SM commands, PCM I/O)
│   ├── audio_service_security.c/.h  # Capability drop + seccomp filter application
│   ├── audio_service.conf       # Default runtime configuration file
│   ├── audio_service.service    # systemd unit file
│   └── CMakeLists.txt           # Per-service build target
│
├── camera_service/              # V4L2 camera capture daemon
│   ├── camera_service.h
│   ├── camera_service_main.c
│   ├── camera_service_hal.c/.h
│   ├── camera_service_loop.c/.h
│   ├── camera_service_security.c/.h
│   ├── camera_service.conf
│   ├── camera_service.service
│   └── CMakeLists.txt
│
├── sensor_service/              # IIO sensor streaming daemon
│   ├── sensor_service.h
│   ├── sensor_service_main.c
│   ├── sensor_service_hal.c/.h
│   ├── sensor_service_loop.c/.h
│   ├── sensor_service_security.c/.h
│   ├── sensor_service.conf
│   ├── sensor_service.service
│   └── CMakeLists.txt
│
├── gpio_service/                # sysfs/libgpiod GPIO control daemon
│   ├── gpio_service.h
│   ├── gpio_service_main.c
│   ├── gpio_service_hal.c/.h
│   ├── gpio_service_loop.c/.h
│   ├── gpio_service_security.c/.h
│   ├── gpio_service.conf
│   ├── gpio_service.service
│   └── CMakeLists.txt
│
├── testing/                     # All test code (unit + integration)
│   ├── CMakeLists.txt           # Defines all 10 test executables
│   ├── mocks/
│   │   ├── mock_hal.c/.h        # In-process fake hw_device_t (vtable-based)
│   │   └── mock_sm.c/.h         # Threaded Unix socket Service Manager stub
│   ├── unit/
│   │   ├── test_service_base.c  # 14 tests — daemon lifecycle
│   │   ├── test_service_ipc.c   # 11 tests — IPC wire protocol
│   │   ├── test_audio_hal_layer.c   # 10 tests — audio HAL integration layer
│   │   ├── test_camera_hal_layer.c  # 8 tests  — camera HAL integration layer
│   │   ├── test_sensor_hal_layer.c  # 8 tests  — sensor HAL integration layer
│   │   └── test_gpio_hal_layer.c    # 9 tests  — GPIO HAL integration layer
│   └── integration/
│       ├── test_audio_full.c    # 5 tests — full audio lifecycle + SM
│       ├── test_camera_full.c   # 5 tests — full camera lifecycle + SM
│       ├── test_sensor_full.c   # 5 tests — full sensor lifecycle + SM
│       └── test_gpio_full.c     # 6 tests — full GPIO lifecycle + SM
│
└── docs/                        # ← You are here
    └── SERVICES_DOCUMENTATION.md
```

---

## 3. Common Layer

The `common/` directory contains three files that are compiled once into the `service_common` static library and linked into every service executable and every test binary. Nothing in `common/` depends on any specific service — it provides the foundation that all services build on top of.

---

### 3.1 `service_base` — Daemon Lifecycle

**Files:** `common/service_base.h`, `common/service_base.c`

This is the lowest-level infrastructure. It handles four concerns:

#### 3.1.1 Context (`svc_context_t`)

Every daemon allocates one `svc_context_t` on the stack inside `main()`. It is the single source of truth for daemon identity and state:

| Field | Type | Description |
|---|---|---|
| `name` | `char[64]` | Short name e.g. `"audio_service"` — used in log tags and PID file name |
| `version` | `char[32]` | SemVer string e.g. `"2.0.0"` — sent to SM on registration |
| `exe_path` | `char[256]` | `realpath()` of the running binary — sent to SM on registration |
| `pid_path` | `char[256]` | Full path to PID file e.g. `/run/audio_service.pid` |
| `config_path` | `char[256]` | Path passed via `-c` CLI argument |
| `log_path` | `char[256]` | Log file path read from `[server] log_file =` in the config |
| `pid` | `pid_t` | PID of the daemon process |
| `state` | `svc_state_t` | One of: `INIT → STARTING → RUNNING → STOPPING → STOPPED / ERROR` |
| `foreground` | `int` | If 1 (set by `-f` CLI flag), `daemonize()` is skipped |
| `verbose` | `int` | If 1 (set by `-v` CLI flag), `LOG_DEBUG` messages are emitted |
| `start_time` | `time_t` | `time(NULL)` at daemon start — used to compute `uptime_sec` |
| `error_count` | `uint64_t` | Total non-fatal errors since start — reported in health updates |

#### 3.1.2 Daemonization (`service_base_daemonize`)

Performs the standard double-fork POSIX daemonize sequence:

```
Parent process
  └─► fork() → Child 1
                 └─► setsid()           (new session, detach from terminal)
                     └─► fork() → Child 2 (grandchild — the actual daemon)
                                    ├─► chdir("/")
                                    ├─► umask(0)
                                    ├─► close stdin/stdout/stderr → /dev/null
                                    └─► continue running
                     └─► Child 1 exits (_exit(0))
  └─► Parent exits (_exit(0))
```

The double-fork ensures the daemon cannot re-acquire a controlling terminal. The function is skipped when `ctx->foreground == 1` (the `-f` flag), which is essential for running under systemd with `Type=simple` or during testing.

#### 3.1.3 PID File Management

- `service_base_write_pid(name)` — writes the current PID to `$SVC_PID_DIR/<name>.pid` (defaults to `/run`). Before writing it checks if the file already exists and whether the stored PID still has a running process (`kill(pid, 0)`). If a live duplicate is found it returns `SVC_ERR_ALREADY`, which causes `main()` to exit — preventing two instances of the same daemon from running.
- `service_base_remove_pid(name)` — deletes the PID file on clean shutdown.

The PID directory can be overridden via the `SVC_PID_DIR` environment variable, which is used during testing to avoid requiring root access to `/run`.

#### 3.1.4 Dual-Sink Logging

All log output goes to two sinks simultaneously:

1. **syslog** — using `LOG_DAEMON` facility so entries appear in `/var/log/syslog` and can be filtered with `journalctl -t <name>`
2. **Log file** — if `g_log_file` is non-NULL (opened by `service_base_open_log`)

Four log macros are defined:

```c
LOG_ERR(fmt, ...)    // priority 3 — errors that need attention
LOG_WARN(fmt, ...)   // priority 4 — warnings, non-fatal problems
LOG_INFO(fmt, ...)   // priority 6 — normal operational messages
LOG_DEBUG(fmt, ...)  // priority 7 — verbose debug (only when g_verbose)
```

Short aliases `SVC_ERR`, `SVC_WARN`, `SVC_INFO`, `SVC_DBG` are provided for use inside `*_service_hal.c` files.

**Important implementation detail:** The macros use numeric priority constants (3, 6, 7) instead of `LOG_ERR`, `LOG_INFO`, `LOG_DEBUG` to avoid symbol conflicts — `syslog.h` defines `LOG_ERR=3` as a plain `#define` which would clash with function-like macros of the same name. After `service_base.h` is included, `LOG_ERR`/`LOG_INFO`/`LOG_DEBUG` are redefined as the dual-sink macros.

---

### 3.2 `service_ipc` — Service Manager Communication

**Files:** `common/service_ipc.h`, `common/service_ipc.c`

Every service maintains a persistent Unix domain socket connection to the Service Manager (SM). The SM acts as a supervisor: it receives registration, monitors health, and can issue commands (shutdown, reload config, health check).

#### 3.2.1 IPC Context (`svc_ipc_t`)

| Field | Type | Description |
|---|---|---|
| `fd` | `int` | Socket file descriptor; `-1` when disconnected |
| `service_name` | `char[64]` | Name sent in REGISTER — must match the SM's known service list |
| `socket_path` | `char[256]` | Path to SM Unix socket (default: `/tmp/servicemanager.sock`) |
| `verify_key_path` | `char[256]` | Path to HMAC-SHA256 key file for message signing |
| `auth_token` | `svc_auth_token_t` | 64-byte token returned by SM after successful registration |
| `last_nonce` | `uint32_t` | Incrementing counter mixed into HMAC to prevent replay attacks |
| `last_heartbeat` | `time_t` | Timestamp of last sent heartbeat (used for interval checking) |
| `reconnect_backoff_sec` | `int` | Current exponential backoff value for reconnect attempts |
| `verify_ctx` | `void *` | Opaque pointer to `verify_context_t` for HMAC operations |

#### 3.2.2 Message Types

| Constant | Value | Direction | Description |
|---|---|---|---|
| `SVC_MSG_REGISTER` | 1 | Service → SM | Initial registration with exe path, version, PID |
| `SVC_MSG_HEARTBEAT` | 3 | Service → SM | Periodic liveness signal (every 5 seconds) |
| `SVC_MSG_UNREGISTER` | 4 | Service → SM | Clean shutdown notification |
| `SVC_MSG_HEALTH_CHECK` | 5 | SM → Service | SM requests a health status report |
| `SVC_MSG_HEALTH_OK` | 6 | Service → SM | Health report payload (`svc_health_status_t`) |
| `SVC_MSG_SHUTDOWN` | 7 | SM → Service | SM orders immediate shutdown |
| `SVC_MSG_RELOAD_CONFIG` | 8 | SM → Service | SM orders config re-read |
| `SVC_MSG_PING` | 9 | Service → SM | Service checks SM liveness |
| `SVC_MSG_PONG` | 10 | SM → Service | SM responds to ping |

#### 3.2.3 Registration Flow

```
Service                              Service Manager
  │                                        │
  ├── service_ipc_connect() ─────────────► accept()
  │       (connect to Unix socket)         │
  │                                        │
  ├── service_ipc_register() ─────────────►│
  │   sends sm_hdr_t + sm_register_req_t   │
  │   (name, exe_path, version, pid,       │
  │    HMAC signature)                     │
  │                                        ├── validate signature
  │                                        ├── store service record
  │◄────────────────────────────── sm_hdr_t + sm_reply_t
  │   (response_code=SM_OK, auth_token)    │
  │                                        │
  │   ipc->auth_token = token from reply   │
  │                                        │
  ├── [every 5s] service_ipc_heartbeat() ─►│
  │◄──────────────────────────────── SM_OK │
```

#### 3.2.4 Wire Format

Every message on the socket uses this two-part structure (defined in `sm_protocol.h`):

```
[ sm_hdr_t (56 bytes) ][ payload (hdr.length bytes) ]
```

`sm_hdr_t` contains:
- `magic` (4 bytes) — protocol magic number for framing validation
- `version` (2 bytes) — protocol version
- `type` (2 bytes) — message type (one of the `SVC_MSG_*` constants)
- `length` (4 bytes) — payload byte count
- `timestamp` (4 bytes) — UNIX timestamp for anti-replay
- `nonce` (4 bytes) — incrementing counter mixed into HMAC
- `hmac` (32 bytes) — HMAC-SHA256 of header fields + payload

`recv_reply()` in `service_ipc.c` reads exactly `sizeof(sm_hdr_t)` first (blocking with `MSG_WAITALL`), then reads exactly `hdr.length` bytes for the payload. If the SM closes the connection mid-message or sends fewer bytes the read fails and `SVC_ERR_IPC` is returned.

#### 3.2.5 Health Status (`svc_health_status_t`)

Sent as the payload of `SVC_MSG_HEALTH_OK` in response to a `SVC_MSG_HEALTH_CHECK` request:

| Field | Type | Description |
|---|---|---|
| `uptime_sec` | `uint32_t` | Seconds since daemon start (`time(NULL) - ctx->start_time`) |
| `error_count` | `uint32_t` | Total non-fatal errors (`ctx->error_count`) |
| `hal_state` | `uint8_t` | Current `hw_device_state_t` of the HAL device (0=closed, 1=open, 2=active) |
| `svc_state` | `uint8_t` | Current `svc_state_t` (0=init … 5=error) |
| `_pad` | `uint16_t` | Alignment padding (always zero) |

---

### 3.3 `service_config` — INI Configuration Parser

**Files:** `common/service_config.h`, `common/service_config.c`

A minimal INI-format parser that reads configuration files for all services. The format follows standard INI conventions:

```ini
[section_name]
key = value
# comments start with #
```

**API:**

```c
int  service_config_load(service_config_t *cfg, const char *path);
void service_config_free(service_config_t *cfg);

const char *service_config_get_string(service_config_t *cfg,
                                      const char *section,
                                      const char *key,
                                      const char *default_val);

int  service_config_get_int(service_config_t *cfg,
                             const char *section,
                             const char *key,
                             int default_val);

int  service_config_get_bool(service_config_t *cfg,
                              const char *section,
                              const char *key,
                              int default_val);
```

All `get_*` functions return the provided `default_val` if the section or key is not found — services never abort due to missing config keys.

---

## 4. Audio Service

**Directory:** `audio_service/`  
**Executable:** `audio_service`  
**Purpose:** Wraps the ALSA PCM HAL, registers with the SM, and provides a continuous audio capture/playback loop.

### 4.1 Context (`audio_service_ctx_t`)

The master struct aggregates everything the audio service needs:

```c
typedef struct {
    svc_context_t    base;           // daemon lifecycle (name, pid, state, log…)
    svc_ipc_t        ipc;            // SM connection handle
    service_config_t config;         // parsed INI config
    void            *hal_device;     // hw_device_t* from audio_hal_create()
    int              security_applied;

    // Audio-specific (resolved from INI + defaults)
    char     alsa_device[64];        // e.g. "hw:0,0"
    uint32_t sample_rate;            // e.g. 44100 Hz
    uint32_t channels;               // 1=mono, 2=stereo
    int      format;                 // audio_format_t (S16_LE etc.)
    uint32_t period_size;            // frames per ALSA period
    uint32_t buffer_size;            // total ALSA ring buffer in frames
    int      direction;              // 0=playback, 1=capture
} audio_service_ctx_t;
```

### 4.2 Configuration File (`audio_service.conf`)

Located at `/etc/audio_service/audio_service.conf` by default (overridden with `-c` flag):

```ini
[server]
log_file = /var/log/audio_service.log

[audio]
device      = hw:0,0
sample_rate = 44100
channels    = 2
format      = 0          # 0 = S16_LE
period_size = 1024
buffer_size = 4096
direction   = 1          # 1 = capture

[security]
enable_seccomp = true
drop_capabilities = true
```

### 4.3 File Responsibilities

| File | Responsibility |
|---|---|
| `audio_service_main.c` | Parses `-f/-v/-c` CLI args; calls init → apply_security → hal_init → ipc_connect → register → run_loop → shutdown |
| `audio_service_hal.c` | `audio_service_hal_init()` — calls `audio_hal_create()` + `ops->open()`; `audio_service_hal_start()` — `ops->start()`; `audio_service_hal_read/write()` — `ops->read/write()`; `audio_service_hal_cleanup()` — `ops->stop()` → `ops->close()` → `audio_hal_destroy()` |
| `audio_service_loop.c` | `epoll`-based event loop: waits on the SM socket fd with a 5-second timeout; on timeout sends heartbeat + reads PCM; dispatches incoming SM commands (`HEALTH_CHECK`, `SHUTDOWN`, `RELOAD_CONFIG`) |
| `audio_service_security.c` | `audio_service_apply_security()` — drops Linux capabilities to the minimum set needed for ALSA access; installs a seccomp-BPF filter allowing only the syscalls required by an ALSA daemon |

### 4.4 HAL Integration Detail

`audio_service_hal.c` is the bridge between the service layer and the ALSA HAL. It uses **only** the `hw_device_t` vtable — it never calls ALSA functions directly:

```
audio_service_hal_init()
  └─► audio_hal_create(name, alsa_device, &config)   ← HAL allocates hw_device_t
      └─► ops->open(dev)                              ← opens ALSA PCM handle

audio_service_hal_start()
  └─► ops->start(dev)                                 ← snd_pcm_prepare + start

audio_service_hal_read(ctx, buf, size)
  └─► ops->read(dev, buf, size)                       ← snd_pcm_readi

audio_service_hal_cleanup()
  └─► ops->stop(dev)                                  ← snd_pcm_drop
      ops->close(dev)                                 ← snd_pcm_close
      audio_hal_destroy(dev)                          ← free hw_device_t
```

Using the `ops` vtable means the exact same code runs in production (real ALSA device) and in tests (mock device), with zero conditional compilation.

### 4.5 systemd Unit (`audio_service.service`)

```ini
[Unit]
Description=Audio Service Daemon
After=network.target servicemanager.service

[Service]
Type=forking
PIDFile=/run/audio_service.pid
ExecStart=/usr/local/bin/audio_service -c /etc/audio_service/audio_service.conf
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

---

## 5. Camera Service

**Directory:** `camera_service/`  
**Executable:** `camera_service`  
**Purpose:** Manages V4L2 camera device capture, exposes frames, registers with the SM.

### 5.1 Context (`camera_service_ctx_t`)

```c
typedef struct {
    svc_context_t    base;
    svc_ipc_t        ipc;
    service_config_t config;
    void            *hal_device;     // hw_device_t* from camera_hal_create()
    int              security_applied;

    // Camera-specific config
    char     v4l2_device[64];        // e.g. "/dev/video0"
    uint32_t width;                  // frame width in pixels
    uint32_t height;                 // frame height in pixels
    int      format;                 // camera_format_t (YUYV, MJPEG…)
    uint32_t fps;                    // frames per second
    uint32_t buffer_count;           // number of V4L2 mmap buffers
} camera_service_ctx_t;
```

### 5.2 Capture Flow

`camera_service_hal_capture()` allocates a local buffer, calls `ops->read(dev, buf, size)`, and returns `frame_data` and `frame_size` to the event loop. The event loop writes the frame to interested consumers (shared memory or a local socket, depending on deployment). `camera_service_hal_return()` signals buffer return via `ops->write(dev, &buf_idx, sizeof(buf_idx))`.

This vtable-based approach means the V4L2 MMAP dequeue logic inside `camera_hal.c` is exercised by the real HAL but the service layer itself remains hardware-independent and testable via the mock.

---

## 6. Sensor Service

**Directory:** `sensor_service/`  
**Executable:** `sensor_service`  
**Purpose:** Reads values from IIO (Industrial I/O) sensors — accelerometers, gyroscopes, thermometers, etc.

### 6.1 Context (`sensor_service_ctx_t`)

```c
typedef struct {
    svc_context_t    base;
    svc_ipc_t        ipc;
    service_config_t config;
    void            *hal_device;     // hw_device_t* from sensor_hal_create()
    int              security_applied;

    // Sensor-specific config
    char     iio_device[64];         // e.g. "/dev/iio:device0"
    int      sensor_type;            // sensor_type_t (ACCEL, GYRO, TEMP…)
    uint32_t sample_rate_hz;         // requested sampling frequency
    int      axis_count;             // 1 (scalar) or 3 (X/Y/Z vector)
} sensor_service_ctx_t;
```

### 6.2 Read Modes

The event loop calls either:
- `sensor_service_hal_read_3axis()` — for 3-axis vector sensors (accelerometer, gyroscope); returns three `float` values (X, Y, Z)
- `sensor_service_hal_read_scalar()` — for single-axis sensors (temperature, pressure); returns one `float` value

Both functions use `ops->read(dev, buf, size)` under the hood, with the sensor HAL handling IIO channel parsing.

---

## 7. GPIO Service

**Directory:** `gpio_service/`  
**Executable:** `gpio_service`  
**Purpose:** Controls and monitors a single GPIO pin via the sysfs or libgpiod GPIO HAL.

### 7.1 Context (`gpio_service_ctx_t`)

```c
typedef struct {
    svc_context_t    base;
    svc_ipc_t        ipc;
    service_config_t config;
    void            *hal_device;     // hw_device_t* from gpio_hal_create()
    int              security_applied;
    char             chip[64];       // GPIO chip path e.g. "/dev/gpiochip0"

    // GPIO-specific config
    uint32_t pin_number;             // GPIO pin number
    int      direction;              // 0=input, 1=output
    int      initial_value;          // GPIO_VALUE_LOW / GPIO_VALUE_HIGH
    int      edge;                   // GPIO_EDGE_NONE / RISING / FALLING / BOTH
    int      interrupt_timeout_ms;   // milliseconds to wait for interrupt events
} gpio_service_ctx_t;
```

### 7.2 Operations

| Function | HAL call | Description |
|---|---|---|
| `gpio_service_hal_set_value(ctx, val)` | `gpio_hal_set_value()` → `ops->write(dev, &level, 1)` | Drive pin HIGH or LOW |
| `gpio_service_hal_get_value(ctx, &val)` | `gpio_hal_get_value()` → `ops->read(dev, &level, 1)` | Sample current pin level |
| `gpio_service_hal_wait_interrupt(ctx, timeout_ms)` | `ops->control(dev, 0, &timeout_ms)` | Block until edge event or timeout |

### 7.3 Interrupt Design

`gpio_service_hal_wait_interrupt()` uses the vtable `ops->control()` (not `gpio_hal_wait_interrupt()` directly) to stay testable. In production the GPIO HAL's `control()` implementation uses `poll(POLLPRI)` on the sysfs value FD. In tests the mock's `control()` returns `HAL_SUCCESS` immediately without blocking.

---

## 8. Testing Infrastructure

All tests are inside the `testing/` subdirectory. The test framework is minimal — a custom `TEST()` macro with pass/fail accounting, not a third-party framework, keeping the dependency tree simple.

### 8.1 Mock HAL

**Files:** `testing/mocks/mock_hal.h`, `testing/mocks/mock_hal.c`

The mock HAL creates a real `hw_device_t` structure with a vtable that instead of touching hardware records calls and returns configurable data.

**State recorded per device (`mock_hal_priv_t`):**

```c
typedef struct {
    struct {
        int open_calls;
        int close_calls;
        int start_calls;
        int stop_calls;
        int read_calls;
        int write_calls;
        int control_calls;
        int reset_calls;
    } counts;                        // call counters for assertions

    ssize_t  read_retval;            // value returned by mock_read()
    uint8_t  read_data[4096];        // bytes copied into read buffer
    size_t   read_data_len;          // valid bytes in read_data

    int      open_retval;            // return value of mock_open()
    int      start_retval;           // return value of mock_start()
    int      control_retval;         // return value of mock_control()
} mock_hal_priv_t;
```

**Key API:**

```c
// Create a mock device of the given type
hw_device_t *mock_hal_create(const char *name, hw_device_type_t type);

// Get the private state for assertions
mock_hal_priv_t *mock_hal_get_priv(hw_device_t *dev);

// Load canned data that mock_read() will copy into callers' buffers
void mock_hal_set_read_data(hw_device_t *dev, const void *data, size_t len);

// Reset all counters and data for test isolation
void mock_hal_reset(hw_device_t *dev);

// Destroy the mock device
void mock_hal_destroy(hw_device_t *dev);
```

**Example test usage:**
```c
hw_device_t *dev = mock_hal_create("audio0", HAL_DEVICE_TYPE_AUDIO);

// Pre-load what ops->read() will return
uint8_t pcm_bytes[128] = { /* test data */ };
mock_hal_set_read_data(dev, pcm_bytes, sizeof(pcm_bytes));

// Run the function under test
audio_service_ctx_t ctx;
ctx.hal_device = dev;
dev->state = HAL_STATE_ACTIVE;
ssize_t n = audio_service_hal_read(&ctx, buf, sizeof(buf));

// Assert
assert(n == 128);
assert(mock_hal_get_priv(dev)->counts.read_calls == 1);
```

---

### 8.2 Mock Service Manager

**Files:** `testing/mocks/mock_sm.h`, `testing/mocks/mock_sm.c`

A real Unix domain socket server running in a background POSIX thread. It accepts connections and responds to the same wire format (`sm_hdr_t + payload`) that `service_ipc.c` sends.

**What it does:**
- Accepts one client connection at a time (re-arms after disconnect)
- Responds to `REGISTER`, `HEARTBEAT`, `UNREGISTER`, `PING` with configurable reply codes
- Can proactively push `HEALTH_CHECK`, `SHUTDOWN`, `RELOAD_CONFIG` to the connected client
- Records every received message type with per-type counters
- All fields are mutex-protected for thread safety

**Key API:**

```c
int  mock_sm_start(mock_sm_t *sm, const char *path); // start server thread
void mock_sm_stop (mock_sm_t *sm);                   // stop and cleanup
void mock_sm_reset(mock_sm_t *sm);                   // zero all counters

// Synchronisation — block until a new message arrives (since last reset or last wait call)
int  mock_sm_wait_request(mock_sm_t *sm, int timeout_ms);

// Check if a service registered itself successfully
int  mock_sm_is_registered(const mock_sm_t *sm, const char *service_name);

// Count messages of a specific type in the log
int  mock_sm_count_msg(const mock_sm_t *sm, uint16_t type);

// Proactive pushes from SM → service
int  mock_sm_send_health_check(mock_sm_t *sm);
int  mock_sm_send_shutdown    (mock_sm_t *sm);
int  mock_sm_send_reload      (mock_sm_t *sm);
```

**Wire format detail:** `mock_sm_wait_request()` uses a `pthread_cond_t` + a per-request generation counter (`last_wait_total`) so each call waits for exactly one *new* message since the previous call. `mock_sm_reset()` zeros the generation counter, ensuring clean per-test isolation. Reply messages are sent as `sm_hdr_t + sm_reply_t` (not bare `sm_reply_t`) to match what `recv_reply()` in `service_ipc.c` expects.

---

### 8.3 Unit Tests

Unit tests test a single component in isolation. All 6 unit test executables pass with zero failures.

#### `test_service_base` — 14 tests
Tests the daemon lifecycle infrastructure:

| Test | What it verifies |
|---|---|
| `test_init_basic` | `service_base_init()` populates context fields |
| `test_init_null_name` | NULL name returns `SVC_ERR_INVALID` |
| `test_init_null_ctx` | NULL context returns `SVC_ERR_INVALID` |
| `test_init_long_name` | Name > 63 chars is truncated safely |
| `test_pidfile_write_read_remove` | PID file lifecycle |
| `test_pidfile_remove_nonexistent` | Does not crash on missing PID file |
| `test_pidfile_duplicate_detection` | Returns `SVC_ERR_ALREADY` for live PID |
| `test_pidfile_stale_overwrite` | Overwrites stale PID file (dead process) |
| `test_error_strings` | Every `svc_error_t` value has a non-empty string |
| `test_install_signals_ok` | Signal handlers install without error |
| `test_signal_sighup_sets_reload` | `raise(SIGHUP)` sets `g_reload=1` |
| `test_signal_sigterm_clears_running` | `raise(SIGTERM)` sets `g_running=0` |
| `test_log_open_close` | Log file open/close cycle |
| `test_log_open_no_file` | NULL path uses syslog-only mode |

#### `test_service_ipc` — 11 tests
Tests the SM wire protocol:

| Test | What it verifies |
|---|---|
| `test_ipc_init_basic` | `service_ipc_init()` sets fields |
| `test_ipc_init_null_name` | NULL name rejected |
| `test_ipc_connect_no_server` | Returns error when SM not running |
| `test_ipc_connect_ok` | Connects to mock SM socket |
| `test_ipc_register_ok` | Full register flow — SM records service |
| `test_ipc_heartbeat_ok` | Heartbeat increments SM's counter |
| `test_ipc_ping_ok` | Ping returns SVC_OK |
| `test_ipc_is_connected` | State transitions: disconnected → connected → disconnected |
| `test_ipc_unregister_ok` | Unregister — SM marks service inactive |
| `test_ipc_send_health_ok` | `SVC_MSG_HEALTH_OK` delivery and counter |

#### HAL Layer Tests (`test_*_hal_layer`) — 10/8/8/9 tests

Each HAL layer test file covers the same pattern for its respective service:

| Test Pattern | What it verifies |
|---|---|
| `test_hal_init_calls_open` | `*_service_hal_init()` calls `ops->open()` exactly once |
| `test_hal_start_sets_active` | After `start()`, `dev->state == HAL_STATE_ACTIVE` |
| `test_hal_read/capture…` | `ops->read()` is called; returned data equals pre-loaded test data |
| `test_hal_set/get_value` (GPIO) | `ops->write()` / `ops->read()` dispatched correctly |
| `test_hal_wait_interrupt` (GPIO) | `ops->control()` called; returns without blocking on zero timeout |
| `test_hal_cleanup_stop_before_close` | `ops->stop()` called **before** `ops->close()` — order verified via counters |
| `test_hal_init_open_error` | HAL init fails cleanly when `ops->open()` returns an error |
| `test_hal_*_error_propagated` | Errors from vtable calls are properly propagated as `SVC_ERR_HAL` |
| `test_mock_reset_isolation` | `mock_hal_reset()` zeroes all counters between tests |

---

### 8.4 Integration Tests

Integration tests exercise the full service layer (HAL + IPC + lifecycle) against both the mock HAL and the mock SM simultaneously. Each `test_*_full` file starts the mock SM, performs real IPC connect/register/communicate/unregister cycles, and verifies end-to-end behaviour.

#### `test_audio_full` — 5 tests

| Test | Scenario |
|---|---|
| `test_full_lifecycle_register_unregister` | Full connect → register → unregister → disconnect cycle |
| `test_full_send_health_ok` | SM sends `HEALTH_CHECK`; service responds with `HEALTH_OK` |
| `test_full_sm_pushes_health_check` | SM proactively pushes check; verify `health_ok_count ≥ 1` |
| `test_full_sm_send_shutdown` | SM sends `SHUTDOWN`; service sets `g_running=0` |
| `test_full_hal_and_ipc_combined` | HAL read + IPC heartbeat within the same test |

#### `test_camera_full` — 5 tests

| Test | Scenario |
|---|---|
| `test_full_lifecycle_register_unregister` | Full lifecycle |
| `test_full_hal_capture_frame` | Capture frame via mock HAL + verify data content |
| `test_full_sm_send_reload` | SM sends `RELOAD_CONFIG`; service re-reads config |
| `test_full_send_health_ok` | Health report flow |
| `test_full_ping_roundtrip` | Service pings SM; verifies SM responds |

#### `test_sensor_full` — 5 tests

| Test | Scenario |
|---|---|
| `test_full_lifecycle_register_unregister` | Full lifecycle |
| `test_full_read_3axis_with_ipc` | 3-axis read while connected to SM |
| `test_full_read_scalar_with_ipc` | Scalar read while connected to SM |
| `test_full_send_health_ok` | Health report flow |
| `test_full_sm_send_reload` | Config reload command |

#### `test_gpio_full` — 6 tests

| Test | Scenario |
|---|---|
| `test_full_lifecycle_register_unregister` | Full lifecycle |
| `test_full_set_value_with_ipc` | Set GPIO while registered with SM |
| `test_full_get_value_with_ipc` | Get GPIO level while registered with SM |
| `test_full_wait_interrupt_with_ipc` | Wait interrupt (immediate on mock) while registered |
| `test_full_send_health_ok` | Health report flow |
| `test_full_sm_send_shutdown` | Shutdown command from SM |

---

## 9. Build System

**File:** `services/CMakeLists.txt`  
**Requires:** CMake ≥ 3.28, GCC ≥ 13, libssl-dev, libseccomp-dev

### 9.1 Library Targets

The root CMakeLists defines the following static library targets. Each is a separate compilation unit that can be linked selectively:

| Library Target | Sources | Purpose |
|---|---|---|
| `service_common` | `common/service_base.c`, `service_config.c`, `service_ipc.c` | Shared daemon infrastructure |
| `hal_interface_lib` | `dev/hal/interface/hal_interface.c` | Base HAL vtable and lifecycle |
| `hal_audio_lib` | `dev/hal/layers/audio/audio_hal.c` | ALSA audio HAL implementation |
| `hal_camera_lib` | `dev/hal/layers/camera/camera_hal.c` | V4L2 camera HAL implementation |
| `hal_sensor_lib` | `dev/hal/layers/sensors/sensor_hal.c` | IIO sensor HAL implementation |
| `hal_gpio_lib` | `dev/hal/layers/gpio/gpio_hal.c` | sysfs/libgpiod GPIO HAL implementation |
| `ring_buffer_lib` | `dev/core/ring_buffer.c` | Lock-free ring buffer |
| `sm_crypto_lib` | `dev/core/service_manager/security/sm_crypto.c`, `sm_logging.c` | HMAC-SHA256 message signing |
| `security_manager_lib` | All `dev/security/**/*.c` | Capabilities, seccomp, sandbox |
| `verify_lib` | `dev/security/verify/**/*.c` | Message verification |

### 9.2 Compiler Flags

All targets are built with:
```
-Wall -Wextra -Werror -Wshadow -Wformat=2 -Wno-unused-parameter
```

`security_manager_lib` additionally gets `-Wno-format-truncation` due to intentional size-limited `snprintf` calls in sandbox_mount.c.

### 9.3 Linking

Each service executable links:
```
hal_*_lib  hal_interface_lib  verify_lib  sm_crypto_lib
ring_buffer_lib  security_manager_lib  service_common
ssl  crypto  pthread  seccomp
```

Each test executable links the same set plus `mock_hal` and `mock_sm`.

### 9.4 Build Commands

```bash
cd services/

# Configure (first time only)
cmake -B build -S .

# Build everything
cmake --build build -- -j$(nproc)

# Run all tests
for t in build/testing/test_*; do
    echo -n "$(basename $t): "
    timeout 30 "$t" && echo PASS || echo FAIL
done
```

---

## 10. Wire Protocol

The service layer communicates with the Service Manager using the `sm_protocol.h` wire format from `dev/core/service_manager/infrastructure/`. The key structures:

### `sm_hdr_t` (56 bytes, always first)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├───────────────────────────────────────────────────────────────────┤
│                           magic (4)                               │
├───────────────────────────────────────────────────────────────────┤
│        version (2)        │        type (2)                       │
├───────────────────────────────────────────────────────────────────┤
│                          length (4)                               │
├───────────────────────────────────────────────────────────────────┤
│                         timestamp (4)                             │
├───────────────────────────────────────────────────────────────────┤
│                           nonce (4)                               │
├───────────────────────────────────────────────────────────────────┤
│                        hmac[32 bytes]                             │
│                           ...                                     │
└───────────────────────────────────────────────────────────────────┘
```

- **magic:** Fixed 4-byte value; receiver validates this before processing
- **type:** One of the `SVC_MSG_*` constants
- **length:** Byte count of the payload that immediately follows the header
- **timestamp:** UTC epoch seconds; the SM checks this is within an acceptable skew window
- **nonce:** Monotonic counter; the SM rejects messages with a nonce equal to or lower than the last seen value from that service (anti-replay)
- **hmac:** HMAC-SHA256 computed over all header bytes before the hmac field, plus the full payload, using the pre-shared key loaded from `verify_key_path`

---

## 11. Security Model

### 11.1 Capability Reduction

Each service calls `*_service_apply_security()` early in startup, before any HAL operations. This function (implemented in `*_service_security.c`) uses the Linux capabilities API to drop all capabilities except those strictly needed:

| Service | Retained Capabilities |
|---|---|
| audio_service | None (ALSA opens as regular user) |
| camera_service | `CAP_SYS_ADMIN` for V4L2 mmap if needed |
| sensor_service | None (IIO readable as `plugdev` group member) |
| gpio_service | `CAP_SYS_RAWIO` for sysfs GPIO value writes |

The capability drop uses the `capabilities_core.c` module from `dev/security/capabilities/`, which reads the target capability set from the service config's `[security]` section.

### 11.2 Seccomp Filter

After capability drop, a seccomp-BPF filter is installed. The `allowed_system_calls.md` in `docs/` lists the syscalls each service is permitted to call. Any syscall not on the allowlist causes the kernel to send `SIGSYS` to the process (or kill it, depending on filter policy), preventing privilege escalation exploits.

### 11.3 Sandbox

For additional isolation, services can optionally be placed in a seccomp sandbox (separate mount and network namespaces) via `sandbox.c` from `dev/security/sandbox/`. This is configurable per-service via `[security] enable_sandbox = true` in the conf file.

### 11.4 Message Authentication

All messages sent to the SM are signed with HMAC-SHA256 using a key file shared at deploy time. The SM verifies the HMAC before processing any command. This prevents a compromised service from spoofing another service's identity or sending forged UNREGISTER messages.

---

## 12. Signal Handling

Signal handlers are installed by `service_base_install_signals()` using `sigaction()` (not the deprecated `signal()`). The handlers only write to `volatile sig_atomic_t` flags — they never call any non-async-signal-safe functions.

| Signal | Handler action | Service response |
|---|---|---|
| `SIGTERM` | `g_running = 0` | Event loop exits → clean shutdown |
| `SIGINT` | `g_running = 0` | Same as SIGTERM (for interactive use) |
| `SIGHUP` | `g_reload = 1` | Event loop re-reads config on next iteration |
| `SIGPIPE` | `SIG_IGN` | Broken pipe on SM socket doesn't kill daemon; IPC layer detects and reconnects |

The event loop checks both flags on every iteration:
```c
while (g_running) {
    if (g_reload) {
        service_config_load(&ctx->config, ctx->base.config_path);
        g_reload = 0;
    }
    // ... epoll_wait / HAL work ...
}
```

---

## 13. How to Build and Run

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt-get install cmake gcc libssl-dev libseccomp-dev pkg-config

# CMake ≥ 3.28, GCC ≥ 13
cmake --version
gcc --version
```

### Build

```bash
cd /path/to/middleware/services

# First-time CMake configuration
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Incremental build (uses all CPU cores)
cmake --build build -- -j$(nproc)
```

Executables are placed in:
- `build/audio_service/audio_service`
- `build/camera_service/camera_service`
- `build/sensor_service/sensor_service`
- `build/gpio_service/gpio_service`
- `build/testing/test_*`

### Run Tests

```bash
cd services/

# Run all 10 test executables
for t in build/testing/test_*; do
    printf "%-35s" "$(basename $t):"
    timeout 30 "$t" 2>/dev/null | grep -E "Results:|FAIL"
done
```

Expected output (all passing):
```
test_service_base:                 14/14 passed
test_service_ipc:                  11/11 passed
test_audio_hal_layer:              10/10 passed
test_sensor_hal_layer:              8/8 passed
test_camera_hal_layer:              8/8 passed
test_gpio_hal_layer:                9/9 passed
test_audio_full:                    5/5 passed
test_camera_full:                   5/5 passed
test_sensor_full:                   5/5 passed
test_gpio_full:                     6/6 passed
```

### Run a Service Manually

```bash
# Foreground mode (no fork, log to stdout via syslog)
sudo build/audio_service/audio_service -f -v -c audio_service/audio_service.conf

# Background daemon mode
sudo build/audio_service/audio_service -c /etc/audio_service/audio_service.conf

# Check PID file
cat /run/audio_service.pid

# Send reload signal
kill -HUP $(cat /run/audio_service.pid)

# Graceful shutdown
kill $(cat /run/audio_service.pid)
```

### Deploy via systemd

```bash
sudo cp audio_service/audio_service.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable audio_service
sudo systemctl start  audio_service
systemctl status      audio_service
journalctl -u         audio_service -f
```

---

## 14. Error Codes Reference

All service functions return an `int`. Zero (`SVC_OK`) indicates success. Negative values indicate errors:

| Code | Value | Meaning |
|---|---|---|
| `SVC_OK` | 0 | Success |
| `SVC_ERR_GENERIC` | -1 | Unclassified error |
| `SVC_ERR_INVALID` | -2 | NULL pointer or invalid argument |
| `SVC_ERR_FORK` | -3 | `fork()` failed during daemonize |
| `SVC_ERR_SETSID` | -4 | `setsid()` failed during daemonize |
| `SVC_ERR_CHDIR` | -5 | `chdir("/")` failed during daemonize |
| `SVC_ERR_PIDFILE` | -6 | Could not write / read PID file |
| `SVC_ERR_SIGNAL` | -7 | `sigaction()` failed |
| `SVC_ERR_ALREADY` | -8 | Duplicate daemon instance detected |
| `SVC_ERR_HAL` | -9 | HAL operation failed (vtable returned error) |
| `SVC_ERR_SECURITY` | -10 | Capability drop or seccomp install failed |
| `SVC_ERR_IPC` | -11 | Socket connect / send / recv failed |
| `SVC_ERR_CONFIG` | -12 | Config file not found or parse error |
| `SVC_ERR_NOMEM` | -13 | `malloc()` / `calloc()` returned NULL |
| `SVC_ERR_TIMEOUT` | -14 | Timed out waiting (interrupt,  SM reply, etc.) |

`svc_error_string(err)` returns a human-readable C string for any of the above values, suitable for log messages.

---

*End of Services Layer Documentation*
