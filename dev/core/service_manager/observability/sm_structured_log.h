#ifndef SM_STRUCTURED_LOG_H
#define SM_STRUCTURED_LOG_H

/*
 * sm_structured_log.h - Structured/JSON logging
 *
 * Log messages in JSON format for easy parsing by log aggregation tools.
 */

#include "../observability/sm_logging.h"

/* Log a structured event as JSON */
void sm_log_json(sm_log_level_t level, const char* component,
                 const char* message, const char* extra_json);

/* Log a service event */
void sm_log_service_event(const char* action, const char* service_name,
                          int service_pid, int result, const char* details);

/* Log a rate limit event */
void sm_log_ratelimit_event(int pid, const char* service, int tokens_left);

#endif /* SM_STRUCTURED_LOG_H */
