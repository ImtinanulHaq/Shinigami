/**
 * @file collector_memory_pool.c
 * @brief Memory pool collector - read shared memory or IPC
 */

#include "collector_memory_pool.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_memory_pool_collector;

static int memory_pool_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to memory pool stats */
    return 0;
}

static int memory_pool_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Update state->snapshot.pools[] */
    (void)state;
    return 0;
}

static int memory_pool_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_memory_pool_register(void)
{
    memset(&g_memory_pool_collector, 0, sizeof(g_memory_pool_collector));
    strncpy(g_memory_pool_collector.name, "memory_pool", sizeof(g_memory_pool_collector.name)-1);
    g_memory_pool_collector.connect = memory_pool_connect;
    g_memory_pool_collector.tick = memory_pool_tick;
    g_memory_pool_collector.disconnect = memory_pool_disconnect;
    return collector_register(&g_memory_pool_collector);
}
