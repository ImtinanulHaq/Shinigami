#ifndef SENSOR_HAL_H
#define SENSOR_HAL_H

#include "hal_interface.h"

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR HAL - IIO (Industrial I/O) Subsystem Implementation
// 
// Purpose: Provides unified interface for various sensors
// Features:
// - Accelerometer (3-axis motion)
// - Gyroscope (3-axis rotation)
// - Magnetometer (3-axis magnetic field)
// - Temperature, pressure, proximity sensors
// - Configurable sampling rate
// ══════════════════════════════════════════════════════════════════════════════

// Sensor types
typedef enum {
    SENSOR_TYPE_ACCEL       = 0x01,  // Accelerometer
    SENSOR_TYPE_GYRO        = 0x02,  // Gyroscope
    SENSOR_TYPE_MAGNET      = 0x03,  // Magnetometer
    SENSOR_TYPE_LIGHT       = 0x04,  // Light sensor
    SENSOR_TYPE_PROXIMITY   = 0x05,  // Proximity sensor
    SENSOR_TYPE_PRESSURE    = 0x06,  // Pressure sensor
    SENSOR_TYPE_TEMPERATURE = 0x07,  // Temperature sensor
    SENSOR_TYPE_HUMIDITY    = 0x08,  // Humidity sensor
} sensor_type_t;

// Sensor data for 3-axis sensors (accel, gyro, magnet)
typedef struct {
    float x;  // X-axis value
    float y;  // Y-axis value
    float z;  // Z-axis value
    uint64_t timestamp;  // Timestamp in nanoseconds
} sensor_data_3axis_t;

// Sensor data for single-value sensors
typedef struct {
    float value;         // Sensor value
    uint64_t timestamp;  // Timestamp in nanoseconds
} sensor_data_1axis_t;

// Sensor configuration
typedef struct {
    sensor_type_t type;         // Sensor type
    uint32_t sampling_rate_hz;  // Sampling rate (Hz)
    uint32_t scale;             // Scale factor (device-specific)
    int enable_buffer;          // Enable buffered reading
} sensor_config_t;

// Sensor info
typedef struct {
    char iio_device[64];        // IIO device path
    sensor_type_t type;         // Sensor type
    uint32_t sampling_rate_hz;  // Current sampling rate
    float resolution;           // Resolution (smallest measurable value)
    float max_range;            // Maximum value
    uint32_t fifo_size;         // Hardware FIFO size
} sensor_info_t;

// Sensor control commands
typedef enum {
    SENSOR_CMD_SET_RATE      = 0x2000,  // arg: uint32_t* (Hz)
    SENSOR_CMD_GET_RATE      = 0x2001,  // arg: uint32_t*
    SENSOR_CMD_ENABLE_BUFFER = 0x2002,  // arg: int* (0=disable, 1=enable)
    SENSOR_CMD_CALIBRATE     = 0x2003,  // arg: NULL
    SENSOR_CMD_GET_CONFIG    = 0x2004,  // arg: sensor_config_t*
} sensor_cmd_t;

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR HAL FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Create sensor device
// name: device name (user-friendly, like "accel0")
// iio_device: IIO device identifier (like "iio:device0")
// config: sensor configuration
// Returns: device pointer on success, NULL on failure
hw_device_t* sensor_hal_create(const char* name,
                               const char* iio_device,
                               const sensor_config_t* config);

// Destroy sensor device
void sensor_hal_destroy(hw_device_t* dev);

// Helper: Get default sensor configuration
sensor_config_t sensor_hal_default_config(sensor_type_t type);

// Helper: Get sensor type name as string
const char* sensor_hal_type_string(sensor_type_t type);

// Helper: Read 3-axis sensor data
// dev: sensor device
// data: output data structure
// Returns: 0 on success, negative error code on failure
int sensor_hal_read_3axis(hw_device_t* dev, sensor_data_3axis_t* data);

// Helper: Read single-value sensor data
// dev: sensor device
// data: output data structure
// Returns: 0 on success, negative error code on failure
int sensor_hal_read_1axis(hw_device_t* dev, sensor_data_1axis_t* data);

#endif // SENSOR_HAL_H