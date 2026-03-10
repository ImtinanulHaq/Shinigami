/**
 * @file    panel_topbar.c
 * @brief   Top status bar implementation.
 */
#include "panel_topbar.h"
#include "ui_colors.h"
#include "../health/health_score.h"
#include <ncurses.h>
#include <stdio.h>
#include <time.h>

void panel_topbar_render(const mon_snapshot_t *snapshot, int y, int cols)
{
    move(y, 0);
    attron(COLOR_PAIR(COLOR_PAIR_HEADER));
    for (int i = 0; i < cols; i++) addch(' ');

    /* System health */
    int health = health_compute_system_score(snapshot->services, SERVICE_MAX);
    const char *grade = health_score_to_grade(health);
    int color = health_score_to_color(health);

    char health_str[64];
    snprintf(health_str, sizeof(health_str), "Health: %s (%d/100)", grade, health);

    /* CPU / RAM */
    char sysinfo_str[128];
    snprintf(sysinfo_str, sizeof(sysinfo_str),
             "CPU: %.1f%% | RAM: %lu/%lu MB | Load: %.2f",
             snapshot->sysinfo.cpu_total_pct,
             snapshot->sysinfo.ram_used_bytes / 1048576,
             snapshot->sysinfo.ram_total_bytes / 1048576,
             snapshot->sysinfo.load_1);

    /* Timestamp */
    time_t now = time(NULL);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));

    /* Render */
    mvprintw(y, 2, "%s", health_str);
    mvprintw(y, 30, "%s", sysinfo_str);
    mvprintw(y, cols - 12, "%s", time_str);

    attroff(COLOR_PAIR(COLOR_PAIR_HEADER));
}
