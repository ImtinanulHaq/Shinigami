/**
 * @file    collector_base.c
 * @brief   Collector framework implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../protocol/monitor_ipc_protocol.h"
#include "collector_base.h"

/* ── Globals ─────────────────────────────────────────────────────────────── */

static collector_t *g_collectors[COLLECTOR_TABLE_MAX];
static uint32_t g_collector_count = 0;

/* ── Helpers ────────────────────────────────────────────────────────────────
 */

static uint64_t monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

void collector_set_state(collector_t *c, collector_state_t new_state) {
  c->state = new_state;
  c->last_state_change_ms = monotonic_ms();
}

int collector_sleep_ms(collector_t *c, uint32_t ms) {
  uint32_t slept = 0;
  while (slept < ms) {
    if (c->stop_flag)
      return 1;
    uint32_t chunk = (ms - slept < 50) ? (ms - slept) : 50;
    struct timespec req = {
        .tv_sec = chunk / 1000,
        .tv_nsec = (long)(chunk % 1000) * 1000000L,
    };
    nanosleep(&req, NULL);
    slept += chunk;
  }
  return c->stop_flag ? 1 : 0;
}

/* ── Thread entry point ─────────────────────────────────────────────────────
 */

typedef struct {
  collector_t *c;
  struct monitord_state *state;
} thread_arg_t;

static void *collector_thread(void *arg) {
  thread_arg_t *ta = (thread_arg_t *)arg;
  collector_t *c = ta->c;
  struct monitord_state *st = ta->state;
  free(ta);

  /* Staggered startup: index * 200 ms */
  if (collector_sleep_ms(c, c->index * MON_STAGGER_DELAY_MS))
    goto done;

  collector_set_state(c, COLLECTOR_STATE_CONNECTING);

  while (!c->stop_flag) {
    switch (c->state) {
    case COLLECTOR_STATE_WAITING:
      if (collector_sleep_ms(c, c->retry_ms))
        goto done;
      collector_set_state(c, COLLECTOR_STATE_CONNECTING);
      break;

    case COLLECTOR_STATE_CONNECTING:
    case COLLECTOR_STATE_SYNCING:
      if (c->connect) {
        int rc = c->connect(c, st);
        if (rc == 0)
          collector_set_state(c, COLLECTOR_STATE_LIVE);
        else {
          if (collector_sleep_ms(c, c->retry_ms))
            goto done;
        }
      } else {
        collector_set_state(c, COLLECTOR_STATE_LIVE);
      }
      break;

    case COLLECTOR_STATE_LIVE:
    case COLLECTOR_STATE_STALE: {
      uint64_t now = monotonic_ms();
      /* Check if stale */
      if (c->state == COLLECTOR_STATE_LIVE && c->last_update_ms &&
          now - c->last_update_ms > (uint64_t)c->interval_ms * 3) {
        collector_set_state(c, COLLECTOR_STATE_STALE);
      }

      if (c->tick) {
        int rc = c->tick(c, st);
        if (rc == 0) {
          c->last_update_ms = monotonic_ms();
          c->collect_count++;
          c->consecutive_errors = 0;
          if (c->state == COLLECTOR_STATE_STALE)
            collector_set_state(c, COLLECTOR_STATE_LIVE);
        } else {
          c->error_count++;
          c->consecutive_errors++;
          if (c->consecutive_errors >= 3) {
            if (c->disconnect) {
              (void)c->disconnect(c, st);
            }
            c->last_offline_ms = monotonic_ms();
            collector_set_state(c, COLLECTOR_STATE_OFFLINE);
          }
        }
      }

      if (!c->stop_flag)
        collector_sleep_ms(c, c->interval_ms);
      break;
    }

    case COLLECTOR_STATE_OFFLINE:
      if (c->disconnect) {
        (void)c->disconnect(c, st);
      }
      if (collector_sleep_ms(c, c->retry_ms))
        goto done;
      collector_set_state(c, COLLECTOR_STATE_WAITING);
      break;

    default:
      break;
    }
  }

done:
  if (c->disconnect) {
    (void)c->disconnect(c, st);
  }
  collector_set_state(c, COLLECTOR_STATE_OFFLINE);
  return NULL;
}

/* ── Public API ─────────────────────────────────────────────────────────────
 */

int collector_register(collector_t *c) {
  if (g_collector_count >= COLLECTOR_TABLE_MAX)
    return -1;
  if (!c->retry_ms)
    c->retry_ms = 2000;
  c->index = g_collector_count;
  c->state = COLLECTOR_STATE_WAITING;
  g_collectors[g_collector_count++] = c;
  return 0;
}

int collector_start_all(struct monitord_state *state) {
  for (uint32_t i = 0; i < g_collector_count; i++) {
    collector_t *c = g_collectors[i];
    c->stop_flag = 0;

    thread_arg_t *ta = malloc(sizeof(thread_arg_t));
    if (!ta)
      return -1;
    ta->c = c;
    ta->state = state;

    if (pthread_create(&c->thread, NULL, collector_thread, ta) != 0) {
      free(ta);
      fprintf(stderr, "[collector_base] pthread_create failed for %s: %s\n",
              c->name, strerror(errno));
      return -1;
    }
  }
  return 0;
}

void collector_stop_all(uint32_t timeout_ms) {
  /* Signal all */
  for (uint32_t i = 0; i < g_collector_count; i++)
    g_collectors[i]->stop_flag = 1;

  uint64_t deadline = monotonic_ms() + timeout_ms;

  for (uint32_t i = 0; i < g_collector_count; i++) {
    collector_t *c = g_collectors[i];
    if (timeout_ms > 0) {
      uint64_t now = monotonic_ms();
      if (now >= deadline) {
        /* Best-effort pthread_cancel */
        pthread_cancel(c->thread);
        continue;
      }
      struct timespec ts;
      uint64_t ns = (deadline - now) * 1000000ULL;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += (long)(ns % 1000000000ULL);
      ts.tv_sec += (long)(ns / 1000000000ULL);
      if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
      }
      pthread_timedjoin_np(c->thread, NULL, &ts);
    } else {
      pthread_join(c->thread, NULL);
    }
  }
}

uint32_t collector_get_count(void) { return g_collector_count; }

uint32_t collector_get_info_all(collector_info_t *out, uint32_t max,
                                uint32_t *live, uint32_t *stale,
                                uint32_t *offline) {
  uint32_t n = 0;
  uint32_t l = 0, s = 0, o = 0;
  uint64_t now = monotonic_ms();

  for (uint32_t i = 0; i < g_collector_count && n < max; i++) {
    collector_t *c = g_collectors[i];
    collector_info_t *ci = &out[n++];

    strncpy(ci->name, c->name, sizeof(ci->name) - 1);
    ci->state = c->state;
    ci->interval_ms = c->interval_ms;
    ci->last_update_ms = c->last_update_ms;
    ci->collect_count = c->collect_count;
    ci->error_count = c->error_count;
    ci->last_offline_ms = c->last_offline_ms;

    switch (c->state) {
    case COLLECTOR_STATE_LIVE:
      l++;
      break;
    case COLLECTOR_STATE_STALE:
      s++;
      break;
    case COLLECTOR_STATE_OFFLINE:
    case COLLECTOR_STATE_WAITING:
    case COLLECTOR_STATE_CONNECTING:
    case COLLECTOR_STATE_SYNCING:
      o++;
      break;
    }
  }

  if (live)
    *live = l;
  if (stale)
    *stale = s;
  if (offline)
    *offline = o;
  return n;
}
