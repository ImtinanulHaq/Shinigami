/**
 * @file    collector_base.h
 * @brief   Base types and framework for all metric collectors.
 *
 * Every collector is a thread with its own state machine, interval timer,
 * and staggered startup.  The collector_register() call adds it to the
 * global table; collector_start_all() launches all threads with the
 * configured stagger delay (index * MON_STAGGER_DELAY_MS).
 *
 * Collector state machine:
 *   WAITING → CONNECTING → SYNCING → LIVE
 *   LIVE → STALE (missed 3× interval)
 *   LIVE/STALE → OFFLINE (socket closed / ENOENT)
 *   OFFLINE → WAITING (after retry_interval_ms)
 *
 * @thread_safety  collector_register() must be called before any threads
 *                 are started.  collector_start_all() / stop_all() are not
 *                 thread-safe with respect to each other.
 */
#pragma once

#include "../protocol/monitor_ipc_protocol.h"
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations */
struct monitord_state;
typedef struct collector collector_t;

/* ── Collector lifecycle callbacks ───────────────────────────────────────── */

/**
 * @brief  Called once when the component's socket/resource is found.
 *         Return 0 on success; collector transitions CONNECTING→SYNCING.
 *         Return -1 to stay in CONNECTING (will retry after interval).
 */
typedef int (*collector_connect_fn)(collector_t *self,
                                    struct monitord_state *state);

/**
 * @brief  Called every interval_ms when LIVE.
 *         Read from the component, update state under the appropriate rwlock.
 *         Return 0 on success (stays LIVE).
 *         Return -1 on transient error (transitions to STALE then OFFLINE).
 */
typedef int (*collector_tick_fn)(collector_t *self,
                                 struct monitord_state *state);

typedef int (*collector_disconnect_fn)(collector_t *self,
                                       struct monitord_state *state);

/* ── Collector descriptor ───────────────────────────────────────────────────
 */

#define COLLECTOR_PRIV_SIZE                                                    \
  256 /**< Private data area inside each collector.                            \
       */

struct collector {
  /* Identity */
  char name[32];
  uint32_t index; /**< Position in the global table. */

  /* Timing */
  uint32_t interval_ms;    /**< Normal collection interval. */
  uint32_t retry_ms;       /**< Retry interval when OFFLINE (default 2000). */
  uint64_t last_tick_ms;   /**< Monotonic ms of last successful tick. */
  uint64_t last_update_ms; /**< Monotonic ms of last state write. */

  /* State machine */
  volatile collector_state_t state;
  uint64_t last_state_change_ms;
  uint64_t last_offline_ms;
  uint64_t collect_count;
  uint64_t error_count;
  uint32_t consecutive_errors;

  /* Callbacks */
  collector_connect_fn connect;
  collector_tick_fn tick;
  collector_disconnect_fn disconnect;

  /* Thread control */
  pthread_t thread;
  volatile sig_atomic_t stop_flag; /**< Set to 1 to request clean shutdown. */

  /* Private per-collector data (e.g. open fd, mmap ptr) */
  uint8_t priv[COLLECTOR_PRIV_SIZE];
};

/* ── Global collector table ──────────────────────────────────────────────── */

#define COLLECTOR_TABLE_MAX 16

/**
 * @brief  Register a collector in the global table.
 * @return 0 on success, -1 if table is full.
 *
 * @thread_safety  Must be called before collector_start_all().
 */
int collector_register(collector_t *c);

/**
 * @brief  Start all registered collectors with staggered startup.
 * @param  state  Shared state struct passed to each callback.
 * @return 0 on success, -1 on pthread_create failure.
 */
int collector_start_all(struct monitord_state *state);

/**
 * @brief  Signal all collector threads to stop and wait for them to join.
 * @param  timeout_ms  If > 0, give up waiting after this many milliseconds.
 */
void collector_stop_all(uint32_t timeout_ms);

/**
 * @brief  Fill a collector_info_t array from the current table (for snapshots).
 * @param  out     Output array.
 * @param  max     Size of out.
 * @param  live    Set to count of LIVE collectors.
 * @param  stale   Set to count of STALE collectors.
 * @param  offline Set to count of OFFLINE/WAITING collectors.
 * @return Number of entries written.
 */
uint32_t collector_snapshot_info(collector_info_t *out, uint32_t max,
                                 uint32_t *live, uint32_t *stale,
                                 uint32_t *offline);

/**
 * @brief  Transition @p c to a new state, recording the timestamp.
 * @thread_safety  May be called from the collector's own thread only.
 */
void collector_set_state(collector_t *c, collector_state_t new_state);

/**
 * @brief  Sleep for @p ms milliseconds, returning early if stop_flag is set.
 * @return 0 if full sleep elapsed, 1 if stop_flag interrupted.
 */
int collector_sleep_ms(collector_t *c, uint32_t ms);

/**
 * @brief  Return the total number of registered collectors.
 */
uint32_t collector_get_count(void);

/**
 * @brief  Retrieve info for all registered collectors.
 * @param  out      Output array of collector_info_t.
 * @param  max      Capacity of @p out.
 * @param  live     Set to count of LIVE collectors.
 * @param  stale    Set to count of STALE collectors.
 * @param  offline  Set to count of OFFLINE/WAITING collectors.
 * @return Number of entries written.
 */
uint32_t collector_get_info_all(collector_info_t *out, uint32_t max,
                                uint32_t *live, uint32_t *stale,
                                uint32_t *offline);
