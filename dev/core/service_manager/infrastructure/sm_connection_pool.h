#ifndef SM_CONNECTION_POOL_H
#define SM_CONNECTION_POOL_H

/*
 * sm_connection_pool.h - Client-side connection pooling
 *
 * Maintains a pool of persistent connections to reduce connection overhead
 * for repeated operations.
 */

/* Initialize connection pool */
int sm_connpool_init(int max_conns);

/* Get a connection from the pool (creates if needed) */
int sm_connpool_get(void);

/* Return connection to pool */
void sm_connpool_put(int fd);

/* Cleanup all pooled connections */
void sm_connpool_cleanup(void);

/* Get pool statistics */
void sm_connpool_stats(int* available, int* in_use);

#endif /* SM_CONNECTION_POOL_H */
