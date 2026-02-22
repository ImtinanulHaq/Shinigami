/*
 * sm_tls.h - TLS/SSL encrypted transport for service manager
 *
 * Optional encryption layer for use with TCP-based management APIs.
 * Wraps socket operations to provide transparent encryption.
 * 
 * NOTES:
 *   - Current implementation uses Unix domain sockets (inherently secure)
 *   - TLS layer useful for future network-based management
 *   - Can be compiled out if not needed
 * 
 * USAGE:
 *   sm_tls_init(cert_file, key_file);
 *   
 *   // Use TLS for socket:
 *   sm_tls_socket_t* tls_sock = sm_tls_create_socket(client_fd);
 *   sm_tls_accept(tls_sock);
 *   
 *   sm_tls_read(tls_sock, buffer, size);
 *   sm_tls_write(tls_sock, buffer, size);
 *   
 *   sm_tls_close(tls_sock);
 *   sm_tls_cleanup();
 */

#ifndef SM_TLS_H
#define SM_TLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque TLS socket handle */
typedef struct sm_tls_socket sm_tls_socket_t;

/* TLS statistics */
typedef struct {
    uint64_t bytes_encrypted;
    uint64_t bytes_decrypted;
    uint64_t active_connections;
    uint64_t total_connections;
    uint64_t total_errors;
    int is_initialized;
} sm_tls_stats_t;

/*
 * sm_tls_init(cert_file, key_file)
 * 
 * Initialize TLS context with certificates.
 * 
 * PARAMETERS:
 *   cert_file - path to PEM certificate file
 *   key_file - path to PEM private key file
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error (file not found, invalid PEM, etc)
 * 
 * NOTES:
 *   - Can be called multiple times to reload certificates
 *   - Non-fatal if TLS unavailable (continues unencrypted)
 */
int sm_tls_init(const char* cert_file, const char* key_file);

/*
 * sm_tls_create_socket(client_fd, is_server)
 * 
 * Wrap raw socket with TLS layer.
 * 
 * PARAMETERS:
 *   client_fd - raw socket file descriptor
 *   is_server - 1 for server-side TLS, 0 for client-side
 * 
 * RETURNS:
 *   TLS socket handle on success
 *   NULL on error
 */
sm_tls_socket_t* sm_tls_create_socket(int client_fd, int is_server);

/*
 * sm_tls_accept(tls_sock)
 * 
 * Complete TLS handshake on server side.
 * 
 * PARAMETERS:
 *   tls_sock - TLS socket handle
 * 
 * RETURNS:
 *   0 on success
 *   -1 on TLS error
 */
int sm_tls_accept(sm_tls_socket_t* tls_sock);

/*
 * sm_tls_connect(tls_sock)
 * 
 * Complete TLS handshake on client side.
 * 
 * PARAMETERS:
 *   tls_sock - TLS socket handle
 * 
 * RETURNS:
 *   0 on success
 *   -1 on TLS error
 */
int sm_tls_connect(sm_tls_socket_t* tls_sock);

/*
 * sm_tls_read(tls_sock, buffer, size)
 * 
 * Read decrypted data from TLS socket.
 * 
 * PARAMETERS:
 *   tls_sock - TLS socket handle
 *   buffer - output buffer
 *   size - buffer size
 * 
 * RETURNS:
 *   bytes read on success (0 for EOF)
 *   -1 on error
 */
int sm_tls_read(sm_tls_socket_t* tls_sock, void* buffer, int size);

/*
 * sm_tls_write(tls_sock, buffer, size)
 * 
 * Write encrypted data to TLS socket.
 * 
 * PARAMETERS:
 *   tls_sock - TLS socket handle
 *   buffer - data to write
 *   size - bytes to write
 * 
 * RETURNS:
 *   bytes written on success
 *   -1 on error
 */
int sm_tls_write(sm_tls_socket_t* tls_sock, const void* buffer, int size);

/*
 * sm_tls_close(tls_sock)
 * 
 * Close TLS socket and clean up.
 * 
 * PARAMETERS:
 *   tls_sock - TLS socket handle
 * 
 * RETURNS:
 *   0 on success
 */
int sm_tls_close(sm_tls_socket_t* tls_sock);

/*
 * sm_tls_get_peer_certificate(tls_sock, subject, subject_len)
 * 
 * Extract X.509 certificate subject from peer.
 * 
 * PARAMETERS:
 *   tls_sock - TLS socket handle
 *   subject - output buffer for subject string
 *   subject_len - buffer size
 * 
 * RETURNS:
 *   0 on success
 *   -1 on error
 */
int sm_tls_get_peer_certificate(sm_tls_socket_t* tls_sock, char* subject, int subject_len);

/*
 * sm_tls_get_stats()
 * 
 * Get TLS layer statistics.
 * 
 * RETURNS:
 *   Statistics structure
 */
sm_tls_stats_t sm_tls_get_stats(void);

/*
 * sm_tls_cleanup()
 * 
 * Shutdown TLS context and clean up.
 * 
 * PARAMETERS: none
 * 
 * RETURNS:
 *   0 on success
 */
int sm_tls_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_TLS_H */
