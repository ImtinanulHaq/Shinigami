/**
 * @file mock_hal.c
 * @brief Thread-safe mock hw_device_t — all ops redirect through priv state.
 *
 * All mutex lock/unlock pairs are inline macros so every op is protected
 * without risk of forgetting a lock on any path.
 */

#define _GNU_SOURCE
#include "mock_hal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── internal helpers ─────────────────────────────────────────────────── */

#define PRIV(dev)  ((mock_hal_priv_t *)(dev)->priv)

#define LOCK(p)    pthread_mutex_lock(&(p)->lock)
#define UNLOCK(p)  pthread_mutex_unlock(&(p)->lock)

/** Set all injected return values to their default-success state. */
static void priv_set_defaults(mock_hal_priv_t *p)
{
    p->open_retval    = HAL_SUCCESS;
    p->close_retval   = HAL_SUCCESS;
    p->start_retval   = HAL_SUCCESS;
    p->stop_retval    = HAL_SUCCESS;
    p->read_retval    = 0;          /* 0 bytes by default — set via mock_hal_set_read_data */
    p->write_retval   = -2;         /* sentinel: use actual byte count */
    p->control_retval = HAL_SUCCESS;
    p->read_data_len  = 0;
    p->write_data_len = 0;
    memset(&p->counts, 0, sizeof(p->counts));
}

/* ── vtable implementations ───────────────────────────────────────────── */

static int mock_open(hw_device_t *dev)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.open_calls++;
    int rc = p->open_retval;
    if (rc == HAL_SUCCESS)
        dev->state = HAL_STATE_OPEN;
    UNLOCK(p);
    return rc;
}

static int mock_close(hw_device_t *dev)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.close_calls++;
    int rc = p->close_retval;
    if (rc == HAL_SUCCESS)
        dev->state = HAL_STATE_CLOSED;
    UNLOCK(p);
    return rc;
}

static int mock_start(hw_device_t *dev)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.start_calls++;
    int rc = p->start_retval;
    if (rc == HAL_SUCCESS)
        dev->state = HAL_STATE_ACTIVE;
    UNLOCK(p);
    return rc;
}

static int mock_stop(hw_device_t *dev)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.stop_calls++;
    int rc = p->stop_retval;
    if (rc == HAL_SUCCESS)
        dev->state = HAL_STATE_OPEN;   /* stopped but still open */
    UNLOCK(p);
    return rc;
}

static ssize_t mock_read(hw_device_t *dev, void *buf, size_t size)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.read_calls++;

    if (p->read_retval < 0) {
        ssize_t rv = p->read_retval;
        UNLOCK(p);
        return rv;
    }

    /* Copy canned data if any was loaded */
    size_t copy_len = p->read_data_len;
    if (copy_len > size)     copy_len = size;
    if (copy_len > 0 && buf) memcpy(buf, p->read_data, copy_len);

    ssize_t rv = (ssize_t)copy_len;
    UNLOCK(p);
    return rv;
}

static ssize_t mock_write(hw_device_t *dev, const void *buf, size_t size)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.write_calls++;

    if (p->write_retval == -1) {
        UNLOCK(p);
        return (ssize_t)-1;
    }

    /* Capture the written data for later assertion */
    size_t cap = size;
    if (cap > MOCK_HAL_MAX_DATA) cap = MOCK_HAL_MAX_DATA;
    if (buf && cap > 0) memcpy(p->write_data, buf, cap);
    p->write_data_len = cap;

    /* write_retval == -2: return actual byte count */
    ssize_t rv = (p->write_retval == -2) ? (ssize_t)cap
                                         : p->write_retval;
    UNLOCK(p);
    return rv;
}

static int mock_control(hw_device_t *dev, uint32_t cmd, void *arg)
{
    mock_hal_priv_t *p = PRIV(dev);
    (void)cmd; (void)arg;
    LOCK(p);
    p->counts.control_calls++;
    int rc = p->control_retval;
    UNLOCK(p);
    return rc;
}

static int mock_get_info(hw_device_t *dev, void *info)
{
    mock_hal_priv_t *p = PRIV(dev);
    (void)info;
    LOCK(p);
    p->counts.get_info_calls++;
    UNLOCK(p);
    return HAL_SUCCESS;
}

static void mock_destroy(hw_device_t *dev)
{
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    p->counts.destroy_calls++;
    UNLOCK(p);
    /* Caller must still call mock_hal_destroy() */
}

static const hw_device_ops_t g_mock_ops = {
    .open     = mock_open,
    .close    = mock_close,
    .start    = mock_start,
    .stop     = mock_stop,
    .read     = mock_read,
    .write    = mock_write,
    .control  = mock_control,
    .get_info = mock_get_info,
    .destroy  = mock_destroy,
};

/* ── public API ───────────────────────────────────────────────────────── */

hw_device_t *mock_hal_create(const char *name, hal_device_type_t type)
{
    hw_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) return NULL;

    mock_hal_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        free(dev);
        return NULL;
    }

    if (pthread_mutex_init(&p->lock, NULL) != 0) {
        free(p);
        free(dev);
        return NULL;
    }

    priv_set_defaults(p);

    if (name)
        snprintf(dev->name, HAL_MAX_NAME_LEN, "%s", name);
    dev->type    = type;
    dev->state   = HAL_STATE_CLOSED;
    dev->version = HAL_CURRENT_VERSION;
    dev->ops     = &g_mock_ops;
    dev->priv    = p;

    return dev;
}

void mock_hal_destroy(hw_device_t *dev)
{
    if (!dev) return;
    mock_hal_priv_t *p = PRIV(dev);
    pthread_mutex_destroy(&p->lock);
    free(p);
    free(dev);
}

mock_hal_priv_t *mock_hal_get_priv(hw_device_t *dev)
{
    if (!dev) return NULL;
    return PRIV(dev);
}

void mock_hal_set_read_data(hw_device_t *dev, const void *data, size_t len)
{
    if (!dev || !data) return;
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    if (len > MOCK_HAL_MAX_DATA) len = MOCK_HAL_MAX_DATA;
    memcpy(p->read_data, data, len);
    p->read_data_len = len;
    p->read_retval   = (ssize_t)len;  /* make read() return len bytes */
    UNLOCK(p);
}

void mock_hal_reset(hw_device_t *dev)
{
    if (!dev) return;
    mock_hal_priv_t *p = PRIV(dev);
    LOCK(p);
    priv_set_defaults(p);
    dev->state = HAL_STATE_CLOSED;
    UNLOCK(p);
}
