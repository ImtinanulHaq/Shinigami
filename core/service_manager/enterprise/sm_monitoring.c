#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/*
 * sm_monitoring.c - Resource monitoring implementation
 *
 * CPU, memory, and file descriptor tracking for service manager
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
#include <math.h>

#define MAX_SAMPLES 128
#define MEMORY_LEAK_THRESHOLD_GROWTH 1.05  /* 5% growth rate */
#define FD_LEAK_THRESHOLD 10               /* Closed fewer than 10 FDs = potential leak */

typedef struct {
    char service_name[64];
    
    sm_monitoring_sample_t samples[MAX_SAMPLES];
    int sample_count;
    int sample_idx;
    
    sm_monitoring_alert_t last_alert;
    uint64_t total_alerts;
    
    /* Thresholds */
    uint64_t memory_threshold_bytes;
    int fd_threshold;
    
    /* Computed stats */
    uint64_t peak_rss_bytes;
    double peak_cpu_percent;
    int peak_open_fds;
    
    double memory_growth_rate;
    
    time_t monitoring_started_time;
    int initialized;
    
    pthread_mutex_t lock;
} monitoring_context_t;

static monitoring_context_t g_monitoring = {0};

static int count_open_fds(void)
{
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) {
        return -1;
    }
    
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        /* Count numeric entries (FD numbers) */
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

static int parse_proc_stat(double* cpu_percent)
{
    FILE* f = fopen("/proc/self/stat", "r");
    if (!f) {
        return -1;
    }
    
    /* Parse /proc/self/stat format */
    int pid;
    char comm[256];
    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned long flags, minflt, cminflt, majflt, cmajflt;
    unsigned long utime, stime, cutime, cstime;
    long priority, nice, num_threads;
    unsigned long itrealvalue;
    unsigned long starttime, vsize;
    long rss;
    
    int ret = fscanf(f, "%d %255s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %lu %lu %lu %ld",
                    &pid, comm, &state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
                    &flags, &minflt, &cminflt, &majflt, &cmajflt,
                    &utime, &stime, &cutime, &cstime, &priority, &nice, &num_threads, &itrealvalue, &starttime);
    
    fclose(f);
    
    if (ret < 20) {
        return -1;
    }
    
    /* Calculate CPU percentage (very rough estimate) */
    unsigned long total_time = utime + stime;
    *cpu_percent = (total_time * 100.0) / sysconf(_SC_CLK_TCK) / 10.0;  /* / 10 = rough scaling */
    if (*cpu_percent > 100.0) *cpu_percent = 100.0;
    
    (void)vsize;  /* Suppress unused variable warning */
    (void)rss;    /* Suppress unused variable warning */
    
    return 0;
}

static int read_proc_status(uint64_t* rss_bytes, uint64_t* vms_bytes)
{
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) {
        return -1;
    }
    
    char line[256];
    *rss_bytes = 0;
    *vms_bytes = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %lu", rss_bytes) == 1) {
            *rss_bytes *= 1024;  /* Convert KB to bytes */
        } else if (sscanf(line, "VmSize: %lu", vms_bytes) == 1) {
            *vms_bytes *= 1024;  /* Convert KB to bytes */
        }
    }
    
    fclose(f);
    return 0;
}

int sm_monitoring_init(const char* service_name)
{
    if (g_monitoring.initialized) {
        sm_log(SM_LOG_WARN, "monitoring: already initialized");
        return 0;
    }
    
    memset(&g_monitoring, 0, sizeof(g_monitoring));
    
    if (pthread_mutex_init(&g_monitoring.lock, NULL) != 0) {
        sm_log(SM_LOG_ERROR, "monitoring: pthread_mutex_init failed: %s", strerror(errno));
        return -1;
    }
    
    if (service_name) {
        strncpy(g_monitoring.service_name, service_name, sizeof(g_monitoring.service_name) - 1);
    }
    
    /* Default thresholds */
    g_monitoring.memory_threshold_bytes = 512 * 1024 * 1024;  /* 512 MB */
    g_monitoring.fd_threshold = 256;
    
    g_monitoring.monitoring_started_time = time(NULL);
    g_monitoring.initialized = 1;
    
    sm_log(SM_LOG_INFO, "monitoring: initialized for service '%s'", 
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
    
    /* Allocate new sample slot */
    if (g_monitoring.sample_count < MAX_SAMPLES) {
        g_monitoring.sample_count++;
    }
    g_monitoring.sample_idx = (g_monitoring.sample_idx + 1) % MAX_SAMPLES;
    
    sm_monitoring_sample_t* sample = &g_monitoring.samples[g_monitoring.sample_idx];
    memset(sample, 0, sizeof(*sample));
    
    sample->timestamp = time(NULL);
    
    /* Read CPU usage */
    if (parse_proc_stat(&sample->cpu_percent) != 0) {
        sample->cpu_percent = 0;
    }
    
    /* Read memory usage */
    if (read_proc_status(&sample->rss_bytes, &sample->vms_bytes) != 0) {
        sample->rss_bytes = 0;
        sample->vms_bytes = 0;
    }
    
    /* Count open FDs */
    sample->open_fds = count_open_fds();
    if (sample->open_fds < 0) {
        sample->open_fds = 0;
    }
    sample->max_fds = (int)sysconf(_SC_OPEN_MAX);
    
    /* Update peak values */
    if (sample->rss_bytes > g_monitoring.peak_rss_bytes) {
        g_monitoring.peak_rss_bytes = sample->rss_bytes;
    }
    if (sample->cpu_percent > g_monitoring.peak_cpu_percent) {
        g_monitoring.peak_cpu_percent = sample->cpu_percent;
    }
    if (sample->open_fds > g_monitoring.peak_open_fds) {
        g_monitoring.peak_open_fds = sample->open_fds;
    }
    
    /* Detect memory leak (consistent growth) */
    if (g_monitoring.sample_count >= 5) {
        int oldest_idx = (g_monitoring.sample_idx + 1) % MAX_SAMPLES;
        sm_monitoring_sample_t* oldest = &g_monitoring.samples[oldest_idx];
        
        if (oldest->rss_bytes > 0) {
            double growth_ratio = (double)sample->rss_bytes / (double)oldest->rss_bytes;
            if (growth_ratio > MEMORY_LEAK_THRESHOLD_GROWTH) {
                memset(&g_monitoring.last_alert, 0, sizeof(g_monitoring.last_alert));
                g_monitoring.last_alert.alert_type = SM_ALERT_MEMORY_LEAK;
                g_monitoring.last_alert.timestamp = time(NULL);
                g_monitoring.last_alert.value = sample->rss_bytes;
                g_monitoring.last_alert.threshold = oldest->rss_bytes * MEMORY_LEAK_THRESHOLD_GROWTH;
                g_monitoring.total_alerts++;
                
                sm_log(SM_LOG_WARN, "monitoring: MEMORY LEAK detected! "
                       "Growth ratio: %.2f (threshold: %.2f)", growth_ratio, MEMORY_LEAK_THRESHOLD_GROWTH);
            }
        }
    }
    
    /* Check memory threshold */
    if (g_monitoring.memory_threshold_bytes > 0 &&
        sample->rss_bytes > g_monitoring.memory_threshold_bytes * 0.8) {
        memset(&g_monitoring.last_alert, 0, sizeof(g_monitoring.last_alert));
        g_monitoring.last_alert.alert_type = SM_ALERT_HIGH_MEMORY;
        g_monitoring.last_alert.timestamp = time(NULL);
        g_monitoring.last_alert.value = sample->rss_bytes;
        g_monitoring.last_alert.threshold = g_monitoring.memory_threshold_bytes;
        g_monitoring.total_alerts++;
        
        sm_log(SM_LOG_WARN, "monitoring: HIGH MEMORY USAGE! "
               "Current: %lu MB, Threshold: %lu MB",
               sample->rss_bytes / (1024*1024), g_monitoring.memory_threshold_bytes / (1024*1024));
    }
    
    /* Check FD threshold */
    if (g_monitoring.fd_threshold > 0 &&
        sample->open_fds > g_monitoring.fd_threshold * 0.8) {
        memset(&g_monitoring.last_alert, 0, sizeof(g_monitoring.last_alert));
        g_monitoring.last_alert.alert_type = SM_ALERT_HIGH_FD_COUNT;
        g_monitoring.last_alert.timestamp = time(NULL);
        g_monitoring.last_alert.value = sample->open_fds;
        g_monitoring.last_alert.threshold = g_monitoring.fd_threshold;
        g_monitoring.total_alerts++;
        
        sm_log(SM_LOG_WARN, "monitoring: HIGH FD USAGE! "
               "Current: %d, Threshold: %d", sample->open_fds, g_monitoring.fd_threshold);
    }
    
    pthread_mutex_unlock(&g_monitoring.lock);
    
    return 0;
}

int sm_monitoring_set_memory_threshold(uint64_t bytes)
{
    if (bytes > 0 && bytes < 1024*1024) {  /* Minimum 1 MB */
        return -1;
    }
    
    pthread_mutex_lock(&g_monitoring.lock);
    g_monitoring.memory_threshold_bytes = bytes;
    pthread_mutex_unlock(&g_monitoring.lock);
    
    sm_log(SM_LOG_INFO, "monitoring: memory threshold set to %lu MB",
           bytes / (1024*1024));
    return 0;
}

int sm_monitoring_set_fd_threshold(int count)
{
    if (count > 0 && count < 10) {  /* Minimum 10 */
        return -1;
    }
    
    pthread_mutex_lock(&g_monitoring.lock);
    g_monitoring.fd_threshold = count;
    pthread_mutex_unlock(&g_monitoring.lock);
    
    sm_log(SM_LOG_INFO, "monitoring: FD threshold set to %d", count);
    return 0;
}

sm_monitoring_stats_t sm_monitoring_get_stats(void)
{
    sm_monitoring_stats_t stats = {0};
    
    if (!g_monitoring.initialized) {
        return stats;
    }
    
    pthread_mutex_lock(&g_monitoring.lock);
    
    stats.sample_count = g_monitoring.sample_count;
    stats.peak_rss_bytes = g_monitoring.peak_rss_bytes;
    stats.peak_cpu_percent = g_monitoring.peak_cpu_percent;
    stats.peak_open_fds = g_monitoring.peak_open_fds;
    
    if (g_monitoring.sample_count > 0) {
        sm_monitoring_sample_t* latest = &g_monitoring.samples[g_monitoring.sample_idx];
        stats.current_rss_bytes = latest->rss_bytes;
        stats.current_open_fds = latest->open_fds;
        
        /* Calculate averages */
        uint64_t rss_sum = 0;
        double cpu_sum = 0;
        for (int i = 0; i < g_monitoring.sample_count; i++) {
            rss_sum += g_monitoring.samples[i].rss_bytes;
            cpu_sum += g_monitoring.samples[i].cpu_percent;
        }
        stats.avg_rss_bytes = rss_sum / (double)g_monitoring.sample_count;
        stats.avg_cpu_percent = cpu_sum / (double)g_monitoring.sample_count;
    }
    
    stats.memory_growth_rate = g_monitoring.memory_growth_rate;
    stats.is_initialized = 1;
    stats.monitoring_started_time = g_monitoring.monitoring_started_time;
    stats.total_alerts = g_monitoring.total_alerts;
    stats.last_alert = g_monitoring.last_alert.alert_type;
    
    pthread_mutex_unlock(&g_monitoring.lock);
    
    return stats;
}

sm_monitoring_sample_t sm_monitoring_get_last_sample(void)
{
    sm_monitoring_sample_t sample = {0};
    
    if (!g_monitoring.initialized) {
        return sample;
    }
    
    pthread_mutex_lock(&g_monitoring.lock);
    if (g_monitoring.sample_count > 0) {
        sample = g_monitoring.samples[g_monitoring.sample_idx];
    }
    pthread_mutex_unlock(&g_monitoring.lock);
    
    return sample;
}

sm_monitoring_alert_t sm_monitoring_get_last_alert(void)
{
    sm_monitoring_alert_t alert = {0};
    
    if (!g_monitoring.initialized) {
        return alert;
    }
    
    pthread_mutex_lock(&g_monitoring.lock);
    alert = g_monitoring.last_alert;
    pthread_mutex_unlock(&g_monitoring.lock);
    
    return alert;
}

int sm_monitoring_cleanup(void)
{
    if (!g_monitoring.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_monitoring.lock);
    pthread_mutex_unlock(&g_monitoring.lock);
    pthread_mutex_destroy(&g_monitoring.lock);
    
    memset(&g_monitoring, 0, sizeof(g_monitoring));
    
    sm_log(SM_LOG_INFO, "monitoring: cleanup complete");
    return 0;
}
