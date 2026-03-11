# Monitoring System - Complete Overview

## What is this system?

This is the monitoring sub-system of the Linux middleware project. Its job is to watch every other part of the middleware (services, memory, security, hardware) and show all of that information to an operator in real time through a terminal dashboard.

The system has two separate programs that work together:

- **monitord** - a background daemon that collects data from all parts of the middleware every second and broadcasts it over a Unix socket.
- **mw_tui** - an interactive terminal UI (the dashboard) that connects to monitord, receives snapshots, and displays them in panels.

---

## How the two programs communicate

```
 [middleware services]
        |
        | (read /proc, shared memory, IPC sockets)
        v
 +------------------+
 |    monitord      |  <-- background daemon, always running
 |                  |
 |  - Collectors    |  collect metrics from every subsystem
 |  - State         |  stores all current values in one big struct
 |  - Alert Engine  |  checks rules, fires alerts when limits are exceeded
 |  - Unix server   |  pushes snapshots to connected clients every 1 second
 |  - HTTP server   |  exposes Prometheus metrics on a TCP port
 +------------------+
        |
        | Unix socket  /tmp/middleware_monitor.sock
        | (snapshot struct pushed every refresh_interval_ms)
        v
 +------------------+
 |    mw_tui        |  <-- terminal dashboard, run manually by operator
 |                  |
 |  - Panels        |  overview, services, memory, security, logs, etc.
 |  - Input handler |  keyboard navigation, panel switching
 |  - Renderer      |  ncurses, no blinking, diff-only screen updates
 +------------------+
```

---

## Folder structure inside dev/monitoring/

| Folder       | What it contains                                                  |
|--------------|-------------------------------------------------------------------|
| alerts/      | Alert rules, alert engine, and notification delivery              |
| collectors/  | One collector per subsystem - reads raw data and fills state      |
| daemon/      | monitord main entry point, config loading, state, socket server   |
| health/      | Health score computation and history for each service             |
| metrics/     | Metric type definitions, registry, delta calculation, Prometheus  |
| protocol/    | The data structures and wire format shared between daemon and TUI |
| tracing/     | Distributed tracing - span collection, storage, waterfall render  |
| tui/         | All ncurses panels, input handling, color engine, layout          |

---

## Data flow from raw data to screen

1. Each collector runs in its own thread and wakes up every 1000 ms.
2. It reads from /proc, shared memory, or a component socket.
3. It writes the new values into the relevant fields of `monitord_state_t` under a per-subsystem read-write lock.
4. The monitord main loop wakes every `refresh_interval_ms` (default 1 second), acquires all 12 locks in a fixed order, copies everything into a `mon_snapshot_t`, and broadcasts it to every connected TUI client.
5. The TUI receives the snapshot over the Unix socket, updates its local copy, and re-renders only the panels that changed.

---

## Locking and thread safety

The shared state struct (`monitord_state_t`) has 12 separate read-write locks, one per subsystem. The lock acquisition order is strictly alphabetical to prevent deadlock:

```
alerts -> collectors -> hal -> health -> ipc_channels ->
pools -> proxy -> ringbufs -> security -> services -> sysinfo -> urings
```

Each collector only holds the lock for its own subsystem during the brief memcpy of new values. The snapshot serializer holds all 12 locks simultaneously for less than 1 ms.

---

## Startup sequence of monitord

1. Parse command line for `--config` path.
2. Load INI configuration file (socket path, HTTP port, refresh interval).
3. Initialize `monitord_state_t` - zeroes everything, initializes all 12 locks.
4. Register all collectors (sets up each collector thread descriptor).
5. Start all collector threads with staggered startup (each thread sleeps `index * 50 ms` before its first tick) so they do not all hit /proc at the same instant.
6. Start the Unix socket server thread - listens for TUI connections.
7. Start the HTTP server thread - serves Prometheus metrics.
8. Enter the main loop: serialize snapshot, broadcast to clients, evaluate alert rules, sleep until the next refresh.
9. On SIGINT or SIGTERM: stop all collector threads, shut down servers, destroy state, exit 0.

---

## Key constants

| Constant                  | Value   | Meaning                                        |
|---------------------------|---------|------------------------------------------------|
| SERVICE_MAX               | 16      | Maximum tracked middleware services            |
| ALERT_MAX_ACTIVE          | 1000    | Maximum simultaneous active alerts             |
| COLLECTOR_TABLE_MAX       | 16      | Maximum registered collectors                  |
| TRACE_STORE_MAX           | 1000    | Completed traces kept in circular buffer       |
| METRICS_REGISTRY_MAX      | 512     | Named metrics in the Prometheus registry       |
| MON_STAGGER_DELAY_MS      | 50      | Extra startup delay per collector index        |

---

## Files at the root of dev/monitoring/

- **middleware_monitor.c** - standalone single-file build of the monitor used for early testing and embedded environments. It duplicates minimal logic from the daemon without the full collector framework.
- **CMakeLists.txt** - CMake build rules for monitord, mw_tui, and the shared collector library.
