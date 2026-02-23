Service Manager — Complete Technical Documentation
Zero se Professional Tak — Har Cheez Detail Mein

TABLE OF CONTENTS
#
Chapter
Module
1
Introduction
Service Manager Kya Hai
2
Overall Architecture
Poori Picture
3
Core Concepts
Bunyadi Samajh
4
Module 01
sm_protocol — Wire Protocol
5
Module 02
sm_socket — Unix Domain Socket
6
Module 03
sm_registry — Service Registry
7
Module 04
sm_handlers — Message Dispatch
8
Module 05
sm_threadpool — Thread Pool
9
Module 06
sm_connection_pool — Connection Pooling
10
Module 07
sm_request_id — Distributed Tracing
11
Module 08
sm_crypto — HMAC-SHA256 Cryptography
12
Module 09
sm_rate_limit — Token Bucket Rate Limiting
13
Module 10
sm_advanced_ratelimit — Advanced Rate Limiting
14
Module 11
sm_security — Privilege Drop + Seccomp
15
Module 12
sm_logging — Thread-Safe Logging
16
Module 13
sm_audit — Audit Trail
17
Module 14
sm_metrics — Performance Metrics
18
Module 15
sm_structured_log — JSON Logging
19
Module 16
sm_health — Health Monitor
20
Module 17
sm_health_callbacks — Custom Health Checks
21
Module 18
sm_watchdog — Hardware Watchdog
22
Module 19
sm_tls — TLS Transport
23
Module 20
sm_discovery — Service Discovery
24
Module 21
sm_eventbus — Event Bus
25
Module 22
sm_monitoring — Resource Monitoring
26
Module 23
sm_plugin — Plugin System
27
Module 24
sm_rolling_restart — Zero-Downtime Restart
28
Module 25
sm_container — Container Support
29
Module 26
sm_cli — Command Line Interface
30
Module 27
sm_main_integration — Integration Point
31
Data Flow
Complete Request Journey
32
Security Model
End to End
33
File Structure
Directory Layout

CHAPTER 1: INTRODUCTION
Service Manager Kya Hai
Service Manager ek system-level daemon hai jo operating system mein chalne wali services ko manage karta hai. Socho isko ek air traffic controller ki tarah — har service (airplane) ko land karne, takne se bachane, aur safely operate karne mein madad karta hai.
Ubuntu mein systemd ye kaam karta hai. Lekin tumhara Service Manager us se alag hai — ye security-first design ke saath banaya gaya hai.

Real World Example
Ek hospital ka system socho:

  Doctor Service   --\
  Patient Service  ---\
  Lab Service      ----->  SERVICE MANAGER  --->  Sab coordinate karta hai
  Pharmacy Service ---/
  Billing Service  --/

Agar Lab Service crash ho jaye:
  - Service Manager detect karta hai (heartbeat timeout)
  - Restart karta hai automatically
  - Doctor Service ko bata deta hai ke Lab abhi available nahi
  - Audit log mein record karta hai
  - Rate limit karta hai takay koi bhi service spam na kare

Tumhara Service Manager vs Ubuntu ka systemd
Ubuntu systemd:
+------------------+
|   Application    |
+------------------+
        |
        | (direct, no authentication)
        v
+------------------+
|     systemd      |  <-- Basic process management
+------------------+
        |
        v
+------------------+
|   Linux Kernel   |
+------------------+
Tumhara Service Manager:
+------------------+
|   Application    |
+------------------+
        |
        | (HMAC authenticated, rate limited)
        v
+------------------+
|   Rate Limiter   |  <-- Tumhara addition
+------------------+
        |
        v
+------------------+
|   Message Auth   |  <-- HMAC-SHA256 verify
+------------------+
        |
        v
+------------------+
| Protocol Validate|  <-- Magic, version, timestamp
+------------------+
        |
        v
+------------------+
|    Registry      |  <-- Thread-safe, hash table
+------------------+
        |
        v
+------------------+
|  Health Monitor  |  <-- Heartbeat tracking
+------------------+
        |
        v
+------------------+
|  Seccomp Filter  |  <-- Syscall whitelist
+------------------+
        |
        v
+------------------+
|   Linux Kernel   |
+------------------+

CHAPTER 2: OVERALL ARCHITECTURE
Poora System Ek Saath
+=========================================+
|         SERVICE MANAGER DAEMON          |
+=========================================+
                    |
    ________________|________________
   |                |                |
+------------------+  +------------------+  +------------------+
|  INFRASTRUCTURE  |  |  OBSERVABILITY   |  |    SECURITY      |
+------------------+  +------------------+  +------------------+
| sm_socket        |  | sm_logging       |  | sm_crypto        |
| sm_registry      |  | sm_audit         |  | sm_rate_limit    |
| sm_handlers      |  | sm_metrics       |  | sm_advanced_rl   |
| sm_protocol      |  | sm_health        |  | sm_security      |
| sm_threadpool    |  | sm_monitoring    |  +------------------+
| sm_conn_pool     |  | sm_structured_log|
| sm_request_id    |  | sm_health_callbacks
| sm_tls           |  +------------------+
+------------------+
                    |
+------------------+
|    ENTERPRISE    |
+------------------+
| sm_discovery     |
| sm_eventbus      |
| sm_watchdog      |
| sm_plugin        |
| sm_rolling_restart|
| sm_container     |
| sm_cli           |
| sm_main_integration|
+------------------+

Request Ka Safar — Client Service Se Response Tak
CLIENT SERVICE
      |
      |  "Mujhe register karna hai"
      v
[1]  UNIX SOCKET (sm_socket.c)
      |  Connection accept karta hai
      v
[2]  THREAD POOL (sm_threadpool.c)
      |  Worker thread assign karta hai
      v
[3]  RATE LIMITER (sm_rate_limit.c)
      |  "Is PID ne bhari requests ki hain?"
      |  Token bucket check
      v
[4]  HEADER RECEIVE (sm_handlers.c)
      |  56 bytes header read karta hai
      v
[5]  PROTOCOL VALIDATE (sm_protocol.c)
      |  Magic number check: 0x534D4B47
      |  Version check: 2
      |  Timestamp check: replay attack prevention
      v
[6]  HMAC VERIFY (sm_crypto.c)
      |  HMAC-SHA256 verify karta hai
      |  Tampered message? REJECT
      v
[7]  MESSAGE DISPATCH (sm_handlers.c)
      |  Register? Lookup? Heartbeat? Unregister?
      v
[8]  REGISTRY OPERATION (sm_registry.c)
      |  Thread-safe hash table mein store
      v
[9]  AUDIT LOG (sm_audit.c)
      |  Har operation record hoti hai
      v
[10] RESPONSE SEND
      |  Client ko result bhejta hai
      v
CLIENT SERVICE
"Successfully registered!"

CHAPTER 3: CORE CONCEPTS — BUNYADI SAMAJH
Ye concepts samajhna zaroori hai takay baaki sab samajh aaye.

Concept 1: Unix Domain Socket
Kya Hota Hai
Socket ek communication channel hai. Jaise telephone line — do programs baat kar sakti hain. Unix Domain Socket specifically local machine ke programs ke beech communication ke liye hai. Network socket nahi hai — ye file system pe based hai.
Normal Network Socket:
  Program A ---[TCP/IP Network]---> Program B
              (IP address chahiye)
              (port number chahiye)

Unix Domain Socket:
  Program A ---[/run/servicemanager.sock]---> Program B
              (sirf file path chahiye)
              (bahut fast, same machine)
              (SO_PEERCRED se peer ID verify ho sakti hai)
Kyun Use Kiya
Feature
Faida
Speed
Network overhead nahi, directly kernel through
Security
SO_PEERCRED se peer ka UID/GID/PID verify hota hai
Simplicity
File system permissions se control

Concept 2: Thread Pool
Masla — Bina Thread Pool Ke
BINA THREAD POOL:
  Client 1   aaya -> Thread banao -> Kaam karo -> Thread destroy karo
  Client 2   aaya -> Thread banao -> Kaam karo -> Thread destroy karo
  Client 3   aaya -> Thread banao -> Kaam karo -> Thread destroy karo
  ...
  Client 1000 aaya -> Thread banao -> SYSTEM CRASH (too many threads)

  Thread banana = expensive operation (memory + time)
Thread Pool Ka Solution
THREAD POOL KE SAATH:
  Startup par: 8 threads banao aur wait karwao

  Client 1    aaya -> Thread 1 le lo -> Kaam karo -> Thread 1 wapas pool mein
  Client 2    aaya -> Thread 2 le lo -> Kaam karo -> Thread 2 wapas pool mein
  Client 1000 aaya -> Sab threads busy? Queue mein dalo
                   -> Queue bhari? DROP (graceful degradation)

  Threads kabhi 8 se zyada nahi! System stable rehta hai.
Queue Kya Hai
QUEUE (FIFO - First In First Out):

  Front                                    Back
  [Task1] [Task2] [Task3] [Task4] [Task5]
     ^-- Thread le jata hai        ^-- Naya task yahan aata hai

  - Jab thread free hota hai: Front se task leta hai
  - Naya task: Back mein jata hai
  - "Bounded queue" hai — maximum 128 tasks

Concept 3: Hash Table
Bina Hash Table Ke Service Dhundhna
Registry mein 32 services hain.
Mujhe "database_service" dhundna hai.

NAIVE APPROACH (Linear Search):
  Check service[0]  -> "audio_service"   -> nahi
  Check service[1]  -> "camera_service"  -> nahi
  Check service[2]  -> "network_service" -> nahi
  ...
  Check service[15] -> "database_service" -> MILA!

  Average case: 16 comparisons
  Worst case:   32 comparisons
Hash Table Se
HASH TABLE APPROACH:
  "database_service" ko hash function se pass karo:
    hash("database_service") = 42
  registry[42] -> "database_service" -> MILA!

  1 comparison!
  O(1) time complexity
  32 services hon ya 3200 — same speed!
Hash Function — DJB2
"database" word ko hash karna:

  h = 5381  (starting value)
  For 'd':  h = ((5381 << 5) + 5381) ^ 100 = 177,641
  For 'a':  h = ((177641 << 5) + 177641) ^ 97 = ...
  ...
  Final result: ek number jo 0-63 ke beech ho (HASH_SIZE = 64)

  Same input  -> HAMESHA same output
  Diff inputs -> (usually) different outputs

Concept 4: Token Bucket Rate Limiting
Masla — Koi Service Spam Kare
BINA RATE LIMITING:
  Malicious service -> 10,000 requests/second -> Service Manager -> OVERLOAD -> Crash

  YA Fork bomb:
    PID 100   -> 100 requests
    PID 101   -> 100 requests
    ...
    PID 10000 -> 100 requests
    TOTAL: 1,000,000 requests -> CRASH
Token Bucket Algorithm
Socho ek bucket hai jismein tokens hain:

  BUCKET: [T][T][T][T][T][T][T][T][T][T]  <- 10 tokens (full)

  Har request ek token consume karti hai:
    Request 1:  [T][T][T][T][T][T][T][T][T][ ]  <- 9 tokens
    Request 2:  [T][T][T][T][T][T][T][T][ ][ ]  <- 8 tokens
    ...
    Request 10: [ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]  <- 0 tokens
    11th request: BUCKET EMPTY -> REJECT

  REFILL: Har second 10 tokens wapas aate hain

  Is tarah:
    - Short burst allow:    10 rapid requests
    - Sustained rate max:   10 req/second
    - Smooth, no sudden cutoff

Concept 5: HMAC-SHA256
Masla — Message Tamper Ho Sakta Hai
BINA AUTHENTICATION:
  Service A -> "Register: database_service, port 5432" -> Service Manager
                         |
               Hacker beech mein: message change karta hai
                         |
  Service Manager <- "Register: database_service, port 6666 (hacker's port)"

  Service Manager ko pata hi nahi ke message change hua!
HMAC Solution
HMAC = Hash-based Message Authentication Code

Process:
  1. Service A aur Service Manager ke paas SHARED SECRET KEY hai
  2. Message bhejne se pehle:
       HMAC = SHA256(key + message)
       Message + HMAC bhejo
  3. Service Manager receive kare:
       Expected_HMAC = SHA256(key + received_message)
       if (received_HMAC == expected_HMAC): message genuine hai
       else: message tampered hai -> REJECT

  Hacker ke paas key nahi hai, isliye valid HMAC banana mushkil hai.
  SHA256 cryptographically secure hai.
Timing Attack aur Constant-Time Compare
VULNERABLE COMPARISON:
  bool compare(hmac1, hmac2):
    for i in range(32):
      if hmac1[i] != hmac2[i]:
        return FALSE  // <-- YE PROBLEM HAI

  Attacker observe karta hai:
    - 1 byte match  -> return 1 microsecond baad
    - 5 bytes match -> return 5 microseconds baad
    - Timing se andaza: kitne bytes sahi hain!

CONSTANT-TIME COMPARISON (tumhara code):
  uint8_t diff = 0;
  for i in range(32):
    diff |= hmac1[i] ^ hmac2[i];  // HAMESHA 32 iterations
  return diff == 0;

  Timing se kuch pata nahi chalta! Secure hai.

Concept 6: Seccomp — Syscall Firewall
Syscall Kya Hai
Program hardware ya OS services access karna chahta hai?
Directly nahi kar sakta — Kernel se kehna padta hai.
Ye "System Call" hai.

Example — File open karna:
  Program: "open('/etc/passwd', O_RDONLY)"
                    |
              (syscall: SYS_open)
                    v
  Kernel: File system access karta hai, fd return karta hai
Seccomp — Syscall Whitelist
BINA SECCOMP:
  Compromised service -> koi bhi syscall -> Kernel
    -> execve('/bin/bash')   -> Shell!
    -> ptrace(other_process) -> Spy!
    -> mount('/dev/sda', '/') -> Filesystem access!

SECCOMP KE SAATH:
  +------------------------+
  |  Allowed Syscalls Only:|
  |  read, write, close    |
  |  socket, bind, listen  |
  |  futex, mmap, brk      |
  |  ... (specific list)   |
  +------------------------+
           |
  Compromised service -> execve('/bin/bash')
    -> KILL PROCESS IMMEDIATELY
    -> No second chance

CHAPTER 4: MODULE 01 — sm_protocol
Files: sm_protocol.h + sm_protocol.c
Maqsad
Protocol define karta hai ke Service Manager aur uske clients ke beech kaise baat hogi. Ye ek "language" hai jo dono parties samjhti hain.

Wire Protocol — Har Message Ka Structure
EVERY MESSAGE = HEADER + PAYLOAD

HEADER (56 bytes, fixed):
+----------+----------+----------+----------+
|  magic   | version  |   type   |  length  |
|  4 bytes |  2 bytes |  2 bytes |  4 bytes |
+----------+----------+----------+----------+
| timestamp|client_pid|  nonce   |          |
|  4 bytes |  4 bytes |  4 bytes |          |
+----------+----------+----------+----------+
|                                           |
|         HMAC-SHA256 (32 bytes)            |
|                                           |
+-------------------------------------------+

Total Header = 4+2+2+4+4+4+4+32 = 56 bytes

PAYLOAD (variable, max 1024 bytes):
  Register request: service_name + socket_path + ring_name
  Lookup request:   service_name
  Heartbeat:        service_name

Har Field Ki Detail
Field
Size
Value
Purpose
magic
4 bytes
0x534D4B47 = "SMKG"
"Ye message SM ke liye hai"
version
2 bytes
2
Protocol version check
type
2 bytes
1/2/3/4                               
REGISTER/LOOKUP/HEARTBEAT/UNREGISTER
length
4 bytes
0–1024
Payload size
timestamp
4 bytes
Unix seconds
Replay attack prevention
client_pid
4 bytes
Informational
SO_PEERCRED se override hota hai
nonce
4 bytes
Random
Double replay protection
hmac[32]
32 bytes
HMAC-SHA256
Message authentication
Message Types:
    • 1 = SM_MSG_REGISTER — service register karna chahta hai 
    • 2 = SM_MSG_LOOKUP — service dhundhna chahta hai 
    • 3 = SM_MSG_HEARTBEAT — "Main zinda hun" message 
    • 4 = SM_MSG_UNREGISTER — service band ho rahi hai 

Protocol Validation Code
int sm_validate_header(const sm_hdr_t* hdr, size_t received_size) {
    // Step 1: NULL check
    if (!hdr) return SM_ERR_INVALID;

    // Step 2: Magic number check — 0x534D4B47 = "SMKG"
    if (hdr->magic != SM_PROTOCOL_MAGIC) {
        return SM_ERR_PROTOCOL;
    }

    // Step 3: Version check — sirf version 2 accept karo
    if (hdr->version != SM_PROTOCOL_VERSION) {
        return SM_ERR_PROTOCOL;
    }

    // Step 4: Message type range check (1 se 4)
    if (hdr->type < SM_MSG_REGISTER || hdr->type > SM_MSG_UNREGISTER) {
        return SM_ERR_INVALID;
    }

    // Step 5: Payload length check
    if (hdr->length == 0 || hdr->length > SM_MAX_PAYLOAD_SIZE) {
        return SM_ERR_INVALID;
    }

    // Step 6: Size consistency check
    if (received_size != sizeof(sm_hdr_t) + (size_t)hdr->length) {
        return SM_ERR_PROTOCOL;
    }

    // Step 7: Timestamp window check
    time_t now = time(NULL);
    uint32_t now32 = (uint32_t)now;

    if (hdr->timestamp > now32 + SM_TIMESTAMP_MAX_SKEW) {
        return SM_ERR_INVALID;  // Future message
    }
    if ((time_t)hdr->timestamp < now - SM_TIMESTAMP_MAX_AGE) {
        return SM_ERR_INVALID;  // Stale/replay
    }

    return SM_OK;
}

Path Validation — Directory Whitelist
int sm_validate_socket_path(const char* path) {
    // Absolute path hona chahiye
    if (path[0] != '/') return SM_ERR_INVALID;

    // Directory traversal attack prevent karo
    if (strstr(path, "..") || strstr(path, "//")) {
        return SM_ERR_INVALID;
    }

    // WHITELIST: Sirf /run/ ya /tmp/ se start hone wale paths
    // /etc/, /home/, /usr/ etc. — BLOCKED
    int allowed = (
        strncmp(path, SM_ALLOWED_PATH_1, strlen(SM_ALLOWED_PATH_1)) == 0 ||
        strncmp(path, SM_ALLOWED_PATH_2, strlen(SM_ALLOWED_PATH_2)) == 0
    );

    if (!allowed) return SM_ERR_INVALID;
    return SM_OK;
}

Protocol Flow Diagram
CLIENT                              SERVER
  |                                   |
  | 1. Build Header:                  |
  |    magic     = 0x534D4B47         |
  |    version   = 2                  |
  |    type      = REGISTER           |
  |    length    = sizeof(payload)    |
  |    timestamp = time(NULL)         |
  |    nonce     = random()           |
  |                                   |
  | 2. Compute HMAC:                  |
  |    hmac = HMAC-SHA256(key, hdr+payload)
  |                                   |
  | 3. Send: [header][payload]        |
  |---------------------------------->|
  |                                   | 4. Receive header (56 bytes)
  |                                   | 5. Validate magic, version, type
  |                                   | 6. Validate length (BEFORE payload!)
  |                                   | 7. Receive payload (length bytes)
  |                                   | 8. Verify HMAC
  |                                   | 9. Process request
  |                                   |
  | [response: SM_OK / error]         |
  |<----------------------------------|10. Send response
  |                                   |

CHAPTER 5: MODULE 02 — sm_socket
Files: sm_socket.h + sm_socket.c
Maqsad
Server socket banata hai jis par Service Manager clients ke connections accept karta hai. Ye "dukaan ka darwaza" hai — sab kuch yahan se shuru hota hai.

Socket Setup Process
sm_socket_setup() function kya karta hai:

Step 1: Purana socket file remove karo
          unlink("/run/servicemanager.sock")

Step 2: Socket create karo
          socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)
          - AF_UNIX      = Unix domain socket (local machine only)
          - SOCK_STREAM  = TCP-like, reliable, ordered
          - SOCK_CLOEXEC = exec() ke baad automatically close ho

Step 3: Non-blocking set karo
          fcntl(server_fd, F_SETFL, flags | O_NONBLOCK)
          - Kyun: epoll_wait() sirf blocking point hona chahiye

Step 4: Bind karo
          bind(server_fd, &addr, sizeof(addr))
          - Socket file banata hai: /run/servicemanager.sock

Step 5: Permissions set karo (PEHLE listen se)
          chmod("/run/servicemanager.sock", 0660)
          - CRITICAL ORDER: chmod PEHLE listen se karna zaroori hai
          - chmod FAIL? -> Fatal error, abort

Step 6: Listen
          listen(server_fd, SM_BACKLOG)
          - SM_BACKLOG = 32 (kernel queue depth)
          - 33rd connection? System reject kar deta hai

Fallback Mechanism
// Socket path try karne ka order:
const char* socket_paths[] = {
    "/run/servicemanager.sock",   // Primary: tmpfs, fast
    "/tmp/servicemanager.sock",   // Fallback: development/testing
    NULL
};
Path
Use Case
/run/
Production — tmpfs (RAM-based), fast, root required
/tmp/
Development/Testing — always writable

Socket Permissions Ka Matlab
chmod 0660:

  0    6    6    0
  |    |    |    |
  |    |    |    +-- Others: no access      (000)
  |    |    +------- Group:  read + write   (110 = 6)
  |    +------------ Owner:  read + write   (110 = 6)
  +----------------- Special bits: none     (0)

Security implication:
  - servicemanager user:  connect kar sakta hai
  - servicemanager group: connect kar sakte hain
  - Baaki sab:            NAHI kar sakte

Permission Validation
int sm_socket_validate_perms(void) {
    struct stat st;
    stat(g_socket_path, &st);

    // Runtime check: permissions galat toh hua?
    if ((st.st_mode & 0777) != SM_SOCKET_MODE) {
        // Automatically correct karo
        chmod(g_socket_path, SM_SOCKET_MODE);
    }
}
// Agar koi attacker permissions change kare toh auto-fix!

CHAPTER 6: MODULE 03 — sm_registry
Files: sm_registry.h + sm_registry.c
Maqsad
Registry central database hai jahan sab registered services ka record rakha jata hai. Ye thread-safe hai — multiple threads ek saath safely access kar sakti hain.

Service Entry Structure
typedef struct {
    char             name[64];         // Service ka naam: "database_service"
    char             socket_path[256]; // Kahan baat karo: "/run/db.sock"
    char             ring_name[256];   // Ring buffer ka naam
    pid_t            pid;              // Process ID (kernel verified)
    uid_t            uid;              // User ID (kernel verified)
    gid_t            gid;              // Group ID (kernel verified)
    service_status_t status;           // RUNNING/STOPPED/CRASHED/DEAD
    time_t           last_heartbeat;   // Last "main zinda hun" message
    time_t           registered_at;    // Kab register hua
    int              restart_count;    // Kitni baar restart hua
    time_t           last_crash_time;  // Last crash kab hua
} service_entry_t;

Registry Internal Structure
REGISTRY = ARRAY + HASH TABLE (dual structure)

ARRAY (direct storage):
  Index 0: {name="db_service",    pid=1234, status=RUNNING, ...}
  Index 1: {name="audio_service", pid=2345, status=RUNNING, ...}
  Index 2: {name="cam_service",   pid=3456, status=RUNNING, ...}

HASH TABLE (fast lookup):
  Slot 0:  NULL
  Slot 1:  NULL
  Slot 5:  -> array[1]  (audio_service)
  Slot 17: -> array[0]  (db_service)
  Slot 42: -> array[2]  (cam_service)

Lookup "db_service":
  1. hash("db_service") = 17
  2. hash_table[17] -> array[0]
  3. array[0].name == "db_service" -> FOUND!
  Total: O(1) time

Thread Safety — Readers-Writer Lock
PROBLEM:
  Thread A: "database_service ka status kya hai?" (READ)
  Thread B: "audio_service crash ho gai"          (WRITE)
  Thread C: "camera register karna hai"            (WRITE)
  Agar sab ek saath chalaen: DATA CORRUPTION!

SOLUTION: pthread_rwlock_t

  READ LOCK (rdlock):
    - Multiple readers ek saath allowed
    - Thread A aur Thread D dono simultaneously read kar sakte hain

  WRITE LOCK (wrlock):
    - Sirf ek writer at a time
    - Jab Thread B likh raha hai: A aur C wait karein

  Timeline:
  Time --->
  Thread A: [READ LOCK]  --[reading]--  [UNLOCK]
  Thread D: [READ LOCK]  --[reading]--  [UNLOCK]  <- saath chal sakta hai
  Thread B: [wait.......]  [WRITE LOCK] [writing] [UNLOCK]
  Thread C: [wait.............]         [WRITE LOCK] [writing] [UNLOCK]

TOCTOU Bug — Kya Tha Aur Kaise Fix Hua
TOCTOU = Time Of Check To Time Of Use
PURANA (BUGGY) CODE:
  entry = sm_registry_find("db_service");  // rdlock, phir RELEASE
  if (entry->pid != peer_pid) return ERROR;
  // *** YE WINDOW: Thread B ab array shift kar sakta hai! ***
  sm_registry_remove("db_service");        // wrlock
  // entry pointer ab INVALID hai! Array shift ho gaya!

NAYA (FIXED) CODE — ek hi wrlock mein check + remove:
int sm_registry_remove_if_owner(const char* name, pid_t owner_pid) {
    pthread_rwlock_wrlock(&registry_lock);  // Lock lo

    idx = hash_find(name);                  // Dhundho
    if (registry[idx].pid != owner_pid) {   // Check karo
        pthread_rwlock_unlock(&registry_lock);
        return SM_ERR_PERMISSION;
    }

    // Remove karo (under same lock)
    hash_remove(name);
    for (int i = idx; i < registry_count - 1; i++) {
        registry[i] = registry[i + 1];     // Array compact karo
    }
    registry_count--;
    hash_rebuild();                         // Hash table rebuild karo

    pthread_rwlock_unlock(&registry_lock);  // Ab unlock
    return SM_OK;
    // Koi window nahi! Check aur remove ek hi lock mein!
}

Hash Rebuild Kyun Zaroori Hai
ARRAY SHIFT KA MASLA:

Before remove (index 1 = "audio"):
  Array: [db_service(0)]  [audio_service(1)]  [cam_service(2)]
  Hash:  slot[5]  -> array[1]  (audio)
         slot[17] -> array[0]  (db)
         slot[42] -> array[2]  (cam)

Remove array[1] (audio), shift left:
  Array: [db_service(0)]  [cam_service(1)]   <- cam moved from 2 to 1!
  Hash:  slot[5]  -> array[1]  (WRONG! Now points to cam_service!)
         slot[42] -> array[2]  (WRONG! array[2] is now empty!)

SOLUTION — hash_rebuild():
  hash_table = {} (clear)
  for i in range(registry_count):
      hash_insert(registry[i].name, i)  // Fresh indices

Result:
  Hash: slot[17] -> array[0]  (db,  correct)
        slot[42] -> array[1]  (cam, correct new index)

get_all() — Safe Snapshot
// PURANA (DANGEROUS): Pointer return karo
service_entry_t* sm_registry_get_all_old(int* count) {
    *count = registry_count;
    return registry;  // Lock release ke baad pointer invalid ho sakta hai!
}

// NAYA (SAFE): Copy return karo
int sm_registry_get_all(service_entry_t** out, int* count) {
    pthread_rwlock_rdlock(&registry_lock);

    size_t size = (size_t)registry_count * sizeof(service_entry_t);
    service_entry_t* copy = malloc(size);
    memcpy(copy, registry, size);          // Copy while lock held hai
    *out   = copy;
    *count = registry_count;

    pthread_rwlock_unlock(&registry_lock);
    // Caller ko heap-allocated copy mili hai — koi bhi change safe nahi karegi
    return SM_OK;
}
// Caller baad mein free karta hai: sm_registry_free_copy(services);

CHAPTER 7: MODULE 04 — sm_handlers
Files: sm_handlers.h + sm_handlers.c
Maqsad
Handlers service ka "brain" hain — har incoming request ko process karte hain. Protocol validate karo, authenticate karo, aur sahi operation perform karo.

Main Handler — sm_handle_client() Lifecycle
sm_handle_client(client_fd):

  Step  1: REQUEST ID GENERATE   -> req_id = random 64-bit ID
  Step  2: SOCKET TIMEOUT SET    -> SO_RCVTIMEO = 5s, SO_SNDTIMEO = 5s
  Step  3: PEER PID              -> SO_PEERCRED se (kernel verified)
  Step  4: RATE LIMIT CHECK      -> sm_rate_limit_check(peer_pid)
  Step  5: HEADER RECEIVE        -> exactly 56 bytes (MSG_WAITALL)
  Step  6: LENGTH VALIDATE       -> PAYLOAD RECEIVE SE PEHLE
                                    (hdr.length = 99999 -> stack overflow prevent)
  Step  7: PAYLOAD RECEIVE       -> n = recv(fd, payload, hdr.length, MSG_WAITALL)
  Step  8: STRUCTURAL VALIDATION -> magic, version, type, timestamp
  Step  9: MESSAGE SIZE VALIDATE -> exact payload size match
  Step 10: HMAC VERIFY           -> message genuine hai?
  Step 11: DISPATCH:
              REGISTER?   -> sm_handle_register()
              LOOKUP?     -> sm_handle_lookup()
              HEARTBEAT?  -> sm_handle_heartbeat()
              UNREGISTER? -> sm_handle_unregister()

Slow Loris Attack — Kya Hai aur Fix
SLOW LORIS ATTACK:
  Attacker: connection open karta hai
  Attacker: header ke 10 bytes bhejta hai... phir rukta hai
  Attacker: 1 byte aur... phir rukta hai
  Attacker: 1 byte aur... phir rukta hai

  Bina timeout ke:
    Server: thread block hai, usi connection pe wait kar raha hai
  Result:
    Server ke sab threads block ho jayen -> New connections reject

SOLUTION:
  SO_RCVTIMEO = 5 seconds
  5 seconds mein pura header nahi aaya? Connection close!
  Thread free ho jata hai.

Register Handler
int sm_handle_register(int fd, const sm_hdr_t* hdr, const sm_register_req_t* req) {
    // 1. PEER CREDENTIALS (kernel verified, tamper-proof)
    uid_t peer_uid = sm_get_peer_uid(fd);
    gid_t peer_gid = sm_get_peer_gid(fd);
    pid_t peer_pid = sm_get_peer_pid(fd);

    // 2. RATE LIMIT (per service)
    if (sm_ratelimit_check_extended(peer_pid, req->service_name, SM_MSG_REGISTER) < 0) {
        sm_audit_log(AUDIT_REGISTER, req->service_name, 0, peer_pid,
                     peer_uid, SM_ERR_RATELIMIT, "rate_limit_exceeded");
        return send_reply(fd, SM_ERR_RATELIMIT);
    }

    // 3. SERVICE NAME VALIDATE
    if (sm_validate_service_name(req->service_name) != SM_OK)
        return send_reply(fd, SM_ERR_INVALID);

    // 4. SOCKET PATH VALIDATE (whitelist check)
    if (sm_validate_socket_path(req->socket_path) != SM_OK)
        return send_reply(fd, SM_ERR_INVALID);

    // 5. ENTRY BUILD
    service_entry_t entry = {0};
    strncpy(entry.name,        req->service_name, SM_MAX_NAME - 1);
    strncpy(entry.socket_path, req->socket_path,  SM_MAX_PATH - 1);

    // CRITICAL: Client-supplied PID USE NAHI KARTE — hdr->client_pid IGNORE
    entry.pid            = peer_pid;   // SO_PEERCRED se (kernel verified)
    entry.uid            = peer_uid;
    entry.gid            = peer_gid;
    entry.status         = SERVICE_RUNNING;
    entry.last_heartbeat = time(NULL);

    // 6. REGISTRY MEIN ADD KARO
    return send_reply(fd, sm_registry_add(&entry));
}

Lookup Handler — TOCTOU Safe
int sm_handle_lookup(int fd, const sm_hdr_t* hdr, const sm_lookup_req_t* req) {
    service_entry_t entry_copy = {0};

    // find_copy() use karo — copy while lock held
    // find() use karna WRONG hoga (dangling pointer risk)
    int rc = sm_registry_find_copy(req->service_name, &entry_copy);
    if (rc != SM_OK)
        return send_reply(fd, SM_ERR_NOT_FOUND);

    if (entry_copy.status != SERVICE_RUNNING)
        return send_reply(fd, SM_ERR_NOT_FOUND);

    // Stack copy use karo — thread-safe!
    return send_reply_lookup(fd, &entry_copy);
}

CHAPTER 8: MODULE 05 — sm_threadpool
Files: sm_threadpool.h + sm_threadpool.c
Maqsad
Worker threads ka pool maintain karta hai. Har incoming client connection ek worker thread ko assign hoti hai. Thread explosion se bachata hai.

Internal Data Structure
typedef struct {
    int               client_fd;  // Client ka file descriptor
    sm_task_handler_t handler;    // Function jo call hoga
    void*             context;    // Extra data
} task_t;

// CIRCULAR QUEUE (Ring Buffer):
task_t queue[128];     // Fixed size array
int    queue_front;    // Pehla item yahan hai
int    queue_back;     // Naya item yahan aata hai
int    queue_used;     // Kitne items hain

// back = (front + used) % max_size

Circular Queue Diagram
Initial state (empty):
  queue: [ ][ ][ ][ ][ ][ ][ ][ ]
          0  1  2  3  4  5  6  7
  front=0, back=0, used=0

After 3 tasks added:
  queue: [T1][T2][T3][ ][ ][ ][ ][ ]
  front=0, back=3, used=3

Thread takes T1:
  queue: [ ][T2][T3][ ][ ][ ][ ][ ]
  front=1, back=3, used=2

Add 6 more (wrapping around):
  queue: [T8][T2][T3][T4][T5][T6][T7][ ]
  front=1, back=0 (wrapped!), used=7

Add 1 more:
  queue: [T8][T2][T3][T4][T5][T6][T7][T9]
  front=1, back=1, used=8  (FULL!)

Worker Thread Logic
static void* worker_thread(void* arg) {
    while (1) {
        pthread_mutex_lock(&g_pool.queue_mutex);

        // Queue empty hai aur shutdown nahi: WAIT
        while (g_pool.queue_used == 0 && !g_pool.shutdown_requested) {
            pthread_cond_wait(&g_pool.queue_cond, &g_pool.queue_mutex);
            // pthread_cond_wait:
            //   1. mutex release karta hai
            //   2. Condition signal ka wait karta hai
            //   3. Signal aane par mutex wapas le leta hai
        }

        // Shutdown? Exit karo
        if (g_pool.shutdown_requested && g_pool.queue_used == 0) {
            pthread_mutex_unlock(&g_pool.queue_mutex);
            break;
        }

        // Queue se task lo (FIFO)
        task_t task = g_pool.queue[g_pool.queue_front];
        g_pool.queue_front = (g_pool.queue_front + 1) % g_pool.queue_max_size;
        g_pool.queue_used--;
        g_pool.stats_tasks_processed++;
        pthread_mutex_unlock(&g_pool.queue_mutex);

        // LOCK KE BAAHAR kaam karo (performance)
        if (task.handler && task.client_fd >= 0) {
            task.handler(task.client_fd, task.context);
        }
        if (task.client_fd >= 0) close(task.client_fd);
    }
    return NULL;
}

Submit Function
int sm_threadpool_submit(int client_fd, sm_task_handler_t handler, void* context) {
    pthread_mutex_lock(&g_pool.queue_mutex);

    // Queue full? DROP (graceful degradation)
    if (g_pool.queue_used >= g_pool.queue_max_size) {
        g_pool.stats_tasks_dropped++;
        pthread_mutex_unlock(&g_pool.queue_mutex);
        return -1;
    }

    int back_index = (g_pool.queue_front + g_pool.queue_used) % g_pool.queue_max_size;
    g_pool.queue[back_index].client_fd = client_fd;
    g_pool.queue[back_index].handler   = handler;
    g_pool.queue[back_index].context   = context;
    g_pool.queue_used++;

    pthread_cond_signal(&g_pool.queue_cond);  // Ek sleeping worker ko jagao
    pthread_mutex_unlock(&g_pool.queue_mutex);
    return 0;
}

Graceful vs Force Shutdown

Graceful
Force
Kya hota hai
Current tasks complete honti hain
Sab tasks discard!
Use case
Normal shutdown, SIGTERM
Emergency, fatal error
Steps
shutdown_requested=true → broadcast → join
shutdown_requested=true → queue_used=0 → broadcast → join
Warning
Safe
Client connections brutally closed!

CHAPTER 9: MODULE 06 — sm_connection_pool
Files: sm_connection_pool.h + sm_connection_pool.c
Maqsad
Client-side connection reuse karta hai. Service Manager se baar baar connect/disconnect karne ki bajaye existing connections reuse karta hai.

Pool ka Faida
BINA POOL:
  Request 1: connect() -> send/recv -> close()  <- 3 ops + overhead
  Request 2: connect() -> send/recv -> close()  <- phir 3 ops
  Request 3: connect() -> send/recv -> close()  <- phir phir 3 ops

POOL KE SAATH:
  Startup:   Pool empty hai []
  Request 1: Pool empty -> create_connection() -> use -> put back [conn1]
  Request 2: Pool has conn1 -> verify alive -> use -> put back [conn1]
  Request 3: Pool has conn1 -> verify alive -> use -> put back [conn1]

  connect() sirf 1 baar! Baaki baar reuse.

Connection Liveness Check
static int verify_connection_alive(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // Peek: data consume kiye bina check karo
    char test_byte;
    ssize_t ret = recv(fd, &test_byte, 1, MSG_PEEK | MSG_DONTWAIT);

    fcntl(fd, F_SETFL, flags);  // Restore blocking mode

    // EAGAIN/EWOULDBLOCK = "No data but connection alive"
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 1;  // ALIVE
    }
    // 0 = closed by peer, other error = broken
    return 0;  // DEAD
}

Pool Operations
sm_connpool_get():
  1. Lock lo
  2. Pool mein koi connection hai?
       a. Alive check karo
       b. Alive? Return karo
       c. Dead? Close karo, agla dhundho
  3. Pool empty? in_use++ karo
  4. Unlock
  5. create_connection() banao (lock ke baahar)
  6. Connection fail? in_use-- wapas karo

sm_connpool_put(fd):
  1. Lock lo
  2. Pool mein space hai?
       a. Haan: fd store karo, available++, in_use--
       b. Nahi: in_use-- karo
  3. Unlock
  4. Pool full tha? close(fd) lock ke baahar

CHAPTER 10: MODULE 07 — sm_request_id
Files: sm_request_id.h + sm_request_id.c
Maqsad
Har request ko unique ID deta hai. Distributed systems mein ek request kaafi jagah log hoti hai — Request ID se sab logs ek saath trace kar sakte hain.

Request ID Structure
64-bit Request ID:
+------------------------+------------------------+
|      High 32 bits      |      Low 32 bits       |
|    (Process base)      |       (Counter)        |
+------------------------+------------------------+

  Process Base: /dev/urandom se random number (startup par)
  Counter:      Atomic counter, har request mein +1

Example:
  Base      = 0xABCD1234
  Counter   = 42
  RequestID = 0xABCD12340000002A

Kya faida:
  - Same process: same high bits (identify karo)
  - Unique per request: low bits different
  - Cross-process: base alag, easy to distinguish

Thread-Local Storage
// _Thread_local: Har thread ka apna alag value
static _Thread_local request_id_t g_thread_request_id = 0;

// Thread A: request_id = 0xABCD000000000001
// Thread B: request_id = 0xABCD000000000002
// Thread C: request_id = 0xABCD000000000003
// Ye sab alag variables hain — ek thread ka change doosre par asar nahi!

Atomic Counter
static _Atomic(uint64_t) g_request_id_counter = 0;

uint64_t counter = atomic_fetch_add(&g_request_id_counter, 1);

// "Atomic" matlab:
//   - Multiple threads ek saath increment kar sakti hain
//   - Koi race condition nahi
//   - No mutex needed!
//
// Thread A: reads 5, writes 6
// Thread B: reads 6, writes 7
// Sequential, even without mutex

CHAPTER 11: MODULE 08 — sm_crypto
Files: sm_crypto.h + sm_crypto.c
Maqsad
Message authentication aur key management. SHA-256 aur HMAC pure C mein implement kiye hain — koi OpenSSL dependency nahi.

SHA-256 Kya Hai
SHA-256 = Secure Hash Algorithm 256-bit
Koi bhi input -> Fixed 256-bit (32 byte) output

Properties:
  1. Deterministic:      Same input = same output, hamesha
  2. One-way:            Output se input reverse nahi kar sakte
  3. Avalanche Effect:   1 bit change = completely different output
  4. Collision Resistant: Do alag inputs ka same output milna practically impossible

Example:
  "hello" -> 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
  "hellp" -> 5568efe6ac5d22de4e6e50b8b82b69879d4ef12a6b4e59b8c43e754bf3a30e23
              (completely alag! Sirf 1 character change)

SHA-256 Algorithm Steps
Step 1: PADDING
  - Data mein '1' bit add karo
  - Phir '0' bits add karo
  - End mein data ka length (64-bit) add karo
  - Total must be multiple of 512 bits (64 bytes)

Step 2: INITIAL VALUES
  8 specific 32-bit numbers (mathematically derived from prime square roots):
  H0 = 0x6a09e667
  H1 = 0xbb67ae85
  ... (8 values)

Step 3: COMPRESSION (64 rounds per block)
  - 64 round constants K[0]...K[63] (prime cube roots se)
  - Har round: CH, MAJ, SIGMA0, SIGMA1 bitwise functions
  - State mix hota jata hai

Step 4: FINAL HASH
  H0...H7 concatenate karo = 32 bytes output

HMAC Construction
HMAC = Hash-based Message Authentication Code (RFC 2104)

  ipad = 0x36 (repeated 64 times)
  opad = 0x5C (repeated 64 times)

  Key preparation:
    if key_len > 64: key = SHA256(key)
    else:            key = key padded to 64 bytes

  HMAC = SHA256( (key XOR opad) || SHA256( (key XOR ipad) || message ) )

  Visual flow:
    +--------+
    |  key   |
    +--------+
         |
         v
    XOR with ipad ------> [k XOR ipad] + [message]
                                   |
                                   v
                              SHA256() = inner_hash
                                             |
    XOR with opad ------> [k XOR opad] + inner_hash
                                   |
                                   v
                              SHA256() = FINAL HMAC

Key Management
sm_crypto_init() flow:

  /run/servicemanager.key file hai?
    YES:
      - Open karo, 32 bytes read karo
      - g_key mein store karo
      - g_key_loaded = 1
      - Done!

    NO (ya truncated):
      - /dev/urandom se 32 random bytes generate karo
        (/dev/urandom = kernel ka cryptographically secure RNG)
      - /run/servicemanager.key mein write karo
        (Mode: 0640 — owner+group read, others nothing)
      - Agar /run/ fail ho: /tmp/ fallback
      - g_key_loaded = 1

  WHY FILE PERSIST:
    Service Manager restart ho toh key same rahe.
    Warna: sab running clients ke HMAC invalid ho jayen!

Constant-Time Compare — Timing Attack Prevention
// VULNERABLE (kabhi use mat karo):
int naive_compare(const uint8_t* a, const uint8_t* b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;  // EARLY RETURN! <- Problem
    }
    return 1;
}
// Attacker: wrong byte 1 -> fast return
//           wrong byte 30 -> slow return
//           Timing se andaza: kitne bytes sahi hain!

// SAFE (tumhara code):
static int consttime_memcmp(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];   // XOR: 0 agar same, non-zero agar alag
        // |= (OR): ek baar non-zero ho toh hamesha non-zero
    }
    // HAMESHA 32 iterations — timing same regardless of match
    return (int)diff;  // 0 = equal, non-zero = different
}

CHAPTER 12: MODULE 09 — sm_rate_limit
Files: sm_rate_limit.h + sm_rate_limit.c
Maqsad
Per-PID aur global token bucket rate limiting. Koi bhi service ya malicious code Service Manager ko overwhelm na kar sake.

Token Bucket Parameters
Parameter
Value
Meaning
SM_RATE_PID_CAPACITY
10
Har PID ka bucket max 10 tokens
SM_RATE_PID_REFILL
10.0
Har second 10 tokens refill
SM_RATE_GLOBAL_CAPACITY
50
Global bucket max 50 tokens
SM_RATE_GLOBAL_REFILL
50.0
Har second 50 tokens globally
FORMULA: tokens = min(capacity, tokens + elapsed_time * refill_rate)

PID BUCKET:
  - Har nayi PID ko full bucket milti hai (10 tokens)
  - Har request: 1 token consume
  - Har second: 10 tokens wapas (max 10)
  - 0 tokens: REJECT

GLOBAL BUCKET:
  - Sab PIDs milake 50 req/second max
  - PID bucket pass ke baad global check bhi hota hai
  - Global empty: sab reject, chahe PID bucket bhara ho

Monotonic Clock Kyun
static double mono_now(void) {                                              
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // MONOTONIC, not REALTIME
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
Clock
Problem
CLOCK_REALTIME
NTP adjust kar sakta hai — time jump forward: 1000 tokens refill! Time jump backward: negative elapsed! Bug!
CLOCK_MONOTONIC
Always increases, NTP se affect nahi hota — Rate limiting ke liye perfect

LRU Eviction — Fork Bomb Protection
MASLA:
  Fork bomb: PID 1, PID 2, ... PID 256
  pid_table full ho gaya!
  PID 257 se legitimate request: table full, kahan rakhen?

PURANI APPROACH (wrong):
  Table full? Simply reject new connection.
  Result: Fork bomb ne PID 1-256 fill kar diye,
          Legitimate PID 257 permanent ban!

NAYA APPROACH (LRU Eviction):
  Table full? Sabse purani (least recently used) entry evict karo.
  pid_table[lru_idx].last_refill = smallest value = sabse purani
  Evict lru_idx, replace with new PID.

  Result:
    - Fork bomb ke stale PIDs evict ho jaate hain
    - Legitimate new PIDs ko jagah milti hai
    - System stable rehta hai

Rate Limit Flow
sm_rate_limit_check(peer_pid):

  1.  mutex lock
  2.  now = mono_now()
  3.  GLOBAL BUCKET REFILL:
        elapsed = now - g_last_refill
        g_tokens = min(50, g_tokens + elapsed * 50.0)
        g_last_refill = now
  4.  global tokens < 1.0? -> REJECT (release lock)
  5.  PID FIND in table (linear scan, 256 max):
        Not found? -> New entry ya LRU evict
  6.  PID BUCKET REFILL:
        elapsed = now - entry.last_refill
        entry.tokens = min(10, entry.tokens + elapsed * 10.0)
  7.  entry.tokens < 1.0? -> REJECT (release lock)
  8.  CONSUME 1 TOKEN:
        entry.tokens -= 1.0
        g_tokens     -= 1.0
  9.  mutex unlock
  10. return SM_OK

CHAPTER 13: MODULE 10 — sm_advanced_ratelimit
Files: sm_advanced_ratelimit.h + sm_advanced_ratelimit.c
Maqsad
Basic rate limit ke upar additional per-service-name limits. Ek service apne naam ke liye specific limits set kar sakti hai.

Two-Layer Rate Limiting Architecture
REQUEST AATA HAI:
        |
        v
[LAYER 1: sm_rate_limit.c]
  Per-PID Token Bucket
  Global Token Bucket
        |
        | (pass hone par)
        v
[LAYER 2: sm_advanced_ratelimit.c]
  Per-Service-Name Token Bucket
  Per-PID-per-Service Token Bucket
        |
        | (dono pass hone par)
        v
  HANDLER

Service Bucket Structure
typedef struct {
    char   service_name[64];  // "database_service"
    pid_t  pid;               // Specific PID ke liye
    double tokens;            // Current token count
    double last_refill;       // Last refill time
} service_bucket_t;

// 256 service buckets max
static service_bucket_t buckets[256];

Usage Example
Database service — sirf 5 req/second:
  sm_ratelimit_set_service_limit("database_service", 1234, 5.0);

PID 1234, service "database_service" request kare:
  Layer 1 check: PID 1234 ok (10 tokens hain)
  Layer 2 check: database_service ok? 5 tokens mein se 1 use karo

  5 rapid requests: OK
  6th request:      Layer 2 mein REJECT (service-specific limit)

  Doosra service "audio_service":
    Layer 2 mein "database_service" limit apply nahi hoti
    Unaffected!

CHAPTER 14: MODULE 11 — sm_security
Files: sm_security.h + sm_security.c
Maqsad
Process-level security: privilege drop, resource limits, seccomp syscall filter, peer credentials.

Privilege Drop — Root Se User Tak
WHY DROP PRIVILEGES:
  Service Manager root se start hota hai (socket setup ke liye)
  Phir root ki zaroorat nahi
  Root rehna = compromised ho toh complete system access!

PROCESS (order zaroori hai):
  1. User  "servicemanager" dhundho (/etc/passwd se)
  2. Group "servicemanager" dhundho (/etc/group se)
  3. initgroups() — root ke sare extra groups remove
  4. setgid()     — GID drop karo PEHLE
                    (UID drop ke baad CAP_SETGID nahi rahti)
  5. setuid()     — UID drop karo
  6. VERIFY:      getuid() != target_uid? ABORT!
  7. VERIFY ROOT REGAIN TEST:
       if (setuid(0) == 0) ABORT!
       (ye kabhi nahi hona chahiye — agar hua: SERIOUS BUG)

Resource Limits — Rlimit
// File Descriptors — FD leak attack protection
struct rlimit rl_nofile = { .rlim_cur = 512, .rlim_max = 512 };
setrlimit(RLIMIT_NOFILE, &rl_nofile);

// Process Count — Fork bomb protection
struct rlimit rl_nproc = { .rlim_cur = 64, .rlim_max = 64 };
setrlimit(RLIMIT_NPROC, &rl_nproc);

// Virtual Memory — Memory exhaustion protection
struct rlimit rl_as = { .rlim_cur = 256MB, .rlim_max = 256MB };
setrlimit(RLIMIT_AS, &rl_as);

// Core Dumps — Sensitive data (HMAC key!) protect karo
struct rlimit rl_core = { .rlim_cur = 0, .rlim_max = 0 };
setrlimit(RLIMIT_CORE, &rl_core);

Seccomp Filter — Allowed Syscalls
CONCEPT:
  Kernel ka BPF (Berkeley Packet Filter) use kar ke har syscall intercept karo.
  Whitelist mein nahi? Process KILL (SECCOMP_RET_KILL_PROCESS).

ALLOWED SYSCALLS (categories):

  Process Lifecycle:   exit_group, exit
  Signal Handling:     rt_sigaction, rt_sigprocmask, rt_sigreturn, sigaltstack
  Socket I/O:          socket, bind, listen, accept, accept4, connect
                       sendto, recvfrom, sendmsg, recvmsg
                       getsockname, getsockopt, setsockopt, shutdown
  File I/O:            read, write, close, fcntl, fstat, lseek
                       openat, rename, unlink, stat
  epoll (event loop):  epoll_create1, epoll_ctl, epoll_wait, epoll_pwait
  Threading:           futex, clone, getpid, gettid, set_robust_list
  Memory:              brk, mmap, munmap, mprotect, mremap
  Time:                clock_gettime, gettimeofday, nanosleep
  Security:            getrandom, prctl
  Health Monitor:      kill, wait4

  DEFAULT: SECCOMP_RET_KILL_PROCESS
  Koi bhi unlisted syscall = process immediately killed

BPF Filter Code
#define ALLOW_SYSCALL(nr)                                          \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1),            \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

struct sock_filter filter[] = {
    // Load syscall number
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
    ALLOW_SYSCALL(SYS_read),
    ALLOW_SYSCALL(SYS_write),
    ALLOW_SYSCALL(SYS_close),
    // ... (sab allowed syscalls)
    // DEFAULT: KILL
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
};

SO_PEERCRED — Peer Credentials
uid_t sm_get_peer_uid(int fd) {
    struct ucred cred = {0};
    socklen_t len = sizeof(cred);
    // Kernel khud peer ka uid/gid/pid batata hai
    // Client is value ko fake nahi kar sakta
    getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len);
    return cred.uid;
}

// WHY NOT TRUST CLIENT-SUPPLIED PID:
//   sm_hdr_t.client_pid field sirf informational hai
//   Hacker message mein client_pid = 1 (root) likh sakta hai
//   SO_PEERCRED = kernel ne verify kiya, tamper-proof!

CHAPTER 15: MODULE 12 — sm_logging
Files: sm_logging.h + sm_logging.c
Maqsad
Thread-safe, rotating file logger jo syslog se bhi integrate karta hai. Re-entrancy protected aur log injection se bachaya hua.

Log Level Hierarchy
Level
Value
Use Case
SM_LOG_DEBUG
0
Development — sabse verbose
SM_LOG_INFO
1
Normal operations
SM_LOG_WARN
2
Unexpected but handled
SM_LOG_ERROR
3
Error hua, fix karo
SM_LOG_CRIT
4
Critical failure, stderr bhi
Filtering Example:
  log_level = SM_LOG_INFO set karo
    -> DEBUG messages:          IGNORE
    -> INFO, WARN, ERROR, CRIT: LOG KARO

  Production:  SM_LOG_WARN ya SM_LOG_ERROR
  Development: SM_LOG_DEBUG

Log Rotation
Log file: /var/log/servicemanager.log
File 10MB ho jaye -> log_rotate_locked() called:

  Step 1: servicemanager.log.4   -> DELETE
  Step 2: servicemanager.log.3   -> servicemanager.log.4
  Step 3: servicemanager.log.2   -> servicemanager.log.3
  Step 4: servicemanager.log.1   -> servicemanager.log.2
  Step 5: servicemanager.log.0   -> servicemanager.log.1
  Step 6: servicemanager.log     -> servicemanager.log.0
  Step 7: Naya servicemanager.log open karo (empty)

  5 backup files = 5 * 10MB = 50MB max log storage

Re-Entrancy Guard
static __thread int in_log = 0;  // Thread-local!

void sm_log(sm_log_level_t level, const char* fmt, ...) {
    if (in_log) return;  // Agar already logging chal raha hai: SKIP
    in_log = 1;
    // ... logging karo ...
    in_log = 0;
}

// WHY NEEDED:
//   Signal handler: sm_log() chal raha hai
//   Signal aata hai: signal handler bhi sm_log() call kare!
//   Bina guard: mutex deadlock!
//   Guard ke saath: signal handler mein in_log=1, silently return
//   Thread-local: Thread A logging -> Thread B independently log kar sakta hai

Log Injection Prevention
static void sanitize_log_buffer(char* buf) {
    for (char* p = buf; *p; p++) {
        unsigned char c = (unsigned char)*p;
        // Control characters (0x00-0x1F) aur DEL (0x7F) replace karo
        if (c < 0x20 || c == 0x7f) *p = ' ';
    }
}

// WHY:
//   Malicious service name: "my_service\nFAKE LOG: root logged in"
//   Bina sanitize:
//     "2024-01-01 INFO my_service"
//     "FAKE LOG: root logged in"   <- injected!
//   Sanitize ke baad:
//     "2024-01-01 INFO my_service FAKE LOG: root logged in"
//     <- newline space se replace, injection fail!

CHAPTER 16: MODULE 13 — sm_audit
Files: sm_audit.h + sm_audit.c
Maqsad
Har security-relevant operation ka immutable audit trail. Compliance (SOC2, ISO27001), forensics aur debugging ke liye.

Audit Format
Log file: /var/log/servicemanager-audit.log
Format:   TIMESTAMP|ACTION|SERVICE|SVC_PID|ACTOR_PID|UID|RESULT|DETAILS

Example entries:
  1704067200|REGISTER  |database_service|1234|1234|1000| 0|success
  1704067210|LOOKUP    |audio_service   |   0|2345|1001| 0|found
  1704067220|HEARTBEAT |camera_service  |3456|3456|1002| 0|updated
  1704067230|AUTH_FAIL |unknown         |   0|9999|   0|-8|hmac_mismatch
  1704067240|CRASH     |network_service |4567|   0|   0|-1|heartbeat_timeout

Log Injection Prevention
static void escape_audit_field(const char* input, char* output, size_t outlen) {
    // |  -> \p  (pipe delimiter se confusion nahi hoga)
    // \n -> \n  (literal backslash-n)
    // \r -> \r
    // \  -> \\
    // c < 32 -> '?'
}

// WHY:
//   Malicious service name: "evil|REGISTER|fake_svc|root|1|0|success"
//   Bina escape:
//     evil|REGISTER|fake_svc|root|1|0|success  <- fake audit entry!
//   Escape ke baad:
//     evil\pREGISTER\pfake_svc\proot\p1\p0\psuccess
//     -> Clearly one field, not multiple entries

Log Rotation (Audit)
static void audit_rotate_if_needed(void) {
    // Har minute se zyada check nahi karte (performance)
    if (now - last_rotation_check < 60) return;

    // File 10MB se badi ho gayi?
    if (st.st_size > AUDIT_LOG_MAX_SIZE) {
        char backup[256];
        snprintf(backup, sizeof(backup), "%s.%ld", AUDIT_LOG_FILE, (long)now);
        rename(AUDIT_LOG_FILE, backup);
        // New empty file open karo
    }
}

CHAPTER 17: MODULE 14 — sm_metrics
Files: sm_metrics.h + sm_metrics.c
Maqsad
Performance aur usage statistics track karta hai. QPS, latency, errors, rate limit hits sab measure karta hai.

Metrics Structure
typedef struct {
    uint64_t total_requests;       // Lifetime total requests
    uint64_t total_register;       // Register messages
    uint64_t total_lookup;         // Lookup messages
    uint64_t total_heartbeat;      // Heartbeat messages
    uint64_t total_unregister;     // Unregister messages
    uint64_t total_errors;         // Failed requests
    uint64_t total_auth_failures;  // HMAC failures
    uint64_t total_ratelimit_hits; // Rate limit rejections
    uint64_t peak_qps;             // Highest QPS seen
    uint64_t avg_latency_us;       // Average latency (microseconds)
} sm_metrics_t;

Rolling Average QPS
QPS = Queries Per Second
Rolling Average = Last 60 seconds ka average

HISTORY ARRAY: qps_history[60]

  [10][15][20][25][30][25][20][15][10]...
    ^second ago             ^60 seconds ago

Har second:
  1. Current second ki count qps_history[history_idx] mein store karo
  2. history_idx = (history_idx + 1) % 60  (circular)
  3. Sum all 60 values, divide by 60 = rolling average

Benefit:
  - Point-in-time spike miss nahi hota
  - Smoothed view: traffic pattern dikhta hai

Latency Tracking
void sm_metrics_request(uint16_t msg_type, int32_t latency_us, int success) {
    // negative handle karo (clock issues)
    uint64_t latency_u = (uint64_t)(latency_us < 0 ? 0 : latency_us);

    pthread_mutex_lock(&g_metrics.mutex);
    g_metrics.total_requests++;
    g_metrics.total_latency_us += latency_u;
    if (!success) g_metrics.total_errors++;
    pthread_mutex_unlock(&g_metrics.mutex);
}

// Average latency:
// avg_latency = total_latency_us / total_requests

CHAPTER 18: MODULE 15 — sm_structured_log
Files: sm_structured_log.h + sm_structured_log.c
Maqsad
Log messages ko JSON format mein output karta hai. Log aggregation tools (ELK Stack, Splunk, Datadog) easily parse kar sakti hain.

JSON Log Format
{
  "ts":        1704067200,
  "level":     "INFO",
  "component": "service",
  "msg":       "service_event",
  "action":    "REGISTER",
  "service":   "database_service",
  "pid":       1234,
  "result":    0,
  "details":   "success"
}

JSON Escaping
static void json_escape_string(const char* input, char* output, size_t outlen) {
    // " -> \"   (string boundary nahi torni)
    // \ -> \\   (escape sequence nahi banna)
    // \n -> \n  (literal)
    // \r -> \r  (literal)
    // \t -> \t  (literal)
    // Control chars (< 32): skip
}

// WHY:
//   Service name: malicious"service","level":"CRIT","hacked":true
//   Bina escape:
//     {"service":"malicious"service","level":"CRIT","hacked":true"}
//     -> Invalid JSON ya JSON injection!
//   Escape ke baad:
//     {"service":"malicious\"service\",\"level\":\"CRIT\",\"hacked\":true"}
//     -> Correctly escaped, single string value

CHAPTER 19: MODULE 16 — sm_health
Files: sm_health.h + sm_health.c
Maqsad
Services ke heartbeats monitor karta hai. Koi service respond karna band kare toh automatically crash detect karta hai aur restart karta hai.

Health Check Cycle
Main event loop mein har SM_HEALTH_CHECK_INTERVAL (3 seconds):
  sm_health_check() call hota hai

sm_health_check():
  1. Registry ka snapshot lo (sm_registry_get_all)
  2. Har service ke liye:
       SERVICE_DEAD?    -> Skip (give up kar chuke hain)
       SERVICE_RUNNING? -> Heartbeat timeout check karo
                           age = now - last_heartbeat
                           age > SM_HEARTBEAT_TIMEOUT (10 sec)?
                             -> Mark CRASHED
       SERVICE_CRASHED? -> Restart logic
  3. Free snapshot (sm_registry_free_copy)

Heartbeat Timeout Detection
SCENARIO:
  Service register karta hai:          last_heartbeat = T0
  Service har 5 seconds heartbeat bhejti hai

  Health check at T12: last_heartbeat = T10 (2 sec ago) -> OK

  Service crash ho gayi! Heartbeats band!

  Health check at T22: last_heartbeat = T10 (12 sec ago)
    age = 12 seconds > 10 seconds = TIMEOUT!
    Status = SERVICE_CRASHED

  Health check at T25:
    Status = CRASHED -> Restart logic

Exponential Backoff Restart
SM_HEALTH_MAX_RESTARTS  = 3
SM_HEALTH_RESTART_DELAY = 2 seconds (base)

  Restart 1: backoff = 2 seconds  (2 * 2^0)
  Restart 2: backoff = 4 seconds  (2 * 2^1)
  Restart 3: backoff = 8 seconds  (2 * 2^2)
  After 3 restarts: SERVICE_DEAD
    Log: "Exceeded max restarts"
    Human intervention required!

WHY EXPONENTIAL:
  - Immediate restart: might fail again immediately
  - Longer waits: dependency services ko time dete hain
  - Max cap (120 seconds): infinite wait nahi

SIGTERM then SIGKILL
// Graceful shutdown attempt:
kill(services[i].pid, SIGTERM);

// 100ms wait
struct timespec ts = {0, 100000000};
nanosleep(&ts, NULL);

// Still alive? Force kill!
if (kill(services[i].pid, 0) == 0) {
    kill(services[i].pid, SIGKILL);
}

// SIGTERM: "Please shutdown gracefully"
//   - Service ko chance deta hai cleanup karne ka
//   - Close connections, flush buffers, save state

// SIGKILL: "Die NOW, no choice"
//   - Cannot be caught or ignored
//   - Kernel forcefully kills

// kill(pid, 0): "Kya process exist karta hai?"
//   Return  0:       exists
//   Return -1 ESRCH: does not exist

CHAPTER 20: MODULE 17 — sm_health_callbacks
Files: sm_health_callbacks.h + sm_health_callbacks.c
Maqsad
Custom health check functions register karne ki facility. Heartbeat ke alawa deeper health checking allow karta hai.

Concept
DEFAULT HEALTH CHECK:
  "Service ne 10 seconds mein heartbeat bheja? Nahi? Crashed."

CUSTOM HEALTH CHECK (callbacks):
  Database service:
    - Heartbeat:  "Process zinda hai"
    - Custom:     "SELECT 1 query successful? Connection pool healthy?"

  Audio service:
    - Custom:     "Audio buffer overflow? Underrun count normal?"

Callback Registration aur Safe Execution
typedef int (*sm_health_check_fn)(const char* service_name);
// Returns: 0 = healthy, negative = unhealthy

// Register:
sm_health_callback_register("database_service", my_db_health_check);

// SAFE Execution pattern:
int sm_health_callback_check(const char* service_name) {
    pthread_mutex_lock(&cb_mutex);

    // Function pointer copy karo (lock ke andar)
    sm_health_check_fn fn = NULL;
    for (int i = 0; i < g_callback_count; i++) {
        if (!strcmp(g_callbacks[i].service_name, service_name)) {
            fn = g_callbacks[i].fn;
            break;
        }
    }
    pthread_mutex_unlock(&cb_mutex);  // PEHLE unlock!

    // LOCK KE BAAHAR execute karo
    // Warna: callback sm_health_ call kare -> DEADLOCK!
    if (fn) return fn(service_name);
    return 0;
}

Bug aur Fix — check_all()
// CURRENT CODE (bug):
void sm_health_callbacks_check_all(void) {
    pthread_mutex_lock(&cb_mutex);
    for (int i = 0; i < g_callback_count; i++) {
        if (g_callbacks[i].fn) {
            g_callbacks[i].fn(service_name);  // LOCK HELD! -> DEADLOCK risk!
        }
    }
    pthread_mutex_unlock(&cb_mutex);
}

// FIXED VERSION:
void sm_health_callbacks_check_all_fixed(void) {
    // Pehle copy karo under lock
    sm_health_check_fn fns[MAX_CALLBACKS];
    char names[MAX_CALLBACKS][64];
    int count;

    pthread_mutex_lock(&cb_mutex);
    count = g_callback_count;
    for (int i = 0; i < count; i++) {
        fns[i] = g_callbacks[i].fn;
        strncpy(names[i], g_callbacks[i].service_name, 63);
    }
    pthread_mutex_unlock(&cb_mutex);  // PEHLE unlock!

    // Lock ke baahar execute karo
    for (int i = 0; i < count; i++) {
        if (fns[i]) fns[i](names[i]);
    }
}

CHAPTER 21: MODULE 18 — sm_watchdog
Files: sm_watchdog.h + sm_watchdog.c
Maqsad
Hardware watchdog timer ke saath integrate karta hai. Agar Service Manager khud hang ho jaye, watchdog system ko automatically restart karta hai.

Hardware Watchdog Kya Hai
HARDWARE WATCHDOG TIMER:
  - Physical timer motherboard par (ya kernel software emulation)
  - Countdown shuru hota hai (e.g., 30 seconds)
  - Software ne 30 sec mein /dev/watchdog mein likha? -> Timer reset
  - Software ne nahi likha (hang ho gaya)?            -> SYSTEM REBOOT

USE CASE:
  Embedded systems, servers, IoT devices
  "Agar software respond nahi kare, reboot karo"
  Human intervention nahi chahiye

Watchdog Thread
sm_watchdog_init():
  1. /dev/watchdog open karo
     Error? Non-fatal -> continue without watchdog
  2. Timeout set karo (30 sec via ioctl WDIOC_SETTIMEOUT)
  3. Background thread start karo (watchdog_thread_main)

watchdog_thread_main():
  Har WATCHDOG_KEEPALIVE_INTERVAL_MS (5000 ms = 5 sec) mein:
    write(watchdog_fd, "1", 1)  <- "Main zinda hun!" Timer reset!

  Agar Service Manager hang ho jaya:
    Thread bhi hang
    5 seconds mein keepalive nahi gaya
    30 seconds baad: SYSTEM REBOOT

  SCHED_FIFO priority:
    Thread high priority se run kare
    System overload mein bhi keepalive miss na ho

Magic Close
// sm_watchdog_cleanup() mein:
write(g_watchdog.watchdog_fd, "V", 1);  // "V" = Magic character
// Watchdog hardware ko bolta hai: "Graceful shutdown hai"
// Watchdog timer disable ho jata hai
// System reboot nahi hoga

// Agar cleanup sahi se na ho (Service Manager crash):
//   Cleanup nahi hua -> watchdog timeout expire -> REBOOT
//   Self-healing system!

CHAPTER 22: MODULE 19 — sm_tls
Files: sm_tls.h + sm_tls.c
Maqsad
TLS encryption layer ka wrapper. Currently stub implementation hai — Unix domain sockets inherently secure hain (localhost only), lekin future network management ke liye ready.

Current Implementation Status
sm_tls.c mein current state:

  // In production, perform SSL_read here:
  // int ret = SSL_read(ctx->ssl, buffer, size);
  int ret = read(ctx->raw_fd, buffer, size);  // Currently: passthrough!

MATLAB:
  - TLS layer exist karta hai (code structure hai)
  - Actual encryption NAHI ho rahi
  - read/write directly socket par
  - OpenSSL integration future kaam hai

KYA KARNA CHAHIYE (future):
  1. OpenSSL link karo: -lssl -lcrypto
  2. SSL_CTX* create karo
  3. Certificate load karo
  4. SSL_read/SSL_write use karo

TLS Statistics
typedef struct {
    uint64_t bytes_encrypted;     // Kitne bytes encrypt hue
    uint64_t bytes_decrypted;     // Kitne bytes decrypt hue
    uint64_t active_connections;  // Current TLS connections
    uint64_t total_connections;   // Lifetime connections
    uint64_t total_errors;        // TLS errors
    int      is_initialized;
} sm_tls_stats_t;

CHAPTER 23: MODULE 20 — sm_discovery
Files: sm_discovery.h + sm_discovery.c
Maqsad
Services ek doosre ke bare mein automatically discover kar sakti hain. Observer pattern implement karta hai.

Publish-Subscribe (Pub/Sub) Pattern
PUBLISHER:
  Service A register hoti hai:
    sm_discovery_publish(SM_EVENT_SERVICE_REGISTERED, "database_service", ...)

SUBSCRIBERS:
  Service B pehle se subscribe thi:
    sm_discovery_subscribe("database_service", on_db_available, context)

RESULT:
  Service A register hoti hai
    -> Service B ka callback automatically call hota hai
    -> on_db_available(notification, context)
    -> "Database ab available hai, connect ho jao!"

BINA PUB/SUB (polling):
  Service B: "Database available hai?"  (har second)
  Service B: "Database available hai?"  (har second)
  ...

PUB/SUB KE SAATH:
  Service B: [subscribe once]
  [Event aata hai automatically]
  Service B callback: "Database available!"

Events
typedef enum {
    SM_EVENT_SERVICE_REGISTERED   = 1,  // Service ne register kiya
    SM_EVENT_SERVICE_DEREGISTERED = 2,  // Service ne unregister kiya
    SM_EVENT_SERVICE_READY        = 3,  // Service ready (custom)
    SM_EVENT_SERVICE_FAILED       = 4,  // Service crash
    SM_EVENT_SERVICE_UPDATED      = 5,  // Service info update
} sm_discovery_event_t;

Callback Under Lock — Risk aur Fix
CURRENT CODE (subtle issue):
  pthread_rwlock_wrlock(&g_discovery.lock);
  sub->callback(&notification, sub->userdata);  // <- LOCK HELD!
  pthread_rwlock_unlock(&g_discovery.lock);

PROBLEM:
  Agar callback khud sm_discovery_subscribe() call kare:
    sm_discovery_subscribe() mein: pthread_rwlock_wrlock() -> DEADLOCK!

BETTER APPROACH:
  1. Notification ka snapshot lo (under lock)
  2. Lock release karo
  3. Callbacks call karo (lock se bahar)

CHAPTER 24: MODULE 21 — sm_eventbus
Files: sm_eventbus.h + sm_eventbus.c
Maqsad
Generic event pub/sub system. Discovery se alag — ye service-to-service messaging ke liye hai, not just lifecycle events.

Event Structure
typedef struct {
    char              event_type[128];        // "database.ready", "cache.full"
    char              publisher_service[64];  // "postgres_db"
    char              event_data[512];        // JSON payload
    sm_event_priority_t priority;             // LOW/NORMAL/HIGH/CRITICAL
    time_t            timestamp;
    uint64_t          event_id;               // Unique per-event ID
} sm_event_t;

Event History
Event history buffer: MAX_EVENT_HISTORY = 512 events
Circular buffer (ring buffer):

  [E1][E2][E3][E4][E5][ ][ ][ ]
         ^current_idx

512 events mein ho jaye:
  [E513][E2][E3]...[E512]  <- E1 overwritten by E513
  current_idx wraps around

Purpose:
  - Debugging
  - Late subscribers catch up kar sakti hain

Wildcard Subscribe
// Specific event:
sm_eventbus_subscribe("database.ready", callback, ctx);

// ALL events:
sm_eventbus_subscribe("*", callback, ctx);

// Matching logic:
int matches = (strcmp(sub->event_type, "*") == 0) ||
              (strcmp(sub->event_type, event_type) == 0);

CHAPTER 25: MODULE 22 — sm_monitoring
Files: sm_monitoring.h + sm_monitoring.c
Maqsad
Service Manager apni resource usage monitor karta hai — CPU, memory, file descriptors. Memory leaks aur resource exhaustion detect karta hai.

/proc Filesystem Readings
Linux mein processes ki info /proc mein hoti hai:

/proc/self/stat:
  pid, comm, state, ppid, ..., utime, stime, ...
  CPU usage = utime + stime

/proc/self/status:
  VmRSS:  4096 kB   <- Resident Set Size (actual RAM usage)
  VmSize: 10240 kB  <- Virtual memory size

/proc/self/fd/:
  Directory jismein open file descriptors listed hain
  Files count = open FD count

Memory Leak Detection
ALGORITHM:
  Sample 5 consecutive readings:
    T1: RSS = 100 MB
    T2: RSS = 105 MB
    T3: RSS = 110 MB
    T4: RSS = 115 MB
    T5: RSS = 120 MB

  Growth ratio = T5 / T1 = 120/100 = 1.2
  THRESHOLD = 1.05 (5% growth)
  1.2 > 1.05 -> MEMORY LEAK SUSPECTED!

  Alert: SM_ALERT_MEMORY_LEAK
  Note: "Suspected" hai, guaranteed nahi.
        Normal startup growth bhi trigger kar sakta hai.

Alert Types
typedef enum {
    SM_ALERT_NONE         = 0,
    SM_ALERT_HIGH_MEMORY,      // > 80% of threshold (default 512MB)
    SM_ALERT_HIGH_CPU,         // > 80% single-core
    SM_ALERT_HIGH_FD_COUNT,    // > 80% of max FDs
    SM_ALERT_MEMORY_LEAK,      // Consistent growth > 5%
    SM_ALERT_FD_LEAK,          // FDs not being closed
} sm_alert_type_t;

CHAPTER 26: MODULE 23 — sm_plugin
Files: sm_plugin.h + sm_plugin.c
Maqsad
External scripts aur shared libraries ke zariye Service Manager ko extend karna. Custom health checks, notifications, restart logic.

Plugin Types
typedef enum {
    PLUGIN_HEALTH_CHECK = 1,  // Custom health verification
    PLUGIN_PRE_RESTART  = 2,  // Restart se pehle run karo
    PLUGIN_POST_RESTART = 3,  // Restart ke baad run karo
    PLUGIN_NOTIFY       = 4,  // Slack, email, webhook notifications
    PLUGIN_ON_CRASH     = 5,  // Service crash hone par
    PLUGIN_ON_STARTUP   = 6,  // Service start hone par
    PLUGIN_CUSTOM       = 7,  // Custom
} sm_plugin_type_t;

Script Plugin Execution
int sm_plugin_execute(const char* plugin_name, const char* argument,
                      char* output_buffer, int output_size) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process:
        execl(entry->path, entry->path, argument, NULL);
        // e.g., execl("/etc/servicemanager/plugins/slack_notify.sh",
        //             "slack_notify.sh", "database_service", NULL)
        exit(127);  // exec fail
    }

    int status;
    waitpid(pid, &status, 0);

    // Exit code:
    //   0       = success
    //   Non-zero = failure
    return WEXITSTATUS(status);
}

TIMEOUT BUG — Ye Fix Karna Hai
// CURRENT CODE (BUG!):
waitpid(pid, &status, 0);  // BLOCKING! No timeout!

// MASLA:
//   Plugin hang ho jaye (infinite loop, network call stuck)
//   waitpid() kabhi return nahi karega
//   Worker thread BLOCK -> Service Manager ke sab threads block -> DEAD!

// FIX — Option 1: WNOHANG + loop
time_t deadline = time(NULL) + timeout;
while (time(NULL) < deadline) {
    if (waitpid(pid, &status, WNOHANG) > 0) break;
    usleep(100000);  // 100ms
}
// Timeout? Force kill
kill(pid, SIGKILL);
waitpid(pid, &status, 0);

Library Plugin (dlopen)
// Shared library load karo:
void* handle = dlopen("/etc/plugins/libnotify.so", RTLD_LAZY);

// Function symbol dhundho:
void* func = dlsym(handle, "notify_handler");

// Call karo:
typedef int (*notify_fn)(const char* service);
notify_fn notify = (notify_fn)func;
notify("database_service");

// Cleanup:
dlclose(handle);

CHAPTER 27: MODULE 24 — sm_rolling_restart
Files: sm_rolling_restart.h + sm_rolling_restart.c
Maqsad
Configuration update ke waqt services ek ek karke restart karta hai — zero downtime.

Rolling Restart vs All-At-Once
ALL-AT-ONCE RESTART (dangerous):
  Time 0:   10 services running
  Time 1:   RESTART ALL! 10 services DOWN!
  Time 1-10: NO SERVICE AVAILABLE! System down!
  Time 11:  Sab wapas aagaye
  Users:    10 second outage!

ROLLING RESTART (safe):
  Time 0:  service1...service10 running
  Time 1:  RESTART service1 only (baaki 9 still running)
  Time 3:  service1 up, RESTART service2
  Time 5:  service2 up, RESTART service3
  ...
  Time 20: service10 up, all done
  Users:   NO OUTAGE!
  (load balancer routes to healthy instances)

State Machine
RESTART_PENDING     -> Initial state, waiting
RESTART_IN_PROGRESS -> Currently restarting
RESTART_SUCCESS     -> Completed successfully
RESTART_FAILED      -> Restart failed
RESTART_SKIPPED     -> Cancelled before reached

Background Thread
static void* rolling_restart_thread_main(void* arg) {
    while (g_rolling_restart.thread_running) {
        if (!g_rolling_restart.is_active) {
            sleep(1);  // Waiting for work
            continue;
        }

        // Find next PENDING service
        int next_idx = -1;
        for (int i = 0; i < service_count; i++) {
            if (services[i].state == RESTART_PENDING) {
                next_idx = i;
                break;
            }
        }

        if (next_idx == -1) {
            is_active = 0;  // All done!
            break;
        }

        services[next_idx].state = RESTART_IN_PROGRESS;

        int ret = restart_single_service(service_name);

        if (ret == 0) services[next_idx].state = RESTART_SUCCESS;
        else          services[next_idx].state = RESTART_FAILED;

        // Delay between restarts (configurable)
        sleep(g_rolling_restart.delay_between_restarts);
    }
}

CHAPTER 28: MODULE 25 — sm_container
Files: sm_container.h + sm_container.c
Maqsad
Services jo containers (Docker, LXC, Podman) mein run kar rahe hain unhe track karna. Container lifecycle aur service mapping manage karna.

Container Detection
static sm_container_type_t detect_container_runtime(void) {
    // Docker: /.dockerenv file hoti hai
    if (access("/.dockerenv", F_OK) == 0) {
        return CONTAINER_TYPE_DOCKER;
    }

    // Cgroup hints: /proc/self/cgroup mein
    FILE* f = fopen("/proc/self/cgroup", "r");
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "/lxc/"))    return CONTAINER_TYPE_LXC;
        if (strstr(line, "/docker/")) return CONTAINER_TYPE_DOCKER;
        if (strstr(line, "/podman/")) return CONTAINER_TYPE_PODMAN;
    }

    return CONTAINER_TYPE_NATIVE;  // Normal host
}

Service-Container Mapping
Service Manager host par run kar raha hai.
Database service Docker container mein run kar raha hai.

REGISTRATION:
  sm_container_register_service(
      "database_service",  // Service name
      "abc123def456",      // Docker container ID
      CONTAINER_TYPE_DOCKER,
      host_pid             // Host system par PID
  );

LOOKUP:
  sm_container_get_service_info("database_service")
    -> container_id = "abc123def456"
    -> host_pid     = 1234
    -> type         = DOCKER

CONTAINER SERVICES:
  sm_container_get_services_in_container("abc123def456", list, max)
    -> ["database_service", "redis_service"]

CHAPTER 29: MODULE 26 — sm_cli
Files: sm_cli.h + sm_cli.c
Maqsad
Command-line interface — admin Service Manager se interact kare terminal se. servicemanagerctl tool ke liye backend.

CLI Commands
Command
Action
servicemanagerctl status
Overall status
servicemanagerctl list
Sab services list
servicemanagerctl info <service>
Service details
servicemanagerctl start <service>
Service start karo
servicemanagerctl stop <service>
Service stop karo
servicemanagerctl restart <service>
Restart
servicemanagerctl logs <service>
Recent logs
servicemanagerctl monitor
Real-time stats
servicemanagerctl help [command]
Help

Connection Model
int sm_cli_connect_to_manager(const char* socket_path) {
    g_cli_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    connect(g_cli_socket, &addr, sizeof(addr));
}

sm_cli_result_t sm_cli_execute_command(command, arg1, arg2) {
    char cmd_buf[1024];
    snprintf(cmd_buf, sizeof(cmd_buf),
             "CMD=%d|ARG1=%s|ARG2=%s", command, arg1, arg2);
    send(g_cli_socket, cmd_buf, strlen(cmd_buf), 0);

    char response[4096];
    recv(g_cli_socket, response, sizeof(response) - 1, 0);
    // Response return karo
}

Monitor Mode Bug aur Fix
// CURRENT CODE (bug):
while (1) {
    // ... display stats ...
    sleep(1);
    if (time(NULL) - last_refresh > 60) break;  // 60 sec hard limit!
}
// MASLA: User Ctrl+C daba de toh? 60 second tak atak jaata hai!

// FIX — Signal handler chahiye:
volatile int g_monitor_running = 1;

void sigint_handler(int sig) {
    g_monitor_running = 0;
}

signal(SIGINT, sigint_handler);

while (g_monitor_running) {
    // ... display stats ...
    sleep(1);
}

CHAPTER 30: MODULE 27 — sm_main_integration
Files: sm_main_integration.h + sm_main_integration.c
Maqsad
10 enterprise features ko ek saath initialize karta hai. Single entry point for startup aur shutdown.

Initialization Order
sm_main_init_all_features():

  [ 1/10] Thread Pool       -> 8 workers, 128 queue
  [ 2/10] Service Discovery -> 256 max subscribers
  [ 3/10] Watchdog Timer    -> /dev/watchdog (non-fatal if unavailable)
  [ 4/10] Resource Monitoring -> 512MB memory, 256 FD threshold
  [ 5/10] TLS Transport     -> Passthrough mode (stub)
  [ 6/10] Rolling Restart   -> 1 concurrent restart max
  [ 7/10] Plugin System     -> /etc/servicemanager/plugins/
  [ 8/10] Container Support -> /proc namespace path
  [ 9/10] Event Bus         -> 512 max subscribers per event
  [10/10] CLI Interface     -> Ready (no init needed)

Success criteria:
  3 ya zyada fail -> return -1 (system unacceptable)
  3 se kam fail   -> return 0  (warnings hain, continue)

Non-fatal features:
  - Watchdog: /dev/watchdog nahi? Continue without
  - TLS:      Cert files nahi? Continue in passthrough mode
  - Plugins:  Plugin dir nahi? Continue without plugins

Cleanup Order
sm_main_cleanup_all_features():
  (Reverse initialization order nahi — sab independent)

   1. Thread Pool shutdown   (graceful, 10 sec timeout)
   2. Discovery cleanup
   3. Watchdog cleanup       (magic close — reboot prevent)
   4. Monitoring cleanup
   5. TLS cleanup
   6. Rolling restart cleanup (cancel if active)
   7. Plugin cleanup         (dlclose all)
   8. Container cleanup
   9. Eventbus cleanup
  10. CLI disconnect

CHAPTER 31: COMPLETE DATA FLOW
Register Request — Shuru se Aakhir Tak
CLIENT SERVICE ("database_service" register karna chahta hai)
      |
      | 1. connect("/run/servicemanager.sock")
      v
sm_socket.c:    accept4() -> client_fd
      |
      | 2. sm_threadpool_submit(client_fd, sm_handle_client, ctx)
      v
sm_threadpool.c: Worker thread task le leta hai
      |
      v
sm_handlers.c:  sm_handle_client(client_fd)
      |
      | 3. req_id = sm_request_id_generate() = 0xABCD000000000042
      | 4. SO_RCVTIMEO = 5s, SO_SNDTIMEO = 5s
      | 5. peer_pid = sm_get_peer_pid() = 1234 (kernel verified)
      v
sm_rate_limit.c: sm_rate_limit_check(1234)
      | PID 1234 bucket: 10 tokens -> 9 tokens
      | Global bucket:   50 tokens -> 49 tokens
      | PASS!
      v
sm_handlers.c:  recv header (56 bytes, MSG_WAITALL)
      | magic=0x534D4B47, version=2, type=REGISTER
      | length=336, timestamp=1704067200, nonce=0xDEAD
      v
sm_protocol.c:  sm_validate_header()
      | magic check:     PASS
      | version check:   PASS
      | type range:      PASS
      | length range:    PASS
      | timestamp window: PASS
      v
sm_handlers.c:  recv payload (336 bytes)
      | payload = {service_name="database_service",
      |            socket_path="/run/db.sock",
      |            ring_name="/run/db_ring"}
      v
sm_protocol.c:  sm_validate_message_size()
      | sizeof(sm_register_req_t) == 336? PASS
      v
sm_crypto.c:    sm_validate_header_hmac()
      | key = g_key (32 bytes from /run/servicemanager.key)
      | mac_input = header[0..23] + payload
      | computed = HMAC-SHA256(key, mac_input)
      | computed == hdr.hmac? PASS!
      v
sm_handlers.c:  sm_handle_register()
      v
sm_advanced_ratelimit.c: sm_ratelimit_check_extended()
      | Per-service check: PASS
      v
sm_protocol.c:  sm_validate_service_name("database_service")
      | alphanumeric + _ - only: PASS
      v
sm_protocol.c:  sm_validate_socket_path("/run/db.sock")
      | absolute: PASS, no ..: PASS, /run/ prefix: PASS
      v
      | service_entry_t entry = {
      |   .name        = "database_service"
      |   .socket_path = "/run/db.sock"
      |   .ring_name   = "/run/db_ring"
      |   .pid         = 1234  // peer_pid (kernel verified)
      |   .uid         = 1000
      |   .gid         = 1000
      |   .status      = SERVICE_RUNNING
      |   .last_heartbeat = now
      | }
      v
sm_registry.c:  sm_registry_add(&entry)
      | wrlock acquire
      | hash_find("database_service") -> not found (new)
      | registry[0] = entry
      | hash_insert("database_service", 0)
      | wrlock release
      | return SM_OK
      v
sm_audit.c:     sm_audit_log(AUDIT_REGISTER, ...)
      | /var/log/servicemanager-audit.log:
      | "1704067200|REGISTER|database_service|1234|1234|1000|0|success"
      v
sm_handlers.c:  send_reply(client_fd, SM_OK)
      v
CLIENT SERVICE receives SM_OK = 0
"database_service successfully registered!"

CHAPTER 32: SECURITY MODEL — END TO END
12 Layers of Defense
LAYER  1: NETWORK ISOLATION
  Unix Domain Socket — local machine only
  No network exposure, no IP-based attacks

LAYER  2: FILESYSTEM PERMISSIONS
  Socket: mode 0660 (group-only access)
  Only servicemanager group members can connect
  chmod fail = abort (security defect = no run)

LAYER  3: RATE LIMITING
  Global:      50 req/sec max (all clients)
  Per-PID:     10 req/sec per process
  LRU eviction: fork bomb protection
  Per-service:  custom limits

LAYER  4: PROTOCOL VALIDATION
  Magic number: 0x534D4B47 — exact match
  Version:      exact match
  Message type: valid range (1-4)
  Payload len:  0 < len <= 1024 — BEFORE receiving payload!

LAYER  5: TIMESTAMP VALIDATION
  Past:   max 120 seconds old
  Future: max 60 seconds ahead
  Replay attack prevention

LAYER  6: HMAC AUTHENTICATION
  HMAC-SHA256 over header[0..23] + payload
  Shared key from /dev/urandom
  Constant-time comparison (timing attack resistant)
  Wrong HMAC = REJECT, no second chance

LAYER  7: PEER CREDENTIALS
  SO_PEERCRED: kernel provides real UID/GID/PID
  Cannot be spoofed by client
  client_pid in header = INFORMATIONAL ONLY

LAYER  8: INPUT VALIDATION
  Service name: [a-zA-Z0-9_-] only
  Socket path:  /run/ or /tmp/ prefix only
  No path traversal (../)
  No double slashes (//)

LAYER  9: PRIVILEGE DROP
  Root -> servicemanager user
  After socket setup and key file creation
  Verify drop success
  Test root cannot be regained

LAYER 10: SECCOMP FILTER
  BPF syscall whitelist (~50 syscalls)
  Any other: SECCOMP_RET_KILL_PROCESS
  Cannot be bypassed from userspace

LAYER 11: RESOURCE LIMITS
  Max 512 FDs   (RLIMIT_NOFILE)
  Max 64 procs  (RLIMIT_NPROC)
  Max 256MB VM  (RLIMIT_AS)
  No core dumps (RLIMIT_CORE = 0)

LAYER 12: AUDIT LOGGING
  Every operation logged
  Log injection prevention (escape)
  Rotation: 10MB max, timestamp backup
  /var/log/servicemanager-audit.log

CHAPTER 33: FILE STRUCTURE
Complete Directory Layout
service_manager/
│
├── infrastructure/
│   ├── sm_protocol.c          Wire protocol validation
│   ├── sm_protocol.h          Protocol definitions
│   ├── sm_socket.c            Server socket setup
│   ├── sm_socket.h
│   ├── sm_registry.c          Service registry (hash table)
│   ├── sm_registry.h
│   ├── sm_handlers.c          Request dispatch
│   ├── sm_handlers.h
│   ├── sm_threadpool.c        Worker thread pool
│   ├── sm_threadpool.h
│   ├── sm_connection_pool.c   Client connection pooling
│   ├── sm_connection_pool.h
│   ├── sm_request_id.c        Distributed tracing IDs
│   ├── sm_request_id.h
│   ├── sm_tls.c               TLS transport (stub)
│   └── sm_tls.h
│
├── security/
│   ├── sm_crypto.c            HMAC-SHA256 implementation
│   ├── sm_crypto.h
│   ├── sm_rate_limit.c        Token bucket rate limiting
│   ├── sm_rate_limit.h
│   ├── sm_advanced_ratelimit.c Per-service rate limiting
│   ├── sm_advanced_ratelimit.h
│   ├── sm_security.c          Privilege drop + seccomp
│   └── sm_security.h
│
├── observability/
│   ├── sm_logging.c           Thread-safe logger
│   ├── sm_logging.h
│   ├── sm_audit.c             Audit trail
│   ├── sm_audit.h
│   ├── sm_metrics.c           Performance metrics
│   ├── sm_metrics.h
│   ├── sm_structured_log.c    JSON logging
│   ├── sm_structured_log.h
│   ├── sm_health.c            Health monitor
│   ├── sm_health.h
│   ├── sm_health_callbacks.c  Custom health checks
│   ├── sm_health_callbacks.h
│   ├── sm_monitoring.c        Resource monitoring
│   └── sm_monitoring.h
│
└── enterprise/
    ├── sm_discovery.c         Service discovery (pub/sub)
    ├── sm_discovery.h
    ├── sm_eventbus.c          Generic event bus
    ├── sm_eventbus.h
    ├── sm_watchdog.c          Hardware watchdog
    ├── sm_watchdog.h
    ├── sm_plugin.c            Plugin system
    ├── sm_plugin.h
    ├── sm_rolling_restart.c   Zero-downtime restart
    ├── sm_rolling_restart.h
    ├── sm_container.c         Container support
    ├── sm_container.h
    ├── sm_cli.c               Command line interface
    ├── sm_cli.h
    ├── sm_main_integration.c  Startup/shutdown integration
    └── sm_main_integration.h

Runtime Files
File
Purpose
/run/servicemanager.sock
Unix domain socket (primary)
/tmp/servicemanager.sock
Unix domain socket (fallback)
/run/servicemanager.key
HMAC shared key (32 bytes)
/tmp/servicemanager.key
HMAC key fallback
/var/log/servicemanager.log
Main log file
/var/log/servicemanager-audit.log
Audit trail
/dev/watchdog
Hardware watchdog device
/etc/servicemanager/plugins/
Plugin scripts directory

Documentation Version: 2.0 | Service Manager Protocol Version: 2
