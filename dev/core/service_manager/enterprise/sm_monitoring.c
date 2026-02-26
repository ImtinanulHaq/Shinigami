#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * sm_monitoring.c - CPU, memory, and file descriptor tracking.
 */

#include "../enterprise/sm_monitoring.h"
#include "../observability/sm_logging.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#define MAX_SAMPLES                   128
#define MEMORY_LEAK_THRESHOLD_GROWTH  1.05
#define MEMORY_LEAK_MIN_SAMPLES       5

/* ── State ───────────────────────────────────────────────────────────────────── */

typedef struct {
    char service_name[64];

    sm_monitoring_sample_t samples[MAX_SAMPLES];
    int sample_count;
    int sample_idx;

    sm_monitoring_alert_t last_alert;
    uint64_t total_alerts;

    uint64_t memory_threshold_bytes;
    int      fd_threshold;

    uint64_t peak_rss_bytes;
    double   peak_cpu_percent;
    int      peak_open_fds;

    double memory_growth_rate;

    time_t monitoring_started_time;
    int    initialized;

    pthread_mutex_t lock;
} monitoring_context_t;

static monitoring_context_t g_monitoring = {0};

/* ── /proc helpers ───────────────────────────────────────────────────────────── */

static int count_open_fds(void)
{
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) return -1;

    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9')
            count++;
    }
    closedir(dir);

    /* subtract the fd consumed by opendir() itself */
    return count > 0 ? count - 1 : 0;
}

static int parse_proc_stat(double* cpu_percent)
{
    FILE* f = fopen("/proc/self/stat", "r");
    if (!f) return -1;

    int           pid;
    char          comm[256];
    char          state;
    int           ppid, pgrp, session, tty_nr, tpgid;
    unsigned long flags;
    unsigned long minflt, cminflt, majflt, cmajflt;
    unsigned long utime, stime;
    long          cutime, cstime, priority, nice, num_threads;
    unsigned long itrealvalue, starttime;

    int ret = fscanf(f,
        "%d %255s %c %d %d %d %d %d "
        "%lu %lu %lu %lu %lu "
        "%lu %lu "
        "%ld %ld %ld %ld %ld "
        "%lu %lu",
        &pid, comm, &state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
        &flags, &minflt, &cminflt, &majflt, &cmajflt,
        &utime, &stime,
        &cutime, &cstime, &priority, &nice, &num_threads,
        &itrealvalue, &starttime);

    fclose(f);
    if (ret < 15) return -1;

    unsigned long total_jiffies = utime + stime;
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    *cpu_percent = ((double)total_jiffies / (double)clk_tck) * 10.0;
    if (*cpu_percent > 100.0) *cpu_percent = 100.0;

    return 0;
}

static int read_proc_status(uint64_t* rss_bytes, uint64_t* vms_bytes)
{
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;

    char     line[256];
    uint64_t rss_kb = 0;
    uint64_t vms_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "VmRSS: %lu",  &rss_kb);
        sscanf(line, "VmSize: %lu", &vms_kb);
    }
    fclose(f);

    *rss_bytes = rss_kb * 1024;
    *vms_bytes = vms_kb * 1024;
    return 0;
}

/* ── Alert helper ────────────────────────────────────────────────────────────── */

/* Higher-priority alert wins; never let a low-priority one overwrite it. */
static void set_alert(sm_alert_type_t type, uint64_t value, uint64_t threshold)
{
    if (g_monitoring.last_alert.alert_type != SM_ALERT_NONE &&
        type <= g_monitoring.last_alert.alert_type) {
        return;
    }
    memset(&g_monitoring.last_alert, 0, sizeof(g_monitoring.last_alert));
    g_monitoring.last_alert.alert_type = type;
    g_monitoring.last_alert.timestamp  = time(NULL);
    g_monitoring.last_alert.value      = value;
    g_monitoring.last_alert.threshold  = threshold;
    g_monitoring.total_alerts++;
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

int sm_monitoring_init(const char* service_name)
{
    if (g_monitoring.initialized) {
        sm_log(SM_LOG_WARN, "monitoring: already initialized");
        return 0;
    }

    memset(&g_monitoring, 0, sizeof(g_monitoring));

    if (pthread_mutex_init(&g_monitoring.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "monitoring: mutex init failed: %s", strerror(errno));
        return -1;
    }

    if (service_name)
        strncpy(g_monitoring.service_name, service_name,
                sizeof(g_monitoring.service_name) - 1);

    g_monitoring.memory_threshold_bytes  = 512ULL * 1024 * 1024;
    g_monitoring.fd_threshold            = 256;
    g_monitoring.monitoring_started_time = time(NULL);
    g_monitoring.initialized             = 1;

    sm_log(SM_LOG_INFO, "monitoring: initialized for '%s'",
           service_name ? service_name : "unknown");
    return 0;
}

int sm_monitoring_sample(void)
{
    if (!g_monitoring.initialized) {
        sm_log(SM_LOG_WARN, "monitoring: not initialized");
        return -1;
    }

    pthread_mutex_lock(&g_monitoring.lock);

    /* Fill slots sequentially first, then wrap as a ring buffer. */
    if (g_monitoring.sample_count < MAX_SAMPLES) {
        g_monitoring.sample_idx = g_monitoring.sample_count;
        g_monitoring.sample_count++;
    } else {
        g_monitoring.sample_idx = (g_monitoring.sample_idx + 1) % MAX_SAMPLES;
    }

    sm_monitoring_sample_t* sample = &g_monitoring.samples[g_monitoring.sample_idx];
    memset(sample, 0, sizeof(*sample));
    sample->timestamp = time(NULL);

    if (parse_proc_stat(&sample->cpu_percent) != 0)
        sample->cpu_percent = 0.0;

    if (read_proc_status(&sample->rss_bytes, &sample->vms_bytes) != 0) {
        sample->rss_bytes = 0;
        sample->vms_bytes = 0;
    }

    sample->open_fds = count_open_fds();
    if (sample->open_fds < 0) sample->open_fds = 0;
    sample->max_fds = (int)sysconf(_SC_OPEN_MAX);

    if (sample->rss_bytes   > g_monitoring.peak_rss_bytes)
        g_monitoring.peak_rss_bytes   = sample->rss_bytes;
    if (sample->cpu_percent > g_monitoring.peak_cpu_percent)
        g_monitoring.peak_cpu_percent = sample->cpu_percent;
    if (sample->open_fds    > g_monitoring.peak_open_fds)
        g_monitoring.peak_open_fds    = sample->open_fds;

    /* Clear per-sample alert before running checks. */
    g_monitoring.last_alert.alert_type = SM_ALERT_NONE;

    /* Compare newest sample against oldest slot in the window. */
    if (g_monitoring.sample_count >= MEMORY_LEAK_MIN_SAMPLES) {
        int oldest_idx = (g_monitoring.sample_count < MAX_SAMPLES)
                         ? 0
                         : (g_monitoring.sample_idx + 1) % MAX_SAMPLES;

        sm_monitoring_sample_t* oldest = &g_monitoring.samples[oldest_idx];
        if (oldest->rss_bytes > 0 && sample->rss_bytes > oldest->rss_bytes) {
            double ratio = (double)sample->rss_bytes / (double)oldest->rss_bytes;
            g_monitoring.memory_growth_rate = ratio;

            if (ratio > MEMORY_LEAK_THRESHOLD_GROWTH) {
                set_alert(SM_ALERT_MEMORY_LEAK,
                          sample->rss_bytes,
                          (uint64_t)(oldest->rss_bytes * MEMORY_LEAK_THRESHOLD_GROWTH));
                sm_log(SM_LOG_WARN,
                       "monitoring: possible memory leak — growth ratio %.2f", ratio);
            }
        }
    }

    if (g_monitoring.memory_threshold_bytes > 0 &&
        sample->rss_bytes > g_monitoring.memory_threshold_bytes * 8 / 10) {
        set_alert(SM_ALERT_HIGH_MEMORY,
                  sample->rss_bytes,
                  g_monitoring.memory_threshold_bytes);
        sm_log(SM_LOG_WARN, "monitoring: high memory — %lu MB / %lu MB",
               (unsigned long)(sample->rss_bytes / (1024*1024)),
               (unsigned long)(g_monitoring.memory_threshold_bytes / (1024*1024)));
    }

    if (g_monitoring.fd_threshold > 0 &&
        sample->open_fds > g_monitoring.fd_threshold * 8 / 10) {
        set_alert(SM_ALERT_HIGH_FD_COUNT,
                  (uint64_t)sample->open_fds,
                  (uint64_t)g_monitoring.fd_threshold);
        sm_log(SM_LOG_WARN, "monitoring: high FD usage — %d / %d",
               sample->open_fds, g_monitoring.fd_threshold);
    }

    pthread_mutex_unlock(&g_monitoring.lock);
    return 0;
}

/* 0 disables the check. Non-zero minimum is 1 MB. */
int sm_monitoring_set_memory_threshold(uint64_t bytes)
{
    if (bytes > 0 && bytes < 1024 * 1024) {
        sm_log(SM_LOG_ERROR, "monitoring: memory threshold too small (%lu bytes)",
               (unsigned long)bytes);
        return -1;
    }
    pthread_mutex_lock(&g_monitoring.lock);
    g_monitoring.memory_threshold_bytes = bytes;
    pthread_mutex_unlock(&g_monitoring.lock);

    sm_log(SM_LOG_INFO, "monitoring: memory threshold = %lu MB",
           (unsigned long)(bytes / (1024*1024)));
    return 0;
}

/* 0 disables the check. Non-zero minimum is 10. */
int sm_monitoring_set_fd_threshold(int count)
{
    if (count > 0 && count < 10) {
        sm_log(SM_LOG_ERROR, "monitoring: FD threshold too small (%d)", count);
        return -1;
    }
    pthread_mutex_lock(&g_monitoring.lock);
    g_monitoring.fd_threshold = count;
    pthread_mutex_unlock(&g_monitoring.lock);

    sm_log(SM_LOG_INFO, "monitoring: FD threshold = %d", count);
    return 0;
}

sm_monitoring_stats_t sm_monitoring_get_stats(void)
{
    sm_monitoring_stats_t stats = {0};
    if (!g_monitoring.initialized) return stats;

    pthread_mutex_lock(&g_monitoring.lock);

    stats.sample_count     = g_monitoring.sample_count;
    stats.peak_rss_bytes   = g_monitoring.peak_rss_bytes;
    stats.peak_cpu_percent = g_monitoring.peak_cpu_percent;
    stats.peak_open_fds    = g_monitoring.peak_open_fds;

    if (g_monitoring.sample_count > 0) {
        sm_monitoring_sample_t* latest =
            &g_monitoring.samples[g_monitoring.sample_idx];
        stats.current_rss_bytes = latest->rss_bytes;
        stats.current_open_fds  = latest->open_fds;

        uint64_t rss_sum = 0;
        double   cpu_sum = 0.0;
        int      n       = g_monitoring.sample_count;
        for (int i = 0; i < n; i++) {
            rss_sum += g_monitoring.samples[i].rss_bytes;
            cpu_sum += g_monitoring.samples[i].cpu_percent;
        }
        stats.avg_rss_bytes   = (double)rss_sum / (double)n;
        stats.avg_cpu_percent = cpu_sum          / (double)n;
    }

    stats.memory_growth_rate      = g_monitoring.memory_growth_rate;
    stats.is_initialized          = 1;
    stats.monitoring_started_time = g_monitoring.monitoring_started_time;
    stats.total_alerts            = g_monitoring.total_alerts;
    stats.last_alert              = g_monitoring.last_alert.alert_type;

    pthread_mutex_unlock(&g_monitoring.lock);
    return stats;
}

sm_monitoring_sample_t sm_monitoring_get_last_sample(void)
{
    sm_monitoring_sample_t sample = {0};
    if (!g_monitoring.initialized) return sample;

    pthread_mutex_lock(&g_monitoring.lock);
    if (g_monitoring.sample_count > 0)
        sample = g_monitoring.samples[g_monitoring.sample_idx];
    pthread_mutex_unlock(&g_monitoring.lock);

    return sample;
}

sm_monitoring_alert_t sm_monitoring_get_last_alert(void)
{
    sm_monitoring_alert_t alert = {0};
    if (!g_monitoring.initialized) return alert;

    pthread_mutex_lock(&g_monitoring.lock);
    alert = g_monitoring.last_alert;
    pthread_mutex_unlock(&g_monitoring.lock);

    return alert;
}

int sm_monitoring_cleanup(void)
{
    if (!g_monitoring.initialized) return 0;

    sm_log(SM_LOG_INFO, "monitoring: cleanup");

    pthread_mutex_lock(&g_monitoring.lock);
    g_monitoring.initialized = 0;
    pthread_mutex_unlock(&g_monitoring.lock);

    pthread_mutex_destroy(&g_monitoring.lock);
    memset(&g_monitoring, 0, sizeof(g_monitoring));
    return 0;
}