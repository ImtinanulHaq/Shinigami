#ifndef SM_MANAGEMENT_H
#define SM_MANAGEMENT_H

/*
 * sm_management.h - Network management API
 *
 * Provides HTTP/REST endpoints for remote management.
 * Requires authentication token.
 */

/* Start management API server on specified port */
int sm_management_start(int port);

/* Stop management API server */
void sm_management_stop(void);

/* Management endpoints:
 *   GET  /status         - Server status and metrics
 *   GET  /services       - List all services
 *   POST /service/{name} - Restart service
 *   GET  /metrics        - Performance metrics
 *   POST /shutdown       - Graceful shutdown
 *   GET  /config         - Current configuration
 */

#endif /* SM_MANAGEMENT_H */
