/**
 * @file collector_hal.c
 * @brief HAL collector - connect to HAL IPC
 */

#include "collector_hal.h"
#include "collector_base.h"
#include "../daemon/monitord_state.h"
#include <stdio.h>
#include <string.h>

static collector_t g_hal_collector;

static int hal_connect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    /* TODO: Connect to HAL interface */
    return 0;
}

static int hal_tick(collector_t *self, struct monitord_state *state)
{
    (void)self;
    /* TODO: Query HAL metrics, update state->services[2] */
    state->services[2].health_score = 100;
    strncpy(state->services[2].name, "HAL", sizeof(state->services[2].name)-1);
    return 0;
}

static int hal_disconnect(collector_t *self, struct monitord_state *state)
{
    (void)self;
    (void)state;
    return 0;
}

int collector_hal_register(void)
{
    memset(&g_hal_collector, 0, sizeof(g_hal_collector));
    strncpy(g_hal_collector.name, "hal", sizeof(g_hal_collector.name)-1);
    g_hal_collector.connect = hal_connect;
    g_hal_collector.tick = hal_tick;
    g_hal_collector.disconnect = hal_disconnect;
    return collector_register(&g_hal_collector);
}
