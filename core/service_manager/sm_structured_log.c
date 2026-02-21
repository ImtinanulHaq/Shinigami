#define _POSIX_C_SOURCE 200809L

/*
 * sm_structured_log.c - Structured logging (JSON)
 */

#include "sm_structured_log.h"
#include "sm_logging.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static void json_escape_string(const char* input, char* output, size_t outlen)
{
    if (!input || !output || outlen == 0) {
        if (output) output[0] = '\0';
        return;
    }
    
    size_t pos = 0;
    for (int i = 0; input[i] && pos < outlen - 1; i++) {
        char c = input[i];
        if (c == '"') {
            if (pos + 2 < outlen) {
                output[pos++] = '\\';
                output[pos++] = '"';
            }
        } else if (c == '\\') {
            if (pos + 2 < outlen) {
                output[pos++] = '\\';
                output[pos++] = '\\';
            }
        } else if (c == '\n') {
            if (pos + 2 < outlen) {
                output[pos++] = '\\';
                output[pos++] = 'n';
            }
        } else if (c == '\r') {
            if (pos + 2 < outlen) {
                output[pos++] = '\\';
                output[pos++] = 'r';
            }
        } else if (c == '\t') {
            if (pos + 2 < outlen) {
                output[pos++] = '\\';
                output[pos++] = 't';
            }
        } else if ((unsigned char)c < 32) {
            /* Control characters - skip them */
            continue;
        } else {
            output[pos++] = c;
        }
    }
    output[pos] = '\0';
}

void sm_log_json(sm_log_level_t level, const char* component,
                 const char* message, const char* extra_json)
{
    char buf[1024];
    char escaped_component[128] = "";
    char escaped_message[256] = "";
    time_t now = time(NULL);
    
    if (component) json_escape_string(component, escaped_component, sizeof(escaped_component));
    if (message) json_escape_string(message, escaped_message, sizeof(escaped_message));
    
    snprintf(buf, sizeof(buf),
             "{\"ts\":%ld,\"level\":\"%s\",\"component\":\"%s\",\"msg\":\"%s\"%s%s}",
             now,
             level == 0 ? "DEBUG" : 
             level == 1 ? "INFO" : 
             level == 2 ? "WARN" : 
             level == 3 ? "ERROR" : "CRIT",
             escaped_component,
             escaped_message,
             extra_json ? "," : "",
             extra_json ? extra_json : "");
    
    sm_log(level, "%s", buf);  /* Log as regular message to preserve to file */
}

void sm_log_service_event(const char* action, const char* service_name,
                          int service_pid, int result, const char* details)
{
    char json[512];
    char escaped_action[64] = "";
    char escaped_service[64] = "";
    char escaped_details[128] = "";
    
    if (action) json_escape_string(action, escaped_action, sizeof(escaped_action));
    if (service_name) json_escape_string(service_name, escaped_service, sizeof(escaped_service));
    if (details) json_escape_string(details, escaped_details, sizeof(escaped_details));
    
    snprintf(json, sizeof(json),
             "\"action\":\"%s\",\"service\":\"%s\",\"pid\":%d,\"result\":%d,\"details\":\"%s\"",
             escaped_action,
             escaped_service,
             service_pid,
             result,
             escaped_details);
    
    sm_log_json(SM_LOG_INFO, "service", "service_event", json);
}

void sm_log_ratelimit_event(int pid, const char* service, int tokens_left)
{
    char json[256];
    char escaped_service[64] = "";
    
    if (service) json_escape_string(service, escaped_service, sizeof(escaped_service));
    
    snprintf(json, sizeof(json),
             "\"pid\":%d,\"service\":\"%s\",\"tokens\":%d",
             pid, escaped_service, tokens_left);
    
    sm_log_json(SM_LOG_WARN, "ratelimit", "limit_hit", json);
}
