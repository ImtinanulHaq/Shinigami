# Secure Linux Middleware — Complete Documentation
### From Zero to Working System | Easy English | Step by Step

> **Who is this for?**
> You know C/C++ language basics but you don't know how to build a real system like this.
> This document will teach you **everything** — what it is, why we do it, how to do it,
> and what every piece of code/concept means.

---

## Table of Contents

1. [What Are We Building?](#1-what-are-we-building)
2. [Background Knowledge You Need First](#2-background-knowledge-you-need-first)
3. [How Linux Works — The Basics](#3-how-linux-works--the-basics)
4. [System Calls — The Most Important Concept](#4-system-calls--the-most-important-concept)
5. [Processes — The Heart of Linux](#5-processes--the-heart-of-linux)
6. [IPC — How Programs Talk to Each Other](#6-ipc--how-programs-talk-to-each-other)
7. [Security Concepts You Must Know](#7-security-concepts-you-must-know)
8. [Speed Concepts You Must Know](#8-speed-concepts-you-must-know)
9. [Our Middleware Architecture — Full Explanation](#9-our-middleware-architecture--full-explanation)
10. [Phase 1 — Ring Buffer (Zero Copy IPC)](#10-phase-1--ring-buffer-zero-copy-ipc)
11. [Phase 2 — Service Manager](#11-phase-2--service-manager)
12. [Phase 3 — Security Layer (seccomp + namespaces)](#12-phase-3--security-layer-seccomp--namespaces)
13. [Phase 4 — HAL Layer](#13-phase-4--hal-layer)
14. [Phase 5 — io_uring Event Loop](#14-phase-5--io_uring-event-loop)
15. [Phase 6 — Services and Client Proxy](#15-phase-6--services-and-client-proxy)
16. [Putting It All Together](#16-putting-it-all-together)
17. [Build System — Makefile Explained](#17-build-system--makefile-explained)
18. [Testing Your Middleware](#18-testing-your-middleware)
19. [Common Errors and How to Fix Them](#19-common-errors-and-how-to-fix-them)
20. [Full Project Folder Structure](#20-full-project-folder-structure)

---

## 1. What Are We Building?

### Simple Explanation

Think of our middleware like a **post office** inside a computer.

- **Applications** are like people who want to send letters
- **Hardware** (speakers, sensors, cameras) are like destinations
- **Middleware** is the post office in between — it takes the letter, figures out where it goes, delivers it safely and fast

Without middleware, every app would need to talk to hardware directly. That is messy, unsafe, and slow.

### Android vs Our System

Android has a middleware too. You use it every time you open an app on your phone. When you press the volume button, an Android middleware service called "AudioService" handles it. When your screen rotates, "SensorService" handles it.

We are building the **same idea** but:
- **No Java, no graphics** — pure C/C++ in the terminal
- **Faster** — Android uses a system called "Binder IPC" which copies data 2 times. We will copy 0 times (called zero-copy)
- **More secure** — each service will be locked in its own sandbox so it cannot harm other parts of the system

### What Our System Will Do

```
BEFORE our middleware:
App wants audio → App writes messy kernel code → Hope it works

AFTER our middleware:
App wants audio → App calls AudioProxy.play() → Middleware handles everything safely and fast
```

---

## 2. Background Knowledge You Need First

Before building anything, you need to understand these ideas. Do not skip this section.

### 2.1 What is a Process?

A **process** is a running program. When you type `./myprogram` in the terminal, Linux creates a process for it. Every process has:

- Its own **memory** — other processes cannot read it (this is security)
- A **Process ID (PID)** — a number that identifies it
- **File descriptors** — numbers that represent open files, sockets, pipes

### 2.2 What is a Daemon?

A **daemon** is a process that runs in the background forever. It has no terminal attached. Examples:
- `sshd` — waits for SSH connections
- `cron` — runs scheduled tasks
- Our middleware services will be daemons

### 2.3 What is a File Descriptor?

In Linux, **everything is a file**. A file descriptor (fd) is just an integer like `3` or `7` that represents:
- An actual file on disk
- A network socket
- A pipe
- A special device like `/dev/audio`

When you open a file, Linux gives you a number. You use that number for all future operations:
```
fd = open("/dev/audio", ...)   → Linux says "okay, this is fd number 5"
write(5, data, size)           → write data to fd 5 (which is /dev/audio)
close(5)                       → done, close fd 5
```

### 2.4 What is Memory-Mapped Memory (mmap)?

Normal file reading: your program asks kernel → kernel copies data → your program has it. That is 2 steps, 1 copy.

`mmap()` is different. It says: "Give me a pointer directly to a piece of memory/file. I will read/write it myself." Zero copies. This is how we achieve speed.

Think of it like this:
- Normal: You ask a librarian for a book → they bring you a photocopy
- mmap: You walk directly to the shelf and read the book

---

## 3. How Linux Works — The Basics

### 3.1 Kernel Space vs User Space

Linux is divided into two worlds:

```
┌─────────────────────────────────────┐
│          USER SPACE                 │
│   Your apps, our middleware, etc.   │
│   Cannot touch hardware directly    │
│   Cannot access other process memory│
└────────────────┬────────────────────┘
                 │  System Calls (the only bridge)
┌────────────────▼────────────────────┐
│          KERNEL SPACE               │
│   Linux Kernel                      │
│   Controls ALL hardware             │
│   Controls ALL memory               │
│   Controls ALL processes            │
└─────────────────────────────────────┘
```

**Why this separation?** Security. If your app crashes, the kernel keeps running. If your app tries to do something evil, the kernel blocks it.

### 3.2 The /proc and /sys Filesystems

These are not real files on disk. They are windows into the kernel:

- `/proc/1234/` — information about process with PID 1234
- `/proc/meminfo` — current memory usage
- `/sys/bus/usb/` — all connected USB devices
- `/dev/` — device files (audio, video, sensors, etc.)

Our middleware will read these to talk to hardware.

### 3.3 Linux Namespaces — Isolation Rooms

Imagine you rent an apartment. You have your own bedroom, bathroom, kitchen — your own small world inside the building.

Linux namespaces do this for processes. We can give a service its own:
- PID namespace — it thinks it is the only process running
- Network namespace — it has its own network, cannot see others
- Mount namespace — it has its own filesystem view
- User namespace — it has its own users

We will use this to sandbox our services. Even if a service gets hacked, it cannot see or affect other services.

---

## 4. System Calls — The Most Important Concept

### What is a System Call?

A **syscall** is the ONLY way a user-space program can ask the kernel to do something. Every time you:
- Open a file → `open()` syscall
- Write to socket → `write()` or `send()` syscall
- Create a process → `fork()` or `clone()` syscall
- Allocate memory → `mmap()` syscall

You are making a system call. Under the hood, your C standard library wraps these. `printf()` eventually calls `write()`. `malloc()` eventually calls `mmap()` or `brk()`.

### The Important Syscalls for Our Middleware

| Category | Syscall | What it does |
|----------|---------|--------------|
| Processes | `fork()` | Create a copy of current process |
| Processes | `clone()` | Create process with specific namespaces |
| Processes | `execve()` | Replace current process with a new program |
| Processes | `waitpid()` | Wait for a child process to finish |
| IPC | `socket()` | Create a network/Unix socket |
| IPC | `bind()` | Attach socket to an address |
| IPC | `accept()` | Accept an incoming connection |
| IPC | `send()` / `recv()` | Send and receive data |
| Memory | `mmap()` | Map memory / file into address space |
| Memory | `munmap()` | Unmap memory |
| Memory | `mprotect()` | Set memory read/write/execute permissions |
| I/O | `read()` / `write()` | Read from / write to file descriptor |
| I/O | `open()` / `close()` | Open and close files |
| I/O | `ioctl()` | Send control commands to devices |
| I/O | `epoll_create()` | Create event monitor |
| I/O | `epoll_wait()` | Wait for events on multiple fds |
| Security | `seccomp()` | Filter which syscalls a process can make |
| Security | `prctl()` | Control process properties |
| Security | `capset()` | Set process capabilities |
| Signals | `sigaction()` | Register signal handlers |
| Signals | `kill()` | Send signal to process |
| Threads | `pthread_create()` | Create a new thread |
| Threads | `pthread_mutex_lock()` | Lock a mutex |
| Time | `timerfd_create()` | Create a timer as a file descriptor |

### How to Look Up Any Syscall

On your Linux terminal, type:
```
man 2 open
man 2 mmap
man 2 socket
```
The `2` means "Section 2 = System Calls". This is your best reference.

---

## 5. Processes — The Heart of Linux

### 5.1 Creating Processes with fork()

`fork()` creates an exact copy of the current process. The original is the **parent**, the copy is the **child**. They run simultaneously.

```
fork() returns:
  - In parent: the child's PID (a positive number like 1234)
  - In child: 0
  - On error: -1
```

**How to use fork():**
```
pid = fork()
if pid == 0:
    → You are the child. Do child work here.
if pid > 0:
    → You are the parent. You know child's PID.
if pid == -1:
    → Error. Fork failed.
```

### 5.2 Creating a Daemon Process

A daemon must:
1. Fork from its parent (so the terminal does not wait for it)
2. Call `setsid()` to become a session leader (detach from terminal)
3. Fork again (prevents re-acquiring a terminal)
4. Change directory to `/` (so it does not block any mounted filesystem)
5. Close file descriptors 0, 1, 2 (stdin, stdout, stderr)
6. Open `/dev/null` for stdin, stdout, stderr

We will do exactly this for every service in our middleware.

### 5.3 Process States

A process can be in these states:
- **Running** — currently executing on CPU
- **Sleeping** — waiting for something (I/O, timer, lock)
- **Stopped** — paused by signal
- **Zombie** — finished but parent has not called waitpid() yet

Our services will spend most time in **sleeping** state, waking up only when there is work to do. This is efficient — no busy-waiting.

### 5.4 Signals

Signals are messages you can send to processes. Important ones:

| Signal | Number | Meaning |
|--------|--------|---------|
| SIGTERM | 15 | Please stop gracefully |
| SIGKILL | 9 | Stop immediately, cannot be ignored |
| SIGHUP | 1 | Reload configuration |
| SIGCHLD | 17 | Child process changed state |
| SIGSEGV | 11 | Segmentation fault (memory error) |

Our Service Manager will use signals to control services. When a service crashes, it sends SIGCHLD to the manager.

---

## 6. IPC — How Programs Talk to Each Other

IPC means **Inter-Process Communication**. Processes have separate memory, so they need special ways to share data.

### 6.1 Types of IPC — Comparison

| Method | Speed | Use Case | Our Usage |
|--------|-------|----------|-----------|
| Pipes | Medium | Parent to child | Simple logging |
| Unix Domain Sockets | Fast | Any two processes | Service Manager |
| Shared Memory (mmap) | Fastest | High-bandwidth data | Ring Buffer (main IPC) |
| POSIX Message Queues | Medium | Async messaging | Notifications |
| TCP Sockets | Slow | Network communication | Not used internally |

### 6.2 Unix Domain Sockets — How They Work

A Unix Domain Socket is like a network socket but it lives in the filesystem as a file (like `/tmp/audio_service.sock`). It is much faster than TCP because there is no network stack involved.

The flow:
```
SERVER SIDE:
1. socket()  → create socket, get fd
2. bind()    → attach to a path like /tmp/myservice.sock
3. listen()  → start accepting connections
4. accept()  → wait for client, get client_fd when connected
5. recv()    → read data from client
6. send()    → reply to client

CLIENT SIDE:
1. socket()  → create socket, get fd
2. connect() → connect to /tmp/myservice.sock
3. send()    → send request
4. recv()    → read reply
```

We will use this for our Service Manager (the central registry of services).

### 6.3 Shared Memory — The Fast Way

This is the key to our speed advantage. Instead of sending data through the kernel twice, we put it in a shared memory region that both processes can read and write directly.

```
NORMAL IPC (socket/pipe):
App writes data → kernel copies to buffer → Service reads → kernel copies again
TWO kernel crossings, TWO copies. SLOW.

SHARED MEMORY:
App writes to shared memory ─────────────────► Service reads from shared memory
ZERO kernel crossings for data, ZERO copies. FAST.
```

How to create shared memory:
```
1. shm_open("/myshared", O_CREAT | O_RDWR, 0600)  → create shared memory object
2. ftruncate(fd, SIZE)                             → set its size
3. mmap(NULL, SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0) → map it into your process
4. Now pointer you got from mmap() points to shared memory
5. Both processes mmap() the same object → they see the same memory
```

### 6.4 Ring Buffer — The Data Structure for Fast IPC

A ring buffer (also called circular buffer) is a fixed-size buffer that wraps around. It has two pointers:
- **Head** — where new data is written
- **Tail** — where data is read from

```
RING BUFFER (size = 8 slots):

[ 0 ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ][ 6 ][ 7 ]
         ↑ TAIL              ↑ HEAD
         (read here)          (write here)

Writer writes to HEAD, then moves HEAD forward.
Reader reads from TAIL, then moves TAIL forward.
When HEAD or TAIL reach the end, they wrap back to 0.
No memory allocation. No copying. Extremely fast.
```

We will put this ring buffer in **shared memory** so two processes can communicate at maximum speed. This is our zero-copy IPC.

### 6.5 Lock-Free Ring Buffer

A lock-free ring buffer uses **atomic operations** instead of mutex locks. Why?

- Mutex: Lock → Write → Unlock (3 steps, one thread at a time)
- Atomic: Write atomically (1 step, no blocking)

The key: `atomic_store()` and `atomic_load()` from `<stdatomic.h>` in C11. These are guaranteed to be thread-safe without needing a lock.

---

## 7. Security Concepts You Must Know

### 7.1 Linux Capabilities

In old Linux, you were either `root` (can do anything) or not (limited). That was bad because a service only needs ONE special thing but with root it could do EVERYTHING.

Linux Capabilities break root's powers into pieces. Examples:

| Capability | What it allows |
|------------|----------------|
| CAP_NET_BIND_SERVICE | Bind to ports below 1024 |
| CAP_SYS_RAWIO | Direct hardware I/O access |
| CAP_SYS_ADMIN | Many admin operations |
| CAP_KILL | Send signals to any process |
| CAP_SETUID | Change process user ID |

**Our approach:** Start as root, then drop ALL capabilities except the ones the specific service truly needs. An audio service only needs `CAP_SYS_RAWIO`. Nothing else. If it gets hacked, the attacker has very limited power.

### 7.2 seccomp — Syscall Firewall

seccomp (Secure Computing Mode) is a Linux feature that lets you **whitelist exactly which syscalls a process can make**. Any other syscall = the kernel kills the process immediately.

```
WITHOUT seccomp:
Audio service can call: open, write, read, socket, fork, execve, ptrace, kill...
(attacker can use ANY syscall to do evil things)

WITH seccomp whitelist:
Audio service can ONLY call: read, write, ioctl, mmap
Any other syscall → SIGKILL immediately
(attacker is completely trapped)
```

We implement this with `libseccomp` or directly using the `seccomp()` syscall with BPF (Berkeley Packet Filter) programs. A BPF program is a tiny program that runs in the kernel to decide: allow or deny.

### 7.3 Linux Namespaces — Deep Dive

We introduced namespaces earlier. Let us go deeper:

**PID Namespace:** Service thinks it is PID 1. It cannot see any other processes. If it tries to signal another process, it cannot — because from its view, there are no other processes.

**Network Namespace:** Service gets its own network stack. It has no internet, no connection to other services' network. Only what you explicitly give it through a virtual network interface.

**Mount Namespace:** Service sees only the directories you give it. You give audio service only `/dev/snd` (sound device). It cannot see `/etc`, `/home`, `/var`. 

**User Namespace:** Service thinks it is root inside its namespace but is actually an unprivileged user outside. Best of both worlds — service can do what it needs, but kernel knows it is not really root.

### 7.4 Binary Signing — Trust Nobody

When our Service Manager loads a service binary, it must verify it is authentic and has not been tampered with.

Method: **HMAC-SHA256 signature**

```
At BUILD TIME:
developer takes service binary → signs it with private key → produces signature file

AT RUNTIME:
Service Manager reads binary + signature file → verifies with public key
→ match: load the service
→ no match: REFUSE to run it, log the attempt
```

This prevents an attacker from replacing a service binary with a malicious one.

### 7.5 Memory Protection

- `mprotect()` sets memory regions as Read-Only or No-Execute
- We mark code regions as `PROT_READ | PROT_EXEC` (no writing)
- We mark data regions as `PROT_READ | PROT_WRITE` (no executing)
- Stack gets a guard page: a region set to `PROT_NONE` — any access kills the process (prevents stack overflow attacks)

---

## 8. Speed Concepts You Must Know

### 8.1 io_uring — The Fastest I/O in Linux

Traditional I/O is slow because:
1. Your program calls `read()` → syscall
2. Kernel reads data → copies to your buffer
3. Your program continues

Each syscall has overhead. For thousands of operations per second, this adds up.

**io_uring** (introduced in Linux 5.1) works differently:
1. Your program submits a batch of I/O requests to a **submission queue** (in shared memory)
2. Kernel processes them all asynchronously
3. Results appear in a **completion queue** (in shared memory)
4. Your program collects results whenever ready

```
TRADITIONAL:
App → syscall → wait → syscall → wait → syscall → wait
   ↑ 3 syscalls, 3 waits

io_uring:
App → submit 3 requests at once → continue doing other work → collect 3 results
   ↑ 1 syscall, 0 waits, results come in
```

This can make I/O 40-60% faster for high-throughput workloads.

### 8.2 epoll — Efficient Event Monitoring

Old way: check each file descriptor one by one → O(n) time
`epoll`: Linux tells you exactly which fds have events → O(1) time

```
epoll_create() → create event monitor
epoll_ctl()    → add/remove fds to monitor
epoll_wait()   → sleep until something happens, wake up with list of active fds
```

We use epoll in our event loop. The service manager sleeps until a client connects, a service sends a message, or a timer fires. No busy waiting, zero CPU usage while idle.

### 8.3 Memory Pool — No malloc in Hot Path

`malloc()` and `free()` are slow because they must manage the heap, possibly call the kernel, handle fragmentation. In a high-speed IPC system, calling malloc for every message is too slow.

**Solution:** Pre-allocate a big pool at startup. Give out fixed-size chunks from the pool. Return them when done. No kernel calls, no fragmentation.

```
AT STARTUP:
pool_init(1000 slots, each 256 bytes)  → allocate 256KB once

DURING OPERATION:
msg = pool_get()     → instant, just return pointer to next slot
... use msg ...
pool_return(msg)     → instant, mark slot as free
```

### 8.4 Avoid False Sharing — CPU Cache Lines

CPUs process memory in 64-byte chunks called **cache lines**. If two threads read/write different variables that happen to be in the same cache line, they fight over it even though they are using different variables. This is called **false sharing** and it is invisible but very slow.

**Solution:** Pad your shared data structures so each important variable is on its own cache line.

```c
// Bad: head and tail share a cache line
struct ring {
    int head;
    int tail;
};

// Good: each is on its own 64-byte cache line
struct ring {
    int head;
    char pad1[60];  // fill up to 64 bytes
    int tail;
    char pad2[60];
};
```

---

## 9. Our Middleware Architecture — Full Explanation

### 9.1 The Big Picture

```
┌──────────────────────────────────────────────────────────────┐
│                    TERMINAL APPLICATIONS                     │
│              app1          app2          app3                │
│               │             │             │                  │
│        AudioProxy    SensorProxy    CameraProxy              │
│         (C++ class)  (C++ class)   (C++ class)              │
└───────────────┬─────────────┬─────────────┬──────────────────┘
                │             │             │
                │    (Unix Socket — Register/Lookup)           │
                ▼             ▼             ▼
┌──────────────────────────────────────────────────────────────┐
│                   SERVICE MANAGER DAEMON                     │
│            (The central registry — always running)           │
│                                                              │
│   - Knows where every service lives                          │
│   - Verifies binary signatures before launching             │
│   - Monitors service health                                  │
│   - Restarts crashed services                               │
└──────┬──────────────────┬──────────────────┬────────────────┘
       │                  │                  │
       │  Ring Buffer     │  Ring Buffer     │  Ring Buffer
       │  (Shared Mem)    │  (Shared Mem)    │  (Shared Mem)
       ▼                  ▼                  ▼
┌────────────┐   ┌──────────────┐   ┌──────────────┐
│  AUDIO     │   │   SENSOR     │   │   CAMERA     │
│  SERVICE   │   │   SERVICE    │   │   SERVICE    │
│  DAEMON    │   │   DAEMON     │   │   DAEMON     │
│            │   │              │   │              │
│ sandboxed  │   │  sandboxed   │   │  sandboxed   │
│ seccomp    │   │  seccomp     │   │  seccomp     │
│ namespace  │   │  namespace   │   │  namespace   │
└─────┬──────┘   └──────┬───────┘   └──────┬───────┘
      │                 │                  │
      ▼                 ▼                  ▼
┌──────────────────────────────────────────────────────────────┐
│                       HAL LAYER (C)                          │
│         audio_hal.c      sensor_hal.c     camera_hal.c       │
│         (simple C structs with function pointers)            │
└──────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│                   LINUX KERNEL                               │
│    /dev/snd   /dev/video0   /sys/bus/iio   ioctl()           │
└──────────────────────────────────────────────────────────────┘
```

### 9.2 Data Flow — Step by Step

When an app wants to play audio:

```
Step 1: App calls AudioProxy::play("song.mp3")

Step 2: AudioProxy writes request into Ring Buffer
        (zero-copy — direct shared memory write)

Step 3: Ring Buffer notifies Audio Service via eventfd
        (one syscall — just writing number 1 to a fd)

Step 4: Audio Service wakes up from epoll_wait()
        (was sleeping, now active)

Step 5: Audio Service reads request from Ring Buffer
        (zero-copy — direct shared memory read)

Step 6: Audio Service calls HAL:
        audio_hal->play(data)

Step 7: HAL calls kernel:
        ioctl(audio_fd, AUDIO_PLAY_CMD, data)

Step 8: Kernel sends audio to hardware

Step 9: Audio Service writes result back to Ring Buffer

Step 10: App reads result from Ring Buffer
```

Total kernel crossings for data: **2** (one eventfd write, one eventfd read)
Data copies: **0**
Compare to Android Binder: 4+ kernel crossings, 2 copies

---

## 10. Phase 1 — Ring Buffer (Zero Copy IPC)

### What Files to Create

```
core/
├── ring_buffer.h    — the struct definition and function declarations
└── ring_buffer.c    — the implementation
```

### Understanding the ring_buffer.h File

This header file defines the structure of our ring buffer and declares functions. Everything in this file goes into shared memory.

**What fields we need in the struct:**

- `head` — atomic integer, position where next item will be written
- `tail` — atomic integer, position where next item will be read
- `capacity` — how many items the buffer can hold (set at creation, never changes)
- `item_size` — how big each item is in bytes (set at creation, never changes)
- `data[]` — a flexible array at the end where actual items are stored
- `notify_fd` — an `eventfd` file descriptor to wake up sleeping readers
- padding bytes between head and tail to avoid false sharing (64 bytes apart)

**Why atomic for head and tail?**

Because one process writes head and another reads it. Without atomic, the CPU might give them a half-written value. `_Atomic int` from `<stdatomic.h>` guarantees they see the complete value.

### Understanding the ring_buffer.c File

**`ring_buffer_create()` function:**
1. Calculate total size needed: `sizeof(ring_buffer_t) + capacity * item_size`
2. Call `shm_open()` to create a named shared memory object like `/ring_audio`
3. Call `ftruncate()` to set its size
4. Call `mmap()` to map it into this process's address space — get a pointer back
5. Initialize head=0, tail=0, capacity, item_size
6. Create an `eventfd(0, EFD_NONBLOCK)` for notifications
7. Return the pointer

**`ring_buffer_write()` function:**
1. Read current head (atomic load)
2. Calculate next_head = (head + 1) % capacity
3. Check if next_head == tail — if yes, buffer is FULL, return error
4. Copy item into `data[head * item_size]`
5. Atomically store next_head as new head
6. Write 1 to eventfd to wake up reader

**`ring_buffer_read()` function:**
1. Read current tail (atomic load)
2. If tail == head — buffer is EMPTY, return error
3. Copy item from `data[tail * item_size]`
4. Atomically store (tail+1) % capacity as new tail
5. Return item

**`ring_buffer_attach()` function:**
Used by the second process (reader) to attach to existing shared memory:
1. `shm_open("/ring_audio", O_RDWR, 0)` — open existing (no O_CREAT)
2. `mmap()` — map into this process
3. Return pointer — now both processes share the same memory

### Key Things to Remember for ring_buffer.c

- Use `__attribute__((aligned(64)))` on head and tail to ensure cache line alignment
- Use `memory_order_acquire` when loading and `memory_order_release` when storing — this ensures proper visibility between processes
- Link with `-lrt` (real-time library) because `shm_open()` requires it
- Link with `-lpthread` because atomics may need it

---

## 11. Phase 2 — Service Manager

### What Files to Create

```
core/
├── service_manager.h    — struct definitions, constants, function declarations
└── service_manager.c    — the main daemon implementation
```

### What the Service Manager Does

Think of it as a **phonebook** + **security guard** + **health monitor**.

- **Phonebook:** Services register their name and socket path. Clients look up service by name.
- **Security guard:** Before starting a service, verify its binary signature.
- **Health monitor:** If a service crashes, restart it.

### The ServiceEntry Struct

Each registered service has:
- `name[64]` — service name like "audio" or "sensor"
- `socket_path[128]` — path to service's Unix socket like `/tmp/audio.sock`
- `pid` — process ID of the service daemon
- `ring_buffer_path[128]` — name of the shared memory ring buffer
- `status` — enum: RUNNING, STOPPED, CRASHED
- `last_heartbeat` — timestamp of last heartbeat (to detect frozen services)

### The Message Format (ServiceMessage struct)

When a client or service communicates with Service Manager, it sends this struct:

```
ServiceMessage {
    int     type;          // what kind of message: REGISTER, LOOKUP, HEARTBEAT
    char    service_name[64];
    char    socket_path[128];
    int     pid;
    int     response_code;  // 0 = success, negative = error
    char    error_msg[256];
}
```

Using a fixed struct (not variable-length messages) is both faster and simpler.

### How Service Manager Starts (main loop)

```
1. Daemonize (fork, setsid, fork again)
2. Create Unix socket at /tmp/servicemanager.sock
3. bind() the socket
4. listen()
5. Set up epoll to watch the socket
6. Set up SIGCHLD handler (to detect crashed services)
7. Set up SIGTERM handler (for graceful shutdown)

MAIN LOOP:
8. epoll_wait() — sleep until activity
9. If new connection: accept() → read ServiceMessage
10. If type == REGISTER: add to registry, start service process
11. If type == LOOKUP: find in registry, send back socket_path
12. If type == HEARTBEAT: update last_heartbeat timestamp
13. Periodically check all services: if last_heartbeat too old → restart
14. Go back to step 8
```

### SIGCHLD Handler

When a service crashes:
1. Kernel sends SIGCHLD to Service Manager (its parent)
2. Handler calls `waitpid(-1, &status, WNOHANG)` to collect the zombie
3. Find which service had that PID
4. Mark it as CRASHED
5. After a delay, restart it

### Starting a Service Process

Service Manager starts services using:
1. `fork()` — create child process
2. In child: apply namespace restrictions with `clone()` flags or `unshare()`
3. Apply seccomp filter (see Phase 3)
4. Drop capabilities
5. `execve(service_binary_path, args, env)` — replace child with service binary

---

## 12. Phase 3 — Security Layer (seccomp + namespaces)

### What Files to Create

```
security/
├── seccomp_filter.h    — declarations
├── seccomp_filter.c    — seccomp BPF filter setup
├── sandbox.c           — namespace isolation
├── capabilities.c      — capability dropping
└── verify.c            — binary signature verification
```

### Understanding seccomp_filter.c

**Method 1: Using libseccomp (easier, recommended)**

Install: `sudo apt install libseccomp-dev`

The flow:
1. `seccomp_init(SCMP_ACT_KILL)` — create filter, default action = kill process
2. For each allowed syscall: `seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0)`
3. `seccomp_load(ctx)` — apply the filter to current process

After `seccomp_load()`, any syscall not in the whitelist will immediately kill the process.

**Different whitelist for each service type:**

Audio Service allowed syscalls:
`read, write, open, close, ioctl, mmap, munmap, brk, exit_group, futex, nanosleep`

Sensor Service allowed syscalls:
`read, write, open, close, ioctl, mmap, munmap, brk, exit_group, futex`

Network Service allowed syscalls:
`read, write, socket, connect, send, recv, close, mmap, munmap, brk, exit_group, futex`

**CRITICAL:** Always add `exit_group` and `futex` to every whitelist or your process cannot even exit cleanly.

### Understanding sandbox.c

**Using unshare() to create namespaces:**

```
Before creating sandbox:
1. unshare(CLONE_NEWPID)     → new PID namespace
2. unshare(CLONE_NEWNET)     → new network namespace
3. unshare(CLONE_NEWNS)      → new mount namespace
4. unshare(CLONE_NEWUSER)    → new user namespace (needs uid/gid mapping)

For user namespace:
Write to /proc/PID/uid_map: "0 1000 1" (map internal uid 0 to external uid 1000)
Write to /proc/PID/gid_map: "0 1000 1"
```

**Setting up a minimal filesystem with bind mounts:**

After `CLONE_NEWNS`, set up only what the service needs:
```
mount("tmpfs", "/tmp/service_root", "tmpfs", 0, NULL)
mkdir("/tmp/service_root/dev")
mount("/dev/snd", "/tmp/service_root/dev/snd", NULL, MS_BIND, NULL)  → only audio device
chroot("/tmp/service_root")  → service now has no access to real filesystem
```

### Understanding capabilities.c

After starting but before doing any work, a service calls `drop_capabilities()`:

```
1. prctl(PR_SET_KEEPCAPS, 1)  → allow keeping caps through uid change
2. setuid(service_uid)         → drop to normal user
3. cap_set_flag()              → set only needed capability
4. capset()                    → apply the capability set
5. prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)  → CRITICAL: can never gain new privileges
```

`PR_SET_NO_NEW_PRIVS` is very important. It means even if a service calls `execve()` to run a setuid binary, it does not gain new privileges. This single line closes many attack vectors.

### Understanding verify.c

**Simple HMAC-SHA256 verification:**

At build time (developer runs a script):
1. Read the service binary
2. Compute HMAC-SHA256 with a secret key
3. Save the hash to `service_name.sig` file

At runtime (Service Manager does this before launching):
1. Read the service binary
2. Compute HMAC-SHA256 with same key
3. Read the `.sig` file
4. Compare — if not equal, refuse to run

Use `openssl/hmac.h` — link with `-lssl -lcrypto`

---

## 13. Phase 4 — HAL Layer

### What the HAL Is

HAL means Hardware Abstraction Layer. It is the C layer that talks to actual hardware. Services above it do not know if they are talking to a real device or a test dummy — they just call functions.

### What Files to Create

```
hal/
├── hal_interface.h    — the base struct all HAL devices share
├── audio_hal.c        — audio hardware implementation  
├── sensor_hal.c       — sensor hardware implementation
└── camera_hal.c       — camera hardware implementation
```

### hal_interface.h — The Base Type

Every hardware device has at least:
```
hw_device_t {
    char    name[64];     — "audio", "sensor", "camera"
    int     version;      — version number for compatibility
    int     fd;           — file descriptor to the device
    int     (*open)(hw_device_t*)    — function pointer to open device
    int     (*close)(hw_device_t*)   — function pointer to close device
    char    reserved[64];            — space for future use
}
```

### audio_hal.c — What It Does

Audio HAL works with `/dev/snd/` (ALSA devices in Linux):

```
audio_open():
    fd = open("/dev/snd/pcmC0D0p", O_WRONLY)   — open playback device
    ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &params)  — set sample rate etc.
    return 0 on success

audio_play(data, size):
    write(fd, data, size)   — write PCM audio data

audio_set_volume(level):
    ioctl(mixer_fd, SOUND_MIXER_WRITE_VOLUME, &level)

audio_close():
    close(fd)
```

### sensor_hal.c — What It Does

Linux exposes sensors through the IIO (Industrial I/O) subsystem at `/sys/bus/iio/devices/`:

```
sensor_open():
    fd = open("/sys/bus/iio/devices/iio:device0/in_accel_x_raw", O_RDONLY)

sensor_read(float* x, float* y, float* z):
    read(fd_x, buf, sizeof(buf)) → parse number → *x = value * scale
    read(fd_y, buf, sizeof(buf)) → parse number → *y = value * scale
    read(fd_z, buf, sizeof(buf)) → parse number → *z = value * scale
```

### Important HAL Design Rules

1. HAL functions should never block for more than a few milliseconds
2. HAL should not allocate memory — caller provides buffers
3. Return 0 on success, negative errno on failure
4. Always close device on error (do not leak file descriptors)

---

## 14. Phase 5 — io_uring Event Loop

### What Files to Create

```
core/
├── io_uring_loop.h    — declarations
└── io_uring_loop.c    — the event loop using io_uring
```

### What the Event Loop Does

Every service runs an event loop — an infinite loop that:
1. Waits for something to happen (new request, timer, signal)
2. Handles it
3. Repeats

### io_uring Setup

You need kernel 5.1+. Check: `uname -r`
Install headers: `sudo apt install liburing-dev`

**The setup steps:**
1. `io_uring_queue_init(32, &ring, 0)` — create ring with 32 submission slots
2. Register file descriptors you care about: your eventfd from ring buffer, timer fds, socket fds

**The loop:**
```
LOOP:
1. io_uring_get_sqe(&ring) → get a Submission Queue Entry
2. io_uring_prep_read(sqe, eventfd, buf, 8, 0) → prepare a read operation
3. io_uring_submit(&ring) → submit it
4. io_uring_wait_cqe(&ring, &cqe) → sleep until completion
5. Check cqe->res (result code)
6. Process the event (call the appropriate handler)
7. io_uring_cqe_seen(&ring, cqe) → mark as processed
8. Go back to step 1
```

### Combining io_uring with epoll

For very complex cases you can use epoll for socket monitoring and io_uring for file/device I/O. Add the epoll fd to the io_uring watch list and handle both in one loop.

---

## 15. Phase 6 — Services and Client Proxy

### Service Template — How to Build Each Service

Every service follows the same pattern:

```
SERVICE STARTUP SEQUENCE:
1. Service Manager starts the binary
2. Apply seccomp filter (Phase 3)
3. Set up namespaces (Phase 3)
4. Drop capabilities (Phase 3)
5. Attach to ring buffer (Phase 1)
6. Open HAL device (Phase 4)
7. Register with Service Manager via Unix socket
8. Start io_uring event loop (Phase 5)
9. Process requests from ring buffer forever
```

### What Files to Create

```
services/
├── service_template.h    — the common service structure
├── audio_service.c       — audio service implementation
└── sensor_service.c      — sensor service implementation
```

### service_template.h

All services share:
```
service_t {
    char            name[64];
    ring_buffer_t*  rb;           — pointer to shared ring buffer
    hw_device_t*    hal;          — pointer to HAL device
    int             manager_fd;   — socket to service manager
    int             running;      — 1 = running, 0 = should stop
    struct io_uring ring;         — io_uring instance
    void (*handle_request)(service_t*, void* request);  — function to handle requests
}
```

### The Client Proxy (C++ Classes)

Proxy classes are what applications use. They hide all the IPC complexity.

```
apps see this:
    AudioProxy audio;
    audio.play("song.mp3");
    audio.setVolume(80);

internally the proxy does:
    ring_buffer_write(rb, &request)
    wait for response on eventfd
    ring_buffer_read(rb, &response)
    return response.result
```

Each proxy:
- Attaches to the service's ring buffer on construction
- Sends requests as fixed-size structs
- Waits for responses using eventfd + epoll
- Disconnects on destruction

---

## 16. Putting It All Together

### Startup Order

The ORDER matters. Start them in this sequence:

```
Step 1:
$ ./service_manager &
→ Creates /tmp/servicemanager.sock
→ Enters epoll wait loop

Step 2:
$ ./audio_service &
→ Applies seccomp, namespaces
→ Opens audio HAL
→ Creates ring buffer /ring_audio in shared memory
→ Connects to service manager
→ Sends REGISTER message: name="audio", ring_path="/ring_audio"
→ Service manager records this
→ Enters io_uring loop

Step 3:
$ ./sensor_service &
→ Same as audio_service but for sensors

Step 4:
$ ./test_app
→ Creates AudioProxy
→ AudioProxy sends LOOKUP to service manager: "audio"
→ Service manager replies: "/ring_audio"
→ AudioProxy attaches to /ring_audio ring buffer
→ App calls audio.play()
→ Request goes through ring buffer
→ Audio service handles it
→ Result comes back through ring buffer
→ App gets result
```

### Shutdown Order

Reverse of startup:
1. Kill applications first (`SIGTERM`)
2. Kill services (`SIGTERM` — they close HAL, unmap ring buffer)
3. Kill service manager last (`SIGTERM` — it cleans up all shared memory)

Service manager cleanup:
- `shm_unlink("/ring_audio")` — delete shared memory objects
- `unlink("/tmp/servicemanager.sock")` — delete socket file
- `unlink("/tmp/audio.sock")` etc.

---

## 17. Build System — Makefile Explained

### What the Makefile Does

A Makefile tells the compiler what to compile and in what order. It avoids recompiling things that have not changed.

### Our Makefile Structure

```makefile
# Compiler settings
CC  = gcc         ← for .c files
CXX = g++         ← for .cpp files

# Compiler flags explained:
# -Wall           → show all warnings
# -Wextra         → show extra warnings
# -O2             → optimize for speed
# -g              → include debug symbols
# -std=c11        → use C11 standard (we need this for atomics)
# -std=c++17      → use C++17 for proxy code
CFLAGS   = -Wall -Wextra -O2 -g -std=c11
CXXFLAGS = -Wall -Wextra -O2 -g -std=c++17

# Libraries to link:
# -lrt            → for shm_open()
# -lpthread       → for pthread and atomics
# -lseccomp       → for seccomp filters
# -lssl -lcrypto  → for HMAC signature verification
# -luring         → for io_uring
LIBS = -lrt -lpthread -lseccomp -lssl -lcrypto -luring

# Build targets
all: service_manager audio_service sensor_service test_app

service_manager: core/service_manager.o core/ring_buffer.o security/verify.o
    $(CC) -o $@ $^ $(LIBS)

audio_service: services/audio_service.o hal/audio_hal.o core/ring_buffer.o \
               security/seccomp_filter.o security/sandbox.o security/capabilities.o
    $(CC) -o $@ $^ $(LIBS)

test_app: apps/test_audio.o proxy/audio_proxy.o core/ring_buffer.o
    $(CXX) -o $@ $^ $(LIBS)

clean:
    rm -f *.o core/*.o services/*.o hal/*.o security/*.o proxy/*.o apps/*.o
    rm -f service_manager audio_service sensor_service test_app

# Install (optional)
install:
    cp service_manager /usr/local/bin/
    cp audio_service /usr/local/bin/
```

### How to Build

```bash
# Build everything
make

# Build only one target
make audio_service

# Clean build artifacts
make clean

# Build with verbose output to see commands
make V=1
```

---

## 18. Testing Your Middleware

### Unit Tests — Test Each Piece Alone

**Test 1: Ring Buffer**
```
1. Create ring buffer with capacity 10
2. Write 5 items
3. Read 5 items
4. Verify they match exactly
5. Write 10 items (fill it up)
6. Try writing 11th — should fail with buffer full
7. Read all 10
8. Try reading again — should fail with buffer empty
```

**Test 2: seccomp Filter**
```
1. Apply audio_service whitelist
2. Try a disallowed syscall like socket()
3. Process should be killed (SIGSYS)
4. Check with strace that only allowed syscalls are used
```

**Test 3: Service Manager Registration**
```
1. Start service_manager
2. Connect via socket
3. Send REGISTER message
4. Send LOOKUP message for same service
5. Verify response has correct socket_path
```

### Integration Tests — Test the Full Flow

```
1. Start entire middleware stack
2. Connect test app
3. Request audio play → verify HAL was called
4. Kill audio_service
5. Verify service_manager detects crash within 5 seconds
6. Verify service_manager restarts audio_service
7. Send another audio request → should work again
```

### Tools for Debugging

**strace** — see every syscall a process makes:
```bash
strace -p PID              → attach to running process
strace ./audio_service     → trace from start
strace -e trace=read,write → only show read and write syscalls
```

**valgrind** — find memory leaks and corruption:
```bash
valgrind --leak-check=full ./test_app
```

**/proc/PID/maps** — see a process's memory mappings:
```bash
cat /proc/$(pgrep audio_service)/maps
```

**ipcs** — list shared memory segments:
```bash
ipcs -m
```

---

## 19. Common Errors and How to Fix Them

### Error: "Operation not permitted" when setting seccomp

**Cause:** Your kernel might need `CONFIG_SECCOMP_FILTER=y` enabled.

**Fix:** Check with `grep SECCOMP /boot/config-$(uname -r)`. If not set, use a different kernel or VM.

### Error: "No such file or directory" for /dev/snd/

**Cause:** No audio hardware or ALSA not loaded.

**Fix:** `sudo modprobe snd-dummy` — loads a dummy sound card for testing.

### Error: Segfault in ring buffer

**Cause:** Almost always the two processes have different sizes for the ring buffer struct.

**Fix:** Make sure both processes use the exact same header file. Do not have two copies.

### Error: "Address already in use" for Unix socket

**Cause:** Previous run crashed and left the socket file behind.

**Fix:** Add to startup code: `unlink("/tmp/servicemanager.sock")` before `bind()`. Or run `rm /tmp/*.sock` manually.

### Error: Shared memory persists after crash

**Cause:** `shm_unlink()` was never called.

**Fix:** List with `ls /dev/shm/`. Delete with `rm /dev/shm/ring_audio` etc. Add signal handlers to always clean up.

### Error: Service Manager does not detect crash

**Cause:** SIGCHLD handler not set up correctly or `waitpid()` not being called.

**Fix:** Make sure `sigaction(SIGCHLD, &sa, NULL)` is called with `SA_NOCLDSTOP` flag so you get SIGCHLD only on process termination, not stop/continue.

### Error: io_uring fails to initialize

**Cause:** Kernel too old (need 5.1+) or user has insufficient locked memory.

**Fix:** `ulimit -l unlimited` or add `* - memlock unlimited` to `/etc/security/limits.conf`.

---

## 20. Full Project Folder Structure

```
secure_middleware/
│
├── Makefile                       ← Build everything
│
├── core/                          ← Core infrastructure
│   ├── ring_buffer.h              ← Zero-copy IPC header
│   ├── ring_buffer.c              ← Zero-copy IPC implementation
│   ├── service_manager.h          ← Service registry header
│   ├── service_manager.c          ← Central daemon
│   ├── memory_pool.h              ← Pre-allocated pool header
│   ├── memory_pool.c              ← Pre-allocated pool impl
│   ├── io_uring_loop.h            ← Event loop header
│   └── io_uring_loop.c            ← io_uring event loop
│
├── security/                      ← All security components
│   ├── seccomp_filter.h           ← Syscall filter header
│   ├── seccomp_filter.c           ← Per-service whitelists
│   ├── sandbox.h                  ← Sandbox header
│   ├── sandbox.c                  ← Namespace isolation
│   ├── capabilities.h             ← Caps header
│   ├── capabilities.c             ← Privilege dropping
│   ├── verify.h                   ← Signature header
│   └── verify.c                   ← HMAC-SHA256 verification
│
├── hal/                           ← Hardware Abstraction Layer
│   ├── hal_interface.h            ← Common hw_device_t struct
│   ├── audio_hal.h                ← Audio HAL header
│   ├── audio_hal.c                ← ALSA implementation
│   ├── sensor_hal.h               ← Sensor HAL header
│   ├── sensor_hal.c               ← IIO sysfs implementation
│   ├── camera_hal.h               ← Camera HAL header
│   └── camera_hal.c               ← V4L2 implementation
│
├── services/                      ← Service daemons
│   ├── service_template.h         ← Common service_t struct
│   ├── audio_service.c            ← Audio daemon
│   └── sensor_service.c           ← Sensor daemon
│
├── proxy/                         ← Client-side C++ APIs
│   ├── service_proxy.h            ← Base proxy class
│   ├── audio_proxy.h              ← AudioProxy header
│   ├── audio_proxy.cpp            ← AudioProxy implementation
│   ├── sensor_proxy.h             ← SensorProxy header
│   └── sensor_proxy.cpp           ← SensorProxy implementation
│
├── apps/                          ← Test applications
│   ├── test_audio.cpp             ← Test audio service
│   ├── test_sensor.cpp            ← Test sensor service
│   └── test_stress.cpp            ← Stress test all services
│
└── tools/                         ← Developer tools
    ├── sign_binary.sh             ← Sign a service binary
    └── monitor.sh                 ← Watch middleware health
```

---

## Quick Reference — Important Linux Headers

| Header | What it gives you |
|--------|------------------|
| `<unistd.h>` | fork, read, write, close, getpid, execve |
| `<sys/socket.h>` | socket, bind, listen, accept, connect, send, recv |
| `<sys/un.h>` | sockaddr_un (Unix domain sockets) |
| `<sys/mman.h>` | mmap, munmap, mprotect, shm_open |
| `<sys/wait.h>` | waitpid, WIFEXITED, WIFSIGNALED |
| `<sys/prctl.h>` | prctl, PR_SET_NO_NEW_PRIVS, PR_SET_KEEPCAPS |
| `<sys/capability.h>` | cap_set_flag, capset, capget |
| `<seccomp.h>` | seccomp_init, seccomp_rule_add, seccomp_load |
| `<stdatomic.h>` | _Atomic, atomic_load, atomic_store, memory_order |
| `<liburing.h>` | io_uring_queue_init, io_uring_get_sqe, io_uring_submit |
| `<sched.h>` | clone, unshare, CLONE_NEWPID, CLONE_NEWNET |
| `<signal.h>` | sigaction, kill, sigemptyset, SIGCHLD, SIGTERM |
| `<fcntl.h>` | open, O_RDWR, O_CREAT, O_NONBLOCK |
| `<sys/epoll.h>` | epoll_create1, epoll_ctl, epoll_wait |
| `<sys/eventfd.h>` | eventfd, EFD_NONBLOCK, EFD_SEMAPHORE |
| `<openssl/hmac.h>` | HMAC, HMAC_CTX_new, EVP_sha256 |

---

## Your Learning Path — What to Study in Order

If you are starting from zero, study these topics in this exact order:

```
Week 1:
  → Processes: fork(), waitpid(), signals
  → File descriptors: open(), read(), write(), close()
  → Man pages: man 2 fork, man 2 open, man 2 write

Week 2:
  → IPC: Unix domain sockets (socket, bind, listen, accept, connect)
  → Shared memory: shm_open(), mmap()
  → Practice: Write a simple client-server with Unix sockets

Week 3:
  → Atomics in C11: _Atomic, atomic_store, atomic_load
  → Implement a simple ring buffer
  → Test it with two processes using shared memory

Week 4:
  → seccomp basics with libseccomp
  → Linux namespaces: unshare()
  → prctl() and capabilities

Week 5:
  → epoll event loop
  → io_uring basics
  → daemonizing a process

Week 6:
  → Combine everything into Phase 1 (ring buffer)
  → Add Phase 2 (service manager)
  → Test

Week 7-8:
  → Security layer (Phase 3)
  → HAL layer (Phase 4)
  → First real service

Week 9-10:
  → io_uring event loop (Phase 5)
  → Client proxy (Phase 6)
  → Full integration testing
```

---

*Documentation Version 1.0 — Secure Linux Middleware*
*Built with: C11, C++17, Linux Kernel 5.10+*
*Dependencies: libseccomp, liburing, openssl, libc, libpthread*
