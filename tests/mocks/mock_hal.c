/**
 * @file mock_hal.c
 * @brief Controllable HAL stubs implementation.
 */
#include "mock_hal.h"
#include <string.h>
#include <stddef.h>

static mock_hal_state_t g_state;

void mock_hal_reset(mock_hal_state_t *s)
{
    memset(s, 0, sizeof(*s));
    /* Default all returns to 0 (success) */
    s->read_ret = 0;
}

mock_hal_state_t *mock_hal_get_state(void)
{
    return &g_state;
}

int mock_hal_open(void *dev)
{
    (void)dev;
    g_state.open_calls++;
    return g_state.open_ret;
}

int mock_hal_close(void *dev)
{
    (void)dev;
    g_state.close_calls++;
    return g_state.close_ret;
}

int mock_hal_start(void *dev)
{
    (void)dev;
    g_state.start_calls++;
    return g_state.start_ret;
}

int mock_hal_stop(void *dev)
{
    (void)dev;
    g_state.stop_calls++;
    return g_state.stop_ret;
}

ssize_t mock_hal_read(void *dev, void *buf, size_t len)
{
    (void)dev;
    g_state.read_calls++;
    if (g_state.read_ret < 0) return g_state.read_ret;

    size_t copy = (size_t)g_state.read_ret;
    if (copy > len) copy = len;
    if (copy > g_state.read_data_len) copy = g_state.read_data_len;
    if (copy > 0 && buf) memcpy(buf, g_state.read_data, copy);
    return (ssize_t)copy;
}

int mock_hal_write(void *dev, const void *buf, size_t len)
{
    (void)dev; (void)buf; (void)len;
    g_state.write_calls++;
    return g_state.write_ret;
}

int mock_hal_control(void *dev, int cmd, void *arg)
{
    (void)dev; (void)arg;
    g_state.control_calls++;
    g_state.last_control_cmd = cmd;
    return g_state.control_ret;
}
