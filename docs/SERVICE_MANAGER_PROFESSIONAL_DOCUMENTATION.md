# Service Manager – Technical Architecture & Code Explanation

## 1. Overview

The Service Manager is a secure, modular Linux middleware component responsible for service registration, health monitoring, security enforcement, and lifecycle management. It acts as a **phonebook** (service registry), **security guard** (binary verification), and **health monitor** (restart crashed services).

---

## 2. Directory & Module Structure

```
core/
  service_manager/
    enterprise/
    infrastructure/
    lifecycle/
    observability/
    security/
  ring_buffer.c/h
Document/
hal/
security/
testing/
```

- **service_manager/**: Main logic, split into submodules for enterprise, infrastructure, lifecycle, observability, and security.
- **hal/**: Hardware abstraction layer.
- **security/**: Sandbox, seccomp, verification.
- **testing/**: Unit, integration, fuzz, e2e tests.

---

## 3. Key Files & Their Roles

### 3.1. Lifecycle

- **sm_config.c/h**: Parses INI-style config files, manages runtime settings (thread pool, queue size, limits, logging, socket, health, persistence).
- **sm_main.c/h**: Entry point, initializes modules, starts threads, handles signals, manages shutdown.
- **sm_graceful_shutdown.c/h**: Ensures safe shutdown, resource cleanup.

### 3.2. Infrastructure

- **sm_connection_pool.c/h**: Manages socket connections, pooling for efficiency.
- **sm_protocol.c/h**: Defines communication protocol, message parsing, serialization.
- **sm_registry.c/h**: Service registry, lookup, registration, deregistration.
- **sm_request_id.c/h**: Generates unique request IDs for tracking.

### 3.3. Observability

- **sm_audit.c/h**: Audit logging, tracks security events.
- **sm_health.c/h**: Health checks, monitors service status, triggers restarts.
- **sm_logging.h**: Structured logging, log rotation, levels.

### 3.4. Security

- **sm_tls.c/h**: TLS encryption for secure communication.
- **sm_watchdog.c/h**: Monitors for abnormal behavior, triggers recovery.
- **seccomp_filter.c/h**: Applies syscall whitelisting for sandboxing.
- **verify.c/h**: Binary signature verification.

### 3.5. Enterprise

- **sm_cli.c/h**: Command-line interface for management.
- **sm_container.c/h**: Container orchestration support.
- **sm_plugin.c/h**: Plugin system for extensibility.

---

## 4. Technical Patterns & Design

- **Modular Architecture**: Each concern (lifecycle, infra, security, observability) is isolated in its own module.
- **Thread Pool**: Configurable via INI file, dynamically allocated, manages worker threads for service tasks.
- **Config Management**: Uses INI parsing, environment overrides, runtime API for flexibility.
- **Secure Coding**: All string operations are bounded, sensitive buffers wiped with `explicit_bzero`, input parsing uses `strtol` for error checking.
- **Error Handling**: Centralized logging, error codes, validation functions.
- **Synchronization**: Uses pthread mutexes and rwlocks for thread safety.
- **Rate Limiting**: PID and global rate limits to prevent abuse.

---

## 5. Code Logic Walkthrough

### 5.1. Initialization

- `sm_main.c` calls `sm_config_load()` to parse config.
- Initializes thread pool, registry, protocol, logging, health, security modules.
- Sets up signal handlers for graceful shutdown.

### 5.2. Service Registration

- Services register via socket, providing name and path.
- Registry stores mapping, clients can lookup services.

### 5.3. Security Enforcement

- Before launching a service, binary is verified (HMAC-SHA256).
- Seccomp filters applied to restrict syscalls.
- TLS used for secure communication.

### 5.4. Health Monitoring

- Periodic health checks via `sm_health.c`.
- If a service crashes, `sm_monitoring.c` triggers restart.
- Audit logs record all events.

### 5.5. Configuration

- `sm_config.c/h` supports INI file, environment variables, and API calls.
- Validates all parameters (thread pool size, queue size, limits).
- Example config keys: `thread_pool_size`, `work_queue_size`, `max_services`, `log_file`, `socket_path`.

### 5.6. Error Handling

- All errors logged with context.
- Functions return error codes, propagate failures.
- Validation functions ensure safe operation.

---

## 6. Security Features

- **Buffer Safety**: No unsafe string ops (`strcpy`, `sprintf` replaced with `memcpy`, `snprintf`).
- **Memory Wiping**: Sensitive buffers wiped with `explicit_bzero`.
- **Input Validation**: Uses `strtol` for parsing integers, checks for errors.
- **Sandboxing**: Seccomp filters restrict syscalls.
- **TLS**: Secure communication between manager and services.
- **Audit Logging**: All security events tracked.

---

## 7. Testing & Observability

- **Unit Tests**: For registry, protocol, rate limiting, crypto.
- **Integration Tests**: Simulate real-world scenarios, check socket, config, health.
- **Fuzz Testing**: Robustness against malformed input.
- **Logging**: Structured logs, log rotation, multiple levels.
- **Metrics**: Health, restart counts, error rates.

---

## 8. Configuration Example

```ini
[server]
socket_path = /tmp/servicemanager.sock
log_file = /var/log/servicemanager.log
persistence_file = /var/lib/servicemanager.registry.dat

[thread_pool]
size = 8
queue_size = 128

[rate_limit]
pid_capacity = 10
global_capacity = 50

[health]
check_interval = 3
restart_delay = 2
```

---

## 9. Summary

The Service Manager is a robust, secure, and modular middleware component. It ensures service registration, health, and security with strong error handling, observability, and configurability. All code is written with professional standards: bounded operations, secure memory handling, dynamic configuration, and comprehensive testing.

---

**For further details, see the code files in each module. This document provides a high-level and technical overview for professionals and developers.**
