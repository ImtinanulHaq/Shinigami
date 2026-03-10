/**
 * @file collector_processes.c
 * @brief Process collector - monitord + TUI PIDs from /proc
 */

#include "collector_processes.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

static collector_t g_processes_collector;
static unsigned long wd_prev_utime = 0, wd_prev_stime = 0;
static unsigned long long wd_prev_total_time = 0;

/* Find process PIDs by name */
static pid_t find_process_by_name(const char *name) {
    DIR *proc = opendir("/proc");
    if (!proc) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
            
        char cmdline_path[256];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
        
        FILE *f = fopen(cmdline_path, "r");
        if (f) {
            char cmdline[256] = {0};
            if (fread(cmdline, 1, sizeof(cmdline) - 1, f) > 0) {
                if (strstr(cmdline, name)) {
                    closedir(proc);
                    fclose(f);
                    return atoi(entry->d_name);
                }
            }
            fclose(f);
        }
    }
    closedir(proc);
    return 0;
}

/* Read process stats */
static int read_process_stats(pid_t pid, uint64_t *rss_bytes, float *cpu_pct, 
                               unsigned long *prev_utime, unsigned long *prev_stime,
                               unsigned long long *prev_total) {
    char stat_path[64];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    
    FILE *f = fopen(stat_path, "r");
    if (!f) return -1;
    
    unsigned long rss_pages = 0;
    unsigned long utime = 0, stime = 0;
    
    if (fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %lu",
               &utime, &stime, &rss_pages) >= 3) {
        *rss_bytes = rss_pages * sysconf(_SC_PAGESIZE);
        
        /* Calculate CPU % from delta */
        unsigned long total_time = utime + stime;
        if (*prev_total > 0) {
            FILE *uptime_f = fopen("/proc/uptime", "r");
            if (uptime_f) {
                double uptime_now;
                if (fscanf(uptime_f, "%lf", &uptime_now) == 1) {
                    unsigned long long total_now = (unsigned long long)(uptime_now * sysconf(_SC_CLK_TCK));
                    unsigned long long time_delta = total_now - *prev_total;
                    unsigned long proc_delta = total_time - (*prev_utime + *prev_stime);
                    
                    if (time_delta > 0) {
                        *cpu_pct = 100.0f * (float)proc_delta / (float)time_delta;
                    }
                    *prev_total = total_now;
                }
                fclose(uptime_f);
            }
        } else {
            FILE *uptime_f = fopen("/proc/uptime", "r");
            if (uptime_f) {
                double uptime_now;
                if (fscanf(uptime_f, "%lf", &uptime_now) == 1) {
                    *prev_total = (unsigned long long)(uptime_now * sysconf(_SC_CLK_TCK));
                }
                fclose(uptime_f);
            }
            *cpu_pct = 0.0f;
        }
        *prev_utime = utime;
        *prev_stime = stime;
    }
    
    fclose(f);
    return 0;
}

static int processes_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

static int processes_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    
    /* Find watchdog/monitoring related processes */
    pthread_rwlock_wrlock(&state->lock_services);
    
    /* Update Watchdog service entry */
    pid_t wd_pid = find_process_by_name("middleware_monitord");
    if (wd_pid > 0) {
        uint64_t rss = 0;
        float cpu = 0.0f;
        read_process_stats(wd_pid, &rss, &cpu, &wd_prev_utime, &wd_prev_stime, &wd_prev_total_time);
        
        strncpy(state->services[1].name, "Watchdog", sizeof(state->services[1].name) - 1);
        state->services[1].pid = wd_pid;
        state->services[1].running = 1;
        state->services[1].health_score = 100;
        state->services[1].cpu_pct = cpu;
        state->services[1].rss_bytes = rss;
    }
    
    pthread_rwlock_unlock(&state->lock_services);
    return 0;
}

static int processes_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_processes_register(void)
{
    memset(&g_processes_collector, 0, sizeof(g_processes_collector));
    strncpy(g_processes_collector.name, "processes", sizeof(g_processes_collector.name)-1);
    g_processes_collector.connect = processes_connect;
    g_processes_collector.tick = processes_tick;
    g_processes_collector.disconnect = processes_disconnect;
    return collector_register(&g_processes_collector);
}
