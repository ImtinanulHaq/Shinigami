#ifndef SM_REQUEST_ID_H
#define SM_REQUEST_ID_H

/*
 * sm_request_id.h - Request ID tracking for distributed tracing
 */

#include <stdint.h>

typedef uint64_t request_id_t;

/* Generate a new request ID */
request_id_t sm_request_id_generate(void);

/* Get current thread's request ID (for logging context) */
request_id_t sm_request_id_current(void);

/* Set current thread's request ID */
void sm_request_id_set(request_id_t req_id);

/* Clear current thread's request ID */
void sm_request_id_clear(void);

/* Format request ID for logging (thread-safe) */
const char* sm_request_id_str(request_id_t req_id);

#endif /* SM_REQUEST_ID_H */
