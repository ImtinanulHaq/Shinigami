/**
 * @file    panel_hal.c
 * @brief   HAL devices panel — audio / camera / sensor / GPIO details.
 */
#include "panel_hal.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static const char *hal_state_str(uint8_t state)
{
    switch (state) {
    case 0: return "CLOSED";
    case 1: return "OPEN  ";
    case 2: return "ACTIVE";
    case 3: return "ERROR ";
    default: return "UNKN  ";
    }
}

static int hal_state_color(uint8_t state)
{
    switch (state) {
    case 2: return COLOR_PAIR_GOOD;
    case 3: return COLOR_PAIR_CRITICAL;
    case 1: return COLOR_PAIR_WARNING;
    default: return COLOR_PAIR_DEFAULT;
    }
}

static const char *hal_type_str(hal_type_t t)
{
    switch (t) {
    case HAL_TYPE_AUDIO:  return "AUDIO ";
    case HAL_TYPE_CAMERA: return "CAMERA";
    case HAL_TYPE_SENSOR: return "SENSOR";
    case HAL_TYPE_GPIO:   return "GPIO  ";
    default:              return "??????";
    }
}

void panel_hal_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;

    /* Section header */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "-- HAL Devices (%u) ", s->hal_count);
    mvhline(row, 22, ACS_HLINE, cols - 24);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    if (s->hal_count == 0) {
        if (row <= max_row)
            mvprintw(row++, 4, "(no HAL devices registered)");
        return;
    }

    /* Summary line */
    uint64_t tot_rd = 0, tot_wr = 0, tot_err = 0;
    for (uint32_t i = 0; i < s->hal_count; i++) {
        tot_rd  += s->hal[i].bytes_read;
        tot_wr  += s->hal[i].bytes_written;
        tot_err += s->hal[i].error_count;
    }
    if (row <= max_row) {
        mvprintw(row++, 4, "Total read: %.2f MB  written: %.2f MB  errors: %llu",
                 tot_rd  / (1024.0 * 1024.0),
                 tot_wr  / (1024.0 * 1024.0),
                 (unsigned long long)tot_err);
    }
    row++;

    int shown = 0;
    for (uint32_t i = 0; i < s->hal_count && i < HAL_MAX_DEVICES; i++) {
        const hal_metrics_t *d = &s->hal[i];
        if (!d->device_name[0]) continue;
        if (shown++ < scroll) continue;
        if (row + 5 > max_row) break;

        int sc = hal_state_color(d->state);

        /* Device header */
        attron(COLOR_PAIR(sc) | A_BOLD);
        mvprintw(row, 2, "[%s] %-16s  State:%-6s  Read:%.2f MB  Write:%.2f MB  Errors:%llu  Lat:%uus",
                 hal_type_str(d->type), d->device_name,
                 hal_state_str(d->state),
                 d->bytes_read  / (1024.0 * 1024.0),
                 d->bytes_written / (1024.0 * 1024.0),
                 (unsigned long long)d->error_count, d->lat_us);
        attroff(A_BOLD | COLOR_PAIR(sc));
        row++;

        /* Type-specific details */
        attron(COLOR_PAIR(COLOR_PAIR_INFO));
        switch (d->type) {
        case HAL_TYPE_AUDIO:
            mvprintw(row++, 6,
                     "rate:%uHz  ch:%u  bits:%u  period:%u  buf:%u  underruns:%u  overruns:%u",
                     d->sample_rate, d->channels, d->bit_depth,
                     d->period_frames, d->buffer_frames,
                     d->underruns, d->overruns);
            break;
        case HAL_TYPE_CAMERA:
            mvprintw(row++, 6,
                     "%ux%u@%ufps  fmt:%-8s  queued:%u  captured:%llu  dropped:%llu  gaps:%u",
                     d->width, d->height, d->fps, d->pixel_fmt,
                     d->bufs_queued,
                     (unsigned long long)d->frames_captured,
                     (unsigned long long)d->frames_dropped,
                     d->gaps);
            break;
        case HAL_TYPE_SENSOR:
            mvprintw(row++, 6,
                     "type:%-12s  rate:%uHz  scale:%.4f  xyz:(%.4f, %.4f, %.4f)",
                     d->sensor_type, d->sample_rate_hz, (double)d->scale,
                     (double)d->x, (double)d->y, (double)d->z);
            break;
        case HAL_TYPE_GPIO:
            mvprintw(row++, 6, "pins:%u", d->num_pins);
            if (d->num_pins > 0 && row <= max_row) {
                int px = 6;
                mvprintw(row, px, "states:");
                px += 8;
                for (uint32_t p = 0; p < d->num_pins && p < 8 && row <= max_row; p++) {
                    if (px + 16 > cols - 2) break;
                    mvprintw(row, px, " %-10s", d->pin_states[p]);
                    px += 12;
                }
                row++;
            }
            break;
        default:
            row++;
            break;
        }
        attroff(COLOR_PAIR(COLOR_PAIR_INFO));
        mvhline(row++, 4, ACS_HLINE, cols - 6);
    }
}
