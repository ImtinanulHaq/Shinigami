# Middleware Monitoring System - Test Suite

**Comprehensive QA validation for the two-process terminal monitoring system**

## Overview

This test suite validates the `middleware_monitord` daemon and `middleware_monitor_tui` terminal UI through 73 deterministic tests across 5 categories:

- **Unit Tests (8)**: Core component validation
- **Integration Tests (5)**: Inter-process communication and system integration
- **Stress Tests (5)**: Performance under extreme load
- **TUI Tests (Planned)**: Terminal rendering validation
- **Quality Checklist (6)**: Manual verification items

## Architecture

```
monitoring/
├── testing/
│   ├── mocks/
│   │   ├── mock_middleware.h       # Configurable mock API
│   │   └── mock_middleware.c       # Full middleware simulation
│   ├── test_framework.h            # Assertion macros, test runner
│   ├── test_framework.c            # Temp dirs, socket helpers, FD counting
│   ├── unit/                       # 8 unit tests
│   │   ├── test_metrics_registry.c
│   │   ├── test_metrics_history.c
│   │   ├── test_metrics_delta.c
│   │   ├── test_health_score.c
│   │   ├── test_alert_engine.c
│   │   ├── test_trace_store.c
│   │   ├── test_wire_format.c
│   │   └── test_collector_state.c
│   ├── integration/                # 5 integration tests
│   │   ├── test_monitord_tui_ipc.c
│   │   ├── test_metric_accuracy.c
│   │   ├── test_concurrency.c
│   │   ├── test_prometheus_export.c
│   │   └── test_alert_delivery.c
│   ├── stress/                     # 5 stress tests
│   │   ├── stress_metric_flood.c
│   │   ├── stress_log_storm.c
│   │   ├── stress_crash_recovery.c
│   │   ├── stress_monitord_restart.c
│   │   └── stress_multi_tui.c
│   ├── Makefile                    # Build system
│   └── README.md                   # This file
```

## Quick Start

```bash
# Build all tests
cd monitoring/testing
make all

# Run all tests
make run

# Run specific category
make run-unit
make run-integration
make run-stress

# Check for memory leaks
make valgrind

# Generate coverage report
make coverage
```

## Test Categories

### 1. Unit Tests (8 tests)

**File**: `unit/test_metrics_registry.c`
- Register and lookup metrics
- Duplicate name detection
- 1000 metrics stress
- Thread safety (16 concurrent threads)

**File**: `unit/test_metrics_history.c`
- Circular buffer push/get
- Wraparound handling
- Capacity calculation

**File**: `unit/test_metrics_delta.c`
- Rate computation (value/time)
- Counter wrap detection
- Division by zero protection
- Stable value (0/s after 5s)

**File**: `unit/test_health_score.c`
- Perfect health (100)
- Violation penalties (-10 each)
- Crashed service (0)
- Floor at zero
- System health = min(all services)

**File**: `unit/test_alert_engine.c`
- Basic alert firing
- CRITICAL cooldown (30s)
- WARNING cooldown (60s)
- INFO cooldown (300s)
- Occurrence counter
- Deduplication

**File**: `unit/test_trace_store.c`
- Add/retrieve traces
- 1000 trace storage
- Eviction on overflow
- Out-of-order assembly

**File**: `unit/test_wire_format.c`
- HELLO/WELCOME handshake serialization
- SNAPSHOT request/response
- Version checking
- Magic number validation
- Random data rejection

**File**: `unit/test_collector_state.c`
- State machine: WAITING→CONNECTING→SYNCING→LIVE
- Staggered startup (index × 200ms)
- Tick count increment
- Restart after stop

### 2. Integration Tests (5 tests)

**File**: `integration/test_monitord_tui_ipc.c`
- HELLO/WELCOME handshake
- Snapshot request (<100ms latency)
- Log subscription
- Two clients simultaneously
- Abrupt disconnect handling

**File**: `integration/test_metric_accuracy.c`
- Service metrics: CPU=45.5%, RSS=10MB, FD=25
- Pool metrics: 199/256 used blocks
- Uring metrics: P99=2.3ms
- Ring buffer: 7 drops
- Sysinfo: 35.2% CPU, 16GB RAM
- Violation injection
- Crash service
- Log storm (5000/s)

**File**: `integration/test_concurrency.c`
- 11 collector threads + 3 TUI clients for 60s
- Zero deadlocks
- Zero data races (ThreadSanitizer)
- Response latency <50ms

**File**: `integration/test_prometheus_export.c`
- /metrics format validation
- Label syntax: `metric{label="value"}`
- Histogram buckets (_bucket, _sum, _count)
- Counter monotonicity
- /health JSON format
- HTTP response codes (200, 404)

**File**: `integration/test_alert_delivery.c`
- Alert delivery <1s
- Cooldown suppression
- 7 simultaneous conditions
- Violation-triggered alerts
- Broadcast to multiple clients

### 3. Stress Tests (5 tests)

**File**: `stress/stress_metric_flood.c`
- 10× metric rate (100ms interval) for 120s
- monitord CPU <5%
- TUI latency <50ms
- Zero lost metrics
- Memory growth <1MB

**File**: `stress/stress_log_storm.c`
- 5000 logs/s from one service
- LOG STORM badge within 2s
- Display rate-limited to 10/s
- Other sources unaffected
- monitord CPU <3%

**File**: `stress/stress_crash_recovery.c`
- Kill service, detect OFFLINE within 3× interval
- Alert fired
- Restart, reconnect <30s
- Repeat 10 cycles
- FD count stable

**File**: `stress/stress_monitord_restart.c`
- Kill monitord with SIGKILL
- TUI detects disconnect <5s
- Restart monitord
- TUI reconnects <10s automatically
- No data corruption

**File**: `stress/stress_multi_tui.c`
- 5 TUI clients at 100ms refresh for 60s
- All get consistent data
- monitord CPU <3%
- Abrupt disconnect 4 clients
- 5th client continues unaffected

## Mock Middleware

The `mock_middleware` system simulates all 11 middleware subsystems without hardware:

```c
mock_middleware_t mock;
mock_middleware_init(&mock);

// Configure service
mock_service_config_t svc = {
    .cpu_percent = 45.5f,
    .rss_bytes = 10 * 1024 * 1024,  // 10MB
    .fd_count = 25,
    .state = SERVICE_STATE_RUNNING
};
mock_set_service(&mock, 0, &svc);

// Inject violations
mock_inject_violation(&mock, 0, 1, 0, 0); // 1 seccomp violation

// Crash service
mock_crash_service(&mock, 1);

// Enable log storm
mock_set_log_storm(&mock, true, 5000); // 5000 logs/s

// Generate snapshot
mon_snapshot_t snap;
mock_middleware_generate_snapshot(&mock, &snap);
```

**Configurable components:**
- 4 services (audio, camera, sensor, gpio)
- 3 memory pools
- 2 io_uring instances
- 3 ring buffers
- HAL (4 devices)
- Sysinfo (CPU, RAM, swap, load)
- IPC channels
- Config watcher
- Watchdog
- Service manager
- Security (seccomp/HMAC/replay violations)

## Test Framework

**Assertion macros:**
```c
TEST_ASSERT(condition, "message");
TEST_ASSERT_EQ(actual, expected, "message");
TEST_ASSERT_NE(actual, expected, "message");
TEST_ASSERT_LT/LE/GT/GE(actual, expected, "message");
TEST_ASSERT_NULL/NOT_NULL(ptr, "message");
TEST_ASSERT_STR_EQ/NE(actual, expected, "message");
TEST_ASSERT_FLOAT_EQ(actual, expected, epsilon, "message");
```

**Test runner:**
```c
test_case_t tests[] = {
    {"test_name", test_function, true},  // true = enabled
};
test_run_suite(tests, count, "Suite Name");
```

**Utilities:**
```c
// Temp directories
char *dir = test_create_temp_dir();  // /tmp/monitor_test_<pid>/
test_cleanup_dir(dir);

// Socket waiting
test_wait_for_socket("/tmp/socket.sock", 5000); // 5s timeout

// Resource tracking
size_t fd_count = test_count_fds();        // /proc/self/fd
size_t rss_bytes = test_get_rss_bytes();   // /proc/self/status VmRSS
```

## Build System

**Compiler flags:**
- `-std=c11 -Wall -Wextra -Werror -pedantic`
- `-g -O0` (debug symbols, no optimization)
- `-fsanitize=address,undefined` (ASan, UBSan)
- `-fsanitize=thread` (TSan for concurrency test)
- `-fprofile-arcs -ftest-coverage` (code coverage)

**Make targets:**
```bash
make              # Build all tests
make unit         # Build unit tests only
make integration  # Build integration tests
make stress       # Build stress tests
make run          # Run all tests
make run-unit     # Run unit tests
make valgrind     # Run Valgrind leak check
make coverage     # Generate coverage report
make clean        # Remove build artifacts
make help         # Show help
make list         # List all tests
```

## Pass Criteria

### Performance
- **Steady state CPU**: <1% (monitord + TUI combined)
- **Snapshot latency**: <100ms (IPC round-trip)
- **TUI render time**: <16ms P99 (60 FPS)
- **Alert delivery**: <1s from trigger to display

### Resource Management
- **FD leaks**: Zero (startup count == post-test count)
- **Memory leaks**: Valgrind-clean (zero definitely lost)
- **Thread count**: Exactly 13 in monitord (11 collectors + HTTP + TUI server)

### Correctness
- **Metric accuracy**: ±0.1% tolerance
- **Delta mode**: 0/s after 5s stable
- **Prometheus**: Counters never decrease across 100 scrapes
- **Alert deduplication**: 1000 identical conditions → 1 log with occurrences: 1000

### Stress
- **Metric flood**: 10× rate, <5% CPU, <50ms latency, <1MB growth
- **Log storm**: 5000/s, badge <2s, display 10/s, <3% CPU
- **Crash recovery**: OFFLINE <3×interval, reconnect <30s, stable FDs over 10 cycles
- **Multi-client**: 5 clients, consistent data, <3% CPU, survive 4 abrupt disconnects

### Time Limits
- **Full suite**: <10 minutes
- **Unit tests**: <30 seconds
- **Integration tests**: <2 minutes
- **Stress tests**: <8 minutes

## Quality Checklist

**Manual verification items:**

1. **Memory Leaks**: Run Valgrind on both binaries for 60s
   ```bash
   valgrind --leak-check=full --show-leak-kinds=all ./middleware_monitord
   valgrind --leak-check=full --show-leak-kinds=all ./middleware_monitor_tui
   ```

2. **FD Leaks**: Monitor /proc/<pid>/fd before/after stress tests

3. **Sensitive Data**: Verify explicit_bzero() called before free() on secrets

4. **Thread Count**: `ps -eLf | grep monitord | wc -l` should be 13

5. **Delta Correctness**: Load stable system, verify all metrics show 0/s after 5s

6. **Prometheus Idempotency**: 100 consecutive scrapes, verify counters never decrease

## Dependencies

**Required packages:**
```bash
# Ubuntu/Debian
sudo apt-get install build-essential gcc libpthread-stubs0-dev libncurses-dev \
                     libcurl4-openssl-dev valgrind lcov

# Fedora/RHEL
sudo dnf install gcc glibc-devel ncurses-devel libcurl-devel valgrind lcov
```

## Troubleshooting

**Build errors:**
- Ensure monitoring system built successfully: `cd .. && mkdir -p build && cd build && cmake .. && make`
- Check include paths in Makefile `INCLUDES`

**Test failures:**
- Check `ASAN_OPTIONS=detect_leaks=1` in environment
- Verify `/tmp/` is writable for temp directories
- Check port 9090 is not in use (Prometheus HTTP)
- Ensure `/tmp/middleware_monitor.sock` is not leftover

**ThreadSanitizer conflicts:**
- TSan and ASan are mutually exclusive
- Concurrency test uses TSan, others use ASan
- Don't mix TSan and ASan in same binary

**Valgrind false positives:**
- TSan and Valgrind conflict, disable TSan for Valgrind runs
- ncurses may have reachable-but-not-freed memory (acceptable)

## CI/CD Integration

**Example GitHub Actions:**
```yaml
- name: Build and test monitoring system
  run: |
    cd monitoring/testing
    make all
    make run
    make valgrind
    make coverage
```

**Expected output:**
```
═══════════════════════════════════════════════════════════════
Running Unit Tests
═══════════════════════════════════════════════════════════════
[Metrics Registry] 5/5 tests passed (0 failed)
[Metrics History] 6/6 tests passed (0 failed)
[Metrics Delta] 8/8 tests passed (0 failed)
[Health Score] 7/7 tests passed (0 failed)
[Alert Engine] 8/8 tests passed (0 failed)
[Trace Store] 7/7 tests passed (0 failed)
[Wire Format] 7/7 tests passed (0 failed)
[Collector State] 7/7 tests passed (0 failed)

═══════════════════════════════════════════════════════════════
Running Integration Tests
═══════════════════════════════════════════════════════════════
[Monitord-TUI IPC] 5/5 tests passed (0 failed)
[Metric Accuracy] 8/8 tests passed (0 failed)
[Concurrency] 5/5 tests passed (0 failed)
[Prometheus Export] 7/7 tests passed (0 failed)
[Alert Delivery] 7/7 tests passed (0 failed)

═══════════════════════════════════════════════════════════════
Running Stress Tests
═══════════════════════════════════════════════════════════════
[Metric Flood Stress] 5/5 tests passed (0 failed)
[Log Storm Stress] 5/5 tests passed (0 failed)
[Crash Recovery Stress] 7/7 tests passed (0 failed)
[Monitord Restart Stress] 7/7 tests passed (0 failed)
[Multi-TUI Stress] 6/6 tests passed (0 failed)

✓ All tests passed!
```

## Contributing

When adding new tests:

1. Place unit tests in `unit/`
2. Place integration tests in `integration/`
3. Place stress tests in `stress/`
4. Add test executable to appropriate list in `Makefile`
5. Follow naming: `test_*.c` or `stress_*.c`
6. Use test framework macros
7. Document pass criteria in file header
8. Add entry to this README

## License

Same as parent project.

## Contact

For test suite issues, see main project README for contact information.
