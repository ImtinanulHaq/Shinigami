# MicroOS Phase 2 - Process Manager

## Overview

**Phase 2** extends MicroOS with advanced process management capabilities, enabling:

- ✅ **Process Management** - Start, stop, restart processes with auto-recovery
- ✅ **App Registry** - Register and manage application configurations
- ✅ **Lifecycle Management** - Coordinate startup/shutdown of interdependent services
- ✅ **Service Discovery** - Dynamic service registration and lookup
- ✅ **Resource Allocation** - Track and allocate system resources
- ✅ **Health Monitoring** - Automatic health checks and crash recovery

## Architecture

```
┌─────────────────────────────────────────────────┐
│          Middleware Daemon (daemon.cpp)          │
│  - Socket IPC communication                     │
│  - Command processing and routing               │
├─────────────────────────────────────────────────┤
│  ProcessManager │ AppRegistry │ LifecycleManager│
│  - Fork/exec    │ - Config DB │ - Dependency   │
│  - Monitoring   │ - Registry  │   resolution   │
│  - Auto-restart │ - Lookup    │ - Startup order│
├─────────────────────────────────────────────────┤
│          ServiceDiscovery                       │
│  - Service registration                         │
│  - Port management                              │
│  - Resource allocation                          │
├─────────────────────────────────────────────────┤
│          CommandHandler                         │
│  - Phase 1 commands (PING, STATUS, etc)        │
│  - Phase 2 commands (SPAWN, KILL, etc)         │
└─────────────────────────────────────────────────┘
```

## Components

### 1. **process_info.hpp** - Data Structures
- `ProcessStatus` enum - Process states (RUNNING, CRASHED, RESTARTING, etc)
- `ProcessInfo` struct - Process metadata and metrics
- `ProcessMetrics` struct - Performance measurements

### 2. **app_config.hpp** - Application Configuration
- `AppConfig` struct - Complete app definition
  - Executable path, arguments, environment variables
  - Resource limits (memory, CPU)
  - Dependencies and critical flag
  - Health check configuration
  - Auto-restart policy

### 3. **process_manager.hpp/.cpp** - Process Lifecycle Control
```
Key Functions:
├── spawnProcess(config)      → Start new process
├── killProcess(app_id)       → Stop process (graceful/force)
├── restartProcess(app_id)    → Restart crashed process
├── getProcessInfo(app_id)    → Get process metadata
├── getAllProcesses()         → List all processes
├── startMonitoring()         → Enable background monitoring
├── stopMonitoring()          → Disable monitoring
└── getSystemMetrics()        → Total resource usage
```

**Features:**
- Fork/exec process spawning with environment setup
- Graceful SIGTERM / force SIGKILL termination
- Automatic child process signal handling (SIGCHLD)
- Background monitoring thread for crash detection
- Auto-restart on crash with configurable retry limits
- Process metrics tracking (uptime, memory, CPU, restarts)

### 4. **app_registry.hpp/.cpp** - Application Management
```
Key Functions:
├── registerApp(config)           → Register new app
├── unregisterApp(app_id)         → Remove app
├── getApp(app_id)                → Lookup app config
├── getAllApps()                  → List all apps
├── addDependency(app1, app2)     → Define dependencies
├── resolveDependencyGraph()      → Topological sort
├── hasCircularDependencies()     → Cycle detection
└── saveToFile()/loadFromFile()   → Persistence
```

**Features:**
- In-memory app configuration database
- Dependency management with circular dependency detection
- Topological sort for startup ordering
- JSON serialization (save/load)
- Thread-safe registry access

### 5. **lifecycle_manager.hpp/.cpp** - System Lifecycle
```
Key Functions:
├── startAllApps()                → Start in dependency order
├── stopAllApps()                 → Stop in reverse order
├── startApp(app_id)              → Start single app
├── gracefulShutdown()            → Coordinated shutdown
├── validateDependencies()        → Check deps satisfied
├── checkAllHealth()              → System health check
├── getSystemState()              → Boot/Ready/Shutdown state
└── getStatistics()               → System metrics
```

**Features:**
- Dependency-aware startup sequencing
- Graceful shutdown with dependency tracking
- Health monitoring and recovery
- System state machine (IDLE → BOOTING → READY → SHUTTING_DOWN)
- Automatic crash recovery

### 6. **service_discovery.hpp/.cpp** - Service Registry
```
Key Functions:
├── registerService(entry)        → Register service
├── unregisterService(id)         → Unregister
├── findServiceByID/Name/Port()   → Service lookup
├── getConnectionString()         → Host:port
├── allocateResources()           → Resource assignment
├── getAvailablePort()            → Port allocation
├── areDependenciesAvailable()    → Dependency check
└── getStatistics()               → Registry stats
```

**Features:**
- Dynamic service registration/deregistration
- Port management and allocation
- Resource allocation tracking (memory, CPU, disk)
- Service health status tracking
- Metadata storage (custom key-value pairs)

### 7. **commands_v2.hpp/.cpp** - Extended Command Handler
```
Phase 1 Commands (Original):
├── PING                         → Health check
├── STATUS                       → Daemon status
├── LOG_LEVEL <0-3>              → Set logging level
├── HELP                         → Show help
└── SHUTDOWN                     → Shutdown daemon

Phase 2 Commands (New):
├── SPAWN_PROCESS <app_id>       → Start app
├── KILL_PROCESS <app_id>        → Stop app
├── LIST_PROCESSES               → Show all processes
├── GET_PROCESS_STATUS <id>      → Process details
├── REGISTER_APP <config>        → Register app
├── UNREGISTER_APP <app_id>      → Unregister app
├── LIST_APPS                    → List registered apps
├── START_ALL                    → Boot all apps
├── STOP_ALL                     → Shutdown all apps
├── SYSTEM_STATUS                → System metrics
├── SYSTEM_HEALTH                → Health status
├── LIST_SERVICES                → List services
└── SERVICE_INFO <service_id>    → Service details
```

## Building Phase 2

### Prerequisites
```bash
# Install ARM64 cross-compiler
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Verify compiler
aarch64-linux-gnu-g++ --version
```

### Build Commands
```bash
cd /home/muhammad-imtinan-ul-haq/Desktop/middleware/phase2/middleware

# Clean and rebuild
make clean
make all

# Output binaries
../build/daemon    # Main service (2.8MB, ARM64 static)
../build/client    # CLI tool (2.8MB, ARM64 static)
```

### Makefile Targets
```bash
make all        # Build daemon and client (default)
make clean      # Remove object files
make clean-all  # Remove all artifacts
make help       # Show configuration
```

## Usage

### Start Daemon
```bash
/usr/sbin/middleware-daemon
```

### Client Commands
```bash
middleware> PING
OK: Daemon is alive

middleware> STATUS
OK: 5 processes running

middleware> LIST_PROCESSES
OK:
  storage-service (PID: 1234, Status: RUNNING)
  network-service (PID: 1235, Status: RUNNING)
  cache-service (PID: 1236, Status: CRASHED)

middleware> SPAWN_PROCESS storage-service
OK: Process spawned for storage-service

middleware> SYSTEM_STATUS
OK:
  Running Apps: 5/6
  Crashed Apps: 1
  Total Restarts: 3
  System Uptime: 3600 seconds

middleware> SYSTEM_HEALTH
OK: System health is DEGRADED
```

## File Structure

```
phase2/
├── middleware/
│   ├── process_info.hpp           # Data structures
│   ├── app_config.hpp             # App configuration
│   ├── process_manager.hpp/.cpp   # Process control
│   ├── app_registry.hpp/.cpp      # App database
│   ├── lifecycle_manager.hpp/.cpp # System lifecycle
│   ├── service_discovery.hpp/.cpp # Service registry
│   ├── commands_v2.hpp/.cpp       # Command handler
│   ├── daemon.cpp                 # Main service
│   ├── client.cpp                 # CLI tool
│   ├── Makefile                   # Build config
│   └── *.o                        # Object files (generated)
│
├── build/
│   ├── daemon                     # Main binary
│   ├── client                     # Client binary
│   └── rootfs.cpio.gz             # RootFS (from Phase 1)
│
├── kernel/
│   └── Image                      # Linux kernel (from Phase 1)
│
└── README.md                      # This file
```

## Key Features

### Auto-Restart Mechanism
```
Process crashes
  ↓
Signal handler (SIGCHLD) triggers
  ↓
Monitoring thread detects status = CRASHED
  ↓
Check: auto_restart && current_count < max_attempts
  ↓
Wait: restart_delay_ms
  ↓
Restart process with new PID
  ↓
Log restart event
```

### Dependency Resolution
```
APP_A depends on APP_B
APP_B depends on APP_C

Startup sequence: APP_C → APP_B → APP_A (resolved via topological sort)
Shutdown sequence: APP_A → APP_B → APP_C (reverse order)
```

### Resource Allocation
```
Service allocation:
- Memory limit: 512 MB
- CPU limit: 50%
- Port range: 8000-8100

Available port detection prevents conflicts
Allocation tracking for system capacity planning
```

### Health Monitoring
```
HEALTHY (all apps running with 0 consecutive failures)
  ↓ [app fails] 
DEGRADED (50% apps running or failures detected)
  ↓ [more failures]
UNHEALTHY (majority of apps crashed)
```

## Performance Characteristics

- **Process spawning**: ~10ms (fork/exec overhead)
- **Monitoring cycle**: 1 second (check interval)
- **Socket communication**: <1ms (local IPC)
- **Daemon memory**: ~2-3MB (base)
- **Per-process overhead**: ~100KB (tracking structures)

## Comparison: Phase 1 vs Phase 2

| Feature | Phase 1 | Phase 2 |
|---------|---------|---------|
| Process Management | Basic | Advanced (fork/exec, monitoring) |
| Auto-Restart | ❌ | ✅ (configurable) |
| App Registry | ❌ | ✅ (full config database) |
| Dependencies | ❌ | ✅ (resolution + validation) |
| Service Discovery | ❌ | ✅ (dynamic registration) |
| Lifecycle Control | ❌ | ✅ (coordinated startup/shutdown) |
| Health Monitoring | ❌ | ✅ (continuous checks) |
| Resource Tracking | ❌ | ✅ (memory, CPU allocation) |
| Commands | 5 | 14+ |

## Next Steps: Phase 3

Phase 3 will add:
- Custom OS design decisions (app model, permissions, APIs)
- Application sandbox environment
- Permission-based access control
- Inter-service communication protocols
- Advanced process isolation

## Technical Notes

- **Threading**: Uses std::thread for background monitoring
- **Locking**: std::mutex for thread-safe access
- **Signaling**: SIGCHLD for child process notification
- **IPC**: Unix domain sockets (AF_UNIX)
- **Compilation**: C++17, ARM64 static linking
- **Memory model**: RAII for automatic cleanup

## Building Documentation

All major components are documented with:
- Detailed line-by-line comments (Urdu/English mix)
- Architecture diagrams
- Usage examples
- Edge case handling

---

**Status**: Phase 2 ✅ COMPLETE
- All components implemented and compiled
- ARM64 static binaries ready for QEMU
- Full command interface operational
- Integration with Phase 1 verified

**Ready for Phase 3** - Custom OS design and security hardening
