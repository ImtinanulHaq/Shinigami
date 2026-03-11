# Protocol Subsystem

## Purpose

The protocol folder defines the communication contract between `monitord` and `mw_tui`. Both programs must agree on the exact byte layout of every message. Anything used by both sides - message types, data structures, constants, and wire-level framing - lives here.

Because both programs run on the same machine, there is no need for network byte-order conversion. All fields are in host byte order.

---

## Files in this folder

| File                      | What it defines                                                  |
|---------------------------|------------------------------------------------------------------|
| monitor_ipc_protocol.h    | All data structures: snapshots, metrics, alerts, traces          |
| monitor_protocol_ver.h    | Protocol version number and compatibility constants              |
| monitor_wire_format.h     | Inline functions for sending and receiving framed messages       |

---

## monitor_ipc_protocol.h - Data structures

This is the most important file in the protocol folder. It defines every struct that crosses the Unix socket boundary.

### Message header

Every message sent over the socket starts with this header:

```c
typedef struct {
    uint32_t  magic;    // always MON_MSG_MAGIC (0x4D4F4E44 = "MOND")
    uint8_t   type;     // mon_msg_type_t value
    uint8_t   version;  // MON_PROTOCOL_VERSION
    uint8_t   flags;    // reserved, always 0
    uint8_t   pad;      // alignment padding
    uint32_t  length;   // payload size in bytes
    uint32_t  seq;      // monotonic sequence number from sender
} mon_msg_header_t;
```

The magic number and version are checked on receive to detect corrupt or version-mismatched connections.

### Message types

```c
typedef enum {
    MON_MSG_HELLO     = 0x01,  // client -> server: announce presence
    MON_MSG_SNAPSHOT  = 0x02,  // server -> client: full system snapshot
    MON_MSG_PING      = 0x03,  // either direction: keepalive
    MON_MSG_PONG      = 0x04,  // response to PING
} mon_msg_type_t;
```

The most important message type is `MON_MSG_SNAPSHOT`. The server sends this every `refresh_interval_ms` to all connected clients. The payload is a serialized `mon_snapshot_t`.

### The snapshot structure

`mon_snapshot_t` is the complete system state at one point in time. It is a flat struct (no pointers, no dynamic allocation) so the entire thing can be sent over a socket with a single `write()`.

Key fields inside the snapshot:

| Field                 | Type                         | Description                                |
|-----------------------|------------------------------|--------------------------------------------|
| sysinfo               | sysinfo_metrics_t            | CPU%, RAM, load average, uptime            |
| sm                    | sm_metrics_t                 | ServiceManager message rates               |
| services[16]          | service_metrics_t array      | Per-service CPU, RAM, PID, health          |
| security              | security_metrics_t           | Violation counts, flags                    |
| pools[N]              | pool_metrics_t array         | Memory pool allocation stats               |
| urings[N]             | uring_metrics_t array        | io_uring queue depths                      |
| ringbufs[N]           | ringbuf_metrics_t array      | Ring buffer fill and drop counts           |
| proxy[N]              | proxy_metrics_t array        | Proxy request rates and latency            |
| ipc_channels[N]       | ipc_metrics_t array          | IPC queue depths                           |
| collectors[N]         | collector_info_t array       | Collector state machine status             |
| alerts                | alert_record_t array         | Active alerts at time of snapshot          |
| alert_count           | uint32_t                     | Number of valid entries in alerts[]        |
| traces                | trace_record_t array         | Recent distributed traces                 |
| sequence              | uint64_t                     | Snapshot counter, increments each push     |
| timestamp_ms          | uint64_t                     | Monotonic timestamp of this snapshot       |

### Per-service metrics

Each slot in `services[]` contains:

```c
typedef struct {
    char     name[32];         // display name e.g. "AudioSvc"
    uint32_t pid;              // process ID, 0 if not running
    uint32_t running;          // 1 if process is alive
    float    cpu_pct;          // CPU usage percentage
    uint64_t rss_bytes;        // resident memory in bytes
    int      health_score;     // 0-100, computed by health_score.c
    uint32_t restart_count;    // number of times service restarted
    uint8_t  sandbox_ok;       // seccomp sandbox is active
    uint8_t  caps_ok;          // capabilities are correct
    uint8_t  verify_ok;        // binary HMAC verified
    uint8_t  seccomp_ok;       // no seccomp violations
} service_metrics_t;
```

### Alert records

```c
typedef struct {
    alert_severity_t  severity;          // CRITICAL, WARNING, or INFO
    char              component[32];     // e.g. "audio_service"
    char              condition[64];     // e.g. "High CPU"
    char              current_value[32]; // e.g. "92.3%"
    char              threshold[32];     // e.g. "80%"
    char              suggestion[128];   // e.g. "Check audio loop for busy waits"
    uint64_t          first_seen_ms;     // when this alert first fired
    uint64_t          last_seen_ms;      // most recent occurrence
    uint32_t          occurrence_count;  // how many times it fired in total
} alert_record_t;
```

---

## monitor_protocol_ver.h - Version control

```c
#define MON_PROTOCOL_VERSION  2
#define MON_MSG_MAGIC         0x4D4F4E44   // "MOND"
#define MON_MAX_PAYLOAD_BYTES (16 * 1024 * 1024)  // 16 MB max payload
```

When the server sends a message to the TUI, the TUI checks that `hdr.version == MON_PROTOCOL_VERSION`. If versions differ the TUI closes the connection and shows an error. This prevents silent data corruption if a new monitord is running with an old mw_tui.

---

## monitor_wire_format.h - Framing functions

This header contains only `static inline` functions so there is no .c file. Both `monitord` and `mw_tui` include it directly.

### Framing concept

Every message is sent as:
```
[mon_msg_header_t (12 bytes)] [payload bytes (header.length bytes)]
```

The receiver always reads the header first (fixed 12 bytes), checks the magic and version, then reads exactly `header.length` more bytes as the payload.

### The read helper

```c
static inline int mon_read_all(int fd, void *buf, size_t n);
```

This function reads exactly `n` bytes from `fd`. It handles:
- `EINTR` - signals interrupting the read call - by retrying transparently.
- `EAGAIN` after a partial read - when `SO_RCVTIMEO` fires mid-stream - by sleeping 1 ms and retrying instead of giving up.
- `EAGAIN` with zero bytes received - returns `MON_WIRE_ERR_AGAIN` so the caller knows to try again next cycle.
- EOF (remote closed the connection) - returns `MON_WIRE_ERR_EOF`.

This design allows the socket to have `SO_RCVTIMEO = 30 ms` without blocking indefinitely. The TUI can check for keyboard input every 30 ms while still receiving large snapshot structs correctly, because partial reads are retried automatically.

### The write helper

```c
static inline ssize_t mon_write_all(int fd, const void *buf, size_t n);
```

Writes exactly `n` bytes to `fd`, retrying on `EINTR`. Returns -1 only on a real I/O error.

### Sending a message

```c
static inline int mon_send_msg(int fd, mon_msg_type_t type, uint32_t seq,
                                const void *payload, uint32_t payload_len);
```

Builds a `mon_msg_header_t` with the given type, sequence, and length, then calls `mon_write_all` twice: once for the header and once for the payload. Returns `MON_WIRE_OK` on success.

### Receiving a message header

```c
static inline int mon_recv_hdr(int fd, mon_msg_header_t *hdr_out);
```

Reads exactly 12 bytes, checks `magic` and `version`. Returns:
- `MON_WIRE_OK` if the header is valid
- `MON_WIRE_ERR_FRAME` if magic or version is wrong
- `MON_WIRE_ERR_EOF` if the connection was closed
- `MON_WIRE_ERR_AGAIN` if no data was available yet

### Return codes

| Code                | Value | Meaning                                              |
|---------------------|-------|------------------------------------------------------|
| MON_WIRE_OK         | 0     | Operation succeeded                                  |
| MON_WIRE_ERR_IO     | -1    | read/write system call failed                        |
| MON_WIRE_ERR_EOF    | -2    | Peer closed the connection                           |
| MON_WIRE_ERR_FRAME  | -3    | Magic number or version mismatch in header           |
| MON_WIRE_ERR_SIZE   | -4    | Payload length exceeds MON_MAX_PAYLOAD_BYTES         |
| MON_WIRE_ERR_AGAIN  | -5    | No data available yet (non-blocking / timeout)       |
