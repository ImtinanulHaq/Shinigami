/**
 * @file    alert_notify.c
 * @brief   Alert notification implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include "alert_notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *g_log_file = NULL;
static char g_webhook_url[512] = {0};

int alert_notify_init(const char *log_path, const char *webhook_url)
{
    /* Open log file */
    const char *path = log_path ? log_path : "/tmp/monitord_alerts.log";
    g_log_file = fopen(path, "a");
    if (!g_log_file) {
        perror("alert_notify_init: fopen");
        return -1;
    }

    /* Store webhook URL */
    if (webhook_url && webhook_url[0]) {
        strncpy(g_webhook_url, webhook_url, sizeof(g_webhook_url) - 1);
    }

    return 0;
}

void alert_notify_shutdown(void)
{
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

int alert_notify(const alert_record_t *alert)
{
    /* Terminal bell for CRITICAL alerts */
    if (alert->severity == ALERT_SEV_CRIT) {
        fprintf(stderr, "\a");  /* ASCII bell */
        fflush(stderr);
    }

    /* Log to file */
    if (g_log_file) {
        time_t now = time(NULL);
        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
                 localtime(&now));

        const char *severity_str = (alert->severity == ALERT_SEV_CRIT) ? "CRIT" :
                                    (alert->severity == ALERT_SEV_WARN)  ? "WARN" :
                                                                            "INFO";

        fprintf(g_log_file, "[%s] [%s] [%s] %s (occurrences: %u)\n",
                timebuf, severity_str, alert->component,
                alert->condition, alert->occurrences);
        fflush(g_log_file);
    }

    /* Webhook POST (optional) */
    if (g_webhook_url[0]) {
        /* TODO: Implement webhook POST using libcurl or simple HTTP */
        /* For now, just log that we would have sent it */
        fprintf(stderr, "[alert_notify] Would POST to %s: %s\n",
                g_webhook_url, alert->condition);
    }

    return 0;
}
