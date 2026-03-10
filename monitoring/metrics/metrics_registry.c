/**
 * @file    metrics_registry.c
 * @brief   Implementation of the named metric registry.
 */
#include "metrics_registry.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static metrics_registry_t g_registry;
static pthread_mutex_t    g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_init = 0;

int metrics_registry_init(void)
{
    pthread_mutex_lock(&g_lock);
    memset(&g_registry, 0, sizeof(g_registry));
    g_init = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void metrics_registry_destroy(void)
{
    pthread_mutex_lock(&g_lock);
    memset(&g_registry, 0, sizeof(g_registry));
    g_init = 0;
    pthread_mutex_unlock(&g_lock);
}

static int register_metric(const char *name, const char *subsystem,
                            const char *help, const char *labels,
                            metric_type_t type, void *ptr)
{
    if (!name || !ptr) return -1;
    pthread_mutex_lock(&g_lock);
    if (g_registry.count >= METRICS_REGISTRY_MAX) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    /* Duplicate check */
    for (uint32_t i = 0; i < g_registry.count; i++) {
        if (strncmp(g_registry.entries[i].name, name, 79) == 0) {
            pthread_mutex_unlock(&g_lock);
            return -1;
        }
    }
    metric_entry_t *e = &g_registry.entries[g_registry.count++];
    snprintf(e->name,      sizeof(e->name),      "%s", name ? name : "");
    snprintf(e->subsystem, sizeof(e->subsystem),  "%s", subsystem ? subsystem : "");
    snprintf(e->help,      sizeof(e->help),       "%s", help ? help : "");
    snprintf(e->labels,    sizeof(e->labels),     "%s", labels ? labels : "");
    e->type = type;
    e->ptr  = ptr;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int metrics_register_gauge(const char *n, const char *sub, const char *help,
                            const char *labels, gauge_t *g)
{ return register_metric(n, sub, help, labels, METRIC_TYPE_GAUGE, g); }

int metrics_register_counter(const char *n, const char *sub, const char *help,
                              const char *labels, counter_t *c)
{ return register_metric(n, sub, help, labels, METRIC_TYPE_COUNTER, c); }

int metrics_register_histogram(const char *n, const char *sub, const char *help,
                                const char *labels, histogram_t *h)
{ return register_metric(n, sub, help, labels, METRIC_TYPE_HISTOGRAM, h); }

const metric_entry_t *metrics_lookup(const char *name)
{
    if (!name) return NULL;
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 0; i < g_registry.count; i++) {
        if (strncmp(g_registry.entries[i].name, name, 79) == 0) {
            const metric_entry_t *e = &g_registry.entries[i];
            pthread_mutex_unlock(&g_lock);
            return e;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

void metrics_iterate(void (*cb)(const metric_entry_t *, void *), void *ud)
{
    if (!cb) return;
    pthread_mutex_lock(&g_lock);
    uint32_t n = g_registry.count;
    /* Copy pointers so we can release lock before callbacks */
    const metric_entry_t *ptrs[METRICS_REGISTRY_MAX];
    for (uint32_t i = 0; i < n; i++) ptrs[i] = &g_registry.entries[i];
    pthread_mutex_unlock(&g_lock);
    for (uint32_t i = 0; i < n; i++) cb(ptrs[i], ud);
}

uint32_t metrics_count(void)
{
    pthread_mutex_lock(&g_lock);
    uint32_t n = g_registry.count;
    pthread_mutex_unlock(&g_lock);
    return n;
}
