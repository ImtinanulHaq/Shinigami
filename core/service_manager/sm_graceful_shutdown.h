#ifndef SM_GRACEFUL_SHUTDOWN_H
#define SM_GRACEFUL_SHUTDOWN_H

/*
 * sm_graceful_shutdown.h - Graceful shutdown with service ordering
 *
 * Stops services in reverse dependency order.
 * Respects priority tiers for shutdown sequence.
 */

/* Initiate graceful shutdown with timeout */
void sm_graceful_shutdown(int timeout_sec);

/* Force immediate shutdown */
void sm_force_shutdown(void);

/* Shutdown hook for services */
void sm_register_shutdown_hook(void (*hook)(void));

#endif /* SM_GRACEFUL_SHUTDOWN_H */
