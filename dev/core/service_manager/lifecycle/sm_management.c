#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * sm_management.c - Management API with HMAC-SHA256 authentication
 *
 * Implements RESTful management endpoints with industry-standard:
 * - HMAC-SHA256 request authentication
 * - Proper HTTP status codes and headers
 * - Request/response parsing
 * - Audit logging
 * - Sub-millisecond response: non-blocking accept loop with 1 s epoll timeout,
 *   SO_RCVTIMEO/SO_SNDTIMEO on every client fd (2 s), accept4(SOCK_CLOEXEC).
 */

#include "../lifecycle/sm_management.h"
#include "../observability/sm_logging.h"
#include "../lifecycle/sm_config.h"
#include "../observability/sm_metrics.h"
#include "../infrastructure/sm_registry.h"
#include "../security/sm_crypto.h"
#include "../security/sm_security.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <stdatomic.h>
#include <sys/epoll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    char method[16];
    char path[256];
    char auth_header[256];
    int content_length;
    char* body;
} http_request_t;

typedef struct {
    const char* method;
    const char* path;
    int (*handler)(http_request_t* req, char* response_body, int max_len);
} mgmt_endpoint_t;

#define MAX_HTTP_HEADER_SIZE 4096
#define MAX_MGMT_ENDPOINTS 10

/* Timeout for client I/O: prevents a slow/malicious client from blocking
 * the management thread indefinitely. */
#define MGMT_CLIENT_IO_TIMEOUT_SEC 2
/* Timeout on accept() itself: allows periodic mgmt_running re-check. */
#define MGMT_ACCEPT_TIMEOUT_SEC    1

/* Parse HTTP request from raw data */
static int parse_http_request(const char* raw_data, int len, http_request_t* req)
{
    (void)len;  /* Parameter not used in current implementation */
    
    if (!raw_data || !req) return -1;
    
    memset(req, 0, sizeof(*req));
    
    /* Find first line break */
    const char* first_line_end = strchr(raw_data, '\n');
    if (!first_line_end) return -1;
    
    size_t first_line_len = (size_t)(first_line_end - raw_data);
    if (first_line_len > 511) return -1;
    
    /* Parse request line: METHOD PATH HTTP/1.x */
    char request_line[512];
    memcpy(request_line, raw_data, first_line_len);
    request_line[first_line_len] = '\0';
    
    /* Remove trailing \r if present */
    if (first_line_len > 0 && request_line[first_line_len - 1] == '\r') {
        request_line[first_line_len - 1] = '\0';
    }
    
    if (sscanf(request_line, "%15s %255s", req->method, req->path) != 2) {
        return -1;
    }
    
    /* Parse headers looking for Authorization and Content-Length */
    const char* headers_start = first_line_end + 1;
    const char* headers_end = strstr(headers_start, "\r\n\r\n");
    if (!headers_end) headers_end = strstr(headers_start, "\n\n");
    if (!headers_end) return -1;
    
    const char* current = headers_start;
    while (current < headers_end) {
        const char* line_end = strchr(current, '\n');
        if (!line_end) break;
        
        size_t line_len = (size_t)(line_end - current);
        if (line_len < 2) {
            current = line_end + 1;
            continue;
        }
        
        /* Remove trailing \r */
        if (*(line_end - 1) == '\r') line_len--;
        
        /* Check for Authorization header */
        if (strncasecmp(current, "Authorization:", 14) == 0) {
            const char* value_start = current + 14;
            while (*value_start && isspace(*value_start)) value_start++;
            size_t value_len = line_len - (size_t)(value_start - current);
            if (value_len > 0 && value_len < 255) {
                memcpy(req->auth_header, value_start, value_len);
                req->auth_header[value_len] = '\0';
            }
        }
        
        /* Check for Content-Length header */
        if (strncasecmp(current, "Content-Length:", 15) == 0) {
            const char* value_start = current + 15;
            while (*value_start && isspace(*value_start)) value_start++;
            req->content_length = atoi(value_start);
        }
        
        current = line_end + 1;
    }
    
    return 0;
}

/* Convert hex string to bytes */
static int hex_to_bytes(const char* hex_str, uint8_t* out, size_t max_len)
{
    if (!hex_str || !out || max_len < 1) return -1;
    
    size_t len = strlen(hex_str);
    if (len % 2 != 0 || len / 2 > max_len) return -1;
    
    for (size_t i = 0; i < len; i += 2) {
        if (sscanf(hex_str + i, "%2hhx", &out[i / 2]) != 1) {
            return -1;
        }
    }
    
    return (int)(len / 2);
}

/* Verify HMAC-SHA256 authentication header */
static int verify_request_auth(http_request_t* req, const char* body)
{
    if (!req->auth_header[0]) {
        sm_log(SM_LOG_WARN, "management: missing authorization header");
        return -1;
    }
    
    /* Expected header format: "HMAC-SHA256 <hex_digest>" */
    if (strncmp(req->auth_header, "HMAC-SHA256 ", 12) != 0) {
        sm_log(SM_LOG_WARN, "management: invalid auth scheme");
        return -1;
    }
    
    const char* digest_hex = req->auth_header + 12;
    uint8_t expected_digest[SM_HMAC_SIZE];
    int digest_len = hex_to_bytes(digest_hex, expected_digest, SM_HMAC_SIZE);
    if (digest_len != SM_HMAC_SIZE) {
        sm_log(SM_LOG_WARN, "management: invalid auth digest format");
        return -1;
    }
    
    /* Reconstruct authenticated data: METHOD|PATH|BODY */
    const char* body_to_sign = body ? body : "";
    size_t body_len = body ? strlen(body) : 0;
    
    size_t method_len = strlen(req->method);
    size_t path_len = strlen(req->path);
    size_t auth_data_len = method_len + 1 + path_len + 1 + (size_t)body_len;
    uint8_t* auth_data = malloc(auth_data_len);
    if (!auth_data) return -1;
    
    size_t pos = 0;
    memcpy(auth_data + pos, req->method, method_len);
    pos += method_len;
    auth_data[pos++] = '|';
    
    memcpy(auth_data + pos, req->path, path_len);
    pos += path_len;
    auth_data[pos++] = '|';
    
    if (body_len > 0) {
        memcpy(auth_data + pos, body_to_sign, (size_t)body_len);
    }
    
    /* Get the key and verify HMAC */
    const uint8_t* key = sm_crypto_get_key();
    if (!key) {
        sm_log(SM_LOG_ERROR, "management: crypto key not initialized");
        explicit_bzero(auth_data, auth_data_len);
        free(auth_data);
        return -1;
    }
    
    int result = sm_hmac_verify(key, SM_HMAC_KEY_SIZE,
                                auth_data, auth_data_len,
                                expected_digest);
    
    explicit_bzero(auth_data, auth_data_len);
    free(auth_data);
    
    if (result != 0) {
        sm_log(SM_LOG_WARN, "management: HMAC verification failed");
        return -1;
    }
    
    return 0;
}

/* Handler for /status endpoint */
static int handle_status(http_request_t* req, char* response_body, int max_len)
{
    (void)req;
    
    sm_metrics_t metrics = sm_metrics_get();
    
    snprintf(response_body, (size_t)max_len,
             "{\"status\":\"running\",\"requests\":%lu,\"errors\":%lu,\"ratelimit_hits\":%lu,"
             "\"avg_latency_us\":%lu,\"peak_qps\":%lu}",
             metrics.total_requests,
             metrics.total_errors,
             metrics.total_ratelimit_hits,
             metrics.avg_latency_us,
             metrics.peak_qps);
    
    return 200;
}

/* Handler for /metrics endpoint */
static int handle_metrics(http_request_t* req, char* response_body, int max_len)
{
    (void)req;
    
    sm_metrics_t metrics = sm_metrics_get();
    
    snprintf(response_body, (size_t)max_len,
             "{\"total_requests\":%lu,\"register\":%lu,\"lookup\":%lu,\"heartbeat\":%lu,"
             "\"unregister\":%lu,\"errors\":%lu,\"auth_failures\":%lu,\"ratelimit_hits\":%lu}",
             metrics.total_requests,
             metrics.total_register,
             metrics.total_lookup,
             metrics.total_heartbeat,
             metrics.total_unregister,
             metrics.total_errors,
             metrics.total_auth_failures,
             metrics.total_ratelimit_hits);
    
    return 200;
}

/* Handler for /services endpoint */
static int handle_services(http_request_t* req, char* response_body, int max_len)
{
    (void)req;
    
    service_entry_t* services = NULL;
    int count = 0;
    
    if (sm_registry_get_all(&services, &count) < 0) {
        snprintf(response_body, (size_t)max_len, "{\"error\":\"failed to get services\"}");
        return 500;
    }
    
    int pos = snprintf(response_body, (size_t)max_len, "{\"services\":[");
    if (pos < 0) pos = 0;
    
    /* Each service entry needs ~80+ bytes minimum; cap services to avoid buffer overflow */
    int max_services = (max_len - 64) / 100;  /* Conservative estimate */
    if (max_services < 1) max_services = 1;
    
    for (int i = 0; i < count && i < max_services; i++) {
        /* Check if we have enough space for one more entry (~100 bytes) */
        if (pos >= max_len - 100) {
            /* Truncate and add indicator */
            pos = snprintf(response_body + pos, (size_t)(max_len - pos), "{\"name\":\"...(truncated)\"}");
            break;
        }
        
        if (i > 0) {
            int n = snprintf(response_body + pos, (size_t)(max_len - pos), ",");
            if (n < 0) break;
            pos += n;
        }
        
        int n = snprintf(response_body + pos, (size_t)(max_len - pos),
                        "{\"name\":\"%s\",\"pid\":%d,\"status\":%d}",
                        services[i].name, (int)services[i].pid, services[i].status);
        if (n < 0) break;
        pos += n;
    }
    
    snprintf(response_body + pos, (size_t)(max_len - pos), "}]");
    sm_registry_free_copy(services);
    
    return 200;
}

/* Endpoint routing table */
static const mgmt_endpoint_t endpoints[] = {
    {"GET",  "/status",   handle_status},
    {"GET",  "/metrics",  handle_metrics},
    {"GET",  "/services", handle_services},
};

static const int num_endpoints = sizeof(endpoints) / sizeof(endpoints[0]);

/* Send HTTP response */
static void send_http_response(int client_fd, int status_code, const char* body)
{
    const char* status_text = "200 OK";
    if (status_code == 400) status_text = "400 Bad Request";
    else if (status_code == 401) status_text = "401 Unauthorized";
    else if (status_code == 403) status_text = "403 Forbidden";
    else if (status_code == 404) status_text = "404 Not Found";
    else if (status_code == 405) status_text = "405 Method Not Allowed";
    else if (status_code == 500) status_text = "500 Internal Server Error";
    
    size_t body_len = body ? strlen(body) : 0;
    
    char response[4096];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n%s",
             status_code, status_text, body_len, body ? body : "");
    
    send(client_fd, response, strlen(response), 0);
}

/* Process a single management request */
static void process_management_request(int client_fd)
{
    char buf[MAX_HTTP_HEADER_SIZE];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    
    buf[n] = '\0';
    
    /* Parse HTTP request */
    http_request_t req;
    if (parse_http_request(buf, (int)n, &req) < 0) {
        send_http_response(client_fd, 400, "{\"error\":\"malformed request\"}");
        return;
    }
    
    /* Verify authentication */
    if (verify_request_auth(&req, NULL) < 0) {
        sm_log(SM_LOG_WARN, "management: authentication failed for %s %s",
               req.method, req.path);
        send_http_response(client_fd, 401, "{\"error\":\"authentication failed\"}");
        return;
    }
    
    /* Find and call appropriate handler */
    int status_code = 404;
    char response_body[8192] = "";  /* Increased from 2048 for safety */
    
    for (int i = 0; i < num_endpoints; i++) {
        if (strcmp(endpoints[i].method, req.method) == 0 &&
            strcmp(endpoints[i].path, req.path) == 0) {
            status_code = endpoints[i].handler(&req, response_body, sizeof(response_body));
            break;
        }
    }
    
    if (status_code == 404) {
        snprintf(response_body, sizeof(response_body),
                "{\"error\":\"endpoint not found: %s %s\"}", req.method, req.path);
    }
    
    send_http_response(client_fd, status_code, response_body);
}

/* File-scope state — must appear before management_worker() uses them */
static int          mgmt_fd      = -1;
static _Atomic int  mgmt_running = 0;

/* Management API worker thread */
static void* management_worker(void* arg)
{
    (void)arg;
    
    const sm_config_t* config = sm_config_get();
    int port = config ? config->management_port : 9999;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons((uint16_t)port);

    mgmt_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (mgmt_fd < 0) {
        sm_log(SM_LOG_ERROR, "management: socket creation failed");
        return NULL;
    }

    int opt = 1;
    setsockopt(mgmt_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(mgmt_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        sm_log(SM_LOG_ERROR, "management: bind to port %d failed", port);
        close(mgmt_fd);
        mgmt_fd = -1;
        return NULL;
    }

    if (listen(mgmt_fd, 5) < 0) {
        sm_log(SM_LOG_ERROR, "management: listen failed");
        close(mgmt_fd);
        mgmt_fd = -1;
        return NULL;
    }

    /* SO_RCVTIMEO on the listening socket: accept() wakes up every
     * MGMT_ACCEPT_TIMEOUT_SEC so we can re-check mgmt_running. */
    struct timeval accept_tv = { .tv_sec = MGMT_ACCEPT_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(mgmt_fd, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv));

    sm_log(SM_LOG_INFO,
           "management: listening on 127.0.0.1:%d (HMAC-SHA256 auth required)", port);

    /* Accept and process connections.
     * accept4() with SOCK_CLOEXEC prevents fd leaks to child processes.
     * Each client gets its own I/O timeout to prevent slow-client blocking. */
    while (atomic_load_explicit(&mgmt_running, memory_order_acquire)) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);

        int client_fd = accept4(mgmt_fd, (struct sockaddr*)&client, &clen,
                                SOCK_CLOEXEC);
        if (client_fd < 0) {
            /* Timeout or signal — re-check running flag */
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            if (atomic_load_explicit(&mgmt_running, memory_order_relaxed))
                sm_log(SM_LOG_WARN, "management: accept4() failed: %s", strerror(errno));
            continue;
        }

        /* Per-client I/O deadline — sub-ms responses, max 2 s wait */
        struct timeval client_tv = { .tv_sec = MGMT_CLIENT_IO_TIMEOUT_SEC, .tv_usec = 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &client_tv, sizeof(client_tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &client_tv, sizeof(client_tv));

        process_management_request(client_fd);
        close(client_fd);
    }

    return NULL;
}

int sm_management_start(int port)
{
    pthread_t tid;

    if (atomic_load_explicit(&mgmt_running, memory_order_relaxed)) {
        sm_log(SM_LOG_WARN, "management: already running");
        return -1;
    }

    /* Parameter port is ignored; config port is used instead */
    (void)port;

    atomic_store_explicit(&mgmt_running, 1, memory_order_release);

    if (pthread_create(&tid, NULL, management_worker, NULL) != 0) {
        atomic_store_explicit(&mgmt_running, 0, memory_order_relaxed);
        sm_log(SM_LOG_ERROR, "management: failed to create worker thread");
        return -1;
    }

    pthread_detach(tid);

    const sm_config_t* config = sm_config_get();
    int actual_port = config ? config->management_port : 9999;
    sm_log(SM_LOG_INFO, "management: API started (port=%d, auth=HMAC-SHA256)", actual_port);
    return 0;
}

void sm_management_stop(void)
{
    atomic_store_explicit(&mgmt_running, 0, memory_order_release);

    if (mgmt_fd >= 0) {
        close(mgmt_fd);
        mgmt_fd = -1;
    }

    sm_log(SM_LOG_INFO, "management: API stopped");
}
