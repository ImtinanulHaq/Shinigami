/**
 * @file    trace_store.h
 * @brief   Circular buffer for storing completed trace spans.
 *
 * Stores the last 1000 completed traces (each trace may have multiple spans).
 * Traces are grouped by trace_id and sorted by timestamp for waterfall rendering.
 */
#pragma once

#include <stdint.h>
#include <pthread.h>
#include "../protocol/monitor_ipc_protocol.h"

#define TRACE_STORE_MAX  1000   /**< Maximum completed traces to store */

/* Use trace_record_t from protocol */

typedef struct {
    trace_record_t     traces[TRACE_STORE_MAX];
    uint32_t           head;         /**< Write index in circular buffer */
    uint32_t           count;        /**< Number of valid traces */
    pthread_rwlock_t   lock;
} trace_store_t;

/**
 * @brief  Initialize the trace store.
 * @param  store  Trace store struct.
 * @return 0 on success.
 */
int trace_store_init(trace_store_t *store);

/**
 * @brief  Destroy the trace store.
 * @param  store  Trace store struct.
 */
void trace_store_destroy(trace_store_t *store);

/**
 * @brief  Add a complete trace to the store.
 * @param  store  Trace store struct.
 * @param  trace  Trace record to add.
 */
void trace_store_add_trace(trace_store_t *store, const trace_record_t *trace);

/**
 * @brief  Get the most recent N completed traces.
 * @param  store  Trace store struct.
 * @param  out    Output buffer.
 * @param  max    Maximum number of traces to retrieve.
 * @return Number of traces written.
 */
uint32_t trace_store_get_recent(trace_store_t *store, trace_record_t *out,
                                 uint32_t max);

/**
 * @brief  Find a specific trace by trace_id.
 * @param  store     Trace store struct.
 * @param  trace_id  Trace ID to find.
 * @param  out       Output buffer (single trace).
 * @return 1 if found, 0 if not found.
 */
int trace_store_find(trace_store_t *store, uint32_t trace_id,
                     trace_record_t *out);
