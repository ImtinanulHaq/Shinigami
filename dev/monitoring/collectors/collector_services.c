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
    (void)state;
    /* slot[3] is owned by collector_processes (sensor_service tracking);
     * this stub intentionally does nothing to avoid overwriting live data. */
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
