/**
 * @file    trace_renderer.c
 * @brief   Waterfall view renderer implementation.
 */
#include "trace_renderer.h"
#include <stdio.h>
#include <string.h>

uint32_t trace_render_waterfall(const trace_record_t *trace, char *buf,
                                 uint32_t size)
{
    uint32_t written = 0;

    for (uint32_t i = 0; i < trace->span_count && written < size - 256; i++) {
        const trace_span_t *span = &trace->spans[i];
        uint64_t duration_us = span->exit_us - span->enter_us;
        double duration_ms = (double)duration_us / 1000.0;

        const char *status_str = (span->status == 0) ? "OK" : 
                                 (span->status == 1) ? "SLOW" : "ERROR";

        written += snprintf(buf + written, size - written,
                            "  %s: %.1fms [%s] %s\n",
                            span->component,
                            duration_ms,
                            status_str,
                            span->annotation);
    }

    return written;
}

void trace_render_timing_bar(uint64_t start_us, uint64_t end_us,
                              uint64_t total_us, uint32_t width, char *buf)
{
    if (total_us == 0 || width == 0) {
        buf[0] = '\0';
        return;
    }

    uint32_t start_pos = (uint32_t)((start_us * width) / total_us);
    uint32_t end_pos = (uint32_t)((end_us * width) / total_us);

    if (start_pos >= width) start_pos = width - 1;
    if (end_pos >= width) end_pos = width - 1;
    if (end_pos <= start_pos) end_pos = start_pos + 1;

    memset(buf, ' ', width);
    for (uint32_t i = start_pos; i < end_pos && i < width; i++) {
        buf[i] = '#';  /* Use ASCII instead of UTF-8 */
    }
    buf[width] = '\0';
}
