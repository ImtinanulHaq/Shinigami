# PHASE 1 - Foundation: Linux Kernel + Middleware

## 🎯 Objectives

1. Boot a Linux ARM64 system via QEMU
2. Create minimal RootFS with BusyBox
3. Launch custom middleware daemon on boot
4. Implement basic logging and command handling
5. Validate entire boot-to-middleware pipeline

**Estimated Duration**: 2-3 weeks

---

## 🏗️ Architecture Diagram

```
QEMU ARM64 Emulator
    ↓
Linux Kernel (ARM64)
    ↓
Init Process (custom init.c)
    ↓
RootFS (BusyBox + overlay)
    ↓
Middleware Daemon (PID 1 or separate)
    ↓
Logging System ← Commands
```

---

## 📋 Detailed Tasks

### Task 1: Kernel Setup
- [ ] Download Linux kernel source
- [ ] Create minimal ARM64 config
- [ ] Cross-compile for ARM64
- [ ] Test boot with QEMU

**Files**:
- `kernel/config-arm64` - Kernel configuration
- `kernel/build.sh` - Build script

**Expected Time**: 3-4 hours

### Task 2: RootFS Creation
- [ ] Build BusyBox
- [ ] Create directory structure
- [ ] Add essential utilities
- [ ] Create init script

**Files**:
- `rootfs/busybox/Makefile`
- `rootfs/scripts/mkrootfs.sh`
- `rootfs/overlay/etc/inittab`

**Expected Time**: 2-3 hours

### Task 3: Middleware Daemon
- [ ] Design daemon architecture
- [ ] Implement logging system
- [ ] Create command parser
- [ ] Implement socket-based IPC

**Files**:
- `middleware/daemon.c` - Main daemon
- `middleware/logger.h` - Logging API
- `middleware/commands.c` - Command handler
- `middleware/socket.c` - IPC mechanism

**Expected Time**: 4-5 hours

### Task 4: Integration & Testing
- [ ] Modify init to launch middleware
- [ ] Create build automation
- [ ] Test on QEMU
- [ ] Validate logs and commands

**Files**:
- `build/Makefile`
- `scripts/run-qemu.sh`
- `scripts/test.sh`

**Expected Time**: 2-3 hours

---

## 🛠️ Technical Details

### Kernel Configuration

Minimal kernel config for ARM64:
```
CONFIG_ARM64=y
CONFIG_QEMU_FIRMWARE=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_PRINTK=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_VT=y
CONFIG_VT_CONSOLE=y
```

### RootFS Structure

```
/
├── bin          → /usr/bin (symlink)
├── sbin         → /usr/sbin (symlink)
├── lib          → /usr/lib (symlink)
├── usr/
│   ├── bin      (busybox + tools)
│   ├── sbin     (daemons)
│   └── lib      (shared libraries)
├── etc/
│   ├── inittab  (init configuration)
│   ├── passwd   (user database)
│   └── group    (group database)
├── proc         (proc filesystem)
├── sys          (sysfs)
├── tmp          (temporary files)
├── var/
│   ├── log      (logs)
│   ├── run      (runtime data)
│   └── tmp      (temporary)
├── dev          (device nodes)
└── init         (custom init binary)
```

### Middleware Daemon Architecture

```
main()
    ↓
initialize_logger()    ← Setup logging
    ↓
setup_signals()        ← Handle SIGTERM, SIGINT
    ↓
create_socket()        ← Listen for commands
    ↓
main_loop()
    ├─ accept_connection()
    ├─ parse_command()
    └─ execute_command()
    ↓
cleanup_and_exit()     ← Graceful shutdown
```

### Logging System

```c
// Log levels
#define LOG_ERROR   0
#define LOG_WARN    1
#define LOG_INFO    2
#define LOG_DEBUG   3

// API
void log_init(const char *logfile);
void log_msg(int level, const char *fmt, ...);
void log_close(void);
```

### Command Protocol

Simple text-based command protocol:

```
Request:  <command> <arg1> <arg2> ... <argN>\n
Response: <status> <result_len>\n<result_data>
```

Example:
```
Request:  STATUS\n
Response: OK 45\nMiddleware running since 2026-01-24 10:30:15\n
```

---

## 🔧 Build System

### Makefile Structure

```makefile
all: kernel rootfs middleware

kernel:
    cd kernel && make

rootfs: kernel
    cd rootfs && bash scripts/mkrootfs.sh

middleware: rootfs
    make -C middleware

test: all
    bash scripts/test.sh

clean:
    make -C kernel clean
    make -C rootfs clean
    make -C middleware clean
```

### Cross-Compilation Settings

```bash
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CFLAGS="-march=armv8-a -O2"
export LDFLAGS="-static"
```

---

## 📊 Middleware Features

### Core Features
- [x] Daemon startup on boot
- [ ] Logging to file + console
- [ ] Command socket (TCP/Unix)
- [ ] Status reporting
- [ ] Graceful shutdown

### Command Set
- `STATUS` - Get middleware status
- `LOG_LEVEL <level>` - Change log level
- `HELP` - Show available commands
- `SHUTDOWN` - Graceful shutdown
- `PING` - Health check

---

## ✅ Testing Checklist

- [ ] Kernel boots without panic
- [ ] RootFS mounts correctly
- [ ] Init process starts
- [ ] Middleware daemon starts (check with ps)
- [ ] Logging works (check /var/log/middleware.log)
- [ ] Socket is created
- [ ] Can connect and send commands
- [ ] Can retrieve status
- [ ] Can change log level
- [ ] Graceful shutdown works

---

## 🚀 Success Criteria

✅ **Phase 1 is complete when:**
1. QEMU ARM64 boots successfully
2. Middleware daemon runs automatically
3. Logs are written to file
4. Can send commands and receive responses
5. System shuts down gracefully
6. No kernel panics or crashes

---

## 📝 Notes & Tips

### QEMU Command
```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a57 \
  -m 512 \
  -kernel kernel/arch/arm64/boot/Image \
  -initrd rootfs.cpio.gz \
  -append "root=/dev/ram0 rw console=ttyAMA0" \
  -serial stdio \
  -nographic
```

### Debugging
- Use `-d exec,cpu_reset` for debug output
- Add `panic=1` to kernel cmdline to reboot on panic
- Use `strace` in RootFS to trace system calls

### Performance
- Minimal kernel config = faster boot
- `-cpu host` in QEMU may improve speed (if available)
- BusyBox is lightweight, good for embedded

---

## 📚 References

- [Linux Kernel Documentation](https://www.kernel.org/doc/)
- [ARM64 Architecture](https://developer.arm.com/documentation)
- [QEMU ARM64 Emulation](https://wiki.qemu.org/Features/ARM)
- [BusyBox Guide](https://busybox.net/)

---

**Next Phase**: [PHASE 2 - Android Principles](./PHASE2.md)
