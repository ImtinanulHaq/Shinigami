# Linux Middleware Platform

**Version 2.1.0**

A production-grade embedded Linux middleware platform built in C. It manages hardware services, enforces security policies, monitors everything in real time, and provides a clean API for applications to talk to hardware without caring about the underlying driver details.

---

## Table of Contents

1. [What this project does](#what-this-project-does)
2. [Architecture overview](#architecture-overview)
3. [Full directory structure](#full-directory-structure)
4. [Module 1 — Core](#module-1--core)
5. [Module 2 — HAL (Hardware Abstraction Layer)](#module-2--hal-hardware-abstraction-layer)
6. [Module 3 — Security](#module-3--security)
7. [Module 4 — Proxy](#module-4--proxy)
8. [Module 5 — Services](#module-5--services)
9. [Module 6 — Monitoring](#module-6--monitoring)
10. [Build system](#build-system)
11. [How to build](#how-to-build)
12. [How to run](#how-to-run)
13. [Data flow — end to end](#data-flow--end-to-end)
15. [License](#license)
16. [Credits](#credits)

---

## What this project does

This platform sits between Linux hardware drivers and application software. Applications do not open `/dev` nodes or write driver-specific ioctl calls. Instead they call a clean proxy API. The middleware handles everything below that:

- Manages the lifecycle of hardware services (start, stop, restart, health check)
- Enforces Linux security (seccomp filters, Linux capabilities, sandboxing, HMAC verification)
- Abstracts hardware through a uniform HAL interface (audio, camera, GPIO, sensors)
- Routes IPC messages between components through a central service manager
- Monitors CPU, RAM, latency, violations, and health scores in real time
- Displays all of the above in a live terminal dashboard

---

## Architecture overview

```
+----------------------------------------------------------+
|                   Application Layer                      |
|           (calls Proxy API  —  libmiddleware_proxy.so)   |
+----------------------------------------------------------+
              |                          |
   service discovery             typed data frames
   (audio, camera, gpio,         (audio_frame_t,
    sensor reads/writes)          sensor_reading_t, ...)
              |                          |
+----------------------------------------------------------+
|                     Proxy Layer                          |
|   audio_proxy  camera_proxy  gpio_proxy  sensor_proxy   |
|   service_discovery  proxy_auth  proxy_connection        |
+----------------------------------------------------------+
              |
    IPC over Unix sockets
    (HMAC-authenticated, replay-protected messages)
              |
+----------------------------------------------------------+
|               Service Manager (sm_daemon)                |
|  sm_registry  sm_handlers  sm_protocol  sm_socket        |
|  sm_watchdog  sm_security  sm_rate_limit  sm_crypto      |
|  sm_eventbus  sm_discovery  sm_health  sm_audit          |
+----------------------------------------------------------+
      |             |              |              |
      v             v              v              v
+----------+  +----------+  +----------+  +----------+
|  audio   |  |  camera  |  |  gpio    |  |  sensor  |
|  service |  |  service |  |  service |  |  service |
+----------+  +----------+  +----------+  +----------+
      |             |              |              |
      v             v              v              v
+----------------------------------------------------------+
|                 HAL Layer                                |
|   audio_hal  camera_hal  gpio_hal  sensor_hal            |
|   (uniform hal_interface — drivers below here)           |
+----------------------------------------------------------+
      |
      v
+----------------------------------------------------------+
|                 Security Layer                           |
|   seccomp  sandbox  capabilities  verify (HMAC/replay)  |
+----------------------------------------------------------+
      |
      v
+----------------------------------------------------------+
|                    Core Layer                            |
|   io_uring_loop  memory_pool  ring_buffer                |
|   service_manager (lifecycle, infrastructure, observ.)  |
+----------------------------------------------------------+
      |
      v  (side channel — reads /proc, sockets, shared mem)
+----------------------------------------------------------+
|              Monitoring (monitord + mw_tui)              |
|  collectors  daemon  health  alerts  metrics  tracing    |
|  protocol  TUI panels                                    |
+----------------------------------------------------------+
```

---

## Full directory structure

```
middleware/
|
|-- CMakeLists.txt               Root build file
|-- CMakePresets.json            Build presets (dev, relwithdebinfo, etc.)
|-- VERSION                      Single version source of truth  (2.1.0)
|-- GNUmakefile                  Convenience wrapper for common cmake targets
|-- Bankai.sh                    Helper: stop/start/sync all installed services
|-- mise.toml                    Dev environment tool versions
|
|-- cmake/                       CMake helper modules
|   |-- BuildOptions.cmake       Compiler flags, optimization levels
|   |-- Hardening.cmake          Stack protector, RELRO, PIE, fortify
|   |-- Packaging.cmake          CPack DEB/RPM config
|   |-- PlatformChecks.cmake     Feature detection (io_uring, seccomp, etc.)
|   |-- Sanitizers.cmake         ASan/UBSan/TSan toggle
|   |-- StaticAnalysis.cmake     clang-tidy and cppcheck integration
|   |-- Versioning.cmake         Reads VERSION file into cmake variables
|   |-- modules/                 Find modules for system libraries
|   |   |-- FindLibCap.cmake
|   |   |-- FindLibSeccomp.cmake
|   |   `-- FindLibUring.cmake
|   `-- toolchain/               Cross-compile toolchain files
|       |-- linux-aarch64.cmake
|       `-- linux-arm.cmake
|
|-- dev/                         All source code lives here
|   |
|   |-- core/                    Foundational building blocks
|   |   |-- io_uring_loop.c/h    Async I/O event loop using io_uring
|   |   |-- memory_pool.c/h      Fixed-size slab memory allocator
|   |   |-- ring_buffer.c/h      Lock-free SPSC ring buffer
|   |   `-- service_manager/     Central process manager daemon
|   |       |-- lifecycle/       Start, stop, restart, config, dependencies
|   |       |-- infrastructure/  Socket server, protocol, registry, handlers
|   |       |-- enterprise/      Watchdog, TLS, event bus, plugin system, CLI
|   |       |-- security/        Rate limiting, replay protection, HMAC crypto
|   |       `-- observability/   Health, audit, metrics, structured logging
|   |
|   |-- hal/                     Hardware Abstraction Layer
|   |   |-- interface/           Common hal_interface.h  (all drivers implement this)
|   |   `-- layers/              Per-device driver implementations
|   |       |-- audio/           ALSA-based audio capture and playback
|   |       |-- camera/          V4L2-based camera frame capture
|   |       |-- gpio/            Linux GPIO character device driver
|   |       `-- sensors/         IIO subsystem sensor reader
|   |
|   |-- security/                Security enforcement modules
|   |   |-- core/                security_manager.h — orchestrates all security
|   |   |-- seccomp/             System call filters per service type
|   |   |-- sandbox/             Namespace, cgroup, mount, network isolation
|   |   |-- capabilities/        Linux capability policy and audit
|   |   `-- verify/              HMAC signing, replay attack detection, token auth
|   |
|   |-- proxy/                   Client-facing API library
|   |   |-- base/                service_proxy.h, connection, event loop, shm
|   |   |-- audio/               audio_proxy.h — typed audio frame API
|   |   |-- camera/              camera_proxy.h — typed video frame API
|   |   |-- gpio/                gpio_proxy.h — read/write/subscribe pins
|   |   |-- sensor/              sensor_proxy.h — typed sensor reading API
|   |   |-- discovery/           service_discovery.h — find services by name
|   |   |-- security/            proxy_auth.h — authentication at proxy layer
|   |   `-- bindings/c_api/      proxy_c_api.h — plain C wrapper for all proxies
|   |
|   |-- services/                Individual hardware service processes
|   |   |-- common/              service_base.h, service_config.h, service_ipc.h
|   |   |-- audio_service/       Audio capture/playback service process
|   |   |-- camera_service/      Camera frame capture service process
|   |   |-- gpio_service/        GPIO pin management service process
|   |   `-- sensor_service/      Sensor data collection service process
|   |
|   `-- monitoring/              Real-time observability stack
|       |-- collectors/          Data gathering threads (one per subsystem)
|       |-- daemon/              monitord — the monitoring background process
|       |-- health/              Health score computation and history
|       |-- alerts/              Alert rules, engine, and notifications
|       |-- metrics/             Gauge/counter/histogram registry, Prometheus
|       |-- protocol/            IPC protocol between monitord and mw_tui
|       |-- tracing/             Distributed trace span collection and storage
|       |-- tui/                 ncurses terminal dashboard (mw_tui)
|       `-- docs/                Detailed per-subsystem documentation
|
|-- docs/                        High-level architecture documentation
|   |-- hal/HAL_DOCUMENTATION.md
|   |-- proxy/PROXY_ARCHITECTURE.md
|   |-- security/SECURITY_DOCUMENTATION.md
|   `-- services/SERVICES_DOCUMENTATION.md
|
|-- install/relwithdebinfo/      Installed binaries (output of cmake --install)
|   `-- sbin/
|       |-- audio_service
|       |-- camera_service
|       |-- gpio_service
|       |-- sensor_service
|       |-- sm_daemon
|       |-- monitord
|       `-- mw_tui
|
`-- packaging/deb/               Debian package post-install scripts
```

---

## Module 1 — Core

**Location:** `dev/core/`

The core module provides the low-level building blocks that every other module uses. Nothing above it needs to know about kernel APIs directly.

### io_uring_loop (io_uring_loop.c/h)

An async I/O event loop built on Linux `io_uring`. Instead of blocking on `read()` or `poll()`, all I/O operations in the middleware are submitted to an io_uring ring and processed in batches when their completions arrive. This eliminates most system call overhead in hot paths.

```
Application code
     |
     | submit_read() / submit_write()
     v
  io_uring SQ (submission queue)
     |
     | kernel processes I/O
     v
  io_uring CQ (completion queue)
     |
     | event_loop_run() drains completions
     v
  callback(result)
```

Key functions:
- `io_uring_loop_init()` — creates the ring with a given queue depth
- `io_uring_loop_submit_read()` — queues an async read
- `io_uring_loop_run()` — blocks until at least one completion is ready, calls callbacks

### memory_pool (memory_pool.c/h)

A fixed-size slab allocator. All services use this instead of `malloc()` for latency-sensitive paths. Every allocation is the same size so there is no fragmentation and every `alloc()` is O(1) via a free-list pointer.

```
pool_create(object_size=64, capacity=4096)
     |
     +-- allocates one large block: 64 * 4096 = 256 KB
     +-- builds a singly-linked free list through the block
     v
pool_alloc()  ->  pops the free list head  (O(1))
pool_free()   ->  pushes back onto the free list  (O(1))
```

Used by: ring buffers, service IPC, proxy frame buffers.

### ring_buffer (ring_buffer.c/h)

A lock-free single-producer single-consumer (SPSC) ring buffer. Used to pass data between the service processing thread and the IO thread without any mutex. The producer writes to the tail; the consumer reads from the head. Because there is exactly one writer and one reader, no locking is needed — only memory barriers.

```
Producer thread          Consumer thread
     |                        |
     v                        v
  [head]-->[slot0]-->[slot1]-->[slot2]-->[tail]
     ^                                     |
     |                                     |
     +--------- circular wrap -------------+
```

Key statistics tracked: total produced, total consumed, drops (when full), current fill level.

---

### service_manager (dev/core/service_manager/)

The service manager (`sm_daemon`) is a background process that runs at system start. Every other service registers with it. Applications discover services through it. It acts as the central nervous system of the middleware.

It is organized into four sub-folders:

#### lifecycle/

| File                   | What it does                                                   |
|------------------------|----------------------------------------------------------------|
| sm_main.c/h            | Entry point, startup, main event loop                         |
| sm_config.c/h          | INI config file loading for sm_daemon                         |
| sm_dependencies.c/h    | Dependency graph: service A cannot start until service B is up|
| sm_management.c/h      | Start, stop, restart commands for registered services         |
| sm_graceful_shutdown.c | SIGTERM handling — stops services in reverse dependency order  |
| sm_persistence.c/h     | Saves service state to disk so restart survives daemon crash   |
| sm_service_tier.c/h    | Priority tiers: critical services restart before non-critical  |

#### infrastructure/

| File                  | What it does                                                    |
|-----------------------|-----------------------------------------------------------------|
| sm_socket.c/h         | Unix domain socket server — each service connects here         |
| sm_protocol.c/h       | Binary message framing for service-to-SM communication         |
| sm_registry.c/h       | In-memory registry of all registered services and their status |
| sm_handlers.c/h       | Dispatches incoming requests (register, lookup, ping, stop)    |
| sm_connection_pool.c/h| Reuses socket connections to avoid per-request connect cost    |
| sm_request_id.c/h     | Monotonically increasing request IDs for correlation           |

#### enterprise/

| File                   | What it does                                                  |
|------------------------|---------------------------------------------------------------|
| sm_watchdog.c/h        | Heartbeat monitor — restarts a service if ping times out      |
| sm_eventbus.c/h        | Pub/sub event bus for service state change notifications      |
| sm_discovery.c/h       | Name-based service lookup (used by proxy layer)               |
| sm_tls.c/h             | Optional TLS encryption for inter-service IPC                 |
| sm_plugin.c/h          | Plugin system for loading custom service handlers at runtime  |
| sm_cli.c/h             | Command-line interface for operator control                   |
| sm_threadpool.c/h      | Thread pool for concurrent request handling                   |
| sm_rolling_restart.c/h | Zero-downtime service update by restarting one instance at a time |
| sm_container.c/h       | Optional container (namespaced process) launch support        |

#### security/

| File                      | What it does                                               |
|---------------------------|------------------------------------------------------------|
| sm_security.c/h           | Top-level security check on every incoming request        |
| sm_crypto.c/h             | HMAC-SHA256 message signing and verification               |
| sm_replay.c/h             | Replay attack detection using nonce + timestamp window     |
| sm_rate_limit.c/h         | Per-client request rate limiting                           |
| sm_advanced_ratelimit.c/h | Token bucket algorithm for burst-tolerant rate limiting    |

#### observability/

| File                        | What it does                                             |
|-----------------------------|----------------------------------------------------------|
| sm_health.c/h               | Health state machine per registered service              |
| sm_health_callbacks.c/h     | Callbacks invoked when health transitions (e.g. UP->DOWN)|
| sm_audit.c/h                | Audit log of all security-relevant events                |
| sm_logging.c/h              | Structured syslog and file logging for the SM daemon     |
| sm_structured_log.c/h       | JSON-formatted log entries for log aggregation tools     |
| sm_metrics.c/h              | Internal counters and gauges exposed to monitord         |

---

## Module 2 — HAL (Hardware Abstraction Layer)

**Location:** `dev/hal/`

The HAL hides the Linux driver API from everything above it. All drivers implement the same `hal_interface_t` struct of function pointers. Services never call `open("/dev/...")` directly — they call `hal_open()`, `hal_read()`, `hal_write()`, `hal_close()` through the interface.

### Architecture

```
service code
     |
     | hal_interface_t *hal = hal_get("audio");
     | hal->open(hal, config);
     | hal->read(hal, buf, len);
     v
+-------------------+
|   hal_interface   |   (interface/hal_interface.h)
|   .open           |----> audio_hal_open()
|   .read           |----> audio_hal_read()
|   .write          |----> audio_hal_write()
|   .close          |----> audio_hal_close()
|   .get_info       |----> audio_hal_get_info()
+-------------------+
     |
     v
Linux kernel driver APIs
 (ALSA, V4L2, GPIO chardev, IIO)
```

### HAL drivers

| Driver         | Location              | Kernel API used     | What it does                        |
|----------------|-----------------------|---------------------|-------------------------------------|
| audio_hal      | layers/audio/         | ALSA (libasound)    | PCM capture/playback, sample rates  |
| camera_hal     | layers/camera/        | V4L2                | Frame capture, format negotiation   |
| gpio_hal       | layers/gpio/          | GPIO character dev  | Pin direction, read, write, events  |
| sensor_hal     | layers/sensors/       | IIO subsystem       | Temperature, accelerometer, etc.    |

---

## Module 3 — Security

**Location:** `dev/security/`

The security module enforces the principle of least privilege on every service process in the middleware. It runs its checks during service startup and monitors for violations at runtime.

### Four security mechanisms

```
Service process startup
        |
        v
+---------------------+
|  security_manager   |   orchestrates all four mechanisms
+---------------------+
    |    |    |    |
    v    v    v    v
 seccomp  sandbox  capabilities  verify
```

### seccomp/ — System call filtering

Each service is given a seccomp BPF filter that allows only the exact system calls it needs. If a compromised service tries to call `execve()`, `ptrace()`, or any other forbidden syscall, the kernel kills it immediately.

| Filter file             | Used by                  | Allowed syscalls                   |
|-------------------------|--------------------------|------------------------------------|
| seccomp_policy_audio.h  | audio_service            | read, write, ioctl (ALSA only)     |
| seccomp_policy_camera.h | camera_service           | read, mmap, ioctl (V4L2 only)      |
| seccomp_policy_sensor.h | sensor_service           | read, write (IIO sysfs)            |
| seccomp_policy_minimal.h| any sandboxed process    | Minimum safe set                   |
| seccomp_policy_network.h| network-enabled services | socket, connect, send, recv        |

Key files:
- `seccomp_core.h` — loads and applies a filter to the calling process
- `seccomp_filter.h` — builds a BPF filter from a policy struct

### sandbox/ — Process isolation

Each service runs in its own isolated environment using Linux namespaces and cgroups.

| File                 | What it enforces                                            |
|----------------------|-------------------------------------------------------------|
| sandbox_core.c/h     | Entry point — applies all sandbox restrictions at startup   |
| sandbox_cgroup.c/h   | CPU and memory limits via cgroups v2                        |
| sandbox_mount.c/h    | Read-only filesystem mounts, pivot_root                     |
| sandbox_network.c/h  | Network namespace isolation (no external network by default)|

### capabilities/ — Linux capabilities

Drops all Linux capabilities that a service does not need. A service that only needs to open a `/dev` node gets `CAP_DAC_READ_SEARCH` stripped after open. No service runs with full root privileges.

| File                      | What it does                                            |
|---------------------------|---------------------------------------------------------|
| capabilities_core.c/h     | Drop capabilities after startup, enforce policy         |
| capabilities_policy.c/h   | Per-service capability allow-lists                      |
| capabilities_audit.c/h    | Logs every capability check and any violation           |

### verify/ — Message integrity

All IPC messages between services are authenticated with HMAC-SHA256. Replay attacks are blocked by a timestamp + nonce window.

| File              | What it does                                                    |
|-------------------|-----------------------------------------------------------------|
| verify_hmac.h     | HMAC-SHA256 sign and verify on message payloads                 |
| verify_replay.h   | Nonce + timestamp window to reject replayed messages            |
| verify_token.h    | Short-lived token generation for service authentication         |

---

## Module 4 — Proxy

**Location:** `dev/proxy/`

The proxy layer is a shared library (`libmiddleware_proxy.so`) that application code links against. It provides a typed, high-level API so that calling code never has to know the details of Unix sockets, HMAC signing, or service discovery.

### How a proxy call works

```
Application
    |
    | sensor_proxy_read(proxy, &reading)
    v
sensor_proxy.c
    |
    | 1. Look up service address via service_discovery
    | 2. Get or reuse a connection from proxy_connection pool
    | 3. Build typed request message
    | 4. Sign with proxy_auth (HMAC)
    | 5. Send over Unix socket (non-blocking via proxy_event_loop)
    | 6. Wait for response (via io_uring or epoll)
    | 7. Verify response signature
    | 8. Deserialize into sensor_reading_t
    v
sensor_reading_t returned to caller
```

### Proxy components

| File/Folder            | What it provides                                                 |
|------------------------|------------------------------------------------------------------|
| base/service_proxy.h   | Base proxy struct all specific proxies inherit from              |
| base/proxy_connection.h| Connection pool: reuse existing sockets instead of reconnecting  |
| base/proxy_event_loop.h| Non-blocking I/O loop for proxy operations                       |
| base/proxy_config.h    | Per-proxy configuration (timeouts, retry policy, buffer sizes)   |
| base/proxy_shared_memory.h | Zero-copy shared memory for large data (audio/video frames) |
| base/proxy_result.h    | Typed result/error codes returned to applications                |
| base/proxy_thread_pool.h | Thread pool for concurrent proxy requests                      |
| discovery/service_discovery.h | Resolves service names to socket addresses              |
| security/proxy_auth.h  | HMAC signing of outgoing requests, verification of responses     |
| bindings/c_api/proxy_c_api.h | Plain C wrapper for all proxy types                      |

### Per-hardware proxies

| Proxy           | Header               | Data type returned           |
|-----------------|----------------------|------------------------------|
| audio_proxy     | audio/audio_proxy.h  | audio_frame_t                |
| camera_proxy    | camera/camera_proxy.h| camera_frame_t (V4L2 format) |
| gpio_proxy      | gpio/gpio_proxy.h    | pin value, event subscription|
| sensor_proxy    | sensor/sensor_proxy.h| sensor_reading_t             |

Audio and camera proxies also support shared memory delivery (`audio_shm_reader.h`, `camera_shm_reader.h`) so that large frame data is transferred as a zero-copy pointer rather than a socket copy.

---

## Module 5 — Services

**Location:** `dev/services/`

Each service is an independent Linux process. It starts up, registers with the service manager, applies its security sandbox, connects to its HAL driver, and enters a processing loop. All four services follow the same internal structure.

### Common infrastructure (services/common/)

| File              | What it provides                                                    |
|-------------------|---------------------------------------------------------------------|
| service_base.h    | Logging macros (`LOG_INFO`, `LOG_ERR`) with HH:MM:SS timestamps     |
| service_config.h  | Config file loading and validation for service INI files            |
| service_ipc.h     | IPC helpers: connect to SM, send/receive typed messages             |

### Service structure (same for all four)

```
service_main.c       entry point: parse args, load config, init security, start loop
service_loop.c       main processing loop: read from HAL, process, send response
service_hal.c        glue between the service and the HAL layer
service_security.c   applies seccomp filter, drops capabilities, enters sandbox
```

### audio_service

- Opens ALSA device through `audio_hal`
- Captures PCM frames at configured sample rate
- Forwards frames to subscribers via shared memory ring buffer
- Applies per-frame HMAC tag so receivers can verify integrity

### camera_service

- Opens V4L2 camera device through `camera_hal`
- Captures frames using mmap'd V4L2 buffers (zero kernel-to-userspace copy)
- Forwards frames to subscribers via shared memory
- Supports basic frame rate control

### gpio_service

- Opens GPIO character device through `gpio_hal`
- Manages pin direction (input/output) and pull resistors
- Supports edge-triggered event subscriptions
- Translates kernel GPIO events to middleware event messages

### sensor_service

- Reads from Linux IIO subsystem through `sensor_hal`
- Supports temperature, acceleration, humidity, pressure sensor types
- Converts raw ADC counts to physical units (degrees C, m/s^2, etc.)
- Publishes readings at configurable intervals

---

## Module 6 — Monitoring

**Location:** `dev/monitoring/`

The monitoring module is a complete observability stack. It runs as two separate programs: `monitord` (daemon, always running) and `mw_tui` (interactive terminal dashboard, run on demand).

### Two-program architecture

```
+----------------------------------------------------------+
|                     monitord                             |
|                                                          |
|  Collectors (13 threads, 1 per subsystem)                |
|    sysinfo  processes  sm  memory_pool  io_uring         |
|    ring_buffer  security  proxy  ipc_channels            |
|    config_watcher  hal  watchdog  services               |
|          |                                               |
|          | write under per-subsystem rwlock              |
|          v                                               |
|  monitord_state_t  (central shared struct)               |
|          |                                               |
|          | serialize to mon_snapshot_t every 1 second    |
|          v                                               |
|  Unix socket server (/tmp/middleware_monitor.sock)       |
|  HTTP server (:9090   Prometheus /metrics)               |
|  Alert engine  (26 rules, 3 severities)                  |
+----------------------------------------------------------+
              |
              | Unix socket push  (framed binary protocol)
              v
+----------------------------------------------------------+
|                      mw_tui                              |
|                                                          |
|  receive snapshot -> ui_layout_render()                  |
|                                                          |
|  Tab 1: Overview      CPU, RAM, load, uptime, health     |
|  Tab 2: Services      per-service table (CPU/RAM/PID)    |
|  Tab 3: Memory        pool fill levels, alloc failures   |
|  Tab 4: Security      flags, violations, sandbox status  |
|  Tab 5: HAL           device error rates, request counts |
|  Tab 6: I/O           io_uring, ring buffer stats        |
|  Tab 7: Alerts        active alerts by severity          |
|  Tab 8: Logs          scrollable HH:MM:SS log viewer     |
|  Tab 9: Traces        waterfall chart for request spans  |
|  Tab 0: Help          keyboard shortcut reference        |
+----------------------------------------------------------+
```

### Collector state machine

Every collector thread follows this lifecycle:

```
WAITING ---[resource found]---> CONNECTING
    ^                                |
    |                           [connect() ok]
    |                                v
    |                          SYNCING
    |                                |
    |                         [first tick ok]
    |                                v
    |                             LIVE <---[tick ok]
    |                                |
    |                          [3 missed ticks]
    |                                v
    |                            STALE
    |                                |
    |                         [socket closed]
    |                                v
    +------[after retry_ms]----  OFFLINE
```

### Alert rules — 26 rules across 3 severities

**CRITICAL (7 rules)**

| Rule | Condition |
|------|-----------|
| 1 | Service restart detected |
| 2 | Seccomp violation (possible attack) |
| 3 | HMAC verification failure |
| 4 | Memory pool exhaustion |
| 5 | io_uring completion queue overflow |
| 6 | Ring buffer message drops |
| 7 | Health score below 50 |

**WARNING (14 rules)**

| Rule | Condition |
|------|-----------|
| 8 | Health score between 50 and 70 |
| 9 | CPU usage above 80% for 30 seconds |
| 10 | RAM usage above 85% for 30 seconds |
| 11 | File descriptor leak |
| 12 | Service p99 latency above threshold |
| 13 | Message drop rate above 1% |
| 14 | HAL error rate above 5% |
| 15 | IPC queue depth above 80% |
| 16 | Replay attack detected |
| 17 | Collector STALE (missed 3 intervals) |
| 18 | Collector OFFLINE |
| 19 | Watchdog starvation |
| 20 | Config file changed unexpectedly |
| 21 | Swap usage above 50% |

**INFO (5 rules)**

| Rule | Condition |
|------|-----------|
| 22 | Service started |
| 23 | Service stopped |
| 24 | Health score between 70 and 90 |
| 25 | Collector reconnected |
| 26 | Config reload successful |

### Health score formula

Each service starts with a score of 100. Deductions are applied:

```
score = 100
      - (restart_count         x 10)
      - (drop_rate_pct         x  5)
      - (p99_over_threshold    x 15)
      - (seccomp_violations    x 50)
      - (fd_leak_detected      x 20)
      - (hmac_failures         x 30)
      - (memory_pool_exhausted x 25)
      - (uring_cq_overflow     x 20)

System health = minimum score across all services
```

Grades: 90-100 = EXCELLENT, 70-89 = GOOD, 50-69 = DEGRADED, 0-49 = CRITICAL

### Metrics types

| Type        | Description                               | Example use               |
|-------------|-------------------------------------------|---------------------------|
| gauge_t     | A value that goes up and down             | CPU%, RAM bytes, queue depth |
| counter_t   | A value that only increases               | Total messages, total errors |
| histogram_t | A distribution across configurable buckets| Request latency            |
| sparkline_t | A small recent-values circular buffer     | CPU chart in Overview panel|

### Monitoring documentation

Detailed documentation for each sub-folder is in `dev/monitoring/docs/`:

| File                                   | Covers                         |
|----------------------------------------|--------------------------------|
| docs/OVERVIEW.md                       | Full architecture and data flow |
| docs/ALERTS.md                         | All 26 rules, deduplication, notifications |
| docs/COLLECTORS.md                     | Every collector, state machine, how to add one |
| docs/DAEMON.md                         | monitord startup, state struct, servers |
| docs/HEALTH.md                         | Score formula, grades, history  |
| docs/METRICS.md                        | Types, registry, Prometheus exporter |
| docs/PROTOCOL.md                       | Wire format, message types, snapshot struct |
| docs/TRACING.md                        | Spans, traces, waterfall renderer |
| docs/TUI.md                            | All panels, keyboard shortcuts, rendering |

---

## Build system

The project uses CMake 3.25+ with a set of helper modules.

| File / Module           | What it controls                                            |
|-------------------------|-------------------------------------------------------------|
| CMakeLists.txt          | Top-level project declaration, subdirectory inclusion       |
| CMakePresets.json       | Named presets: dev (Debug+ASan), relwithdebinfo (release)   |
| VERSION                 | Single source of version: 2.1.0                             |
| cmake/Versioning.cmake  | Reads VERSION into cmake variables, generates version.h     |
| cmake/BuildOptions.cmake| -O2/-O0, -Wall -Wextra, LTO toggle                         |
| cmake/Hardening.cmake   | -fstack-protector-strong, RELRO, PIE, _FORTIFY_SOURCE=2     |
| cmake/Sanitizers.cmake  | -fsanitize=address,undefined,thread (dev preset)            |
| cmake/StaticAnalysis.cmake | clang-tidy and cppcheck rules                            |
| cmake/Packaging.cmake   | CPack DEB and RPM config for distribution packages          |
| cmake/toolchain/        | Cross-compile for linux-arm and linux-aarch64               |

### Installed binaries

| Binary          | What it is                    |
|-----------------|-------------------------------|
| sm_daemon       | Service manager daemon        |
| audio_service   | Audio hardware service        |
| camera_service  | Camera hardware service       |
| gpio_service    | GPIO hardware service         |
| sensor_service  | Sensor hardware service       |
| monitord        | Monitoring daemon             |
| mw_tui          | Terminal dashboard            |

---

## How to build

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt install cmake ninja-build gcc libncurses-dev \
                 liburing-dev libseccomp-dev libcap-dev
```

### Build (release with debug info)

```bash
# Configure
cmake --preset relwithdebinfo

# Build everything
cmake --build build/relwithdebinfo --parallel $(nproc)

# Install to install/relwithdebinfo/
cmake --install build/relwithdebinfo
```

### Build (dev — with Address Sanitizer)

```bash
cmake --preset dev
cmake --build build/dev --parallel $(nproc)
```

### Cross-compile for ARM

```bash
cmake -B build/arm \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/linux-arm.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/arm --parallel $(nproc)
```

---

## How to run

### Start all services

```bash
# The Bankai.sh script manages start/stop/sync
sudo bash Bankai.sh start
```

### Start individually

```bash
# Service manager first (other services depend on it)
sudo install/relwithdebinfo/sbin/sm_daemon --config /etc/middleware/sm.ini &

# Hardware services
sudo install/relwithdebinfo/sbin/audio_service  &
sudo install/relwithdebinfo/sbin/gpio_service   &
sudo install/relwithdebinfo/sbin/sensor_service &
sudo install/relwithdebinfo/sbin/camera_service &

# Monitoring daemon
sudo install/relwithdebinfo/sbin/monitord &

# Terminal dashboard (run in any terminal)
install/relwithdebinfo/sbin/mw_tui
```

### TUI keyboard shortcuts

| Key         | Action                          |
|-------------|---------------------------------|
| 1 through 9 | Jump to panel 1-9               |
| 0           | Help panel                      |
| Tab         | Next panel                      |
| Shift+Tab   | Previous panel                  |
| Up / k      | Scroll up                       |
| Down / j    | Scroll down                     |
| Page Up/Dn  | Scroll one page                 |
| r / F5      | Force refresh                   |
| q / Q       | Quit                            |

---

## Data flow — end to end

This trace shows what happens from the moment an application asks for a sensor reading to the moment the value is displayed in the monitoring dashboard.

```
1. Application calls:
      sensor_reading_t r;
      sensor_proxy_read(proxy, &r);

2. sensor_proxy.c:
   - Calls service_discovery to find sensor_service socket path
   - Builds a typed request struct
   - Signs with HMAC via proxy_auth
   - Sends over Unix socket

3. sensor_service (receives request):
   - Verifies HMAC — rejects if invalid
   - Checks replay nonce — rejects if replayed
   - Calls sensor_hal->read() to get raw IIO value
   - Converts raw value to physical units
   - Signs response with HMAC
   - Sends response back over socket

4. sensor_proxy.c (receives response):
   - Verifies HMAC on response
   - Deserializes into sensor_reading_t
   - Returns to application

5. Meanwhile — collector_processes.c (every 1 second):
   - Scans /proc for sensor_service PID
   - Reads /proc/<pid>/stat for CPU% and RSS
   - Writes to state->services[3] under lock_services

6. monitord main loop (every 1 second):
   - Serializes all state into mon_snapshot_t
   - Broadcasts snapshot over Unix socket to all connected mw_tui clients
   - Evaluates 26 alert rules against the snapshot

7. mw_tui receives snapshot:
   - Updates local snapshot copy
   - Calls ui_layout_render()
   - panel_services.c displays sensor_service row with updated CPU/RAM values
   - panel_overview.c shows system health grade
```

---

## License

This project is open source and released under a custom open-source license.
See the [LICENSE](LICENSE) file for the full terms.

**Summary of key terms:**
- You are free to use, study, modify, and contribute to this software.
- You must credit the original authors in any distribution or derivative work.
- You may not remove author names or claim this work as your own.
- Contributions are welcome via Pull Request and are reviewed by the maintainers before merging.

---

## Credits

This project was designed and built by:

**Muhammad Imtinan ul Haq**
**Abdullah Ahmad Khan**

Both contributed to the architecture, implementation, and testing of all modules in this platform.
