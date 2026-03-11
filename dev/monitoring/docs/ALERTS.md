# Alerts Subsystem

## Purpose

The alerts subsystem watches the current system snapshot and decides whether something has gone wrong. When a problem is detected, it creates an alert record, stores it in memory, and delivers a notification. The same problem will not create a hundred duplicate alerts - there is a cooldown period per severity level.

---

## Files in this folder

| File              | What it does                                                          |
|-------------------|-----------------------------------------------------------------------|
| alert_engine.c/h  | Stores active alerts, handles deduplication, manages cooldown timers  |
| alert_rules.c/h   | Defines all 26 rules and evaluates them against each snapshot         |
| alert_notify.c/h  | Delivers notifications - terminal bell, log file, optional webhook    |

---

## alert_engine.c/h - The alert store

This file manages a fixed-size array of up to 1000 `alert_record_t` entries. Every time a rule fires, it calls `alert_fire()`.

### How deduplication works

When `alert_fire()` is called:
1. It scans the active alerts array for an entry with the same `component` and `condition` strings.
2. If found and the alert is still within its cooldown window, it increments `occurrence_count` instead of creating a new entry.
3. If the cooldown has expired, it creates a new entry (the old one is effectively replaced).
4. If no match is found, a new entry is appended to the array.

This means the alert list stays clean even if a CPU overload condition fires every second for five minutes. You will see one alert with a high occurrence count, not 300 separate lines.

### Cooldown periods by severity

| Severity  | Cooldown  | Reason                                                   |
|-----------|-----------|----------------------------------------------------------|
| CRITICAL  | 30 s      | Critical problems need frequent re-notification          |
| WARNING   | 60 s      | Warnings can be batched slightly longer                  |
| INFO      | 300 s     | Informational events happen often and are low priority   |

### Key functions

```c
int alert_state_init(alert_state_t *state);
```
Zeros the alert array and initializes the internal read-write lock. Must be called once at daemon startup.

```c
uint64_t alert_fire(alert_state_t *state,
                    alert_severity_t severity,
                    const char *component,
                    const char *condition,
                    const char *current_value,
                    const char *threshold,
                    const char *suggestion,
                    uint64_t timestamp);
```
The main entry point for creating or updating an alert. `component` is a short name like "SM" or "audio_service". `condition` describes what went wrong, e.g. "High CPU". `suggestion` is a human-readable fix hint shown in the TUI.

```c
uint32_t alert_get_active(alert_state_t *state,
                           alert_record_t *out,
                           uint32_t max,
                           uint64_t timestamp);
```
Copies all alerts that are still within their cooldown window into `out`. This is what the TUI calls to populate the Alerts panel.

```c
void alert_clear_all(alert_state_t *state);
```
Resets the alert count to zero. Called when the operator presses the clear key in the TUI.

---

## alert_rules.c/h - The 26 rules

`alert_rules_evaluate()` is called once per snapshot cycle by the monitord main loop. It checks every rule and calls `alert_fire()` for any that trigger.

Rules are split into three severity groups:

### CRITICAL rules (7 total)

These indicate an immediate, serious problem.

| # | Condition checked               | Reason it is critical                              |
|---|---------------------------------|----------------------------------------------------|
| 1 | Service restart detected        | A service crashed and had to be restarted          |
| 2 | Seccomp violation               | A process tried a forbidden system call (possible attack) |
| 3 | HMAC verification failure       | A message was tampered with or corrupted           |
| 4 | Memory pool exhaustion          | The system cannot allocate memory - may deadlock   |
| 5 | io_uring completion queue full  | Async I/O is being dropped                         |
| 6 | Ring buffer overflow            | Messages are being lost                            |
| 7 | Health score below 50           | Overall system health in the danger zone           |

### WARNING rules (14 total)

These indicate a degraded condition that needs attention soon.

| #  | Condition checked                    |
|----|--------------------------------------|
| 8  | Health score between 50 and 70       |
| 9  | CPU usage above 80% for 30 seconds   |
| 10 | RAM usage above 85% for 30 seconds   |
| 11 | File descriptor leak detected        |
| 12 | Service p99 latency above threshold  |
| 13 | Message drop rate above 1%           |
| 14 | HAL error rate above 5%              |
| 15 | IPC queue depth above 80% capacity   |
| 16 | Replay attack detected               |
| 17 | Collector is STALE (missed 3 ticks)  |
| 18 | Collector is OFFLINE                 |
| 19 | Watchdog starvation                  |
| 20 | Config file changed unexpectedly     |
| 21 | Swap usage above 50%                 |

### INFO rules (5 total)

These are normal lifecycle events that are worth recording.

| #  | Event                             |
|----|-----------------------------------|
| 22 | A service started                 |
| 23 | A service stopped                 |
| 24 | Health score is between 70 and 90 |
| 25 | A collector reconnected           |
| 26 | Config reload was successful      |

---

## alert_notify.c/h - Delivering notifications

When `alert_fire()` creates a new alert (not just incrementing a counter), it calls `alert_notify()` to deliver the notification through one or more channels.

### Notification channels

| Channel         | How it works                                                   | When active           |
|-----------------|----------------------------------------------------------------|-----------------------|
| Terminal bell   | Writes `\a` (ASCII BEL) to stderr                             | CRITICAL alerts only  |
| Log file        | Appends a formatted line to `monitord_alerts.log`              | Always                |
| Webhook POST    | HTTP POST with JSON body to a configured URL                   | Optional, via INI config |

### Key functions

```c
int alert_notify_init(const char *log_path, const char *webhook_url);
```
Opens the log file for appending. If `webhook_url` is not NULL, stores it for later use. Called once at daemon startup.

```c
int alert_notify(const alert_record_t *alert);
```
Delivers one alert through all active channels. Called by the alert engine internally after `alert_fire()`.

```c
void alert_notify_shutdown(void);
```
Closes the log file and cleans up. Called during daemon shutdown.

---

## How alerts flow through the system

```
monitord main loop (every refresh_interval_ms)
        |
        v
alert_rules_evaluate(snapshot, alert_state, timestamp)
        |
        | for each of 26 rules that triggers:
        v
alert_fire(alert_state, severity, component, condition, ...)
        |
        |--- existing entry within cooldown? --> increment occurrence_count
        |
        |--- new entry or expired cooldown? --> create new alert_record_t
                    |
                    v
            alert_notify(alert_record_t)
                    |
                    |--- log file always
                    |--- bell if CRITICAL
                    |--- webhook if configured
```

The TUI reads the active alert list every refresh cycle and shows them in the Alerts panel sorted by severity.
