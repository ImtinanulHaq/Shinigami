/**
 * @file    panel_topbar.c
 * @brief   Top status bar — responsive left / centre / right layout.
 */
#include "panel_topbar.h"
#include "ui_colors.h"
#include "../health/health_score.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void panel_topbar_render(const mon_snapshot_t *snapshot, int y, int cols)
{
    /* ── Fill background ─────────────────────────────────────────── */
    move(y, 0);
    attron(COLOR_PAIR(COLOR_PAIR_HEADER));
    for (int i = 0; i < cols; i++) addch(' ');

    /* ── Left: Health grade ──────────────────────────────────────── */
    int health = health_compute_system_score(snapshot->services, SERVICE_MAX);
    const char *grade = health_score_to_grade(health);
    char left[64];
    snprintf(left, sizeof(left), " Health: %s (%d/100)", grade, health);

    /* ── Centre: CPU / RAM / Load ────────────────────────────────── */
    char centre[128];
    snprintf(centre, sizeof(centre),
             "CPU: %.1f%%  RAM: %lu/%lu MB  Load: %.2f %.2f %.2f",
             (double)snapshot->sysinfo.cpu_total_pct,
             snapshot->sysinfo.ram_used_bytes  / (1024UL * 1024UL),
             snapshot->sysinfo.ram_total_bytes / (1024UL * 1024UL),
             (double)snapshot->sysinfo.load_1,
             (double)snapshot->sysinfo.load_5,
             (double)snapshot->sysinfo.load_15);

    /* ── Right: Timestamp ────────────────────────────────────────── */
    time_t now = time(NULL);
    char right[24];
    strftime(right, sizeof(right), "%H:%M:%S ", localtime(&now));

    /* ── Layout: left | (centre, dynamically centred) | right ───── */
    int left_len   = (int)strlen(left);
    int centre_len = (int)strlen(centre);
    int right_len  = (int)strlen(right);

    /* Centre position: try to keep it mid-screen; if no room, skip it */
    int centre_x = (cols - centre_len) / 2;
    /* Ensure it doesn't overlap the left or right sections */
    if (centre_x < left_len + 2) centre_x = left_len + 2;
    int right_x = cols - right_len;
    int centre_end = centre_x + centre_len;

    mvprintw(y, 0, "%s", left);

    if (centre_end < right_x - 1)          /* fits without overlap */
        mvprintw(y, centre_x, "%s", centre);

    if (right_x > left_len + 2)
        mvprintw(y, right_x, "%s", right);

    attroff(COLOR_PAIR(COLOR_PAIR_HEADER));
}

