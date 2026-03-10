/**
 * @file collector_config_watcher.c
 * @brief Config watcher collector - monitor middleware config changes
 */

#include "collector_config_watcher.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_config_watcher_collector;

static int config_watcher_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Setup inotify on config files */
    return 0;
}

static int config_watcher_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Check for config changes */
    (void)state;
    return 0;
}

static int config_watcher_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_config_watcher_register(void)
{
    memset(&g_config_watcher_collector, 0, sizeof(g_config_watcher_collector));
    strncpy(g_config_watcher_collector.name, "config_watcher", sizeof(g_config_watcher_collector.name)-1);
    g_config_watcher_collector.connect = config_watcher_connect;
    g_config_watcher_collector.tick = config_watcher_tick;
    g_config_watcher_collector.disconnect = config_watcher_disconnect;
    return collector_register(&g_config_watcher_collector);
}
