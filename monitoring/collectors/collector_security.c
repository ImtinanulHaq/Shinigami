/**
 * @file collector_security.c
 * @brief Security subsystem collector
 */

#include "collector_security.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_security_collector;

static int security_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to security subsystem */
    return 0;
}

static int security_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Update state->snapshot.security.* */
    (void)state;
    return 0;
}

static int security_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_security_register(void)
{
    memset(&g_security_collector, 0, sizeof(g_security_collector));
    strncpy(g_security_collector.name, "security", sizeof(g_security_collector.name)-1);
    g_security_collector.connect = security_connect;
    g_security_collector.tick = security_tick;
    g_security_collector.disconnect = security_disconnect;
    return collector_register(&g_security_collector);
}
