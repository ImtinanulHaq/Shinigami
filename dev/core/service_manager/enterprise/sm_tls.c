#define _POSIX_C_SOURCE 200809L

/*
 * sm_tls.c - TLS/SSL encrypted transport implementation
 *
 * Wrapper layer for OpenSSL (can be compiled out if not needed)
 *
 * NOTE: This is a minimal stub implementation.
 * For production, integrate with OpenSSL/BoringSSL for actual TLS.
 *
 * STUB STATUS — functions marked [STUB] pass data through the raw socket
 * without encryption.  Replace every [STUB] block with real OpenSSL calls
 * before deploying to production.
 *
 * FIX SUMMARY:
 *   1. sm_tls_init()   — g_tls.initialized set AFTER mutex_init, not before.
 *   2. sm_tls_init()   — early-return on missing cert/key now sets initialized=1
 *                        so the rest of the system can still use passthrough mode.
 *   3. sm_tls_read()   — uses recv() with MSG_NOSIGNAL instead of read()
 *                        to avoid SIGPIPE on broken connections.
 *   4. sm_tls_write()  — uses send() with MSG_NOSIGNAL instead of write()
 *                        to avoid SIGPIPE on broken connections.
 *   5. sm_tls_close()  — NULL-guard added; ctx zeroed before free() to
 *                        prevent use-after-free information leaks.
 *   6. sm_tls_cleanup()— mutex destroyed AFTER memset, not while still held.
 *   7. All public functions — NULL-guard on tls_sock cast before any field
 *                        access to prevent NULL-dereference crashes.
 */

#include "../enterprise/sm_tls.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>

/* ── Internal context structs ──────────────────────────────────────────────── */

typedef struct {
    int      raw_fd;
    int      is_server;
    int      handshake_complete;

    uint64_t bytes_encrypted;
    uint64_t bytes_decrypted;

    /* [STUB] In production, hold OpenSSL SSL* handle here:
     *   SSL* ssl;
     */
} tls_socket_context_t;

typedef struct {
    int  initialized;
    char cert_file[256];
    char key_file[256];

    uint64_t total_bytes_encrypted;
    uint64_t total_bytes_decrypted;
    uint64_t active_connections;
    uint64_t total_connections;
    uint64_t total_errors;

    pthread_mutex_t lock;

    /* [STUB] In production, hold OpenSSL SSL_CTX* here:
     *   SSL_CTX* ssl_ctx;
     */
} tls_context_t;

static tls_context_t g_tls = {0};

/* ── sm_tls_init ───────────────────────────────────────────────────────────── */

int sm_tls_init(const char* cert_file, const char* key_file)
{
    if (g_tls.initialized) {
        sm_log(SM_LOG_WARN, "tls: already initialized");
        return 0;
    }

    memset(&g_tls, 0, sizeof(g_tls));

    /* FIX 1: mutex_init pehle karo, initialized baad mein set karo
     * Pehle: initialized=1 set ho jata tha, phir mutex fail hota tha
     * Doosra thread initialized=1 dekh ke use karta tha bina mutex ke */
    if (pthread_mutex_init(&g_tls.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "tls: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }

    /* FIX 2: Cert/key missing ho toh bhi initialized=1 set karo
     * Passthrough mode mein kaam karta rahega system
     * Pehle: early return karta tha bina initialized set kiye
     * Matlab: baaki sab functions NULL return karte the — system band ho jata */
    if (cert_file) {
        if (access(cert_file, R_OK) != 0) {
            sm_log(SM_LOG_WARN, "tls: cert file not readable: %s (%s) — passthrough mode",
                   cert_file, strerror(errno));
            g_tls.initialized = 1;  /* passthrough mode */
            return 0;
        }
        strncpy(g_tls.cert_file, cert_file, sizeof(g_tls.cert_file) - 1);
    }

    if (key_file) {
        if (access(key_file, R_OK) != 0) {
            sm_log(SM_LOG_WARN, "tls: key file not readable: %s (%s) — passthrough mode",
                   key_file, strerror(errno));
            g_tls.initialized = 1;  /* passthrough mode */
            return 0;
        }
        strncpy(g_tls.key_file, key_file, sizeof(g_tls.key_file) - 1);
    }

    /* [STUB] Production mein yahan SSL_CTX banao:
     *   g_tls.ssl_ctx = SSL_CTX_new(TLS_method());
     *   SSL_CTX_use_certificate_file(g_tls.ssl_ctx, cert_file, SSL_FILETYPE_PEM);
     *   SSL_CTX_use_PrivateKey_file(g_tls.ssl_ctx, key_file, SSL_FILETYPE_PEM);
     *   SSL_CTX_check_private_key(g_tls.ssl_ctx);
     */

    g_tls.initialized = 1;
    sm_log(SM_LOG_INFO, "tls: initialized (cert=%s, key=%s)",
           cert_file ? cert_file : "none",
           key_file  ? key_file  : "none");
    return 0;
}

/* ── sm_tls_create_socket ──────────────────────────────────────────────────── */

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

    tls_socket_context_t* ctx =
        (tls_socket_context_t*)calloc(1, sizeof(tls_socket_context_t));
    if (!ctx) {
        sm_log(SM_LOG_ERROR, "tls: malloc failed");
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_errors++;
        pthread_mutex_unlock(&g_tls.lock);
        return NULL;
    }

    ctx->raw_fd    = client_fd;
    ctx->is_server = is_server;

    /* [STUB] Production mein yahan SSL object banao:
     *   ctx->ssl = SSL_new(g_tls.ssl_ctx);
     *   SSL_set_fd(ctx->ssl, client_fd);
     */

    pthread_mutex_lock(&g_tls.lock);
    g_tls.active_connections++;
    g_tls.total_connections++;
    pthread_mutex_unlock(&g_tls.lock);

    sm_log(SM_LOG_DEBUG, "tls: created %s socket (fd=%d)",
           is_server ? "server" : "client", client_fd);
    return (sm_tls_socket_t*)ctx;
}

/* ── sm_tls_accept ─────────────────────────────────────────────────────────── */

int sm_tls_accept(sm_tls_socket_t* tls_sock)
{
    /* FIX 7: NULL guard — cast ke baad check karo */
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    if (!ctx || !ctx->is_server) {
        sm_log(SM_LOG_ERROR, "tls: accept — invalid context");
        return -1;
    }

    /* [STUB] Production mein OpenSSL handshake karo:
     *   int ret = SSL_accept(ctx->ssl);
     *   if (ret <= 0) {
     *       int err = SSL_get_error(ctx->ssl, ret);
     *       sm_log(SM_LOG_ERROR, "tls: SSL_accept failed: %d", err);
     *       return -1;
     *   }
     */

    ctx->handshake_complete = 1;
    sm_log(SM_LOG_DEBUG, "tls: accept handshake complete [STUB — no real TLS]");
    return 0;
}

/* ── sm_tls_connect ────────────────────────────────────────────────────────── */

int sm_tls_connect(sm_tls_socket_t* tls_sock)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    if (!ctx || ctx->is_server) {
        sm_log(SM_LOG_ERROR, "tls: connect — invalid context");
        return -1;
    }

    /* [STUB] Production mein OpenSSL handshake karo:
     *   int ret = SSL_connect(ctx->ssl);
     *   if (ret <= 0) {
     *       int err = SSL_get_error(ctx->ssl, ret);
     *       sm_log(SM_LOG_ERROR, "tls: SSL_connect failed: %d", err);
     *       return -1;
     *   }
     */

    ctx->handshake_complete = 1;
    sm_log(SM_LOG_DEBUG, "tls: connect handshake complete [STUB — no real TLS]");
    return 0;
}

/* ── sm_tls_read ───────────────────────────────────────────────────────────── */

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

    /* [STUB] Production mein SSL_read use karo:
     *   int ret = SSL_read(ctx->ssl, buffer, size);
     *   if (ret <= 0) {
     *       int err = SSL_get_error(ctx->ssl, ret);
     *       // handle SSL_ERROR_WANT_READ / SSL_ERROR_ZERO_RETURN etc.
     *   }
     */

    /* FIX 3: read() ki jagah recv() with MSG_NOSIGNAL use karo
     * read() pe broken connection = SIGPIPE signal = process crash!
     * recv(MSG_NOSIGNAL) = SIGPIPE nahi aata, errno=EPIPE milta hai */
    int ret = (int)recv(ctx->raw_fd, buffer, (size_t)size, MSG_NOSIGNAL);

    if (ret > 0) {
        ctx->bytes_decrypted += (uint64_t)ret;
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_bytes_decrypted += (uint64_t)ret;
        pthread_mutex_unlock(&g_tls.lock);
    } else if (ret < 0) {
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_errors++;
        pthread_mutex_unlock(&g_tls.lock);
        sm_log(SM_LOG_WARN, "tls: read error fd=%d: %s", ctx->raw_fd, strerror(errno));
    }

    return ret;
}

/* ── sm_tls_write ──────────────────────────────────────────────────────────── */

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

    /* [STUB] Production mein SSL_write use karo:
     *   int ret = SSL_write(ctx->ssl, buffer, size);
     *   if (ret <= 0) {
     *       int err = SSL_get_error(ctx->ssl, ret);
     *       // handle errors
     *   }
     */

    /* FIX 4: write() ki jagah send() with MSG_NOSIGNAL use karo
     * Same reason as sm_tls_read() — SIGPIPE se bacho */
    int ret = (int)send(ctx->raw_fd, buffer, (size_t)size, MSG_NOSIGNAL);

    if (ret > 0) {
        ctx->bytes_encrypted += (uint64_t)ret;
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_bytes_encrypted += (uint64_t)ret;
        pthread_mutex_unlock(&g_tls.lock);
    } else if (ret < 0) {
        pthread_mutex_lock(&g_tls.lock);
        g_tls.total_errors++;
        pthread_mutex_unlock(&g_tls.lock);
        sm_log(SM_LOG_WARN, "tls: write error fd=%d: %s", ctx->raw_fd, strerror(errno));
    }

    return ret;
}

/* ── sm_tls_close ──────────────────────────────────────────────────────────── */

int sm_tls_close(sm_tls_socket_t* tls_sock)
{
    /* FIX 5: NULL guard pehle check karo */
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    if (!ctx) {
        return -1;
    }

    /* [STUB] Production mein SSL graceful shutdown karo:
     *   SSL_shutdown(ctx->ssl);
     *   SSL_free(ctx->ssl);
     *   ctx->ssl = NULL;
     */

    if (ctx->raw_fd >= 0) {
        close(ctx->raw_fd);
        ctx->raw_fd = -1;
    }

    pthread_mutex_lock(&g_tls.lock);
    if (g_tls.active_connections > 0) {
        g_tls.active_connections--;
    }
    pthread_mutex_unlock(&g_tls.lock);

    /* FIX 5: free se pehle zero karo — sensitive data leak na ho */
    memset(ctx, 0, sizeof(tls_socket_context_t));
    free(ctx);

    sm_log(SM_LOG_DEBUG, "tls: socket closed");
    return 0;
}

/* ── sm_tls_get_peer_certificate ───────────────────────────────────────────── */

int sm_tls_get_peer_certificate(sm_tls_socket_t* tls_sock,
                                char* subject, int subject_len)
{
    tls_socket_context_t* ctx = (tls_socket_context_t*)tls_sock;
    if (!ctx || !subject || subject_len <= 0) {
        return -1;
    }

    /* [STUB] Production mein X509 certificate extract karo:
     *   X509* cert = SSL_get_peer_certificate(ctx->ssl);
     *   if (!cert) return -1;
     *   X509_NAME_oneline(X509_get_subject_name(cert), subject, subject_len);
     *   X509_free(cert);
     */

    snprintf(subject, subject_len, "not-implemented-stub");
    return 0;
}

/* ── sm_tls_get_stats ──────────────────────────────────────────────────────── */

sm_tls_stats_t sm_tls_get_stats(void)
{
    sm_tls_stats_t stats = {0};

    pthread_mutex_lock(&g_tls.lock);
    stats.is_initialized      = g_tls.initialized;
    stats.bytes_encrypted     = g_tls.total_bytes_encrypted;
    stats.bytes_decrypted     = g_tls.total_bytes_decrypted;
    stats.active_connections  = g_tls.active_connections;
    stats.total_connections   = g_tls.total_connections;
    stats.total_errors        = g_tls.total_errors;
    pthread_mutex_unlock(&g_tls.lock);

    return stats;
}

/* ── sm_tls_cleanup ────────────────────────────────────────────────────────── */

int sm_tls_cleanup(void)
{
    if (!g_tls.initialized) {
        return 0;
    }

    /* [STUB] Production mein SSL_CTX free karo:
     *   if (g_tls.ssl_ctx) {
     *       SSL_CTX_free(g_tls.ssl_ctx);
     *       g_tls.ssl_ctx = NULL;
     *   }
     */

    /* FIX 6: lock lo, memset karo, unlock karo, PHIR destroy karo
     * Pehle: lock ke andar hi destroy ho raha tha — undefined behaviour!
     * Destroyed mutex ke andar koi aur lock nahi le sakta */
    pthread_mutex_lock(&g_tls.lock);
    memset(&g_tls, 0, sizeof(g_tls));
    pthread_mutex_unlock(&g_tls.lock);

    pthread_mutex_destroy(&g_tls.lock);

    sm_log(SM_LOG_INFO, "tls: cleanup complete");
    return 0;
}