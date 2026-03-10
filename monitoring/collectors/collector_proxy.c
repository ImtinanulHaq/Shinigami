/**
 * @file collector_proxy.c
 * @brief Proxy subsystem collector
 */

#include "collector_proxy.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_proxy_collector;

static int proxy_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to proxy subsystem */
    return 0;
}

static int proxy_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Update state->snapshot.proxy.* */
    (void)state;
    return 0;
}

static int proxy_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_proxy_register(void)
{
    memset(&g_proxy_collector, 0, sizeof(g_proxy_collector));
    strncpy(g_proxy_collector.name, "proxy", sizeof(g_proxy_collector.name)-1);
    g_proxy_collector.connect = proxy_connect;
    g_proxy_collector.tick = proxy_tick;
    g_proxy_collector.disconnect = proxy_disconnect;
    return collector_register(&g_proxy_collector);
}
