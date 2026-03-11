/**
 * @file    panel_alerts.c
 * @brief   Alerts panel — active and recent alerts with severity.
 */
#include "panel_alerts.h"
#include "ui_colors.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *sev_label(alert_severity_t s)
{
    switch (s) {
    case ALERT_SEV_INFO: return "INFO";
    case ALERT_SEV_WARN: return "WARN";
    case ALERT_SEV_CRIT: return "CRIT";
    default:             return "????";
    }
}

static int sev_color(alert_severity_t s)
{
    switch (s) {
    case ALERT_SEV_INFO: return COLOR_PAIR_INFO;
    case ALERT_SEV_WARN: return COLOR_PAIR_WARNING;
    case ALERT_SEV_CRIT: return COLOR_PAIR_CRITICAL;
    default:             return COLOR_PAIR_DEFAULT;
    }
}

void panel_alerts_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;

    /* Header + summary */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "── Alerts ");
    mvhline(row, 12, ACS_HLINE, cols - 14);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;

    if (row <= max_row) {
        mvprintw(row, 4, "Total: %u  |  ", s->alert_count);

        attron(COLOR_PAIR(s->alerts_warn ? COLOR_PAIR_WARNING : COLOR_PAIR_GOOD));
        printw("WARN: %u", s->alerts_warn);
        attroff(COLOR_PAIR(s->alerts_warn ? COLOR_PAIR_WARNING : COLOR_PAIR_GOOD));

        printw("  |  ");

        attron(COLOR_PAIR(s->alerts_crit ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD));
        printw("CRIT: %u", s->alerts_crit);
        attroff(COLOR_PAIR(s->alerts_crit ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD));
        row++;
    }
    row++;

    if (s->alert_count == 0) {
        if (row <= max_row) {
            attron(COLOR_PAIR(COLOR_PAIR_GOOD));
            mvprintw(row++, 4, "  ✓  No active alerts");
            attroff(COLOR_PAIR(COLOR_PAIR_GOOD));
        }
        return;
    }

    /* Column header */
    if (row <= max_row) {
        attron(A_BOLD);
        mvprintw(row++, 4, "%-4s %-5s %-16s %-24s %-14s %-14s  Count  Stat",
                 "Sev", "Act", "Component", "Condition", "Current", "Threshold");
        attroff(A_BOLD);
        if (row <= max_row) mvhline(row++, 4, ACS_HLINE, cols - 6);
    }

    /* Sort: CRIT first, WARN second, INFO last; active first */
    /* Simple pass — show CRIT active, then WARN active, then others */
    int pass;
    int shown = 0;
    for (pass = 0; pass < 3 && row + 3 <= max_row; pass++) {
        alert_severity_t want_sev = (pass == 0) ? ALERT_SEV_CRIT
                                  : (pass == 1) ? ALERT_SEV_WARN
                                  :               ALERT_SEV_INFO;
        for (uint32_t i = 0; i < s->alert_count && i < ALERT_STORE_MAX; i++) {
            const alert_record_t *a = &s->alerts[i];
            if (a->severity != want_sev) continue;
            if (!a->active && pass < 2) continue;   /* first two passes: active only */
            if (shown++ < scroll) continue;
            if (row + 3 > max_row) break;

            int col = sev_color(a->severity);

            /* Main alert line */
            attron(COLOR_PAIR(col) | (a->severity == ALERT_SEV_CRIT ? A_BOLD : 0));
            mvprintw(row, 4, "%-4s %-5s %-16s %-24s %-14s %-14s  %-6u %s",
                     sev_label(a->severity),
                     a->active ? "●ACT " : "○past",
                     a->component, a->condition,
                     a->current_value, a->threshold,
                     a->occurrences,
                     a->active ? "ACTIVE" : "CLEAR ");
            attroff((a->severity == ALERT_SEV_CRIT ? A_BOLD : 0) | COLOR_PAIR(col));
            row++;

            /* Suggestion line */
            if (a->suggestion[0] && row <= max_row) {
                attron(COLOR_PAIR(COLOR_PAIR_INFO));
                mvprintw(row++, 8, "→ %s", a->suggestion);
                attroff(COLOR_PAIR(COLOR_PAIR_INFO));
            }

            if (row <= max_row)
                mvhline(row++, 6, ACS_HLINE, cols - 8);
        }
    }
}
