# Health Subsystem

## Purpose

The health subsystem turns raw metric numbers into a single score between 0 and 100 for each service and for the system as a whole. This score is displayed in the TUI, used by the alert rules to decide when to fire degraded-health alerts, and stored over time in a history buffer so trends can be visualized.

---

## Files in this folder

| File                  | What it does                                                    |
|-----------------------|-----------------------------------------------------------------|
| health_score.c/h      | Computes health scores from service metrics                     |
| health_history.c/h    | Stores a time series of health scores for sparkline display     |

---

## health_score.c/h - Computing the score

### The formula

The health score for a single service starts at 100 and deductions are subtracted based on bad events detected in that service's metrics:

| Condition                   | Points deducted | Why this weight                                     |
|-----------------------------|-----------------|-----------------------------------------------------|
| Each service restart        | -10             | Restarts indicate instability                        |
| Each 1% of message drop rate| -5              | Drops cause data loss                               |
| p99 latency over threshold  | -15             | High tail latency affects users                     |
| Seccomp violation           | -50             | Security violation - extremely serious              |
| File descriptor leak        | -20             | FD leaks eventually cause the service to stop working |
| HMAC verification failure   | -30             | Indicates message tampering                         |
| Memory pool exhaustion      | -25             | Risk of OOM and deadlock                            |
| io_uring CQ overflow        | -20             | Async I/O requests are being dropped                |

The score is clamped to 0 if the sum goes negative.

### System health score

```c
int health_compute_system_score(const service_metrics_t *services, uint32_t count);
```

The system-wide health score is the minimum of all individual service scores. This is a conservative choice: if any one service is critically ill, the entire system is considered critically ill. There is no averaging that would hide a broken service.

### Health grades

```c
const char *health_score_to_grade(int score);
```

| Score range | Grade      | Color in TUI |
|-------------|------------|--------------|
| 90 - 100    | EXCELLENT  | Green        |
| 70 - 89     | GOOD       | Green        |
| 50 - 69     | DEGRADED   | Yellow       |
| 0 - 49      | CRITICAL   | Red          |

### Color mapping

```c
int health_score_to_color(int score);
```
Returns an ncurses color pair index (1=red, 2=yellow, 3=green) used by the TUI panels to visually highlight degraded services.

### Key functions

```c
int health_compute_service_score(const service_metrics_t *s);
```
Takes one service metrics struct and returns a score 0-100. This is called once per service per refresh cycle.

---

## health_history.c/h - Storing trends over time

A single score is useful but a trend line is more useful. If the score was 90 ten minutes ago and is now 55 and dropping, that is more alarming than a score that has been stable at 55 for hours.

`health_history_t` is a fixed-size circular buffer that stores health score samples with their timestamps.

### Structure

```c
typedef struct {
    health_sample_t  samples[HEALTH_HISTORY_MAX];  // circular buffer
    uint32_t         head;                           // write index
    uint32_t         count;                          // number of valid entries
    pthread_rwlock_t lock;
} health_history_t;
```

Each sample stores:
- `score` - the health score (0-100) at that moment
- `timestamp_ms` - monotonic timestamp in milliseconds

### Key functions

```c
int health_history_init(health_history_t *h);
```
Zeros the buffer and initializes the lock.

```c
void health_history_push(health_history_t *h, int score, uint64_t ts_ms);
```
Adds one new sample to the circular buffer. If the buffer is full, the oldest entry is overwritten.

```c
uint32_t health_history_get_recent(health_history_t *h,
                                    health_sample_t *out,
                                    uint32_t max);
```
Copies the most recent `max` samples into `out`. Used by the TUI to draw sparkline charts.

---

## How health flows through the system

```
Every refresh cycle in the monitord main loop:

    service_metrics_t (from state)
            |
            v
    health_compute_service_score()   <-- health_score.h
            |
            v
    health_score (0-100)
            |
            |--- written to state->services[i].health_score
            |
            v
    health_history_push()            <-- health_history.h
            |
            v
    stored in health_history_services[i] circular buffer

    system health = min of all service scores
            |
            v
    stored in health_history_system circular buffer

    TUI reads health_score from snapshot and calls
    health_score_to_grade() and health_score_to_color()
    to render the Services panel.
```
