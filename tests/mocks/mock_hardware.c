/**
 * @file mock_hardware.c
 * @brief Implementation of simulated hardware devices.
 */
#include "mock_hardware.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ── helpers ──────────────────────────────────────────────────────── */

static int make_nonblock_pipe(int fds[2])
{
    if (pipe2(fds, O_CLOEXEC) < 0) return -1;
    /* make write end non-blocking so inject never blocks the test */
    int flags = fcntl(fds[1], F_GETFL);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    return 0;
}

/* ── Mock Audio ───────────────────────────────────────────────────── */

int mock_audio_hw_create(mock_audio_hw_t *dev, int sample_rate,
                          int channels, int bytes_per_frame)
{
    int fds[2];
    if (make_nonblock_pipe(fds) < 0) return -1;
    dev->read_fd        = fds[0];
    dev->inject_fd      = fds[1];
    dev->sample_rate    = sample_rate;
    dev->channels       = channels;
    dev->bytes_per_frame = bytes_per_frame;
    return 0;
}

int mock_audio_hw_inject(mock_audio_hw_t *dev, const void *pcm, size_t len)
{
    return (int)write(dev->inject_fd, pcm, len);
}

void mock_audio_hw_destroy(mock_audio_hw_t *dev)
{
    if (dev->read_fd   >= 0) { close(dev->read_fd);   dev->read_fd   = -1; }
    if (dev->inject_fd >= 0) { close(dev->inject_fd); dev->inject_fd = -1; }
}

/* ── Mock Camera ──────────────────────────────────────────────────── */

int mock_camera_hw_create(mock_camera_hw_t *dev, int w, int h)
{
    int fds[2];
    if (make_nonblock_pipe(fds) < 0) return -1;
    dev->event_fd   = fds[0];
    dev->trigger_fd = fds[1];
    dev->frame_width  = w;
    dev->frame_height = h;
    dev->frame_size   = (size_t)(w * h * 3); /* RGB24 */
    dev->frame_buf    = calloc(1, dev->frame_size);
    if (!dev->frame_buf) {
        close(fds[0]); close(fds[1]);
        return -1;
    }
    return 0;
}

int mock_camera_hw_trigger(mock_camera_hw_t *dev, const void *frame_data)
{
    if (frame_data)
        memcpy(dev->frame_buf, frame_data, dev->frame_size);
    uint8_t sig = 1;
    return (int)write(dev->trigger_fd, &sig, 1);
}

void mock_camera_hw_destroy(mock_camera_hw_t *dev)
{
    if (dev->event_fd   >= 0) { close(dev->event_fd);   dev->event_fd   = -1; }
    if (dev->trigger_fd >= 0) { close(dev->trigger_fd); dev->trigger_fd = -1; }
    free(dev->frame_buf);
    dev->frame_buf = NULL;
}

/* ── Mock Sensor ──────────────────────────────────────────────────── */

int mock_sensor_hw_create(mock_sensor_hw_t *dev)
{
    int fds[2];
    if (make_nonblock_pipe(fds) < 0) return -1;
    dev->event_fd   = fds[0];
    dev->trigger_fd = fds[1];
    dev->x = dev->y = dev->z = dev->scalar = 0.0f;
    return 0;
}

int mock_sensor_hw_trigger_3axis(mock_sensor_hw_t *dev,
                                  float x, float y, float z)
{
    dev->x = x; dev->y = y; dev->z = z;
    uint8_t sig = 1;
    return (int)write(dev->trigger_fd, &sig, 1);
}

int mock_sensor_hw_trigger_scalar(mock_sensor_hw_t *dev, float val)
{
    dev->scalar = val;
    uint8_t sig = 1;
    return (int)write(dev->trigger_fd, &sig, 1);
}

void mock_sensor_hw_destroy(mock_sensor_hw_t *dev)
{
    if (dev->event_fd   >= 0) { close(dev->event_fd);   dev->event_fd   = -1; }
    if (dev->trigger_fd >= 0) { close(dev->trigger_fd); dev->trigger_fd = -1; }
}

/* ── Mock GPIO ────────────────────────────────────────────────────── */

int mock_gpio_hw_create(mock_gpio_hw_t *dev, int initial_value)
{
    int fds[2];
    if (make_nonblock_pipe(fds) < 0) return -1;
    dev->value_fd     = fds[0];
    dev->write_fd     = fds[1];
    dev->current_value = initial_value;
    /* Write initial value into pipe so first read succeeds */
    char buf[4];
    int  n = snprintf(buf, sizeof(buf), "%d\n", initial_value);
    write(dev->write_fd, buf, (size_t)n);
    return 0;
}

int mock_gpio_hw_set_value(mock_gpio_hw_t *dev, int value)
{
    dev->current_value = value;
    char buf[4];
    int  n = snprintf(buf, sizeof(buf), "%d\n", value);
    return (int)write(dev->write_fd, buf, (size_t)n);
}

void mock_gpio_hw_destroy(mock_gpio_hw_t *dev)
{
    if (dev->value_fd >= 0) { close(dev->value_fd); dev->value_fd = -1; }
    if (dev->write_fd >= 0) { close(dev->write_fd); dev->write_fd = -1; }
}
