/**
 * @file mock_hardware.h
 * @brief Simulated hardware devices for HAL unit tests.
 *
 * Each device is backed by a Unix pipe pair so the test can inject
 * data (write to write-end) and the HAL reads from the read-end —
 * exactly as it would with real hardware file descriptors.
 *
 * Devices available:
 *   mock_audio_hw  — ALSA PCM capture simulation via pipe
 *   mock_camera_hw — V4L2 frame-ready event simulation
 *   mock_sensor_hw — IIO event fd simulation
 *   mock_gpio_hw   — GPIO sysfs value file simulation
 */
#ifndef MOCK_HARDWARE_H
#define MOCK_HARDWARE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Mock Audio Device ────────────────────────────────────────────── */

typedef struct {
    int  read_fd;       /**< HAL reads PCM from this fd  */
    int  inject_fd;     /**< Test writes PCM into this fd */
    int  sample_rate;
    int  channels;
    int  bytes_per_frame;
} mock_audio_hw_t;

/** Create a mock audio device. Returns 0 on success. */
int  mock_audio_hw_create(mock_audio_hw_t *dev, int sample_rate,
                           int channels, int bytes_per_frame);
/** Inject PCM frames into the pipe (test side). */
int  mock_audio_hw_inject(mock_audio_hw_t *dev, const void *pcm, size_t len);
/** Destroy mock device and close fds. */
void mock_audio_hw_destroy(mock_audio_hw_t *dev);

/* ── Mock Camera Device ───────────────────────────────────────────── */

typedef struct {
    int  event_fd;      /**< HAL polls this fd for POLLIN */
    int  trigger_fd;    /**< Test writes 1 byte to signal frame ready */
    int  frame_width;
    int  frame_height;
    uint8_t *frame_buf;  /**< Frame data returned by mock capture */
    size_t   frame_size;
} mock_camera_hw_t;

int  mock_camera_hw_create(mock_camera_hw_t *dev, int w, int h);
/** Trigger a frame-ready event (test side). */
int  mock_camera_hw_trigger(mock_camera_hw_t *dev, const void *frame_data);
void mock_camera_hw_destroy(mock_camera_hw_t *dev);

/* ── Mock Sensor Device ───────────────────────────────────────────── */

typedef struct {
    int    event_fd;    /**< HAL polls this fd */
    int    trigger_fd;
    float  x, y, z;    /**< Values returned by mock read */
    float  scalar;
} mock_sensor_hw_t;

int  mock_sensor_hw_create(mock_sensor_hw_t *dev);
int  mock_sensor_hw_trigger_3axis(mock_sensor_hw_t *dev, float x, float y, float z);
int  mock_sensor_hw_trigger_scalar(mock_sensor_hw_t *dev, float val);
void mock_sensor_hw_destroy(mock_sensor_hw_t *dev);

/* ── Mock GPIO Device ─────────────────────────────────────────────── */

typedef struct {
    int  value_fd;      /**< HAL reads value from this fd  */
    int  write_fd;      /**< Test writes "0\n" or "1\n" here */
    int  current_value;
} mock_gpio_hw_t;

int  mock_gpio_hw_create(mock_gpio_hw_t *dev, int initial_value);
int  mock_gpio_hw_set_value(mock_gpio_hw_t *dev, int value);
void mock_gpio_hw_destroy(mock_gpio_hw_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_HARDWARE_H */
