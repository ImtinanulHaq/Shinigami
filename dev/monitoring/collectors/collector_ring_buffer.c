/**
 * @file collector_ring_buffer.c
 * @brief Ring buffer collector - read shared memory or IPC
 */

#include "collector_ring_buffer.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_ring_buffer_collector;

static int ring_buffer_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to ring buffer stats */
    return 0;
}

static int ring_buffer_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Update state->snapshot.ringbufs[] */
    (void)state;
    return 0;
}

static int ring_buffer_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_ring_buffer_register(void)
{
    memset(&g_ring_buffer_collector, 0, sizeof(g_ring_buffer_collector));
    strncpy(g_ring_buffer_collector.name, "ring_buffer", sizeof(g_ring_buffer_collector.name)-1);
    g_ring_buffer_collector.connect = ring_buffer_connect;
    g_ring_buffer_collector.tick = ring_buffer_tick;
    g_ring_buffer_collector.disconnect = ring_buffer_disconnect;
    return collector_register(&g_ring_buffer_collector);
}
