/**
 * @file collector_watchdog.c
 * @brief Watchdog collector - connect to watchdog IPC
 */

#include "collector_watchdog.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_watchdog_collector;

static int watchdog_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to watchdog IPC */
    return 0;
}

static int watchdog_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* slot[1] is owned by collector_processes (audio_service tracking);
     * this stub intentionally does nothing to avoid overwriting live data. */
    return 0;
}

static int watchdog_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_watchdog_register(void)
{
    memset(&g_watchdog_collector, 0, sizeof(g_watchdog_collector));
    strncpy(g_watchdog_collector.name, "watchdog", sizeof(g_watchdog_collector.name)-1);
    g_watchdog_collector.connect     = watchdog_connect;
    g_watchdog_collector.tick        = watchdog_tick;
    g_watchdog_collector.disconnect  = watchdog_disconnect;
    g_watchdog_collector.interval_ms = 1000;
    return collector_register(&g_watchdog_collector);
}
