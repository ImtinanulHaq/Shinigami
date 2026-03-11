# Collectors Subsystem

## Purpose

Collectors are the data gathering layer of monitord. Each collector is a thread that runs in the background, wakes up every second, reads data from one part of the middleware (a process, a kernel interface, a shared memory region, or a socket), and writes the result into the shared `monitord_state_t` under a lock.

Think of them as sensors. Every collector knows how to read exactly one kind of metric. They all run independently and do not know about each other.

---

## Files in this folder

| File                         | What it collects                                               |
|------------------------------|----------------------------------------------------------------|
| collector_base.c/h           | Thread framework shared by all collectors (not a collector itself) |
| collector_sysinfo.c/h        | System-wide CPU%, RAM, load average, uptime from /proc/stat   |
| collector_processes.c/h      | Per-service CPU%, RAM, PID by scanning /proc for known names  |
| collector_sm.c/h             | ServiceManager daemon metrics via its IPC socket              |
| collector_watchdog.c/h       | Watchdog stub (slot 1 owned by processes collector)           |
| collector_hal.c/h            | HAL stub (slot 2 owned by processes collector)                |
| collector_services.c/h       | Services stub (slot 3 owned by processes collector)           |
| collector_memory_pool.c/h    | Custom memory pool allocation stats                           |
| collector_io_uring.c/h       | io_uring submission/completion queue depths and overflow      |
| collector_ring_buffer.c/h    | Ring buffer fill levels and drop counters                     |
| collector_security.c/h       | Seccomp violations, HMAC failures, capability checks          |
| collector_proxy.c/h          | Proxy layer request counts, latency, drop rate                |
| collector_ipc_channels.c/h   | IPC channel queue depth and error rates                       |
| collector_config_watcher.c/h | Detects changes to config files using inotify                 |

---

## collector_base.c/h - The thread framework

This file defines the `collector_t` struct and the lifecycle every collector goes through. You never call these functions directly from a collector - the framework calls them for you.

### Collector state machine

Every collector moves through these states:

```
WAITING
   |
   | (resource found / socket connects)
   v
CONNECTING
   |
   | (connect() callback returns 0)
   v
SYNCING
   |
   | (first tick succeeds)
   v
LIVE  <----> STALE  (missed 3 x interval without a successful tick)
   |              |
   +------+-------+
          |
          | (socket closed, file not found, etc.)
          v
       OFFLINE
          |
          | (after retry_interval_ms)
          v
       WAITING  (cycle restarts)
```

### The collector struct

```c
struct collector {
    char     name[32];          // human-readable name shown in the TUI
    uint32_t interval_ms;       // how often tick() is called  (must be >0)
    uint32_t retry_ms;          // how long to wait before retrying when OFFLINE

    collector_connect_fn    connect;      // called once when resource is found
    collector_tick_fn       tick;         // called every interval_ms when LIVE
    collector_disconnect_fn disconnect;   // called when going OFFLINE
};
```

Every collector must set `interval_ms` to a non-zero value in its `register` function. If it is 0, the collector thread spins at full CPU speed and CPU delta calculations in /proc will always be zero.

### Key framework functions

```c
int collector_register(collector_t *c);
```
Adds a collector to the global table. Must be called before `collector_start_all()`. The table holds a maximum of 16 collectors.

```c
int collector_start_all(struct monitord_state *state);
```
Spawns one `pthread` per registered collector. Each thread sleeps for `index * MON_STAGGER_DELAY_MS` before its first tick so they do not all wake up at the same moment.

```c
void collector_stop_all(uint32_t timeout_ms);
```
Sets the `stop_flag` on every collector and waits for all threads to exit cleanly within the timeout.

---

## collector_sysinfo.c/h - System metrics

Reads `/proc/stat` and `/proc/meminfo` and `/proc/loadavg` to produce overall system health numbers.

### What it fills in monitord_state

- `state->sysinfo.cpu_total_pct` - total CPU usage as a percentage (0.0 to 100.0)
- `state->sysinfo.ram_used_bytes` and `ram_total_bytes`
- `state->sysinfo.load_avg_1m`, `load_avg_5m`, `load_avg_15m`
- `state->sysinfo.uptime_seconds`

### How CPU percentage is calculated

The collector reads the cumulative user + system + idle ticks from `/proc/stat`. Each tick it computes the delta from the previous reading. CPU% = (active ticks delta / total ticks delta) * 100. This is why `interval_ms` must be at least 100 ms - if the interval is 0 ms the delta is effectively zero every time.

---

## collector_processes.c/h - Per-service process metrics

Scans `/proc` to find the actual installed middleware service processes and fill in per-service CPU and RAM numbers.

### Which process goes to which slot

| Slot | Process name   | Display label |
|------|----------------|---------------|
| 1    | audio_service  | AudioSvc      |
| 2    | gpio_service   | GPIOSvc       |
| 3    | sensor_service | SensorSvc     |

Slot 0 is owned by `collector_sm.c` (the ServiceManager daemon).

### How it finds a process

```c
static pid_t find_process_by_name(const char *name)
```
Opens `/proc`, iterates all numeric directories, reads each `/proc/[pid]/cmdline`, and checks if the `name` string appears anywhere in it. Returns 0 if not found.

### How it reads CPU and RAM

```c
static int read_process_stats(pid_t pid, uint64_t *rss_bytes, float *cpu_pct, ...)
```
Reads `/proc/[pid]/stat` and extracts:
- `utime` and `stime` (user and kernel CPU ticks for this process)
- `rss` (resident set size in pages, converted to bytes using `sysconf(_SC_PAGESIZE)`)

CPU% for the process is `(process ticks delta / total system ticks delta) * 100`, using the same delta approach as the sysinfo collector. Three separate sets of previous-tick state variables are kept (one per slot) so the deltas are independent.

### What it writes per slot

- `name` - the display label
- `pid` - current PID, or 0 if not running
- `running` - 1 if process was found, 0 otherwise
- `cpu_pct` - process CPU usage percentage
- `rss_bytes` - resident memory in bytes
- `health_score` - 100 if running, 0 if not
- `sandbox_ok`, `caps_ok`, `verify_ok`, `seccomp_ok` - all set to 1

---

## collector_sm.c/h - ServiceManager metrics

Connects to the ServiceManager (sm_daemon) IPC socket and reads service registration counts, message rates, and watchdog ping status.

---

## Stub collectors (watchdog, hal, services)

`collector_watchdog.c`, `collector_hal.c`, and `collector_services.c` are stubs. Their `tick()` functions do nothing because `collector_processes.c` already owns those slots with real /proc data.

They exist as placeholders. In the future they can be replaced with real collectors that talk to specific hardware watchdog chips or HAL driver sockets.

---

## collector_memory_pool.c/h - Memory pool stats

Reads allocation statistics from the custom memory pool in `dev/core/memory_pool.c`. Reports:
- Total pool capacity in bytes
- Currently allocated memory
- Number of allocation failures (used to fire a CRITICAL alert if it is non-zero)

---

## collector_io_uring.c/h - io_uring stats

Reads queue depths and overflow counters from the io_uring instances managed by `dev/core/io_uring_loop.c`. Reports submission queue fill level, completion queue fill level, and number of dropped events.

---

## collector_ring_buffer.c/h - Ring buffer stats

Reads statistics from the ring buffers in `dev/core/ring_buffer.c`. Reports per-buffer fill level, total messages produced, total messages consumed, and drop counter.

---

## collector_security.c/h - Security metrics

Reads security violation counters from `dev/security/`. Reports:
- Seccomp violation count (triggers CRITICAL alert immediately)
- HMAC verification failure count
- Capability check failures
- Binary verification failures

---

## collector_proxy.c/h - Proxy metrics

Reads from the proxy layer in `dev/proxy/`. Reports per-proxy-instance request rates, error rates, p99 latency, and active connection count.

---

## collector_ipc_channels.c/h - IPC channel metrics

Monitors the IPC channels used by services to communicate with each other. Reports queue depth and error rate per channel.

---

## collector_config_watcher.c/h - Config file watcher

Uses Linux `inotify` to watch the config files for changes. When a change is detected it sets a flag that causes the daemon to trigger a CONFIG_CHANGED INFO alert.

---

## How to add a new collector

1. Create `collector_myname.c` and `collector_myname.h` in this folder.
2. Define a static `collector_t g_myname_collector` struct.
3. Implement `myname_connect()`, `myname_tick()`, and `myname_disconnect()` functions.
4. In `myname_disconnect()` call `myname_disconnect_register()` which calls `collector_register()`.
5. Set `interval_ms = 1000` (or another appropriate value).
6. Add `#include "collector_myname.h"` and call `collector_myname_register()` in `monitord_main.c`.
7. Add the new .c file to the `mw_mon_collectors` CMake target in `CMakeLists.txt`.
