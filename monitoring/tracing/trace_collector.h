/**
 * @file    trace_collector.h
 * @brief   Distributed tracing span collector.
 *
 * Collects tracing spans from middleware components via shared memory or IPC.
 * Each span represents a request flow through the system with entry/exit
 * timestamps, service boundaries, and parent/child relationships.
 */
#pragma once

#include <stdint.h>
#include "../protocol/monitor_ipc_protocol.h"

/* Use types from protocol: trace_span_t, trace_record_t */

/**
 * @brief  Initialize the trace collector.
 * @return 0 on success, -1 on failure.
 */
int trace_collector_init(void);

/**
 * @brief  Shutdown the trace collector.
 */
void trace_collector_shutdown(void);

/**
 * @brief  Collect spans from all middleware components.
 * @param  out    Output buffer for spans.
 * @param  max    Maximum number of spans to collect.
 * @return Number of spans written.
 */
uint32_t trace_collector_collect(trace_span_t *out, uint32_t max);
