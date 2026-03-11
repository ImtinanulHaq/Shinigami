/**
 * @file    panel_logs.c
 * @brief   Logs panel — tail service log files from /var/log/middleware/.
 *
 * Reads the last N lines of each log file and displays them colour-coded
 * by severity level prefix ([ERROR], [WARN], etc.).
 */
#include "panel_logs.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LOG_DIR        "/var/log/middleware"
#define MAX_LOG_LINES  512
#define LINE_CAPACITY  256

static const char *g_log_files[] = {
    LOG_DIR "/sm_daemon.log",
    LOG_DIR "/audio_service.log",
    LOG_DIR "/camera_service.log",
    LOG_DIR "/gpio_service.log",
    LOG_DIR "/sensor_service.log",
    LOG_DIR "/monitord.log",
    NULL,
};

/* Simple circular buffer of log lines */
static char  g_lines[MAX_LOG_LINES][LINE_CAPACITY];
static int   g_line_count = 0;
static char  g_line_src[MAX_LOG_LINES][24];   /* "audio_service" etc. */

static void classify_line(const char *src, const char *line,
                           int idx, int *color_out)
{
    strncpy(g_lines[idx], line, LINE_CAPACITY - 1);
    g_lines[idx][LINE_CAPACITY - 1] = '\0';
    strncpy(g_line_src[idx], src, 23);
    g_line_src[idx][23] = '\0';

    /* Strip trailing newline */
    size_t l = strlen(g_lines[idx]);
    while (l > 0 && (g_lines[idx][l-1] == '\n' || g_lines[idx][l-1] == '\r'))
        g_lines[idx][--l] = '\0';

    if (strstr(line, "[ERROR]") || strstr(line, "ERROR") || strstr(line, "error"))
        *color_out = COLOR_PAIR_CRITICAL;
    else if (strstr(line, "[WARN]") || strstr(line, "WARN") || strstr(line, "warning"))
        *color_out = COLOR_PAIR_WARNING;
    else if (strstr(line, "[INFO]") || strstr(line, "info:"))
        *color_out = COLOR_PAIR_GOOD;
    else
        *color_out = COLOR_PAIR_DEFAULT;
}

/* Read last ~(budget) lines of a log file appending to g_lines. */
static void load_log(const char *path, int budget)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    /* Extract base name */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char src[24];
    strncpy(src, base, 23);
    src[23] = '\0';
    /* Strip ".log" */
    char *dot = strrchr(src, '.');
    if (dot) *dot = '\0';

    /* Collect last `budget` lines via a ring */
    char   ring[64][LINE_CAPACITY];
    int    ring_colors[64];
    int    ring_n = 0, ring_head = 0;
    char   linebuf[LINE_CAPACITY];
    int    cap = budget < 64 ? budget : 64;

    while (fgets(linebuf, sizeof(linebuf), f)) {
        int col = COLOR_PAIR_DEFAULT;
        int idx = ring_head % cap;
        classify_line(src, linebuf, 0, &col);
        strncpy(ring[idx], g_lines[0], LINE_CAPACITY - 1);
        ring_colors[idx] = col;
        ring_head++;
        if (ring_n < cap) ring_n++;
    }
    fclose(f);

    /* Append to g_lines in order */
    int start = ring_head - ring_n;
    for (int i = 0; i < ring_n && g_line_count < MAX_LOG_LINES; i++) {
        int idx = (start + i) % cap;
        strncpy(g_lines[g_line_count], ring[idx], LINE_CAPACITY - 1);
        strncpy(g_line_src[g_line_count], src, 23);
        g_line_count++;
        (void)ring_colors;
    }
}

void panel_logs_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    (void)s;
    int row = y;
    const int max_row = y + h - 1;

    /* Header */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "── Service Logs (live tail) ");
    mvhline(row, 29, ACS_HLINE, cols - 31);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    if (row <= max_row) {
        attron(COLOR_PAIR(COLOR_PAIR_INFO));
        mvprintw(row++, 4, "Source: %s  |  Up/Down to scroll  |  'r' to refresh", LOG_DIR);
        attroff(COLOR_PAIR(COLOR_PAIR_INFO));
    }
    row++;

    /* Load logs */
    g_line_count = 0;
    int budget = ((max_row - row) + 1);
    if (budget < 4) budget = 4;
    if (budget > 48) budget = 48;

    for (int i = 0; g_log_files[i]; i++)
        load_log(g_log_files[i], budget);

    if (g_line_count == 0) {
        if (row <= max_row)
            mvprintw(row++, 4, "(no log files found in %s)", LOG_DIR);
        return;
    }

    /* Apply scroll */
    int total = g_line_count;
    int start = total - (max_row - row + 1) - scroll;
    if (start < 0) start = 0;
    if (start >= total) start = total > 1 ? total - 1 : 0;

    for (int i = start; i < total && row <= max_row; i++) {
        /* Colour code */
        int col = COLOR_PAIR_DEFAULT;
        const char *line = g_lines[i];
        if (strstr(line, "ERROR") || strstr(line, "error") || strstr(line, "CRIT"))
            col = COLOR_PAIR_CRITICAL;
        else if (strstr(line, "WARN") || strstr(line, "warn"))
            col = COLOR_PAIR_WARNING;
        else if (strstr(line, "OK") || strstr(line, "ready") || strstr(line, "started"))
            col = COLOR_PAIR_GOOD;

        attron(COLOR_PAIR(COLOR_PAIR_INFO));
        mvprintw(row, 2, "%-14s ", g_line_src[i]);
        attroff(COLOR_PAIR(COLOR_PAIR_INFO));

        attron(COLOR_PAIR(col));
        /* Print up to cols-18 chars of the line */
        int max_len = cols - 18;
        if (max_len < 1) max_len = 1;
        mvprintw(row, 18, "%.*s", max_len, line);
        attroff(COLOR_PAIR(col));
        row++;
    }
}
