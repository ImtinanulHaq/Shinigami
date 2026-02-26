/**
 * @file sensor_hal.h
 * @brief Sensor HAL — Linux IIO subsystem interface.
 *
 * Supports 3-axis inertial sensors (accelerometer, gyroscope, magnetometer)
 * and single-value environmental sensors (temperature, pressure, etc.).
 *
 * Two read modes are selected at open time:
 *   - Buffer mode (preferred): one read() syscall on /dev/iio:deviceN per
 *     sample; hardware-stamped timestamp; atomic multi-channel capture.
 *   - Sysfs fallback: three open/read/close cycles on in_*_raw attributes
 *     per 3-axis sample; software timestamp via CLOCK_MONOTONIC.
 *
 * Buffer mode is activated by setting enable_buffer = 1 in the config.
 * If the kernel driver does not expose /dev/iio:deviceN, the implementation
 * silently falls back to sysfs mode.
 *
 * PROPOSED CHANGES:
 *   - Added SENSOR_CMD_SET_SCALE for changing the hardware scale factor at
 *     runtime without a full close/open cycle.
 *   - Added SENSOR_CMD_GET_OFFSETS / SENSOR_CMD_SET_OFFSETS to read and
 *     write per-axis calibration offsets directly.
 */

#ifndef SENSOR_HAL_H
#define SENSOR_HAL_H

#include "hal_interface.h"

/* ── sensor types ─────────────────────────────────────────────────────── */

/**
 * @brief Classification of the physical quantity being measured.
 *
 * @p SENSOR_TYPE_ACCEL        3-axis linear acceleration (m/s²).
 * @p SENSOR_TYPE_GYRO         3-axis angular velocity (rad/s).
 * @p SENSOR_TYPE_MAGNET       3-axis magnetic field (µT).
 * @p SENSOR_TYPE_LIGHT        Ambient illuminance (lux).
 * @p SENSOR_TYPE_PROXIMITY    Proximity (device-specific units).
 * @p SENSOR_TYPE_PRESSURE     Barometric pressure (hPa).
 * @p SENSOR_TYPE_TEMPERATURE  Ambient temperature (°C).
 * @p SENSOR_TYPE_HUMIDITY     Relative humidity (% RH).
 */
typedef enum {
  SENSOR_TYPE_ACCEL       = 0x01,
  SENSOR_TYPE_GYRO        = 0x02,
  SENSOR_TYPE_MAGNET      = 0x03,
  SENSOR_TYPE_LIGHT       = 0x04,
  SENSOR_TYPE_PROXIMITY   = 0x05,
  SENSOR_TYPE_PRESSURE    = 0x06,
  SENSOR_TYPE_TEMPERATURE = 0x07,
  SENSOR_TYPE_HUMIDITY    = 0x08,
} sensor_type_t;

/* ── data structures ──────────────────────────────────────────────────── */

/**
 * @brief One sample from a 3-axis sensor.
 *
 * Values are in SI units after the device scale factor is applied: m/s²
 * for accelerometers, rad/s for gyroscopes, µT for magnetometers.  In
 * buffer mode the timestamp is supplied by the IIO hardware trigger and
 * reflects actual DMA-completion time; in sysfs mode it is the software
 * time at which the last axis read returned.
 *
 * @p x          X-axis measurement in SI units.
 * @p y          Y-axis measurement in SI units.
 * @p z          Z-axis measurement in SI units.
 * @p timestamp  Nanoseconds on CLOCK_MONOTONIC at sample time.
 */
typedef struct {
  float    x;
  float    y;
  float    z;
  uint64_t timestamp;
} sensor_data_3axis_t;

/**
 * @brief One sample from a single-value sensor.
 *
 * @p value      Measurement in the sensor's native SI unit.
 * @p timestamp  Nanoseconds on CLOCK_MONOTONIC at sample time.
 */
typedef struct {
  float    value;
  uint64_t timestamp;
} sensor_data_1axis_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Configuration for a sensor HAL device.
 *
 * @p type              Physical quantity this sensor measures.
 * @p sampling_rate_hz  Requested sample rate written to sampling_frequency.
 * @p scale             Integer scale multiplier; use 1 for default.
 * @p enable_buffer     Non-zero to request IIO hardware buffer mode.
 */
typedef struct {
  sensor_type_t type;
  uint32_t      sampling_rate_hz;
  uint32_t      scale;
  int           enable_buffer;
} sensor_config_t;

/* ── per-axis calibration offsets [PROPOSED] ──────────────────────────── */

/**
 * @brief Per-axis calibration offsets applied after scale.
 *
 * Added as a named struct so SENSOR_CMD_GET_OFFSETS / SENSOR_CMD_SET_OFFSETS
 * can transfer all three offsets atomically in a single control() call.
 *
 * @p x  Offset added to the scaled X reading.
 * @p y  Offset added to the scaled Y reading.
 * @p z  Offset added to the scaled Z reading.
 */
typedef struct {
  float x;
  float y;
  float z;
} sensor_offsets_t;

/* ── device info ──────────────────────────────────────────────────────── */

/**
 * @brief Runtime snapshot of sensor device state.
 *
 * @p iio_device_path      Absolute sysfs path, e.g. /sys/bus/iio/devices/…
 * @p type                 Physical quantity from sensor_type_t.
 * @p sampling_rate_hz     Currently active sample rate in Hz.
 * @p resolution           Scale factor (units-per-LSB) read from sysfs.
 * @p max_range            Maximum measurable value; 0 if not queried.
 * @p fifo_size            Hardware FIFO depth in samples; 0 if none.
 * @p buffer_mode_active   Non-zero when IIO buffer mode is active.
 */
typedef struct {
  char          iio_device_path[64];
  sensor_type_t type;
  uint32_t      sampling_rate_hz;
  float         resolution;
  float         max_range;
  uint32_t      fifo_size;
  int           buffer_mode_active;
} sensor_info_t;

/* ── control commands ─────────────────────────────────────────────────── */

/**
 * @brief Commands accepted by the sensor device control() operation.
 *
 * @p SENSOR_CMD_SET_RATE      arg: const uint32_t *   Target rate in Hz.
 * @p SENSOR_CMD_GET_RATE      arg: uint32_t *          Receives current rate.
 * @p SENSOR_CMD_ENABLE_BUFFER arg: const int *         0=disable, 1=enable.
 * @p SENSOR_CMD_CALIBRATE     arg: NULL               Zero current reading.
 * @p SENSOR_CMD_GET_CONFIG    arg: sensor_config_t *  Receives live config.
 * @p SENSOR_CMD_SET_SCALE     [PROPOSED] arg: const float *   New scale.
 * @p SENSOR_CMD_GET_OFFSETS   [PROPOSED] arg: sensor_offsets_t *  Reads offsets.
 * @p SENSOR_CMD_SET_OFFSETS   [PROPOSED] arg: const sensor_offsets_t *  Writes.
 */
typedef enum {
  SENSOR_CMD_SET_RATE      = 0x2000,
  SENSOR_CMD_GET_RATE      = 0x2001,
  SENSOR_CMD_ENABLE_BUFFER = 0x2002,
  SENSOR_CMD_CALIBRATE     = 0x2003,
  SENSOR_CMD_GET_CONFIG    = 0x2004,
  SENSOR_CMD_SET_SCALE     = 0x2005,  /* [PROPOSED] */
  SENSOR_CMD_GET_OFFSETS   = 0x2006,  /* [PROPOSED] */
  SENSOR_CMD_SET_OFFSETS   = 0x2007,  /* [PROPOSED] */
} sensor_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/** @brief Allocate and initialise a sensor HAL device. */
hw_device_t *sensor_hal_create(const char *device_name,
                               const char *iio_device_id,
                               const sensor_config_t *sensor_config);

/** @brief Release all resources held by a sensor device. */
void sensor_hal_destroy(hw_device_t *device_ptr);

/** @brief Return the default configuration for a given sensor type. */
sensor_config_t sensor_hal_default_config(sensor_type_t sensor_type);

/** @brief Return a human-readable string for a sensor type. */
const char *sensor_hal_type_string(sensor_type_t sensor_type);

/** @brief Read one sample from a 3-axis sensor (accel, gyro, magnet). */
int sensor_hal_read_3axis(hw_device_t *device_ptr,
                          sensor_data_3axis_t *data_out);

/** @brief Read one sample from a single-value sensor. */
int sensor_hal_read_1axis(hw_device_t *device_ptr,
                          sensor_data_1axis_t *data_out);

#endif /* SENSOR_HAL_H */
