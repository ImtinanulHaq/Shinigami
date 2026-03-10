/**
 * @file    trace_renderer.h
 * @brief   Waterfall view renderer for distributed traces.
 *
 * Renders a visual waterfall view of a trace, showing spans hierarchically
 * with timing bars. Example output:
 *
 *   ┌─ audio_service::capture (2.3ms)
 *   │  ├─ hal::read (1.8ms)
 *   │  └─ buffer::copy (0.5ms)
 *   └─ ringbuf::write (0.3ms)
 */
#pragma once

#include <stdint.h>
#include "trace_store.h"

/**
 * @brief  Render a trace as a waterfall view into a text buffer.
 * @param  trace  Completed trace to render.
 * @param  buf    Output text buffer.
 * @param  size   Size of output buffer.
 * @return Number of bytes written.
 */
uint32_t trace_render_waterfall(const trace_record_t *trace, char *buf,
                                 uint32_t size);

/**
 * @brief  Render a timing bar for ncurses display.
 * @param  start_us   Start time in microseconds.
 * @param  end_us     End time in microseconds.
 * @param  total_us   Total trace duration in microseconds.
 * @param  width      Display width in characters.
 * @param  buf        Output buffer (must be at least width+1 bytes).
 */
void trace_render_timing_bar(uint64_t start_us, uint64_t end_us,
                              uint64_t total_us, uint32_t width, char *buf);
