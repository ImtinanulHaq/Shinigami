/**
 * @file collector_ipc_channels.c
 * @brief IPC channels collector
 */

#include "collector_ipc_channels.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_ipc_channels_collector;

static int ipc_channels_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to IPC channel stats */
    return 0;
}

static int ipc_channels_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Update state->snapshot.ipc_channels[] */
    (void)state;
    return 0;
}

static int ipc_channels_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_ipc_channels_register(void)
{
    memset(&g_ipc_channels_collector, 0, sizeof(g_ipc_channels_collector));
    strncpy(g_ipc_channels_collector.name, "ipc_channels", sizeof(g_ipc_channels_collector.name)-1);
    g_ipc_channels_collector.connect = ipc_channels_connect;
    g_ipc_channels_collector.tick = ipc_channels_tick;
    g_ipc_channels_collector.disconnect = ipc_channels_disconnect;
    return collector_register(&g_ipc_channels_collector);
}
