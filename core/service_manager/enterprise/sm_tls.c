#define _POSIX_C_SOURCE 200809L

/*
 * sm_tls.c - TLS/SSL encrypted transport implementation
 *
 * Wrapper layer for OpenSSL (can be compiled out if not needed)
 * 
 * NOTE: This is a minimal stub implementation.
 * For production, integrate with OpenSSL/BoringSSL for actual TLS.
 */

#include "../enterprise/sm_tls.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/* Opaque TLS socket context */
typedef struct {
    int raw_fd;
    int is_server;
    int handshake_complete;
    
    uint64_t bytes_encrypted;
    uint64_t bytes_decrypted;
    
    /* In production, would hold OpenSSL SSL* context here */
} tls_socket_context_t;

typedef struct {
    int initialized;
    char cert_file[256];
    char key_file[256];
    
    uint64_t total_bytes_encrypted;
    uint64_t total_bytes_decrypted;
    uint64_t active_connections;
    uint64_t total_connections;
    uint64_t total_errors;
    
    pthread_mutex_t lock;
} tls_context_t;

static tls_context_t g_tls = {0};

int sm_tls_init(const char* cert_file, const char* key_file)
{
    if (g_tls.initialized) {
        sm_log(SM_LOG_WARN, "tls: already initialized");
        return 0;
    }
    
    memset(&g_tls, 0, sizeof(g_tls));
    
    if (pthread_mutex_init(&g_tls.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "tls: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }
    
    /* Validate certificate and key files exist */
    if (cert_file) {
        if (access(cert_file, R_OK) != 0) {
            sm_log(SM_LOG_WARN, "tls: cert file not readable: %s (%s)",
                   cert_file, strerror(errno));
            /* Non-fatal, continue with passthrough */
            return 0;
        }
        strncpy(g_tls.cert_file, cert_file, sizeof(g_tls.cert_file) - 1);
    }
    
    if (key_file) {
        if (access(key_file, R_OK) != 0) {
            sm_log(SM_LOG_WARN, "tls: key file not readable: %s (%s)",
                   key_file, strerror(errno));
            /* Non-fatal, continue with passthrough */
            return 0;
        }
        strncpy(g_tls.key_file, key_file, sizeof(g_tls.key_file) - 1);
    }
    
    g_tls.initialized = 1;
    sm_log(SM_LOG_INFO, "tls: initialized (cert=%s, key=%s)",
           cert_file ? cert_file : "none", key_file ? key_file : "none");
    return 0;
}

sm_tls_socket_t* sm_tls_create_socket(int client_fd, int is_server)
{
    if (!g_tls.initialized) {
        sm_log(SM_LOG_WARN, "tls: not initialized");
        return NULL;
    }
    
    if (client_fd < 0) {
        sm_log(SM_LOG_ERROR, "tls: invalid client_fd %d", client_fd);
        return NULL;
    }
    
    tls_socket_context_t* ctx = (tls_socket_context_t*)calloc(1, sizeof(tls_socket_context_t));
    if (!ctx) {
        sm_log(SM_LOG_ERROR, "tls: malloc failed");
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_errors++;
        pthread_mutex_unlock(&g_tls.lock);
        return NULL;
    }
    
    ctx->raw_fd = client_fd;
    ctx->is_server = is_server;
    
    pthread_mutex_lock(&g_tls.lock);
    g_tls.active_connections++;
    g_tls.total_connections++;
    pthread_mutex_unlock(&g_tls.lock);
    
    sm_log(SM_LOG_DEBUG, "tls: created %s socket (fd=%d)",
           is_server ? "server" : "client", client_fd);
    return (sm_tls_socket_t*)ctx;
}

int sm_tls_accept(sm_tls_socket_t* tls_sock)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    
    if (!ctx || !ctx->is_server) {
        return -1;
    }
    
    /* In production, perform OpenSSL handshake here:
     *   SSL_accept(ctx->ssl)
     */
    
    ctx->handshake_complete = 1;
    sm_log(SM_LOG_DEBUG, "tls: accept handshake complete");
    return 0;
}

int sm_tls_connect(sm_tls_socket_t* tls_sock)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    
    if (!ctx || ctx->is_server) {
        return -1;
    }
    
    /* In production, perform OpenSSL handshake here:
     *   SSL_connect(ctx->ssl)
     */
    
    ctx->handshake_complete = 1;
    sm_log(SM_LOG_DEBUG, "tls: connect handshake complete");
    return 0;
}

int sm_tls_read(sm_tls_socket_t* tls_sock, void* buffer, int size)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    
    if (!ctx || !buffer || size <= 0) {
        return -1;
    }
    
    if (!ctx->handshake_complete) {
        sm_log(SM_LOG_WARN, "tls: read attempted before handshake");
        return -1;
    }
    
    /* In production, perform SSL_read here:
     *   int ret = SSL_read(ctx->ssl, buffer, size);
     */
    
    /* For now, passthrough read from raw socket */
    int ret = read(ctx->raw_fd, buffer, size);
    
    if (ret > 0) {
        ctx->bytes_decrypted += ret;
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_bytes_decrypted += ret;
        pthread_mutex_unlock(&g_tls.lock);
    }
    
    if (ret < 0) {
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_errors++;
        pthread_mutex_unlock(&g_tls.lock);
    }
    
    return ret;
}

int sm_tls_write(sm_tls_socket_t* tls_sock, const void* buffer, int size)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    
    if (!ctx || !buffer || size <= 0) {
        return -1;
    }
    
    if (!ctx->handshake_complete) {
        sm_log(SM_LOG_WARN, "tls: write attempted before handshake");
        return -1;
    }
    
    /* In production, perform SSL_write here:
     *   int ret = SSL_write(ctx->ssl, buffer, size);
     */
    
    /* For now, passthrough write to raw socket */
    int ret = write(ctx->raw_fd, buffer, size);
    
    if (ret > 0) {
        ctx->bytes_encrypted += ret;
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_bytes_encrypted += ret;
        pthread_mutex_unlock(&g_tls.lock);
    }
    
    if (ret < 0) {
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_errors++;
        pthread_mutex_unlock(&g_tls.lock);
    }
    
    return ret;
}

int sm_tls_close(sm_tls_socket_t* tls_sock)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    
    if (!ctx) {
        return -1;
    }
    
    /* In production, perform SSL_shutdown here:
     *   SSL_shutdown(ctx->ssl);
     *   SSL_free(ctx->ssl);
     */
    
    close(ctx->raw_fd);
    
    pthread_mutex_lock(&g_tls.lock);
    if (g_tls.active_connections > 0) {
        g_tls.active_connections--;
    }
    pthread_mutex_unlock(&g_tls.lock);
    
    free(ctx);
    sm_log(SM_LOG_DEBUG, "tls: socket closed");
    return 0;
}

int sm_tls_get_peer_certificate(sm_tls_socket_t* tls_sock, char* subject, int subject_len)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    
    if (!ctx || !subject || subject_len <= 0) {
        return -1;
    }
    
    /* In production, extract X509 subject:
     *   X509 *cert = SSL_get_peer_certificate(ctx->ssl);
     *   X509_NAME_oneline(X509_get_subject_name(cert), ...);
     */
    
    snprintf(subject, subject_len, "not-implemented");
    return 0;
}

sm_tls_stats_t sm_tls_get_stats(void)
{
    sm_tls_stats_t stats = {0};
    
    pthread_mutex_lock(&g_tls.lock);
    stats.is_initialized = g_tls.initialized;
    stats.bytes_encrypted = g_tls.total_bytes_encrypted;
    stats.bytes_decrypted = g_tls.total_bytes_decrypted;
    stats.active_connections = g_tls.active_connections;
    stats.total_connections = g_tls.total_connections;
    stats.total_errors = g_tls.total_errors;
    pthread_mutex_unlock(&g_tls.lock);
    
    return stats;
}

int sm_tls_cleanup(void)
{
    if (!g_tls.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_tls.lock);
    
    /* In production, perform SSL_CTX_free() here */
    
    memset(&g_tls, 0, sizeof(g_tls));
    pthread_mutex_unlock(&g_tls.lock);
    pthread_mutex_destroy(&g_tls.lock);
    
    sm_log(SM_LOG_INFO, "tls: cleanup complete");
    return 0;
}
