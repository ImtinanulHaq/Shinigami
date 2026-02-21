#ifndef SM_AUDIT_H
#define SM_AUDIT_H

/*
 * sm_audit.h - Audit logging for compliance and forensics
 *
 * Separate audit trail of all service management operations.
 */

#include <time.h>
#include <sys/types.h>

typedef enum {
    AUDIT_REGISTER = 1,
    AUDIT_UNREGISTER = 2,
    AUDIT_HEARTBEAT = 3,
    AUDIT_LOOKUP = 4,
    AUDIT_CRASH = 5,
    AUDIT_RESTART = 6,
    AUDIT_AUTH_FAIL = 7,
} audit_action_t;

typedef struct {
    time_t           timestamp;
    audit_action_t   action;
    char             service_name[64];
    pid_t            service_pid;
    pid_t            actor_pid;
    uid_t            actor_uid;
    int              result;  /* 0=success, <0=error code */
    char             details[256];
} audit_entry_t;

/* Initialize audit logging */
int sm_audit_init(void);

/* Log an audit event */
void sm_audit_log(audit_action_t action, const char* service, 
                  pid_t svc_pid, pid_t actor_pid, uid_t uid, 
                  int result, const char* details);

/* Get audit log file path */
const char* sm_audit_get_log_path(void);

/* Cleanup */
void sm_audit_cleanup(void);

#endif /* SM_AUDIT_H */
