/**
 * @file sensor_hal.h
 * @brief Sensor HAL — Linux IIO subsystem interface.
 *
 * Supports 3-axis inertial sensors (accelerometer, gyroscope, magnetometer)
 * and single-value environmental sensors (temperature, pressure, etc.).
 *
 * Two read modes are available and selected at open time:
 *   - Buffer mode (preferred): reads from /dev/iio:deviceN — one syscall per
 *     sample, hardware-stamped timestamp, atomic multi-channel read.
 *   - Sysfs fallback: reads each axis from /sys/bus/iio/devices/.../in_*_raw —
 *     three open/read/close cycles per 3-axis sample, software timestamp.
 *
 * Buffer mode is activated by setting enable_buffer = 1 in the configuration.
 */

#ifndef SENSOR_HAL_H
#define SENSOR_HAL_H

#include "hal_interface.h"

/* ── sensor types ─────────────────────────────────────────────────────── */

/**
 * @brief Classification of the physical quantity being measured.
 */
typedef enum {
  SENSOR_TYPE_ACCEL = 0x01,
  SENSOR_TYPE_GYRO = 0x02,
  SENSOR_TYPE_MAGNET = 0x03,
  SENSOR_TYPE_LIGHT = 0x04,
  SENSOR_TYPE_PROXIMITY = 0x05,
  SENSOR_TYPE_PRESSURE = 0x06,
  SENSOR_TYPE_TEMPERATURE = 0x07,
  SENSOR_TYPE_HUMIDITY = 0x08,
} sensor_type_t;

/* ── data structures ──────────────────────────────────────────────────── */

/**
 * @brief One sample from a 3-axis sensor.
 *
 * Values are in SI units after the device scale factor is applied:
 * m/s² for accelerometers, rad/s for gyroscopes, µT for magnetometers.
 *
 * @p timestamp is nanoseconds on CLOCK_MONOTONIC.  In buffer mode the
 * timestamp is supplied by the IIO hardware trigger and reflects actual
 * DMA-completion time rather than the time the application dequeued it.
 */
typedef struct {
  float x;
  float y;
  float z;
  uint64_t timestamp;
} sensor_data_3axis_t;

/**
 * @brief One sample from a single-value sensor.
 */
typedef struct {
  float value;
  uint64_t timestamp;
} sensor_data_1axis_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Configuration for a sensor HAL device.
 *
 * Set @p enable_buffer to 1 to request IIO hardware buffer mode.  If the
 * kernel driver does not expose /dev/iio:deviceN the implementation
 * silently falls back to per-axis sysfs reads.
 */
typedef struct {
  sensor_type_t type;
  uint32_t sampling_rate_hz;
  uint32_t scale;
  int enable_buffer;
} sensor_config_t;

/* ── device info ──────────────────────────────────────────────────────── */

/**
 * @brief Runtime snapshot of sensor device state.
 */
typedef struct {
  char iio_device_path[64];
  sensor_type_t type;
  uint32_t sampling_rate_hz;
  float resolution;
  float max_range;
  uint32_t fifo_size;
  int buffer_mode_active;
} sensor_info_t;

/* ── control commands ─────────────────────────────────────────────────── */

/**
 * @brief Commands accepted by the sensor device control() operation.
 *
 *   SENSOR_CMD_SET_RATE      — arg: const uint32_t *  (Hz)
 *   SENSOR_CMD_GET_RATE      — arg: uint32_t *
 *   SENSOR_CMD_ENABLE_BUFFER — arg: const int *        (0=disable, 1=enable)
 *   SENSOR_CMD_CALIBRATE     — arg: NULL   (zeroes current reading as offset)
 *   SENSOR_CMD_GET_CONFIG    — arg: sensor_config_t *
 */
typedef enum {
  SENSOR_CMD_SET_RATE = 0x2000,
  SENSOR_CMD_GET_RATE = 0x2001,
  SENSOR_CMD_ENABLE_BUFFER = 0x2002,
  SENSOR_CMD_CALIBRATE = 0x2003,
  SENSOR_CMD_GET_CONFIG = 0x2004,
} sensor_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a sensor HAL device.
 *
 * Validates @p iio_device_id against the expected IIO naming pattern
 * and confirms the resolved path is anchored under the IIO sysfs base
 * directory before storing it.
 *
 * @param device_name    Human-readable name for registry lookup.
 * @param iio_device_id  IIO device identifier, e.g. "iio:device0".
 * @param sensor_config  Sensor parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
hw_device_t *sensor_hal_create(const char *device_name,
                               const char *iio_device_id,
                               const sensor_config_t *sensor_config);

/**
 * @brief Release all resources held by a sensor device.
 * @param device_ptr  Device returned by @ref sensor_hal_create.
 */
void sensor_hal_destroy(hw_device_t *device_ptr);

/**
 * @brief Return the default configuration for a given sensor type.
 *
 * 100 Hz, no buffer, scale = 1.
 *
 * @param sensor_type  Physical quantity to measure.
 * @return Populated sensor_config_t; no heap allocation.
 */
sensor_config_t sensor_hal_default_config(sensor_type_t sensor_type);

/**
 * @brief Return a human-readable string for a sensor type.
 * @param sensor_type  Classification code.
 * @return Static string; never NULL.
 */
const char *sensor_hal_type_string(sensor_type_t sensor_type);

/**
 * @brief Read one sample from a 3-axis sensor (accel, gyro, magnet).
 *
 * @param device_ptr  Active sensor device.
 * @param data_out    Caller-allocated struct to fill.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int sensor_hal_read_3axis(hw_device_t *device_ptr,
                          sensor_data_3axis_t *data_out);

/**
 * @brief Read one sample from a single-value sensor.
 *
 * @param device_ptr  Active sensor device.
 * @param data_out    Caller-allocated struct to fill.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int sensor_hal_read_1axis(hw_device_t *device_ptr,
                          sensor_data_1axis_t *data_out);

#endif /* SENSOR_HAL_H */
