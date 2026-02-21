#ifndef SM_MAIN_H
#define SM_MAIN_H

/*
 * sm_main.h - Public client API for the service manager.
 *
 * Services include this header to register, query, send heartbeats, and
 * unregister with the service manager daemon over its Unix socket.
 *
 * Prerequisites for client processes:
 *   1. The process must belong to the 'servicemanager' group (so it can read
 *      SM_KEY_FILE to compute message HMACs).
 *   2. sm_crypto_init() must be called once before any API function below.
 *
 * Typical usage:
 *   #include "sm_main.h"
 *   #include "sm_crypto.h"
 *
 *   sm_crypto_init();
 *   sm_register("myservice", "/run/myservice.sock", "/run/myservice.ring");
 *   for (;;) {
 *       // do work ...
 *       sm_heartbeat("myservice");
 *       sleep(5);
 *   }
 *   sm_unregister("myservice");
 */

/*
 * sm_register() - Register this process as a named service.
 *
 * 'socket_path' and 'ring_name' must begin with /run/ or /tmp/.
 * The server records the kernel-verified PID, UID, and GID from SO_PEERCRED.
 * Returns SM_OK on success, SM_ERR_EXISTS if already registered,
 * SM_ERR_FULL if the registry is at capacity, or another negative error code.
 */
int sm_register(const char* name, const char* socket_path, const char* ring_name);

/*
 * sm_lookup() - Look up a registered service by name.
 *
 * On success, copies the service's socket path and ring name into the caller-
 * provided buffers (each must be at least SM_MAX_PATH bytes).
 * Returns SM_OK, SM_ERR_NOT_FOUND if the service does not exist or is not
 * in SERVICE_RUNNING state, or another negative error code.
 */
int sm_lookup(const char* name, char* socket_path_out, char* ring_name_out);

/*
 * sm_heartbeat() - Report that this service is still alive.
 *
 * Should be sent at least once every SM_HEARTBEAT_TIMEOUT seconds.
 * Returns SM_OK or a negative error code.
 */
int sm_heartbeat(const char* name);

/*
 * sm_unregister() - Remove this service from the registry.
 *
 * Only the process that originally registered the service may unregister it;
 * the server enforces this via SO_PEERCRED (kernel-verified PID).
 * Returns SM_OK, SM_ERR_NOT_FOUND, SM_ERR_PERMISSION, or another error code.
 */
int sm_unregister(const char* name);

/*
 * sm_disconnect() - Close a connection file descriptor.
 *
 * In the current per-call connection model each API function opens and closes
 * its own connection, so this is provided for future persistent-connection use.
 */
void sm_disconnect(int fd);

/*
 * sm_run() - Start the service manager server.
 *
 * Initializes all subsystems, drops privileges, installs seccomp, then runs
 * the accept loop until SIGTERM or SIGINT is received.
 * Returns 0 on clean shutdown, -1 on a fatal initialization error.
 */
int sm_run(void);

#endif /* SM_MAIN_H */