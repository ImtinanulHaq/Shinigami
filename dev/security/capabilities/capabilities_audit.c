#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "capabilities_audit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <syslog.h>

static struct {
    int enabled;
    int fd;
    pthread_mutex_t lock;
} audit_state = {
    .enabled = 0,
    .fd = -1,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static __thread char audit_buffer[512];

int capabilities_audit_init(const char* log_path)
{
    pthread_mutex_lock(&audit_state.lock);

    if (audit_state.enabled) {
        pthread_mutex_unlock(&audit_state.lock);
        return 0;
    }

    int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) {
        pthread_mutex_unlock(&audit_state.lock);
        syslog(LOG_ERR, "[cap_audit] failed to open audit log %s: %s",
               log_path, strerror(errno));
        return -1;
    }

    audit_state.fd = fd;
    audit_state.enabled = 1;

    pthread_mutex_unlock(&audit_state.lock);

    syslog(LOG_INFO, "[cap_audit] capability auditing enabled: %s", log_path);
    return 0;
}

void capabilities_audit_cleanup(void)
{
    pthread_mutex_lock(&audit_state.lock);

    if (audit_state.fd >= 0) {
        close(audit_state.fd);
        audit_state.fd = -1;
    }
    audit_state.enabled = 0;

    pthread_mutex_unlock(&audit_state.lock);
}

void capabilities_audit_log(const char* action, cap_flags_t caps, const char* details)
{
    pthread_mutex_lock(&audit_state.lock);

    if (!audit_state.enabled || audit_state.fd < 0) {
        pthread_mutex_unlock(&audit_state.lock);
        return;
    }

    time_t now = time(NULL);
    int len = snprintf(audit_buffer, sizeof(audit_buffer),
        "[%ld] CAP_%s: flags=0x%x pid=%d uid=%d %s\n",
        now, action, caps, getpid(), getuid(), details ? details : "");

    if (len < 0 || len >= (int)sizeof(audit_buffer)) {
        pthread_mutex_unlock(&audit_state.lock);
        syslog(LOG_ERR, "[cap_audit] buffer overflow prevented");
        return;
    }

    ssize_t written = write(audit_state.fd, audit_buffer, (size_t)len);
    pthread_mutex_unlock(&audit_state.lock);

    if (written != len) {
        syslog(LOG_CRIT, "[cap_audit] AUDIT WRITE FAILED: %s (action=%s caps=0x%x)",
               strerror(errno), action, caps);
    }
}

int capabilities_audit_is_enabled(void)
{
    pthread_mutex_lock(&audit_state.lock);
    int enabled = audit_state.enabled;
    pthread_mutex_unlock(&audit_state.lock);
    return enabled;
}

int capabilities_audit_rotate(const char* new_log_path)
{
    if (!new_log_path) return -1;

    int new_fd = open(new_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (new_fd < 0) {
        syslog(LOG_ERR, "[cap_audit] rotate: failed to open %s: %s",
               new_log_path, strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&audit_state.lock);

    int old_fd = audit_state.fd;
    audit_state.fd = new_fd;

    pthread_mutex_unlock(&audit_state.lock);

    if (old_fd >= 0) {
        close(old_fd);
    }

    syslog(LOG_INFO, "[cap_audit] audit log rotated to %s", new_log_path);
    return 0;
}
