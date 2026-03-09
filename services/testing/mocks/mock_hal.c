/**
 * @file mock_hal.c
 * @brief Mock hw_device_t implementation — all ops tracked in memory.
 */

#include "mock_hal.h"

#include <stdlib.h>
#include <string.h>

/* ── mock vtable functions ─────────────────────────────────────────────── */

static int mock_open(hw_device_t *dev)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.open_calls++;
    dev->state = HAL_STATE_OPEN;
    return p->open_retval;
}

static int mock_close(hw_device_t *dev)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.close_calls++;
    dev->state = HAL_STATE_CLOSED;
    return p->close_retval;
}

static int mock_start(hw_device_t *dev)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.start_calls++;
    dev->state = HAL_STATE_ACTIVE;
    return p->start_retval;
}

static int mock_stop(hw_device_t *dev)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.stop_calls++;
    dev->state = HAL_STATE_OPEN;
    return p->stop_retval;
}

static ssize_t mock_read(hw_device_t *dev, void *buf, size_t size)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.read_calls++;

    if (p->read_retval < 0)
        return p->read_retval;

    size_t copy_len = p->read_data_len;
    if (copy_len > size)     copy_len = size;
    if (copy_len > 0 && buf) memcpy(buf, p->read_data, copy_len);

    return (ssize_t)copy_len;
}

static ssize_t mock_write(hw_device_t *dev, const void *buf, size_t size)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.write_calls++;

    if (p->write_retval < 0)
        return p->write_retval;

    size_t store = size;
    if (store > MOCK_HAL_MAX_DATA) store = MOCK_HAL_MAX_DATA;
    memcpy(p->write_data, buf, store);
    p->write_data_len = store;

    return (ssize_t)store;
}

static int mock_control(hw_device_t *dev, uint32_t cmd, void *arg)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.control_calls++;
    (void)cmd; (void)arg;
    return p->control_retval;
}

static int mock_get_info(hw_device_t *dev, void *info)
{
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    p->counts.get_info_calls++;
    (void)info;
    return 0;
}

static const hw_device_ops_t mock_ops = {
    .open     = mock_open,
    .close    = mock_close,
    .start    = mock_start,
    .stop     = mock_stop,
    .read     = mock_read,
    .write    = mock_write,
    .control  = mock_control,
    .get_info = mock_get_info,
};

/* ── public API ─────────────────────────────────────────────────────────── */

hw_device_t *mock_hal_create(const char *name, hal_device_type_t type)
{
    hw_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) return NULL;

    mock_hal_priv_t *priv = calloc(1, sizeof(*priv));
    if (!priv) { free(dev); return NULL; }

    /* Default: all operations succeed */
    priv->open_retval    = 0;
    priv->close_retval   = 0;
    priv->start_retval   = 0;
    priv->stop_retval    = 0;
    priv->read_retval    = 0;
    priv->write_retval   = 0;
    priv->control_retval = 0;

    hal_device_init(dev, name, type);
    dev->ops  = &mock_ops;
    dev->priv = priv;
    return dev;
}

void mock_hal_destroy(hw_device_t *dev)
{
    if (!dev) return;
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    if (p) { p->counts.destroy_calls++; free(p); }
    hal_device_destroy(dev);
    free(dev);
}

mock_hal_priv_t *mock_hal_get_priv(hw_device_t *dev)
{
    return dev ? (mock_hal_priv_t *)dev->priv : NULL;
}

void mock_hal_set_read_data(hw_device_t *dev, const void *data, size_t len)
{
    if (!dev) return;
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    if (!p) return;
    if (len > MOCK_HAL_MAX_DATA) len = MOCK_HAL_MAX_DATA;
    memcpy(p->read_data, data, len);
    p->read_data_len = len;
    p->read_retval   = (ssize_t)len;
}

void mock_hal_reset(hw_device_t *dev)
{
    if (!dev) return;
    mock_hal_priv_t *p = (mock_hal_priv_t *)dev->priv;
    if (!p) return;

    memset(&p->counts, 0, sizeof(p->counts));
    p->write_data_len = 0;
    p->read_data_len  = 0;
    /* Keep injected retvals intact */
}
