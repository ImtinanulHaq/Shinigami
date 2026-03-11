# Daemon Subsystem

## Purpose

The daemon folder contains the core of `monitord` - the monitoring background process. It ties all the other parts together: it starts the collectors, manages the shared state, serializes snapshots, and serves them to connected clients.

---

## Files in this folder

| File                 | What it does                                                         |
|----------------------|----------------------------------------------------------------------|
| monitord_main.c      | Entry point, startup sequence, main loop, signal handling            |
| monitord_state.c/h   | Central shared state struct and snapshot serializer                  |
| monitord_config.c/h  | INI configuration file parsing                                       |
| monitord_server.c/h  | Unix socket server - accepts TUI connections, pushes snapshots       |
| monitord_http.c/h    | HTTP server - serves Prometheus metrics on a TCP port                |

---

## monitord_main.c - Entry point

This is the `main()` function of the monitord process. It runs the following startup sequence in order:

### Step 1 - Parse arguments
```
monitord [--config /path/to/monitord.ini]
```
If no config path is given, it falls back to a built-in default path.

### Step 2 - Load configuration
Calls `monitord_config_load()`. If the config file does not exist, built-in defaults are used so the daemon can start without any setup.

### Step 3 - Initialize state
Calls `monitord_state_init()` which zeroes all metric structs and initializes all 12 read-write locks.

### Step 4 - Register and start collectors
Calls each `collector_*_register()` function in sequence, then calls `collector_start_all()` which spawns one pthread per collector with staggered startup delays.

### Step 5 - Start servers
Starts the Unix socket server and the HTTP server in their own threads.

### Step 6 - Main loop
Runs until SIGINT or SIGTERM arrives:
1. Sleep for `refresh_interval_ms`.
2. Call `monitord_state_serialize_snapshot()` to copy current values into a `mon_snapshot_t`.
3. Pass the snapshot to `monitord_server_broadcast()` which sends it to all connected TUI clients.
4. Call `alert_rules_evaluate()` to check all 26 rules against the new snapshot.

### Step 7 - Clean shutdown
When the stop flag is set by the signal handler:
1. `collector_stop_all()` - joins all collector threads.
2. `monitord_server_stop()` - closes the Unix socket and disconnects clients.
3. `monitord_http_stop()` - stops the HTTP server.
4. `monitord_state_destroy()` - destroys all locks and frees resources.
5. Return 0.

---

## monitord_state.c/h - The shared state

`monitord_state_t` is the single large struct that holds every metric from every subsystem. It lives in the daemon process for the entire duration that monitord is running.

### Structure layout

```c
typedef struct monitord_state {
    // Metric arrays - one entry per subsystem instance
    sysinfo_metrics_t      sysinfo;
    sm_metrics_t           sm;
    watchdog_metrics_t     watchdog;
    hal_metrics_t          hal[HAL_MAX_DEVICES];
    service_metrics_t      services[SERVICE_MAX];    // max 16 services
    pool_metrics_t         pools[POOL_MAX];
    uring_metrics_t        urings[URING_MAX];
    ringbuf_metrics_t      ringbufs[RINGBUF_MAX];
    security_metrics_t     security;
    proxy_metrics_t        proxy[PROXY_MAX];
    ipc_metrics_t          ipc_channels[IPC_CHAN_MAX];

    // One read-write lock per subsystem
    pthread_rwlock_t  lock_alerts;
    pthread_rwlock_t  lock_collectors;
    pthread_rwlock_t  lock_hal;
    // ... (12 locks total)

    // Health, alerts, and tracing
    health_history_t   health_history_system;
    health_history_t   health_history_services[SERVICE_MAX];
    alert_state_t      alert_state;
    trace_store_t      trace_store;

    // Metadata
    uint64_t   snapshot_seq;    // increments by 1 every broadcast
    uint64_t   boot_time_ms;    // monotonic timestamp when daemon started
} monitord_state_t;
```

### Why separate locks per subsystem

If there was a single global lock, every collector write would block every other collector for its full tick duration. With one lock per subsystem, the sysinfo collector and the security collector can run their ticks in parallel. They only conflict with each other if they try to write the same subsystem at the same time, which never happens by design.

### Snapshot serialization

```c
void monitord_state_serialize_snapshot(monitord_state_t *state,
                                        mon_snapshot_t *snapshot);
```

This function:
1. Acquires all 12 read-write locks in alphabetical order (same order always, preventing deadlock).
2. Copies each metric array into the corresponding field of `mon_snapshot_t`.
3. Increments `snapshot_seq`.
4. Releases all 12 locks.

The total time with locks held is under 1 ms because it is just `memcpy` calls with no I/O or computation.

---

## monitord_config.c/h - Configuration

The config file is a simple INI format file read at startup. All values have sensible defaults so the file is optional.

### Key configuration values

| Key                   | Default                           | Meaning                                |
|-----------------------|-----------------------------------|----------------------------------------|
| unix_socket_path      | /tmp/middleware_monitor.sock      | Where the TUI connects                 |
| http_port             | 9090                              | Prometheus metrics HTTP endpoint       |
| refresh_interval_ms   | 1000                              | How often to broadcast snapshots       |
| alert_log_path        | /var/log/monitord_alerts.log      | Where alert notifications are written  |
| webhook_url           | (empty)                           | Optional alert webhook endpoint        |

### Key functions

```c
int monitord_config_load(monitord_config_t *config, const char *path);
```
Reads the INI file at `path`. If `path` is NULL, tries the default path. If the file does not exist, fills `config` with built-in defaults and returns 0 (not an error).

---

## monitord_server.c/h - Unix socket server

This is the server that TUI clients connect to. It listens on a Unix domain socket, accepts connections, and pushes snapshots to each connected client every refresh cycle.

### How it works

1. Creates a `SOCK_STREAM` Unix socket at `config.unix_socket_path`.
2. Runs an accept loop in a background thread.
3. Each accepted client gets its own entry in a small connection table.
4. When `monitord_server_broadcast(snapshot)` is called from the main loop, it calls `mon_send_msg()` on every connected client fd to push the snapshot.
5. Clients that have disconnected (write returns EOF or error) are removed from the table.

### Key functions

```c
int monitord_server_start(const char *socket_path);
```
Binds the socket and starts the accept-loop thread.

```c
void monitord_server_broadcast(const mon_snapshot_t *snapshot);
```
Serializes the snapshot using the wire format and sends it to every connected client.

```c
void monitord_server_stop(void);
```
Closes the socket, disconnects all clients, joins the accept thread.

---

## monitord_http.c/h - HTTP server for Prometheus

This module provides an HTTP endpoint that Prometheus (or any monitoring tool that can scrape Prometheus format) can use to pull metrics from the daemon.

### How it works

1. Binds a TCP port (default 9090) in a background thread.
2. When a GET request arrives on `/metrics`, it iterates the metrics registry and formats each entry in Prometheus text exposition format.
3. Returns the formatted text as the HTTP response body.

### Prometheus format example

```
# HELP cpu_total_pct Total CPU usage percentage
# TYPE cpu_total_pct gauge
cpu_total_pct 42.3

# HELP service_restart_count_total Service restart events
# TYPE service_restart_count_total counter
service_restart_count_total{service="audio_service"} 0
```

### Key functions

```c
int monitord_http_start(uint16_t port);
```
Starts the HTTP server thread on the given port.

```c
void monitord_http_stop(void);
```
Stops the HTTP server and closes the port.
