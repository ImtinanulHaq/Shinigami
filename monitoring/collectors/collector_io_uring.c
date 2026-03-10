/**
 * @file collector_io_uring.c
 * @brief io_uring collector - read shared memory or IPC
 */

#include "collector_io_uring.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_io_uring_collector;

static int io_uring_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to io_uring stats */
    return 0;
}

static int io_uring_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Update state->snapshot.urings[] */
    (void)state;
    return 0;
}

static int io_uring_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_io_uring_register(void)
{
    memset(&g_io_uring_collector, 0, sizeof(g_io_uring_collector));
    strncpy(g_io_uring_collector.name, "io_uring", sizeof(g_io_uring_collector.name)-1);
    g_io_uring_collector.connect = io_uring_connect;
    g_io_uring_collector.tick = io_uring_tick;
    g_io_uring_collector.disconnect = io_uring_disconnect;
    return collector_register(&g_io_uring_collector);
}
