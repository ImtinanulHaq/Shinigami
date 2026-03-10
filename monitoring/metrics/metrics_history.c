/**
 * @file    metrics_history.c
 * @brief   sparkline_render implementation (only non-inline function).
 */
#include "metrics_types.h"
#include <string.h>
#include <stdio.h>

/* Unicode block characters ▁▂▃▄▅▆▇█ — each is 3 bytes in UTF-8. */
static const char * const BLOCKS[8] = {
    "\xe2\x96\x81",  /* ▁ 1/8 */
    "\xe2\x96\x82",  /* ▂ 2/8 */
    "\xe2\x96\x83",  /* ▃ 3/8 */
    "\xe2\x96\x84",  /* ▄ 4/8 */
    "\xe2\x96\x85",  /* ▅ 5/8 */
    "\xe2\x96\x86",  /* ▆ 6/8 */
    "\xe2\x96\x87",  /* ▇ 7/8 */
    "\xe2\x96\x88",  /* █ 8/8 */
};

void sparkline_render(const sparkline_t *s, char *buf,
                      uint32_t points, size_t buf_len)
{
    if (!s || !buf || buf_len == 0) return;
    buf[0] = '\0';
    if (s->count == 0) return;

    uint32_t avail = s->count < points ? s->count : points;
    float mn = s->min_seen, mx = s->max_seen;
    if (mx <= mn) mx = mn + 1.0f;

    /* Start from (head - avail) going forward */
    uint32_t start = (s->head + SPARKLINE_CAPACITY - avail) % SPARKLINE_CAPACITY;
    size_t pos = 0;
    for (uint32_t i = 0; i < avail; i++) {
        float v = s->values[(start + i) % SPARKLINE_CAPACITY];
        float norm = (v - mn) / (mx - mn);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        int idx = (int)(norm * 7.0f + 0.5f);
        if (idx < 0) idx = 0;
        if (idx > 7) idx = 7;
        const char *blk = BLOCKS[idx];
        size_t blen = 3;  /* UTF-8 block is always 3 bytes */
        if (pos + blen + 1 >= buf_len) break;
        memcpy(buf + pos, blk, blen);
        pos += blen;
    }
    buf[pos] = '\0';
}
