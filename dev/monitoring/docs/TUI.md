# TUI Subsystem

## Purpose

The TUI (Terminal User Interface) is the visual dashboard that an operator runs in a terminal to watch the middleware in real time. It connects to `monitord`, receives a snapshot every second, and renders multiple panels showing CPU, memory, services, security, logs, alerts, and traces.

The TUI is a completely separate process (`mw_tui`). It does not collect any data itself - it only displays data that monitord has already collected and packaged into a snapshot.

---

## Files in this folder

| File                 | What it does                                                          |
|----------------------|-----------------------------------------------------------------------|
| tui_main.c           | Entry point, socket connection, main render loop                     |
| ui_engine.c/h        | ncurses initialization, shutdown, terminal size query                |
| ui_colors.c/h        | Color scheme - defines and initializes all color pairs                |
| ui_layout.c/h        | Layout manager - renders all panels, manages tab switching, scrolling |
| ui_input.c/h         | Keyboard input polling                                               |
| panel_topbar.c/h     | Top bar: title, time, connection status, tab names                   |
| panel_overview.c/h   | Tab 1: CPU, RAM, load, uptime, system health grade                   |
| panel_services.c/h   | Tab 2: Per-service table with CPU%, RAM, PID, health score           |
| panel_memory.c/h     | Tab 3: Memory pool fill levels and allocation failure counts         |
| panel_security.c/h   | Tab 4: Security flags, violation counts, sandbox status              |
| panel_hal.c/h        | Tab 5: Hardware abstraction layer metrics and error rates            |
| panel_io.c/h         | Tab 6: io_uring and ring buffer statistics                           |
| panel_alerts.c/h     | Tab 7: List of active alerts sorted by severity                      |
| panel_logs.c/h       | Tab 8: Scrollable log view with timestamp, source, and message       |
| panel_traces.c/h     | Tab 9: Distributed trace list and waterfall viewer                   |
| panel_help.c/h       | Tab 0: Keyboard shortcut reference                                   |

---

## tui_main.c - How the TUI works

### Startup

1. Parse `--socket /path` argument (default: `/tmp/middleware_monitor.sock`).
2. Connect to the Unix socket with `connect_to_monitord()`.
3. Set `SO_RCVBUF = 8 MB` on the socket so large snapshots are received in one kernel pass.
4. Set `SO_RCVTIMEO = 30 ms` so the socket times out instead of blocking, allowing keyboard input to be checked every 30 ms.
5. Call `ui_engine_init()` to start ncurses.
6. Install `signal_handler` for SIGINT/SIGTERM.

### Main loop

The main loop runs until the stop flag is set. Each iteration:

1. Use `select()` with a 30 ms timeout to wait for either socket data or keyboard input.
2. If socket data is available, call `mon_recv_hdr()` then `mon_read_all()` to receive a complete snapshot. Store it in a local `mon_snapshot_t`.
3. Call `ui_input_poll()` to read any key that was pressed and dispatch it.
4. If a new snapshot arrived (or a forced refresh was requested), call `ui_layout_render()`.

### Shutdown

When the stop flag is set (by signal or 'q' key):
1. `ui_engine_shutdown()` - restores the terminal to normal mode.
2. Close the socket.
3. Print a goodbye message to stdout.
4. Return 0.

---

## ui_engine.c/h - ncurses setup

### What it does

- Calls `initscr()` to start ncurses.
- Calls `raw()` and `noecho()` so keys are read immediately without waiting for Enter and without being echoed.
- Calls `keypad(stdscr, TRUE)` to enable arrow key detection.
- Calls `curs_set(0)` to hide the cursor.
- Calls `ui_colors_init()` to set up all color pairs.

### Key functions

```c
int ui_engine_init(void);
```
Full ncurses startup. Returns 0 on success. Must be called before any panel rendering.

```c
void ui_engine_shutdown(void);
```
Calls `endwin()` to restore the terminal. Must be called on exit even if the program crashes via signal.

```c
void ui_engine_get_size(int *rows, int *cols);
```
Returns the current terminal size. Panel renderers call this to know how much space they have.

---

## ui_colors.c/h - Color scheme

All color usage in the TUI goes through named color pair constants. This makes it easy to change the entire color scheme by editing one file.

| Constant             | Index | Used for                                             |
|----------------------|-------|------------------------------------------------------|
| COLOR_PAIR_DEFAULT   | 1     | Normal text, borders                                 |
| COLOR_PAIR_HEADER    | 2     | Panel headers, tab bar                               |
| COLOR_PAIR_GOOD      | 3     | Healthy values (green)                               |
| COLOR_PAIR_WARNING   | 4     | Degraded values (yellow)                             |
| COLOR_PAIR_CRITICAL  | 5     | Critical values (red)                                |
| COLOR_PAIR_INFO      | 6     | Informational items, timestamps (cyan)               |
| COLOR_PAIR_SELECTED  | 7     | Selected row in a scrollable list (reverse video)    |

---

## ui_layout.c/h - Panel layout and tabs

The layout manager owns the top-level rendering loop. It knows which tab is currently active and calls the correct panel render function.

### How rendering avoids flickering

The key decision is to use `erase()` instead of `clear()` at the start of each frame. `clear()` tells ncurses to mark every cell as dirty and repaint the entire screen, causing a visible flash. `erase()` only marks the virtual screen buffer as blank; ncurses then diffs the new virtual screen against what was previously sent to the terminal and only outputs the characters that actually changed. The result is smooth, flicker-free updates.

### Key functions

```c
void ui_layout_render(const mon_snapshot_t *snapshot);
```
The main render call. Calls `erase()`, renders the top bar, then calls the active panel's render function, then calls `refresh()` to push changes to the terminal.

```c
void ui_layout_set_tab(int tab_index);
```
Switches the active panel. Called when the user presses a number key or Tab.

```c
void ui_layout_scroll(int direction);
```
Forwards a scroll event to the currently active panel. Each panel maintains its own scroll offset.

---

## ui_input.c/h - Keyboard handling

```c
int ui_input_poll(void);
```
Calls `getch()` (non-blocking) and translates raw key codes into `UI_INPUT_*` constants.

| Key               | Constant               | Action                            |
|-------------------|------------------------|-----------------------------------|
| q or Q            | UI_INPUT_QUIT          | Exit the TUI                      |
| Tab               | UI_INPUT_NEXT_TAB      | Switch to next panel              |
| Shift+Tab         | UI_INPUT_PREV_TAB      | Switch to previous panel          |
| Up arrow / k      | UI_INPUT_UP            | Scroll up                         |
| Down arrow / j    | UI_INPUT_DOWN          | Scroll down                       |
| Page Up           | UI_INPUT_PGUP          | Scroll up one page                |
| Page Down         | UI_INPUT_PGDN          | Scroll down one page              |
| r or F5           | UI_INPUT_REFRESH       | Force re-render                   |
| 1 through 0       | UI_INPUT_TAB_N + N     | Jump directly to panel N          |

---

## Panels - What each one shows

### panel_topbar - Always visible

Shows across the top of the screen:
- Program name and version
- Current time
- Socket connection status (Connected / Disconnected)
- Tab names with the active one highlighted

### panel_overview - Tab 1

System-wide summary:
- CPU total percentage with a mini sparkline chart
- RAM used / total with percentage
- Load averages (1m, 5m, 15m)
- System uptime (days, hours, minutes)
- Overall system health grade (EXCELLENT / GOOD / DEGRADED / CRITICAL)

### panel_services - Tab 2

A table with one row per tracked service:

```
Service         PID    Running  CPU%   RAM       Health  Sandbox  HMAC
AudioSvc       12345   YES      2.4%   14.2 MB   100     Y        Y
GPIOSvc        12346   YES      0.1%   4.1 MB    100     Y        Y
SensorSvc      12347   YES      0.8%   6.7 MB    100     Y        Y
```

Health score is color-coded: green (>= 70), yellow (50-69), red (< 50). If a service is not running its entire row is shown in red.

### panel_memory - Tab 3

Shows each memory pool:
- Pool name and total capacity
- Currently allocated bytes and fill percentage
- Number of allocation failures (highlighted red if > 0)

### panel_security - Tab 4

Security status table:
- Per-service security flags (sandbox enabled, capabilities locked, binary verified, seccomp active)
- Violation counts: seccomp violations, HMAC failures, capability errors
- One row per service, all green if healthy

### panel_hal - Tab 5

Hardware abstraction layer metrics:
- Per-HAL-device request counts and error rates
- Active device count and driver status

### panel_io - Tab 6

I/O subsystem metrics:
- Per io_uring instance: submission queue depth, completion queue depth, overflow count
- Per ring buffer: fill level, produce count, consume count, drop count

### panel_alerts - Tab 7

Scrollable list of active alerts sorted by severity (CRITICAL first):
- Severity badge (color-coded)
- Component name
- Condition description
- Current value vs threshold
- Time first seen and number of occurrences
- Suggested action

### panel_logs - Tab 8

Scrollable log viewer showing the most recent log lines from service log files:

```
Time      Source           Message
14:22:01  [INFO] audio_service::audio_service_loop  Starting audio capture loop
14:22:01  [INFO] audio_service::open_device          Device /dev/snd/pcm0c opened
14:22:02  [ERR ] gpio_service::gpio_init             Failed to open /dev/gpiochip0
```

Timestamps are extracted from the log line prefix written by `_SVC_LOG`. If a line has no timestamp, the Time column shows `--:--:--`.

### panel_traces - Tab 9

Shows a list of recent completed distributed traces. The operator can navigate to a specific trace and press Enter to see its full waterfall chart. The waterfall shows each span as a horizontal bar scaled to the trace duration.

### panel_help - Tab 0 (or '?' key)

A reference card listing all keyboard shortcuts so operators do not need to remember them.
