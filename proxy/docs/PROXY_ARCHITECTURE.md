# Middleware Proxy Layer — Architecture Documentation

## 1. Overview

The proxy layer is a C++17 client-side library
(`libmiddleware_proxy.so` / `.a`) that bridges application code to
the suite of C middleware daemons running on the embedded Linux target.

```
┌─────────────────────────────────────────────────────────┐
│                   Application / Service                 │
│         (C++17, Python-ctypes, Rust FFI, plain C)       │
└──────────────┬──────────────────────────────────────────┘
               │  C++ API  (Result<T>, callbacks)
               │  C API    (proxy_c_api.h opaque handles)
┌──────────────▼──────────────────────────────────────────┐
│              libmiddleware_proxy.so                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │AudioProxy│ │CameraProxy│ │SensorProxy│ │GpioProxy│   │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘   │
│       │             │             │             │         │
│  ┌────▼─────────────▼─────────────▼─────────────▼──────┐ │
│  │               ServiceProxy (base class)              │ │
│  │  ProxyConnection │ ProxyThreadPool │ ProxyEventLoop  │ │
│  │  ProxyAuth       │ ProxySharedMem  │ ServiceDiscovery│ │
│  └──────────────────┬────────────────────────────────────┘ │
└─────────────────────│───────────────────────────────────────┘
                      │  Unix domain sockets + POSIX SHM
┌─────────────────────▼───────────────────────────────────┐
│               C Middleware Daemons                       │
│  audio_daemon │ camera_daemon │ sensor_daemon │ gpio_daemon │
│               └──────────────────────────────┘           │
│                    ServiceManager (sm_protocol.h)         │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Directory Structure

```
proxy/
├── base/
│   ├── proxy_result.h          Result<T,E> monad
│   ├── proxy_config.h          All configuration in one struct
│   ├── proxy_connection.h/.cpp Unix socket + HMAC framing
│   ├── proxy_thread_pool.h/.cpp Lock-free MPSC callback dispatcher
│   ├── proxy_shared_memory.h/.cpp POSIX SHM RAII wrapper
│   ├── proxy_event_loop.h/.cpp epoll event loop
│   └── service_proxy.h/.cpp    Abstract base class
│
├── security/
│   └── proxy_auth.h/.cpp       RAII verify_context_t wrapper
│
├── discovery/
│   └── service_discovery.h/.cpp SM registry lookup
│
├── audio/
│   ├── audio_frame.h           AudioFrame + AudioShmHeader
│   ├── audio_shm_reader.h/.cpp Zero-copy SHM ring reader
│   └── audio_proxy.h/.cpp      Full AudioProxy API
│
├── camera/
│   ├── camera_frame.h          CameraFrame + CameraShmHeader
│   ├── camera_shm_reader.h/.cpp SHM ring reader for video frames
│   └── camera_proxy.h/.cpp     Full CameraProxy API
│
├── sensor/
│   ├── sensor_reading.h        52-byte SensorReading struct
│   └── sensor_proxy.h/.cpp     SensorProxy (socket-only, no SHM)
│
├── gpio/
│   └── gpio_proxy.h/.cpp       GpioProxy (configure/set/get/edges)
│
├── bindings/
│   └── c_api/
│       ├── proxy_c_api.h       Pure-C opaque-handle API
│       └── proxy_c_api.cpp     C shims over C++ proxies
│
├── testing/
│   ├── mocks/
│   │   ├── mock_service_daemon.h/.cpp   Fake Unix socket daemon
│   │   └── mock_sm_registry.h           SM registry stub
│   ├── unit/
│   │   ├── test_proxy_result.cpp
│   │   └── test_proxy_connection.cpp
│   └── integration/
│       └── test_proxy_sm_integration.cpp
│
├── examples/
│   ├── audio_capture_example.cpp
│   ├── camera_stream_example.cpp
│   └── sensor_poll_example.cpp
│
├── CMakeLists.txt
└── docs/
    └── PROXY_ARCHITECTURE.md   (this file)
```

---

## 3. Design Principles

### 3.1 Zero-Exception Error Handling

Every fallible function returns `Result<T, ProxyError>`.

```cpp
auto r = proxy.connect();
if (r.isErr()) {
    logger.error("connect failed: {}", r.message());
    return r;
}
// r.value() is safe here
```

`ProxyError` is a scoped enum (18 values). The `PROXY_TRY(expr)` macro
propagates errors early-return style.

### 3.2 Two-Thread I/O Model

Each `ServiceProxy` runs two private threads:

| Thread | Role |
|--------|------|
| **io_thread** | `epoll_wait` on the socket fd; reads frames; posts callbacks to MPSC queue |
| **callback_thread** | Drains MPSC queue; invokes application callbacks |

Callbacks are **never** called from the io thread. This prevents deadlocks
when a callback calls a proxy method that writes to the socket.

### 3.3 Lock-Free MPSC Queue

`ProxyThreadPool` implements a power-of-two ring of `Slot{fn, atomic<bool> ready}`.

- **Producers** (io thread, timer thread): claim a slot via `head_.fetch_add()`
  relaxed, then store payload and set `ready = true`.
- **Consumer** (callback thread): spins on `tail_` slot's `ready` flag, processes,
  clears ready.
- **Overflow**: oldest slot is overwritten and `overflow_handler_` is called
  with `ProxyError::BufferFull`.

### 3.4 Shared Memory Zero-Copy (Audio + Camera)

PCM/video frames travel via `shm_open` + `mmap`. The Unix socket carries only
signals (`AUDIO_MSG_FRAME_READY`, `CAMERA_MSG_FRAME_READY`).

SHM layout:

```
┌─────────────────────┐
│  AudioShmHeader     │  56 bytes: magic, version, slot_count, slot_size, ...
│  volatile wi/ri     │  write_index (daemon) / read_index (proxy)
├─────────────────────┤
│  slot[0]            │  slot_size_bytes each
│  slot[1]            │
│  ...                │
│  slot[N-1]          │
└─────────────────────┘
```

Frame data is **copied** out of the SHM slot before being posted to the
callback thread (the slot may be reused by the daemon immediately).

### 3.5 HMAC Authentication

`ProxyAuth` wraps `verify_context_t` (from `verify.h`).

- Each outgoing `sm_hdr_t` is signed over `header[0..HMAC_OFFSET-1] || payload`
  using `verify_sign_message()`.
- On receive, `verify_check_message()` validates the HMAC.  Failure → immediate
  socket close + reconnect.
- The key file path is wiped from memory after `verify_init_from_file`.
- Dev mode (empty `hmac_key_file`) uses a zero key — never use in production.

### 3.6 Reconnect Resilience

`ServiceProxy::startReconnectLoop()` runs a detached thread with exponential
backoff:

```
sleep = reconnect_initial_ms
loop:
    try connect
    on success → break
    sleep = min(sleep * 2, reconnect_max_ms)
    if attempt_count >= max_attempts → fire on_lost_cb_ and stop
```

Applications set `on_lost_cb_` to be notified of permanent loss.

---

## 4. Protocol Wire Format

All messages use `sm_hdr_t` from `sm_protocol.h` (56 bytes):

```
Offset  Size  Field
     0     4  magic       0x534D4B47 ('SMKG')
     4     1  version     2
     5     3  reserved
     8     2  msg_type    e.g. AUDIO_MSG_START_CAPTURE = 0x0101
    10     2  flags
    12     4  payload_len
    16     4  nonce       random u32 for replay protection
    20     4  reserved
    24     8  timestamp   CLOCK_MONOTONIC ns
    32    24  hmac        HMAC-SHA256 (first 24 bytes)
```

### Message type ranges

| Range       | Owner       |
|-------------|-------------|
| 0x0001–0x00FF | Service Manager (SM_MSG_*) |
| 0x0101–0x01FF | Audio daemon |
| 0x0201–0x02FF | Camera daemon |
| 0x0301–0x03FF | Sensor daemon |
| 0x0401–0x04FF | GPIO daemon |

---

## 5. C API Bindings (proxy_c_api.h)

The C bindings provide opaque handles for all four proxy types:

```c
AudioProxyHandle h = audio_proxy_create(NULL);   // NULL = defaults
ProxyErrCode rc    = audio_proxy_connect(h);
if (rc != PROXY_OK) { fprintf(stderr, "%s\n", proxy_err_str(rc)); }

audio_proxy_set_frame_callback(h, my_cb, userdata);
audio_proxy_start_capture(h);
// ...
audio_proxy_destroy(h);   // calls disconnect() internally
```

Error codes are defined in `ProxyErrCode` and map 1-to-1 to `ProxyError`.

---

## 6. Build

```bash
mkdir proxy/build && cd proxy/build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DMIDDLEWARE_BUILD_DIR=../../build/dev \
    -DBUILD_PROXY_EXAMPLES=ON
make -j$(nproc)
ctest --output-on-failure
```

CMake targets exported:

| Target | Type | Use |
|--------|------|-----|
| `middleware::proxy_shared` | shared lib | preferred for applications |
| `middleware::proxy_static` | static lib | for tests / embedded deployment |

Link your code:
```cmake
find_package(middleware_proxy REQUIRED)
target_link_libraries(myapp PRIVATE middleware::proxy_shared)
```

---

## 7. Thread Safety Summary

| Method | Thread Safe |
|--------|-------------|
| `connect()` / `disconnect()` | Yes (internal mutex) |
| `startCapture()` / `stopStream()` / etc. | Yes |
| `onFrameReady(cb)` | Yes (cb_mutex) |
| `onEdgeEvent(cb)` | Yes |
| Callbacks invoked by proxy | Always on callback thread only |
| `ProxyConfig` struct | Not after `connect()` |

---

## 8. Security Considerations

1. **Key material** — never logged; wiped with `explicit_bzero` before free.
2. **HMAC validation** — every received frame is verified; failure closes the socket.
3. **Nonce + timestamp** — replay window ≤ 5 seconds enforced by `verify.h`.
4. **SHM paths** start with `/middleware_proxy_` and include the process PID.
5. **Visibility** — all non-API symbols have `visibility=hidden`; only `PROXY_API`
   decorated symbols appear in the final `.so` ABI.

---

*Generated by GitHub Copilot — middleware proxy layer v1.0.0*
