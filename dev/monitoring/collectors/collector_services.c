/**
 * @file collector_services.c
 * @brief Services collector - connect to security IPC
 */

#include "collector_services.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_services_collector;

static int services_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to security subsystem */
    return 0;
}

static int services_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Query security metrics, update state->services[3] */
    state->services[3].health_score = 100;
    strncpy(state->services[3].name, "Security", sizeof(state->services[3].name)-1);
    state->services[3].running      = 1;
    state->services[3].sandbox_ok   = 1;
    state->services[3].caps_ok      = 1;
    state->services[3].verify_ok    = 1;
    state->services[3].seccomp_ok   = 1;
    return 0;
}

static int services_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_services_register(void)
{
    memset(&g_services_collector, 0, sizeof(g_services_collector));
    strncpy(g_services_collector.name, "services", sizeof(g_services_collector.name)-1);
    g_services_collector.connect     = services_connect;
    g_services_collector.tick        = services_tick;
    g_services_collector.disconnect  = services_disconnect;
    g_services_collector.interval_ms = 1000;
    return collector_register(&g_services_collector);
}
