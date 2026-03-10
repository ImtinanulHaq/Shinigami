/**
 * @file    metrics_registry.h
 * @brief   Central registry: register any named metric, look it up by name.
 *
 * The registry is a global singleton in monitord.  It is not used in the TUI
 * (which reads from the snapshot struct directly).
 *
 * @thread_safety  Registry lookup/register are protected by an internal mutex.
 *                 Registered metric pointers remain valid for the process lifetime.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "metrics_types.h"

#define METRICS_REGISTRY_MAX  512

typedef enum {
    METRIC_TYPE_GAUGE     = 0,
    METRIC_TYPE_COUNTER   = 1,
    METRIC_TYPE_HISTOGRAM = 2,
    METRIC_TYPE_SPARKLINE = 3,
} metric_type_t;

typedef struct {
    char         name[80];
    char         subsystem[32];
    char         help[128];
    metric_type_t type;
    void        *ptr;    /**< Points to the underlying gauge_t / counter_t / etc. */
    char         labels[128]; /**< Comma-separated "key=value" labels for Prometheus. */
} metric_entry_t;

typedef struct {
    metric_entry_t entries[METRICS_REGISTRY_MAX];
    uint32_t       count;
    /* Internal mutex held only during register/lookup — not during value reads. */
    /* pthread_mutex_t lock; — defined in .c */
} metrics_registry_t;

/**
 * @brief  Initialise the global metrics registry.
 * @return 0 on success, -1 on failure.
 */
int metrics_registry_init(void);

/**
 * @brief  Destroy the registry and release resources.
 */
void metrics_registry_destroy(void);

/**
 * @brief  Register a gauge metric.
 * @param  name       Fully-qualified metric name (e.g. "cpu_total_pct").
 * @param  subsystem  Logical subsystem (e.g. "sysinfo").
 * @param  help       Human-readable description.
 * @param  labels     Prometheus labels string (e.g. "service=\"audio\"").
 * @param  g          Pointer to the gauge_t (owned by caller, must outlive registry).
 * @return 0 on success, -1 if registry is full or name already registered.
 */
int metrics_register_gauge(const char *name, const char *subsystem,
                            const char *help, const char *labels, gauge_t *g);

/**
 * @brief  Register a counter metric.
 * @thread_safety  Safe to call from any thread.
 */
int metrics_register_counter(const char *name, const char *subsystem,
                              const char *help, const char *labels, counter_t *c);

/**
 * @brief  Register a histogram metric.
 * @thread_safety  Safe to call from any thread.
 */
int metrics_register_histogram(const char *name, const char *subsystem,
                                const char *help, const char *labels, histogram_t *h);

/**
 * @brief  Look up a metric by exact name.
 * @return Pointer to entry, or NULL if not found.
 */
const metric_entry_t *metrics_lookup(const char *name);

/**
 * @brief  Iterate all registered metrics.
 * @param  cb       Callback invoked once per entry (must not call register/lookup).
 * @param  userdata Passed through to callback unchanged.
 */
void metrics_iterate(void (*cb)(const metric_entry_t *e, void *userdata),
                     void *userdata);

/**
 * @brief  Return count of registered metrics.
 */
uint32_t metrics_count(void);
