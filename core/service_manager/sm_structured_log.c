#define _POSIX_C_SOURCE 200809L

/*
 * sm_structured_log.c - Structured logging (JSON)
 */

#include "sm_structured_log.h"
#include "sm_logging.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

void sm_log_json(sm_log_level_t level, const char* component,
                 const char* message, const char* extra_json)
{
    char buf[1024];
    time_t now = time(NULL);
    
    snprintf(buf, sizeof(buf),
             "{\"ts\":%ld,\"level\":\"%s\",\"component\":\"%s\",\"msg\":\"%s\"%s%s}",
             now,
             level == 0 ? "DEBUG" : 
             level == 1 ? "INFO" : 
             level == 2 ? "WARN" : 
             level == 3 ? "ERROR" : "CRIT",
             component ? component : "",
             message ? message : "",
             extra_json ? "," : "",
             extra_json ? extra_json : "");
    
    sm_log(level, "%s", buf);  /* Log as regular message to preserve to file */
}

void sm_log_service_event(const char* action, const char* service_name,
                          int service_pid, int result, const char* details)
{
    char json[512];
    
    snprintf(json, sizeof(json),
             "\"action\":\"%s\",\"service\":\"%s\",\"pid\":%d,\"result\":%d,\"details\":\"%s\"",
             action ? action : "",
             service_name ? service_name : "",
             service_pid,
             result,
             details ? details : "");
    
    sm_log_json(SM_LOG_INFO, "service", "service_event", json);
}

void sm_log_ratelimit_event(int pid, const char* service, int tokens_left)
{
    char json[256];
    
    snprintf(json, sizeof(json),
             "\"pid\":%d,\"service\":\"%s\",\"tokens\":%d",
             pid, service ? service : "", tokens_left);
    
    sm_log_json(SM_LOG_WARN, "ratelimit", "limit_hit", json);
}
