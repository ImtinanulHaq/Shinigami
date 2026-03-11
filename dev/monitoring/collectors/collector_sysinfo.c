/**
 * @file collector_sysinfo.c
 * @brief Sysinfo collector - CPU, RAM, swap, load from /proc
 */

#include "collector_sysinfo.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <time.h>

static collector_t g_sysinfo_collector;
static unsigned long long prev_total = 0, prev_idle = 0;

static int sysinfo_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* Prime CPU delta counters so the first tick gives a real short-term
     * measurement instead of the average-since-boot (which reads ~100%
     * on busy machines). */
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            if (sscanf(line, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
                prev_total = user + nice + system + idle + iowait + irq + softirq + steal;
                prev_idle  = idle;
            }
        }
        fclose(f);
    }
    return 0;
}

static int sysinfo_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    
    pthread_rwlock_wrlock(&state->lock_sysinfo);
    
    /* Get CPU count */
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs > 0 && nprocs <= 16) {
        state->sysinfo.num_cores = (uint8_t)nprocs;
    }
    
    /* Read /proc/stat for CPU usage */
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            if (sscanf(line, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
                unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
                unsigned long long total_delta = total - prev_total;
                unsigned long long idle_delta = idle - prev_idle;
                
                if (total_delta > 0) {
                    state->sysinfo.cpu_total_pct = 100.0f * (float)(total_delta - idle_delta) / (float)total_delta;
                }
                prev_total = total;
                prev_idle = idle;
            }
        }
        fclose(f);
    }
    
    /* Read /proc/meminfo for memory usage */
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        uint64_t mem_total_kb = 0, mem_available_kb = 0;
        uint64_t swap_total_kb = 0, swap_free_kb = 0;
        
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %lu kB", &mem_total_kb) == 1) {
                state->sysinfo.ram_total_bytes = mem_total_kb * 1024;
            } else if (sscanf(line, "MemAvailable: %lu kB", &mem_available_kb) == 1) {
                if (state->sysinfo.ram_total_bytes > 0) {
                    state->sysinfo.ram_used_bytes = state->sysinfo.ram_total_bytes - (mem_available_kb * 1024);
                }
            } else if (sscanf(line, "SwapTotal: %lu kB", &swap_total_kb) == 1) {
                state->sysinfo.swap_total_bytes = swap_total_kb * 1024;
            } else if (sscanf(line, "SwapFree: %lu kB", &swap_free_kb) == 1) {
                state->sysinfo.swap_used_bytes = (swap_total_kb - swap_free_kb) * 1024;
            }
        }
        fclose(f);
    }
    
    /* Read /proc/loadavg */
    f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%f %f %f", &state->sysinfo.load_1, &state->sysinfo.load_5, &state->sysinfo.load_15) != 3) {
            /* Handle error silently */
        }
        fclose(f);
    }
    
    /* Get uptime */
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        state->sysinfo.uptime_s = si.uptime;
    }
    
    /* Update timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    state->sysinfo.ts_ms = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
    
    pthread_rwlock_unlock(&state->lock_sysinfo);
    return 0;
}

static int sysinfo_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_sysinfo_register(void)
{
    memset(&g_sysinfo_collector, 0, sizeof(g_sysinfo_collector));
    strncpy(g_sysinfo_collector.name, "sysinfo", sizeof(g_sysinfo_collector.name)-1);
    g_sysinfo_collector.connect = sysinfo_connect;
    g_sysinfo_collector.tick = sysinfo_tick;
    g_sysinfo_collector.disconnect = sysinfo_disconnect;
    return collector_register(&g_sysinfo_collector);
}
