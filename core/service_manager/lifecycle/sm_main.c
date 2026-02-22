#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/*
 * sm_main.c - Service manager: entry point, event loop, thread pool, client API.
 *
 * Fixes applied:
 *   - Thread pool: connections are handed off to worker threads immediately,
 *     so a slow or malicious client cannot block the accept loop.
 *   - Client API functions (sm_register, sm_lookup, etc.) now send BOTH the
 *     header AND the payload body in every message.
 *   - Client API computes and inserts a valid HMAC into every header.
 *   - Nonce is populated from getrandom() for replay protection.
 *   - accept4() used for atomic SOCK_CLOEXEC (no SOCK_NONBLOCK on client fds).
 *   - Crypto subsystem initialised before seccomp is applied.
 *   - Thread pool created before seccomp (clone() is restricted after).
 */

#include "../observability/sm_logging.h"
#include "../security/sm_security.h"
#include "../infrastructure/sm_socket.h"
#include "../infrastructure/sm_registry.h"
#include "../infrastructure/sm_handlers.h"
#include "../observability/sm_health.h"
#include "../security/sm_rate_limit.h"
#include "../infrastructure/sm_protocol.h"
#include "../security/sm_crypto.h"
#include "../lifecycle/sm_config.h"
#include "../observability/sm_metrics.h"
#include "../observability/sm_audit.h"
#include "../lifecycle/sm_dependencies.h"
#include "../observability/sm_health_callbacks.h"
#include "../lifecycle/sm_persistence.h"
#include "../lifecycle/sm_service_tier.h"
#include "../observability/sm_structured_log.h"
#include "../lifecycle/sm_graceful_shutdown.h"
#include "../lifecycle/sm_management.h"
#include "../infrastructure/sm_connection_pool.h"
#include "../enterprise/sm_main_integration.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/random.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

#pragma GCC diagnostic ignored "-Wstringop-truncation"

/* ── THREAD POOL ────────────────────────────────────────────────────────────── */

#define DEFAULT_THREAD_POOL_SIZE  4     /* worker threads (overridden by config) */
#define DEFAULT_WORK_QUEUE_SIZE   64    /* pending file descriptors (overridden by config) */
#define MAX_THREAD_POOL_SIZE      64
#define MAX_WORK_QUEUE_SIZE       4096

typedef struct {
    int*     fds;
    int      capacity;
    int      head;
    int      tail;
    int      count;
    int      shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} work_queue_t;

static work_queue_t  work_queue;
static pthread_t*    workers = NULL;
static int           pool_size = DEFAULT_THREAD_POOL_SIZE;

static void wq_init(work_queue_t* q, int queue_capacity)
{
    memset(q, 0, sizeof(*q));
    q->capacity = queue_capacity;
    q->fds = calloc((size_t)queue_capacity, sizeof(int));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond,  NULL);
}

/* Returns 0 on success, -1 if queue is full */
static int wq_push(work_queue_t* q, int fd)
{
    int ret = -1;

    pthread_mutex_lock(&q->mutex);
    if (q->count < q->capacity) {
        q->fds[q->tail] = fd;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        ret = 0;
        pthread_cond_signal(&q->cond);
    }
    pthread_mutex_unlock(&q->mutex);
    return ret;
}

static int wq_pop(work_queue_t* q)
{
    int fd;

    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->shutdown) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    if (q->shutdown && q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    fd = q->fds[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    return fd;
}

static void wq_shutdown(work_queue_t* q)
{
    pthread_mutex_lock(&q->mutex);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

static void* worker_thread(void* arg)
{
    (void)arg;

    for (;;) {
        int fd = wq_pop(&work_queue);
        if (fd < 0) break;   /* shutdown signal */
        sm_handle_client(fd);
        close(fd);
    }
    return NULL;
}

static int threadpool_init(void)
{
    int i;
    const sm_config_t* cfg = sm_config_get();
    
    pool_size = (cfg && cfg->thread_pool_size > 0 && cfg->thread_pool_size <= MAX_THREAD_POOL_SIZE)
                ? cfg->thread_pool_size : DEFAULT_THREAD_POOL_SIZE;
    int queue_size = (cfg && cfg->work_queue_size >= 4 && cfg->work_queue_size <= MAX_WORK_QUEUE_SIZE)
                     ? cfg->work_queue_size : DEFAULT_WORK_QUEUE_SIZE;
    
    wq_init(&work_queue, queue_size);
    if (!work_queue.fds) {
        sm_log(SM_LOG_ERROR, "main: work queue allocation failed");
        return -1;
    }
    
    workers = calloc((size_t)pool_size, sizeof(pthread_t));
    if (!workers) {
        sm_log(SM_LOG_ERROR, "main: worker array allocation failed");
        free(work_queue.fds);
        return -1;
    }

    for (i = 0; i < pool_size; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0) {
            sm_log(SM_LOG_ERROR, "main: pthread_create worker %d failed: %s", i, strerror(errno));
            return -1;
        }
    }

    sm_log(SM_LOG_INFO, "main: thread pool started (%d workers, queue=%d)", pool_size, queue_size);
    return 0;
}

static void threadpool_shutdown(void)
{
    int i;

    wq_shutdown(&work_queue);
    for (i = 0; i < pool_size; i++) {
        pthread_join(workers[i], NULL);
    }
    pthread_mutex_destroy(&work_queue.mutex);
    pthread_cond_destroy(&work_queue.cond);
    free(work_queue.fds);
    work_queue.fds = NULL;
    free(workers);
    workers = NULL;
    sm_log(SM_LOG_INFO, "main: thread pool stopped");
}

/* ── SIGNAL HANDLING ────────────────────────────────────────────────────────── */

static volatile int            running         = 1;

static void setup_signals(void)
{
    sigset_t set;
    
    /* Block SIGTERM, SIGHUP, and SIGPIPE - handle via signalfd */
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    /* Signal handlers are no longer needed - signalfd will handle signals */
}

/* ── CLEANUP ────────────────────────────────────────────────────────────────── */

static void cleanup(void)
{
    sm_log(SM_LOG_INFO, "main: shutting down");

    threadpool_shutdown();
    
    /* Cleanup all 10 enterprise features */
    sm_main_cleanup_all_features();
    
    /* Save registry state before shutting down */
    sm_persistence_save(NULL);
    
    sm_management_stop();
    sm_connpool_cleanup();
    sm_health_callbacks_cleanup();
    sm_deps_cleanup();
    sm_audit_cleanup();
    sm_metrics_cleanup();
        sm_socket_cleanup();
    sm_registry_cleanup();
    sm_rate_limit_cleanup();
    sm_crypto_cleanup();

    sm_logging_close();
}

/* ── MAIN EVENT LOOP ────────────────────────────────────────────────────────── */

int sm_run(void)
{
    int    epoll_fd;
    int    server_fd;
    struct epoll_event ev;
    struct epoll_event events[32];
    time_t last_health_check;
    int    n;

    if (sm_logging_init() < 0) {
        fprintf(stderr, "fatal: logging init failed\n");
        return -1;
    }

    sm_log(SM_LOG_INFO, "===== Service Manager Starting =====");

    if (sm_set_resource_limits() < 0) {
        sm_log(SM_LOG_ERROR, "main: resource limits failed");
        cleanup();
        return -1;
    }

    /* Load or generate the HMAC key BEFORE dropping privileges */
    if (sm_crypto_init() < 0) {
        sm_log(SM_LOG_ERROR, "main: crypto init failed");
        cleanup();
        return -1;
    }

    if (sm_drop_privileges() < 0) {
        sm_log(SM_LOG_ERROR, "main: privilege drop failed");
        cleanup();
        return -1;
    }

    setup_signals();

    if (sm_registry_init() < 0 || sm_rate_limit_init() < 0) {
        sm_log(SM_LOG_ERROR, "main: subsystem init failed");
        cleanup();
        return -1;
    }

    /* Initialize new subsystems */
    if (sm_config_load(NULL) < 0) {
        sm_log(SM_LOG_WARN, "main: config load failed, using defaults");
    }
    
    if (sm_metrics_init() < 0) {
        sm_log(SM_LOG_ERROR, "main: metrics init failed");
        cleanup();
        return -1;
    }
    
    if (sm_audit_init() < 0) {
        sm_log(SM_LOG_WARN, "main: audit init failed");
    }
    
    sm_deps_cleanup();  /* initialize deps */
    sm_health_callbacks_cleanup();  /* initialize callbacks */
    sm_connpool_init(10);  /* initialize connection pool */
    
    /* Load persisted registry from previous run */
    if (sm_persistence_load(NULL) < 0) {
        sm_log(SM_LOG_WARN, "main: persistence load failed, starting fresh");
    }

    if (sm_socket_setup() < 0) {
        sm_log(SM_LOG_ERROR, "main: socket setup failed");
        cleanup();
        return -1;
    }

    /*
     * Create the thread pool BEFORE seccomp is applied.
     * pthread_create uses clone() which is in the whitelist, but to avoid
     * any ordering issues we create threads while still unrestricted.
     */
    if (threadpool_init() < 0) {
        sm_log(SM_LOG_ERROR, "main: thread pool init failed");
        cleanup();
        return -1;
    }

    /* Initialize all 10 enterprise features (discovery, watchdog, monitoring, etc.) */
    if (sm_main_init_all_features() < 0) {
        sm_log(SM_LOG_WARN, "main: some enterprise features failed to initialize");
        /* Non-fatal - continue with core features */
    }

    /* Install seccomp filter - must be last before event loop */
    if (sm_setup_seccomp() < 0) {
        sm_log(SM_LOG_ERROR, "main: seccomp setup failed");
        cleanup();
        return -1;
    }
    
    /* Start management API if enabled */
    const sm_config_t* cfg = sm_config_get();
    if (cfg && cfg->enable_management) {
        if (sm_management_start(cfg->management_port) < 0) {
            sm_log(SM_LOG_WARN, "main: management API start failed");
        }
    }

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        sm_log(SM_LOG_ERROR, "main: epoll_create1 failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    /* Create signalfd for signal-safe event handling */
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGHUP);
    
    int signal_fd = signalfd(-1, &sigset, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signal_fd < 0) {
        sm_log(SM_LOG_ERROR, "main: signalfd failed: %s", strerror(errno));
        close(epoll_fd);
        cleanup();
        return -1;
    }

    server_fd = sm_socket_get_fd();
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        sm_log(SM_LOG_ERROR, "main: epoll_ctl ADD failed: %s", strerror(errno));
        close(signal_fd);
        close(epoll_fd);
        cleanup();
        return -1;
    }

    /* Add signal_fd to epoll for signal-safe event handling */
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.fd = signal_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, signal_fd, &ev) < 0) {
        sm_log(SM_LOG_ERROR, "main: epoll_ctl ADD signal_fd failed: %s", strerror(errno));
        close(signal_fd);
        close(epoll_fd);
        cleanup();
        return -1;
    }

    sm_log(SM_LOG_INFO, "main: ready");

    last_health_check = time(NULL);

    while (running) {
        n = epoll_wait(epoll_fd, events, 32, 3000 /* ms */);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            sm_log(SM_LOG_ERROR, "main: epoll_wait failed: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            /* Handle signals via signalfd */
            if (events[i].data.fd == signal_fd) {
                struct signalfd_siginfo sinfo;
                ssize_t s = read(signal_fd, &sinfo, sizeof(sinfo));
                
                if (s == sizeof(sinfo)) {
                    if (sinfo.ssi_signo == SIGTERM) {
                        sm_log(SM_LOG_INFO, "main: SIGTERM received - initiating shutdown");
                        running = 0;
                    } else if (sinfo.ssi_signo == SIGHUP) {
                        sm_log(SM_LOG_INFO, "main: SIGHUP received - reloading config");
                        
                        /* Reload configuration file */
                        const char* config_file = "/etc/servicemanager.conf";
                        if (sm_config_load(config_file) < 0) {
                            sm_log(SM_LOG_WARN, "main: config reload failed for %s", config_file);
                        } else {
                            sm_log(SM_LOG_INFO, "main: config reloaded successfully");
                        }
                    }
                }
                continue;
            }
            
            if (events[i].data.fd != server_fd) continue;

            /*
             * accept4() with SOCK_CLOEXEC only - client fd is BLOCKING.
             *
             * IMPORTANT: do NOT set SOCK_NONBLOCK here.
             * The listening server fd is O_NONBLOCK (for the epoll loop).
             * Client fds must be blocking so that SO_RCVTIMEO works correctly
             * with MSG_WAITALL in sm_handle_client().  On Linux, MSG_WAITALL
             * is ignored on non-blocking sockets - recv() returns whatever
             * data is immediately available (or EAGAIN), breaking the header
             * and payload receive logic in sm_handlers.c.
             */
            int client_fd = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
            if (client_fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    sm_log(SM_LOG_ERROR, "main: accept4 failed: %s", strerror(errno));
                continue;
            }

            /* Hand off to thread pool; if full, drop the connection */
            if (wq_push(&work_queue, client_fd) < 0) {
                sm_log(SM_LOG_WARN, "main: work queue full - dropping connection");
                close(client_fd);
            }
        }

        time_t now = time(NULL);
        if (now - last_health_check >= SM_HEALTH_CHECK_INTERVAL) {
            sm_health_check();
            last_health_check = now;
        }
    }

    close(signal_fd);
    close(epoll_fd);
    cleanup();

    sm_log(SM_LOG_INFO, "===== Service Manager Stopped =====");
    return 0;
}

/* ── CLIENT-SIDE API ────────────────────────────────────────────────────────── */

/*
 * Build and send a complete message (header + payload) then recv reply.
 * The header HMAC covers the header bytes and the payload.
 */
static int _sm_transact(int fd,
                        const void* req_payload, size_t req_payload_size,
                        uint16_t    msg_type,
                        void*       reply_buf,   size_t reply_size)
{
    sm_hdr_t       hdr;
    uint8_t        mac_input[SM_HDR_HMAC_OFFSET + SM_MAX_PAYLOAD_SIZE];
    const uint8_t* key;
    uint32_t       nonce_buf;
    ssize_t        n;

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic      = SM_PROTOCOL_MAGIC;
    hdr.version    = SM_PROTOCOL_VERSION;
    hdr.type       = msg_type;
    hdr.length     = (uint32_t)req_payload_size;
    hdr.timestamp  = (uint32_t)time(NULL);
    hdr.client_pid = (uint32_t)getpid();

    /* Generate a random nonce for replay protection */
    if (getrandom(&nonce_buf, sizeof(nonce_buf), 0) == sizeof(nonce_buf))
        hdr.nonce = nonce_buf;

    /* Compute HMAC over (header fields before hmac) + payload */
    key = sm_crypto_get_key();
    if (key) {
        memcpy(mac_input,                    &hdr,        SM_HDR_HMAC_OFFSET);
        memcpy(mac_input + SM_HDR_HMAC_OFFSET, req_payload, req_payload_size);
        sm_hmac_sha256(key, SM_HMAC_KEY_SIZE,
                       mac_input, SM_HDR_HMAC_OFFSET + req_payload_size,
                       hdr.hmac);
    }

    /* Send header */
    n = send(fd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
    if (n != (ssize_t)sizeof(hdr)) return -1;

    /* Send payload body */
    if (req_payload_size > 0) {
        n = send(fd, req_payload, req_payload_size, MSG_NOSIGNAL);
        if (n != (ssize_t)req_payload_size) return -1;
    }

    /* Receive reply */
    n = recv(fd, reply_buf, reply_size, MSG_WAITALL);
    if (n != (ssize_t)reply_size) return -1;

    return 0;
}

static int sm_connect_persistent(void)
{
    int                fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sm_socket_get_path(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int sm_register(const char* name, const char* socket_path, const char* ring_name)
{
    int               fd;
    sm_register_req_t req  = {0};
    sm_reply_t        reply = {0};
    
    /* Input validation */
    if (!name || !socket_path || !ring_name) {
        return SM_ERR_INVALID;
    }
    if (strlen(name) == 0 || strlen(socket_path) == 0 || strlen(ring_name) == 0) {
        return SM_ERR_INVALID;
    }

    fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    strncpy(req.service_name, name,        SM_MAX_NAME - 1);
    strncpy(req.socket_path,  socket_path, SM_MAX_PATH - 1);
    strncpy(req.ring_name,    ring_name,   SM_MAX_PATH - 1);
    req.pid = (int32_t)getpid();

    if (_sm_transact(fd, &req, sizeof(req), SM_MSG_REGISTER,
                     &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);
    return (int)reply.response_code;
}

int sm_lookup(const char* name, char* socket_path_out, char* ring_name_out)
{
    int               fd;
    sm_lookup_req_t   req   = {0};
    sm_lookup_reply_t reply = {0};
    
    /* Input validation */
    if (!name || strlen(name) == 0) {
        return SM_ERR_INVALID;
    }
    if (!socket_path_out || !ring_name_out) {
        return SM_ERR_INVALID;
    }

    fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    strncpy(req.service_name, name, SM_MAX_NAME - 1);

    if (_sm_transact(fd, &req, sizeof(req), SM_MSG_LOOKUP,
                     &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);

    if (socket_path_out) strncpy(socket_path_out, reply.socket_path, SM_MAX_PATH - 1);
    if (ring_name_out)   strncpy(ring_name_out,   reply.ring_name,   SM_MAX_PATH - 1);

    return SM_OK;
}

int sm_heartbeat(const char* name)
{
    int                  fd;
    sm_heartbeat_req_t   req   = {0};
    sm_reply_t           reply = {0};
    
    /* Input validation */
    if (!name || strlen(name) == 0) {
        return SM_ERR_INVALID;
    }

    fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    strncpy(req.service_name, name, SM_MAX_NAME - 1);

    if (_sm_transact(fd, &req, sizeof(req), SM_MSG_HEARTBEAT,
                     &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);
    return (int)reply.response_code;
}

int sm_unregister(const char* name)
{
    int                   fd;
    sm_unregister_req_t   req   = {0};
    sm_reply_t            reply = {0};
    
    /* Input validation */
    if (!name || strlen(name) == 0) {
        return SM_ERR_INVALID;
    }

    fd = sm_connect_persistent();
    if (fd < 0) return SM_ERR_NOT_FOUND;

    strncpy(req.service_name, name, SM_MAX_NAME - 1);

    if (_sm_transact(fd, &req, sizeof(req), SM_MSG_UNREGISTER,
                     &reply, sizeof(reply)) < 0) {
        close(fd);
        return SM_ERR_INVALID;
    }

    close(fd);
    return (int)reply.response_code;
}

void sm_disconnect(int fd)
{
    if (fd >= 0) close(fd);
}

/* ── ENTRY POINT ────────────────────────────────────────────────────────────── */

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    return sm_run();
}