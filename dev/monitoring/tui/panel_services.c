/**
 * @file    panel_services.c
 * @brief   Services panel — per-service metrics, watchdog, security flags.
 */
#include "panel_services.h"
#include "ui_colors.h"
#include "../health/health_score.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */
static void sec_flag(int y, int x, const char *label, int ok)
{
    attron(COLOR_PAIR(ok ? COLOR_PAIR_GOOD : COLOR_PAIR_CRITICAL));
    mvprintw(y, x, "%s:%s", label, ok ? "✓" : "✗");
    attroff(COLOR_PAIR(ok ? COLOR_PAIR_GOOD : COLOR_PAIR_CRITICAL));
}

static void draw_bar(int y, int x, int width, float pct, int color)
{
    if (width <= 0) return;
    int filled = (int)(pct / 100.0f * width);
    if (filled > width) filled = width;
    attron(COLOR_PAIR(color));
    for (int i = 0; i < width; i++)
        mvaddch(y, x + i, i < filled ? ACS_BLOCK : '.');
    attroff(COLOR_PAIR(color));
}

static const char *svc_state(uint8_t running) { return running ? "RUNNING" : "STOPPED"; }

static void fmt_uptime(uint64_t s, char *buf, int len)
{
    if (s < 60)              snprintf(buf, (size_t)len, "%llus", (unsigned long long)s);
    else if (s < 3600)       snprintf(buf, (size_t)len, "%llum%02llus", (unsigned long long)s/60, (unsigned long long)s%60);
    else if (s < 86400)      snprintf(buf, (size_t)len, "%lluh%02llum", (unsigned long long)s/3600, (unsigned long long)(s%3600)/60);
    else                     snprintf(buf, (size_t)len, "%llud%02lluh", (unsigned long long)s/86400, (unsigned long long)(s%86400)/3600);
}

/* ── Main render ──────────────────────────────────────────────────────── */
void panel_services_render(const mon_snapshot_t *s, int y, int h, int cols, int scroll)
{
    int row = y;
    const int max_row = y + h - 1;

#define EMIT(...)  do { if (row <= max_row) { mvprintw(row, 2, __VA_ARGS__); row++; } } while(0)
#define SKIP()     do { if (row <= max_row) row++; } while(0)

    /* Section header */
    attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
    mvprintw(row, 2, "── Services (%u registered) ", s->service_count);
    mvhline(row, 2 + 22 + (int)snprintf(NULL, 0, "%u", s->service_count),
            ACS_HLINE, cols - 26);
    attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
    row++;
    SKIP();

    /* Column header */
    attron(A_BOLD);
    mvprintw(row++, 2, "%-16s %-3s %7s %8s %5s %5s  %-7s  Sec: SBX CAP VRF SCP",
             "Name", "ST", "CPU%", "RAM MB", "FDs", "Thrd", "Uptime");
    attroff(A_BOLD);
    mvhline(row++, 2, ACS_HLINE, cols - 4);

    /* Per-service rows */
    int svc_shown = 0;
    for (uint32_t i = 0; i < SERVICE_MAX; i++) {
        const service_metrics_t *sv = &s->services[i];
        if (!sv->name[0]) continue;
        if (svc_shown++ < scroll) continue;
        if (row + 4 > max_row) break;

        int health = (int)sv->health_score;
        int hcol = (health >= 80) ? COLOR_PAIR_GOOD
                 : (health >= 50) ? COLOR_PAIR_WARNING
                 :                  COLOR_PAIR_CRITICAL;
        if (!sv->running) hcol = COLOR_PAIR_CRITICAL;

        char upbuf[16];
        fmt_uptime(sv->uptime_s, upbuf, sizeof(upbuf));

        /* Main line */
        attron(COLOR_PAIR(hcol) | A_BOLD);
        mvprintw(row, 2, "%-16s %-3s %6.1f%% %6lu MB %5u %5u  %-7s",
                 sv->name, svc_state(sv->running),
                 (double)sv->cpu_pct,
                 sv->rss_bytes / (1024UL * 1024UL),
                 sv->fd_count, sv->thread_count, upbuf);
        attroff(A_BOLD | COLOR_PAIR(hcol));

        /* Security flags inline */
        int fx = 2 + 16 + 4 + 8 + 9 + 6 + 6 + 9 + 4;
        sec_flag(row, fx,      "SBX", sv->sandbox_ok);
        sec_flag(row, fx + 8,  "CAP", sv->caps_ok);
        sec_flag(row, fx + 16, "VRF", sv->verify_ok);
        sec_flag(row, fx + 24, "SCP", sv->seccomp_ok);
        row++;

        /* CPU bar */
        mvprintw(row, 4, "CPU ");
        draw_bar(row, 8, 20, sv->cpu_pct,
                 sv->cpu_pct > 80 ? COLOR_PAIR_CRITICAL :
                 sv->cpu_pct > 50 ? COLOR_PAIR_WARNING : COLOR_PAIR_GOOD);
        int ram_pct = (s->sysinfo.ram_total_bytes > 0)
            ? (int)(sv->rss_bytes * 100 / s->sysinfo.ram_total_bytes)
            : 0;
        mvprintw(row, 30, " RAM ");
        draw_bar(row, 35, 20, (float)ram_pct,
                 ram_pct > 80 ? COLOR_PAIR_CRITICAL :
                 ram_pct > 50 ? COLOR_PAIR_WARNING : COLOR_PAIR_GOOD);
        mvprintw(row, 57, " PID:%u  Health:%d/100", sv->pid, health);
        row++;

        /* SM connection + restart info */
        attron(COLOR_PAIR(COLOR_PAIR_INFO));
        mvprintw(row, 4, "SM: %-4s | Restarts: %u | Seccomp viol: %u | HMAC fail: %u | Replay: %u",
                 sv->sm_connected ? "OK" : "DISC",
                 sv->restart_count, sv->seccomp_violations,
                 sv->hmac_failures, sv->replay_attacks);
        attroff(COLOR_PAIR(COLOR_PAIR_INFO));
        row++;

        /* Service-specific line */
        if (sv->frames_captured || sv->readings_count || sv->gpio_events || sv->rb_drops) {
            mvprintw(row, 4,
                     "frames: %llu drop: %llu | readings: %llu | gpio_evt: %u | rb_drops: %llu | rb_fill: %.1f%%",
                     (unsigned long long)sv->frames_captured,
                     (unsigned long long)sv->frames_dropped,
                     (unsigned long long)sv->readings_count,
                     sv->gpio_events,
                     (unsigned long long)sv->rb_drops,
                     (double)sv->rb_fill_pct);
            row++;
        }

        mvhline(row++, 4, ACS_HLINE, cols - 6);
    }

    if (svc_shown == 0) {
        EMIT("  (no services registered)");
    }
    SKIP();

    /* ── Watchdog summary ─────────────────────────────────────────── */
    if (row + 3 <= max_row) {
        attron(COLOR_PAIR(COLOR_PAIR_INFO) | A_BOLD);
        mvprintw(row, 2, "── Watchdog ");
        mvhline(row, 14, ACS_HLINE, cols - 16);
        attroff(A_BOLD | COLOR_PAIR(COLOR_PAIR_INFO));
        row++;

        uint32_t alive = 0, dead = 0;
        for (uint32_t i = 0; i < s->watchdog.count; i++) {
            if (s->watchdog.entries[i].alive) alive++;
            else dead++;
        }

        mvprintw(row++, 4, "Monitored: %u  |  Alive: ", s->watchdog.count);
        attron(COLOR_PAIR(COLOR_PAIR_GOOD));
        printw("%u", alive);
        attroff(COLOR_PAIR(COLOR_PAIR_GOOD));
        printw("  Dead: ");
        attron(COLOR_PAIR(dead ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD));
        printw("%u", dead);
        attroff(COLOR_PAIR(dead ? COLOR_PAIR_CRITICAL : COLOR_PAIR_GOOD));
        printw("  |  Total restarts: %u", s->watchdog.total_restarts);

        for (uint32_t i = 0; i < s->watchdog.count && row <= max_row; i++) {
            const watchdog_entry_t *w = &s->watchdog.entries[i];
            int wc = w->alive ? COLOR_PAIR_GOOD : COLOR_PAIR_CRITICAL;
            attron(COLOR_PAIR(wc));
            mvprintw(row++, 6, "%-16s  %s  restarts:%-3u  escalation:%u",
                     w->service_name, w->alive ? "ALIVE " : "DEAD  ",
                     w->restart_count, w->escalation_state);
            attroff(COLOR_PAIR(wc));
        }
    }

#undef EMIT
#undef SKIP
}
