# Metrics Subsystem

## Purpose

The metrics subsystem defines the fundamental building blocks for all measurements in the system. It provides the basic types (gauge, counter, histogram, sparkline), a central registry that maps names to metric pointers, a delta calculator for computing rates of change, a history buffer for sparklines, and a Prometheus exporter for external scraping.

---

## Files in this folder

| File                           | What it does                                                     |
|--------------------------------|------------------------------------------------------------------|
| metrics_types.h                | Defines gauge_t, counter_t, histogram_t, sparkline_t            |
| metrics_registry.c/h           | Global name-to-pointer registry, used for Prometheus export      |
| metrics_delta.c/h              | Computes rate of change for counters between two snapshots       |
| metrics_history.c/h            | Time series storage for sparkline charts in the TUI              |
| metrics_export_prometheus.c/h  | Formats the registry contents as Prometheus text output          |

---

## metrics_types.h - The four fundamental types

### gauge_t - A value that can go up or down

```c
typedef struct {
    double   value;           // current value
    double   min_lifetime;    // smallest value seen since process start
    double   max_lifetime;    // largest value seen since process start
    uint64_t last_update_ms;  // when this was last changed
} gauge_t;
```

Used for: CPU%, RAM bytes, queue depth, fill percentage, temperature - anything that can go both up and down.

To update a gauge:
```c
gauge_set(&g, new_value, timestamp_ms);
```
This updates `value`, and automatically tracks the lifetime min/max.

### counter_t - A value that only increases

```c
typedef struct {
    uint64_t value;           // current cumulative total
    uint64_t prev_value;      // value at last reading (for rate calculation)
    uint64_t last_update_ms;
    uint64_t reset_count;     // how many times the counter has wrapped or reset
} counter_t;
```

Used for: total messages sent, total errors, total bytes read - anything that only ever goes up. If it ever goes down, `reset_count` is incremented so callers know a counter reset happened.

To add to a counter:
```c
counter_add(&c, delta, timestamp_ms);
```
To set a counter to an absolute value (read from /proc):
```c
counter_set(&c, new_absolute_value, timestamp_ms);
```

### histogram_t - A distribution

```c
typedef struct {
    double   upper_bounds[32]; // bucket boundaries (Prometheus "le" values)
    uint64_t counts[32];       // cumulative observation counts per bucket
    uint32_t num_buckets;
    double   sum;              // sum of all observed values
    uint64_t count;            // total number of observations
} histogram_t;
```

Used for: request latency, message processing time. Compatible with Prometheus histogram format. Callers observe a value by incrementing the appropriate bucket counts.

### sparkline_t - A scrolling recent-values buffer

A small circular buffer of floating-point samples that the TUI renders as a simple ASCII chart. For example, the Overview panel uses a sparkline to show CPU usage over the last N seconds as a row of bar characters.

---

## metrics_registry.c/h - The global name registry

The registry is a global singleton (one instance for the whole monitord process). Every metric that should be exported to Prometheus must be registered here. The TUI does not use the registry - it reads directly from the snapshot struct.

### What the registry stores

Each entry in the registry contains:
- `name` - unique metric name, e.g. `"cpu_total_pct"`
- `subsystem` - logical group, e.g. `"sysinfo"`
- `help` - human-readable description shown in Prometheus output
- `type` - gauge, counter, histogram, or sparkline
- `ptr` - pointer to the actual `gauge_t`, `counter_t`, or `histogram_t` in memory
- `labels` - Prometheus label string, e.g. `service="audio_service"`

### Why use a registry

Without the registry, the Prometheus exporter would need to know the exact internal addresses of every metric struct. With the registry, the exporter just iterates entries and calls the appropriate formatting function for each type. New metrics are added to the system just by calling `metrics_register_gauge()` or similar in `monitord_state_init()`.

### Key functions

```c
int metrics_registry_init(void);
```
Must be called once at startup before any registration calls.

```c
int metrics_register_gauge(const char *name, const char *subsystem,
                            const char *help, const char *labels,
                            gauge_t *g);
```
Registers a gauge metric. The `gauge_t` must remain valid for the lifetime of the process. Returns -1 if the registry is full (max 512 entries) or the name is already taken.

```c
int metrics_register_counter(const char *name, const char *subsystem,
                              const char *help, const char *labels,
                              counter_t *c);
```
Same as above but for a counter.

```c
int metrics_register_histogram(const char *name, const char *subsystem,
                                const char *help, const char *labels,
                                histogram_t *h);
```
Same as above but for a histogram.

```c
const metric_entry_t *metrics_lookup(const char *name);
```
Finds a metric entry by exact name. Returns NULL if not found.

```c
void metrics_iterate(void (*cb)(const metric_entry_t *e, void *userdata),
                     void *userdata);
```
Calls `cb` for every registered metric. Used by the Prometheus exporter.

```c
uint32_t metrics_count(void);
```
Returns the total number of registered metrics.

---

## metrics_delta.c/h - Rate calculation

Counters accumulate over time. The delta module computes the rate of change between two snapshots.

### How it works

Given two snapshots taken at times `t1` and `t2`:
```
rate = (counter_value_at_t2 - counter_value_at_t1) / (t2 - t1)
```

This is used to compute things like:
- Messages per second
- Errors per second
- Bytes per second

### Key function

```c
double metrics_delta_rate(const counter_t *c, uint64_t interval_ms);
```
Returns the per-second rate of change for the counter based on its `value` and `prev_value` fields and the given time interval.

---

## metrics_history.c/h - Time series for sparklines

`metrics_history_t` is a circular buffer of recent float values with timestamps. Used by the TUI panels to draw mini sparkline charts next to CPU and memory gauges.

### Key functions

```c
int metrics_history_init(metrics_history_t *h, uint32_t capacity);
```
Allocates the circular buffer with `capacity` slots.

```c
void metrics_history_push(metrics_history_t *h, double value, uint64_t ts_ms);
```
Adds one sample to the history. If full, overwrites the oldest sample.

```c
uint32_t metrics_history_get_recent(metrics_history_t *h,
                                     double *out,
                                     uint32_t max);
```
Copies the most recent `max` values into `out` in chronological order.

---

## metrics_export_prometheus.c/h - Prometheus exporter

This module converts the entire metrics registry into the Prometheus text exposition format and writes it to a provided buffer or file descriptor.

### Output format

```
# HELP cpu_total_pct Total CPU usage percentage
# TYPE cpu_total_pct gauge
cpu_total_pct{instance="middleware"} 42.30

# HELP service_restart_count_total Total service restarts
# TYPE service_restart_count_total counter
service_restart_count_total{service="audio_service"} 0
service_restart_count_total{service="gpio_service"} 0
```

### Key function

```c
int metrics_export_prometheus_write(int fd);
```
Formats all registered metrics and writes them to `fd`. Called by the HTTP server when it receives a GET `/metrics` request.
