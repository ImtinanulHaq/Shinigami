/**
 * @file    metrics_export_prometheus.h
 * @brief   Prometheus text format exposition for all middleware metrics.
 *
 * Builds the complete /metrics response body from the current mon_snapshot_t.
 * Output conforms to Prometheus text format version 0.0.4:
 *   https://prometheus.io/docs/instrumenting/exposition_formats/
 *
 * @thread_safety  metrics_prometheus_render() reads a *copy* of the snapshot
 *                 and is therefore safe to call from the HTTP thread.
 */
#pragma once

#include <stddef.h>
#include "../protocol/monitor_ipc_protocol.h"

/**
 * @brief  Render the complete Prometheus /metrics text body into @p buf.
 * @param  snap      Pointer to the current metrics snapshot (read-only).
 * @param  buf       Output buffer.
 * @param  buf_len   Size of buf in bytes.
 * @return Number of bytes written (excl. NUL), or -1 if buf was too small.
 *
 * @thread_safety  Reads snap but does not modify it.  buf is written
 *                 exclusively by this call — caller must synchronise buf.
 */
uint32_t prometheus_render(const mon_snapshot_t *snap,
                           char *buf, uint32_t bufsiz);

/**
 * @brief  Render the /health JSON endpoint body.
 * @param  snap      Current snapshot.
 * @param  buf       Output buffer.
 * @param  buf_len   Size of buf.
 * @return Bytes written, or -1 if buf too small.
 */
uint32_t health_json_render(const mon_snapshot_t *snap,
                            char *buf, uint32_t bufsiz);
