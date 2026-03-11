# Tracing Subsystem

## Purpose

The tracing subsystem implements distributed tracing for the middleware. A "trace" is a record of one complete request flowing through the system - for example, an audio sample arriving at the audio service, being processed by the HAL, and a response being sent back. Each step in that flow is a "span". By collecting and displaying these spans, an operator can see exactly where time is being spent in a request.

---

## Files in this folder

| File                  | What it does                                                        |
|-----------------------|---------------------------------------------------------------------|
| trace_collector.c/h   | Gathers spans from middleware components via IPC or shared memory   |
| trace_store.c/h       | Circular buffer that stores completed traces in monitord memory     |
| trace_renderer.c/h    | Formats trace data as a waterfall chart for the TUI Traces panel    |

---

## Core concepts

### Span

A span is the smallest unit of tracing. It represents a single operation with a start time and an end time.

```c
typedef struct {
    uint32_t  trace_id;       // which trace this span belongs to
    uint32_t  span_id;        // unique ID for this span
    uint32_t  parent_span_id; // ID of the parent span (0 if root)
    char      name[64];       // human-readable operation name
    char      service[32];    // which service generated this span
    uint64_t  start_us;       // start timestamp in microseconds
    uint64_t  end_us;         // end timestamp in microseconds
    uint8_t   status;         // 0=ok, 1=error
} trace_span_t;
```

The `parent_span_id` field is what makes distributed tracing work. By following parent relationships, you can build a tree that shows which operation called which. The root span has `parent_span_id = 0`.

### Trace

A trace is a collection of spans that all share the same `trace_id`. It represents one end-to-end request.

```c
typedef struct {
    uint32_t     trace_id;
    trace_span_t spans[TRACE_MAX_SPANS_PER_TRACE];
    uint32_t     span_count;
    uint64_t     start_us;   // start time of the root span
    uint64_t     duration_us; // total duration from root start to last span end
} trace_record_t;
```

---

## trace_collector.c/h - Gathering spans

The trace collector is called once per refresh cycle by the monitord main loop. It contacts each middleware component and retrieves any completed spans since the last collection.

### How collection works

1. `trace_collector_init()` - opens connections to each component's trace endpoint.
2. `trace_collector_collect(out, max)` - iterates all component endpoints, reads new spans into `out`, and returns the count.
3. Collected spans are grouped by `trace_id`. When all spans for a trace are present (the root span has arrived and the root's duration has elapsed), the complete trace is passed to `trace_store_add_trace()`.

### Key functions

```c
int trace_collector_init(void);
```
Sets up connections to middleware components that emit spans. Called once at daemon startup.

```c
uint32_t trace_collector_collect(trace_span_t *out, uint32_t max);
```
Reads up to `max` spans from all connected components into `out`. Returns the number of spans actually written. This is called by the monitord main loop, and the spans are then assembled into complete traces and stored.

```c
void trace_collector_shutdown(void);
```
Closes all component connections. Called during daemon shutdown.

---

## trace_store.c/h - In-memory trace storage

The trace store is a circular buffer inside `monitord_state_t` that keeps the last 1000 completed traces in memory.

### Structure

```c
typedef struct {
    trace_record_t   traces[TRACE_STORE_MAX];  // 1000 slots
    uint32_t         head;                      // next write position
    uint32_t         count;                     // number of valid entries
    pthread_rwlock_t lock;
} trace_store_t;
```

It is a circular buffer: when it is full and a new trace arrives, the oldest trace is silently overwritten. This keeps memory usage bounded while always having the most recent 1000 traces available.

### Key functions

```c
int trace_store_init(trace_store_t *store);
```
Zeros the buffer and initializes the read-write lock. Must be called during state initialization.

```c
void trace_store_add_trace(trace_store_t *store, const trace_record_t *trace);
```
Adds one completed trace to the circular buffer. Acquires the write lock, writes the record at `head`, advances `head`, increments `count` (capped at TRACE_STORE_MAX), releases the lock. This is thread-safe and can be called from the collector thread.

```c
uint32_t trace_store_get_recent(trace_store_t *store,
                                 trace_record_t *out,
                                 uint32_t max);
```
Copies the most recent `max` traces into `out` in chronological order (oldest first). Used when building the snapshot to include traces in the TUI display.

```c
int trace_store_find(trace_store_t *store, uint32_t trace_id,
                     trace_record_t *out);
```
Searches the store for a trace with the given `trace_id`. Returns 1 if found and writes it to `out`, returns 0 if not found. Used when an operator wants to inspect a specific trace by ID.

---

## trace_renderer.c/h - Waterfall chart rendering

The trace renderer takes a `trace_record_t` and formats it as a waterfall chart for the TUI Traces panel.

### What a waterfall chart looks like

A waterfall chart shows each span as a horizontal bar. The bar starts at the span's `start_us` offset from the trace root and ends at `end_us`. Spans are arranged in parent-child order with child spans indented below their parents.

```
Trace #4201  Total: 14.2 ms
 [audio_service::receive    ] ||||||||||||||||                    0.0 - 2.1 ms
   [hal::open_device        ]   ||||||||||                        0.5 - 1.8 ms
     [hal::read_samples     ]     ||||||||                        0.8 - 1.6 ms
   [audio_service::encode   ] ||||||||||||||||||||||||||||        2.1 - 9.3 ms
   [audio_service::send_resp]                     |||||||||       9.3 - 14.2 ms
```

### How it works

1. Spans are sorted by `start_us`.
2. Parent-child relationships are resolved using `parent_span_id` to determine indentation depth.
3. The total time range (root start to last span end) is mapped to the available column width.
4. Each span is drawn as a run of block characters with the span name on the left and the time range on the right.
5. Error spans are rendered in red (via ncurses color pairs).

### Key functions

```c
void trace_render_waterfall(WINDOW *win, const trace_record_t *trace,
                             int rows, int cols);
```
Renders the waterfall chart directly into an ncurses `WINDOW`. Called by the TUI Traces panel when the user selects a trace.

```c
void trace_render_summary(WINDOW *win, const trace_record_t *traces,
                           uint32_t count, int rows, int cols);
```
Renders a scrollable list of all available traces showing trace ID, total duration, root service name, and status. This is the default view of the Traces panel before the user selects a single trace to drill into.

---

## How tracing flows through the system

```
Middleware components (audio_service, etc.)
    |
    | emit spans to a local trace endpoint
    v
trace_collector_collect()   [called by monitord main loop]
    |
    | completed traces assembled
    v
trace_store_add_trace()
    |
    | stored in trace_store circular buffer inside monitord_state
    v
monitord_state_serialize_snapshot()
    |
    | copies recent traces into mon_snapshot_t.traces[]
    v
broadcast over Unix socket to mw_tui
    |
    v
panel_traces.c  (render_summary or render_waterfall)
```
