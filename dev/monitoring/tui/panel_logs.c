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
#include <time.h>

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

/* Source name colour cycling */
static const int g_src_colors[] = {
    COLOR_PAIR_INFO,
    COLOR_PAIR_GOOD,
    COLOR_PAIR_WARNING,
    COLOR_PAIR_CRITICAL,
    COLOR_PAIR_DEFAULT,
};
#define SRC_COLORS_N 5

/* Circular buffer of log lines */
static char g_lines[MAX_LOG_LINES][LINE_CAPACITY];
static int  g_line_colors[MAX_LOG_LINES];   /* ncurses COLOR_PAIR id */
static int  g_line_src_col[MAX_LOG_LINES];  /* colour for the source tag */
static char g_line_src[MAX_LOG_LINES][24];
static char g_line_time[MAX_LOG_LINES][12]; /* "HH:MM:SS" or "--:--:--" */
static int  g_line_count = 0;

/* ── Extract HH:MM:SS from a log line; fall back to file mtime ────────── */
static void extract_time(const char *line, const struct tm *fallback,
                         char *out, int out_len)
{
    const char *p = line;
    while (*p) {
        if (p[0] >= '0' && p[0] <= '2' &&
            p[1] >= '0' && p[1] <= '9' &&
            p[2] == ':' &&
            p[3] >= '0' && p[3] <= '5' &&
            p[4] >= '0' && p[4] <= '9' &&
            p[5] == ':' &&
            p[6] >= '0' && p[6] <= '5' &&
            p[7] >= '0' && p[7] <= '9') {
            snprintf(out, (size_t)out_len, "%.8s", p);
            return;
        }
        p++;
    }
    /* Fallback: file modification time so timestamps are never dashes */
    if (fallback)
        snprintf(out, (size_t)out_len, "%02d:%02d:%02d",
                 fallback->tm_hour, fallback->tm_min, fallback->tm_sec);
    else {
        strncpy(out, "--:--:--", (size_t)out_len - 1);
        out[out_len - 1] = '\0';
    }
}

/* ── Classify severity from line text ─────────────────────────────────── */
static int classify_color(const char *line)
{
    if (strstr(line, "[CRIT]")  || strstr(line, "CRITICAL") ||
        strstr(line, "[ERROR]") || strstr(line, " ERROR")   ||
        strstr(line, ":ERROR")  || strstr(line, "error:")   ||
        strstr(line, "FAILED")  || strstr(line, "failed")   ||
        strstr(line, "FAULT")   || strstr(line, "fault"))
        return COLOR_PAIR_CRITICAL;

    if (strstr(line, "[WARN]")  || strstr(line, " WARN")    ||
        strstr(line, ":WARN")   || strstr(line, "warning")  ||
        strstr(line, "WARNING") || strstr(line, "TIMEOUT")  ||
        strstr(line, "timeout") || strstr(line, "retry"))
        return COLOR_PAIR_WARNING;

    if (strstr(line, "[INFO]")   || strstr(line, " INFO")    ||
        strstr(line, "started")  || strstr(line, "Starting") ||
        strstr(line, "ready")    || strstr(line, "Ready")    ||
        strstr(line, "OK")       || strstr(line, "success")  ||
        strstr(line, "registered"))
        return COLOR_PAIR_GOOD;

    if (strstr(line, "[DEBUG]") || strstr(line, " DEBUG") || strstr(line, ":DEBUG"))
        return COLOR_PAIR_DEFAULT;

    return COLOR_PAIR_DEFAULT;
}

/* ── Read last `budget` lines from a log file ─────────────────────────── */
static void load_log(const char *path, const char *src_name, int src_col, int budget)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    /* Use file modification time as timestamp fallback for lines with no timestamp */
    struct stat st;
    struct tm  *file_tm = NULL;
    if (stat(path, &st) == 0)
        file_tm = localtime(&st.st_mtime);

    /* Ring buffer to keep last `budget` lines */
    int cap = (budget < 64) ? budget : 64;
    char   ring[64][LINE_CAPACITY];
    int    ring_col[64];
    int    ring_n = 0, ring_head = 0;
    char   linebuf[LINE_CAPACITY];
    char   ring_time[64][12];

    while (fgets(linebuf, sizeof(linebuf), f)) {
        /* Strip trailing newline */
        size_t l = strlen(linebuf);
        while (l > 0 && (linebuf[l-1] == '\n' || linebuf[l-1] == '\r'))
            linebuf[--l] = '\0';
        if (l == 0) continue;

        int idx = ring_head % cap;
        strncpy(ring[idx], linebuf, LINE_CAPACITY - 1);
        ring[idx][LINE_CAPACITY - 1] = '\0';
        ring_col[idx] = classify_color(linebuf);
        extract_time(linebuf, file_tm, ring_time[idx], sizeof(ring_time[idx]));
        ring_head++;
        if (ring_n < cap) ring_n++;
    }
    fclose(f);

    /* Append ordered lines to global buffer */
    int start = ring_head - ring_n;
    for (int i = 0; i < ring_n && g_line_count < MAX_LOG_LINES; i++) {
        int idx = (start + i) % cap;
        strncpy(g_lines[g_line_count],      ring[idx],       LINE_CAPACITY - 1);
        strncpy(g_line_src[g_line_count],   src_name,        23);
        strncpy(g_line_time[g_line_count],  ring_time[idx],  11);
        g_line_colors[g_line_count]  = ring_col[idx];
        g_line_src_col[g_line_count] = src_col;
        g_line_count++;
    }
}

/* ── Main render ──────────────────────────────────────────────────────── */
void panel_logs_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    (void)s;
    int row = y;
    const int max_row = y + h - 1;

    /* ── Header: live refresh clock ── */
    time_t now_t   = time(NULL);
    struct tm *ntm = localtime(&now_t);
    char now_str[12] = "--:--:--";
    if (ntm)
        snprintf(now_str, sizeof(now_str), "%02d:%02d:%02d",
                 ntm->tm_hour, ntm->tm_min, ntm->tm_sec);

    attron(COLOR_PAIR(COLOR_PAIR_BORDER) | A_BOLD);
    mvprintw(row, 0, "|");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_BORDER));

    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvprintw(row, 2, "-- SERVICE LOGS ");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_HEADER));

    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    mvprintw(row, 18, "Dir: %s", LOG_DIR);
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));

    /* Refresh badge right-aligned */
    attron(COLOR_PAIR(COLOR_PAIR_OK) | A_BOLD);
    mvprintw(row, cols - 14, " LIVE %s ", now_str);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_OK));
    row++;

    /* Legend line */
    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    mvprintw(row, 4, "Severity: ");
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    attron(COLOR_PAIR(COLOR_PAIR_GOOD) | A_BOLD);
    printw("INFO");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_GOOD));
    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    printw(" | ");
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    attron(COLOR_PAIR(COLOR_PAIR_WARNING) | A_BOLD);
    printw("WARN");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_WARNING));
    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    printw(" | ");
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    attron(COLOR_PAIR(COLOR_PAIR_CRITICAL) | A_BOLD);
    printw("ERROR");
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_CRITICAL));
    attron(COLOR_PAIR(COLOR_PAIR_DIM));
    printw("  [Up/Down] scroll  [r] refresh  [q] quit");
    attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    row++;

    /* Column header */
    attron(COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD | A_UNDERLINE);
    mvprintw(row++, 2, "%-8s  %-13s  %s", "Time", "Source", "Message");
    attroff(A_BOLD | A_UNDERLINE | COLOR_PAIR(COLOR_PAIR_HEADER));

    if (row <= max_row) {
        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        mvhline(row++, 0, '-', cols);
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
    }

    /* ── Load logs ── */
    g_line_count = 0;
    int budget = (max_row - row + 1);
    if (budget < 4) budget = 4;
    if (budget > 60) budget = 60;

    int src_col_idx = 0;
    for (int i = 0; g_log_files[i]; i++) {
        const char *path = g_log_files[i];
        /* Extract base src name */
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        char src[24];
        strncpy(src, base, 23); src[23] = '\0';
        char *dot = strrchr(src, '.');
        if (dot) *dot = '\0';

        int sc = g_src_colors[src_col_idx % SRC_COLORS_N];
        src_col_idx++;
        load_log(path, src, sc, budget);
    }

    /* ── No logs message ── */
    if (g_line_count == 0) {
        if (row <= max_row) {
            attron(COLOR_PAIR(COLOR_PAIR_WARNING));
            mvprintw(row++, 4, "(no log files found in %s)", LOG_DIR);
            attroff(COLOR_PAIR(COLOR_PAIR_WARNING));
        }
        return;
    }

    /* ── Apply scroll ── */
    int visible = max_row - row + 1;
    int total   = g_line_count;
    int start   = total - visible - scroll;
    if (start < 0) start = 0;
    if (start >= total) start = (total > 1) ? total - 1 : 0;

    /* ── Render log lines ── */
    for (int i = start; i < total && row <= max_row; i++) {
        int src_c  = g_line_src_col[i];
        int line_c = g_line_colors[i];

        /* Timestamp */
        attron(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        mvprintw(row, 2, "%-8s", g_line_time[i]);
        attroff(COLOR_PAIR(COLOR_PAIR_DEFAULT));
        mvaddch(row, 10, '|');

        /* Source tag */
        attron(COLOR_PAIR(src_c) | A_BOLD);
        mvprintw(row, 12, "%-13s", g_line_src[i]);
        attroff(A_BOLD | COLOR_PAIR(src_c));
        mvaddch(row, 25, '|');

        /* Line text with severity colour */
        int text_col  = line_c;
        int text_attr = (line_c == COLOR_PAIR_CRITICAL) ? A_BOLD : A_NORMAL;
        attron(COLOR_PAIR(text_col) | text_attr);
        int max_len = cols - 27;
        if (max_len < 1) max_len = 1;
        mvprintw(row, 27, "%.*s", max_len, g_lines[i]);
        attroff(text_attr | COLOR_PAIR(text_col));

        row++;
    }

    /* ── Scroll indicator ── */
    if (row <= max_row) {
        attron(COLOR_PAIR(COLOR_PAIR_BORDER));
        mvhline(row, 0, '-', cols);
        attroff(COLOR_PAIR(COLOR_PAIR_BORDER));
        attron(COLOR_PAIR(COLOR_PAIR_DIM));
        if (total > visible)
            mvprintw(row, 2, " lines %d-%d of %d  [Up/Down to scroll] ",
                     start + 1, start + visible, total);
        else
            mvprintw(row, 2, " %d line%s  [all visible] ",
                     total, total == 1 ? "" : "s");
        attroff(COLOR_PAIR(COLOR_PAIR_DIM));
    }
}

