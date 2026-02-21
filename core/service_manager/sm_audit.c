#define _POSIX_C_SOURCE 200809L

/*
 * sm_audit.c - Audit logging
 */

#include "sm_audit.h"
#include "sm_logging.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#define AUDIT_LOG_FILE "/var/log/servicemanager-audit.log"
#define AUDIT_LOG_MAX_SIZE (10 * 1024 * 1024)  /* 10 MB */

static int audit_fd = -1;
static pthread_mutex_t audit_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t last_rotation_check = 0;

static void audit_rotate_if_needed(void)
{
    time_t now = time(NULL);
    /* Check rotation at most once per minute */
    if (now - last_rotation_check < 60) return;
    last_rotation_check = now;
    
    struct stat st;
    if (stat(AUDIT_LOG_FILE, &st) < 0) return;
    
    if (st.st_size <= AUDIT_LOG_MAX_SIZE) return;
    
    /* File too large, rotate it */
    char backup[256];
    snprintf(backup, sizeof(backup), "%s.%ld", AUDIT_LOG_FILE, (long)now);
    
    close(audit_fd);
    audit_fd = -1;
    
    rename(AUDIT_LOG_FILE, backup);
    
    /* Reopen the log file (will be created fresh) */
    audit_fd = open(AUDIT_LOG_FILE, 
                    O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
    
    sm_log(SM_LOG_INFO, "audit: rotated log to %s", backup);
}

static const char* action_name(audit_action_t a)
{
    switch (a) {
        case AUDIT_REGISTER:   return "REGISTER";
        case AUDIT_UNREGISTER: return "UNREGISTER";
        case AUDIT_HEARTBEAT:  return "HEARTBEAT";
        case AUDIT_LOOKUP:     return "LOOKUP";
        case AUDIT_CRASH:      return "CRASH";
        case AUDIT_RESTART:    return "RESTART";
        case AUDIT_AUTH_FAIL:  return "AUTH_FAIL";
        default:               return "UNKNOWN";
    }
}

int sm_audit_init(void)
{
    audit_fd = open(AUDIT_LOG_FILE, 
                    O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
    if (audit_fd < 0) {
        sm_log(SM_LOG_WARN, "audit: cannot open %s", AUDIT_LOG_FILE);
        return -1;
    }
    sm_log(SM_LOG_INFO, "audit: initialized");
    return 0;
}

void sm_audit_log(audit_action_t action, const char* service,
                  pid_t svc_pid, pid_t actor_pid, uid_t uid,
                  int result, const char* details)
{
    if (audit_fd < 0) return;
    
    char buf[512];
    time_t now = time(NULL);
    
    snprintf(buf, sizeof(buf),
             "%ld|%s|%s|%d|%d|%d|%d|%s\n",
             now, action_name(action), service ? service : "", 
             (int)svc_pid, (int)actor_pid, (int)uid, result,
             details ? details : "");
    
    pthread_mutex_lock(&audit_mutex);
    audit_rotate_if_needed();
    if (audit_fd >= 0) {
        ssize_t n = write(audit_fd, buf, strlen(buf));
        (void)n;  /* suppress unused return value warning */
    }
    pthread_mutex_unlock(&audit_mutex);
    
    sm_log(SM_LOG_INFO, "audit: %s service=%s result=%d uid=%d",
           action_name(action), service, result, (int)uid);
}

const char* sm_audit_get_log_path(void)
{
    return AUDIT_LOG_FILE;
}

void sm_audit_cleanup(void)
{
    if (audit_fd >= 0) {
        close(audit_fd);
        audit_fd = -1;
    }
}
