#include "sensor_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

// IIO sysfs base path
#define IIO_BASE_PATH  "/sys/bus/iio/devices"

// Private sensor device data
typedef struct {
    char iio_path[256];             // Full IIO device path
    sensor_config_t config;         // Configuration
    int scale_fd;                   // File descriptor for scale
    int sampling_fd;                // File descriptor for sampling_frequency
    float scale_value;              // Cached scale value
    float offset_x, offset_y, offset_z;  // Calibration offsets
} sensor_priv_t;

// ══════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

const char* sensor_hal_type_string(sensor_type_t type)
{
    switch (type) {
        case SENSOR_TYPE_ACCEL:       return "accelerometer";
        case SENSOR_TYPE_GYRO:        return "gyroscope";
        case SENSOR_TYPE_MAGNET:      return "magnetometer";
        case SENSOR_TYPE_LIGHT:       return "light";
        case SENSOR_TYPE_PROXIMITY:   return "proximity";
        case SENSOR_TYPE_PRESSURE:    return "pressure";
        case SENSOR_TYPE_TEMPERATURE: return "temperature";
        case SENSOR_TYPE_HUMIDITY:    return "humidity";
        default:                      return "unknown";
    }
}

sensor_config_t sensor_hal_default_config(sensor_type_t type)
{
    sensor_config_t config;
    config.type = type;
    config.sampling_rate_hz = 100;  // 100 Hz default
    config.scale = 1;
    config.enable_buffer = 0;
    return config;
}

// Read a single value from sysfs file
static int read_sysfs_value(const char* path, char* buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return HAL_ERROR_NO_DEVICE;
    }
    
    ssize_t bytes = read(fd, buf, size - 1);
    close(fd);
    
    if (bytes <= 0) {
        return HAL_ERROR_IO;
    }
    
    buf[bytes] = '\0';
    
    // Remove trailing newline
    if (bytes > 0 && buf[bytes - 1] == '\n') {
        buf[bytes - 1] = '\0';
    }
    
    return HAL_SUCCESS;
}

// Write a value to sysfs file
static int write_sysfs_value(const char* path, const char* value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return HAL_ERROR_NO_DEVICE;
    }
    
    ssize_t bytes = write(fd, value, strlen(value));
    close(fd);
    
    if (bytes != (ssize_t)strlen(value)) {
        return HAL_ERROR_IO;
    }
    
    return HAL_SUCCESS;
}

// Get current timestamp in nanoseconds
static uint64_t get_timestamp_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Get channel name prefix based on sensor type
static const char* get_channel_prefix(sensor_type_t type)
{
    switch (type) {
        case SENSOR_TYPE_ACCEL:       return "in_accel";
        case SENSOR_TYPE_GYRO:        return "in_anglvel";
        case SENSOR_TYPE_MAGNET:      return "in_magn";
        case SENSOR_TYPE_LIGHT:       return "in_illuminance";
        case SENSOR_TYPE_PROXIMITY:   return "in_proximity";
        case SENSOR_TYPE_PRESSURE:    return "in_pressure";
        case SENSOR_TYPE_TEMPERATURE: return "in_temp";
        case SENSOR_TYPE_HUMIDITY:    return "in_humidity";
        default:                      return "in_unknown";
    }
}

// Read axis value from IIO device
static int read_axis_value(sensor_priv_t* priv, const char* axis, float* value)
{
    char path[512];
    char buf[64];
    const char* prefix = get_channel_prefix(priv->config.type);
    
    // Construct path: /sys/bus/iio/devices/iio:deviceN/in_accel_x_raw
    snprintf(path, sizeof(path), "%s/%s_%s_raw", priv->iio_path, prefix, axis);
    
    if (read_sysfs_value(path, buf, sizeof(buf)) != HAL_SUCCESS) {
        return HAL_ERROR_IO;
    }
    
    // Convert to integer
    int raw_value = atoi(buf);
    
    // Apply scale
    *value = raw_value * priv->scale_value;
    
    return HAL_SUCCESS;
}

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE OPERATIONS
// ══════════════════════════════════════════════════════════════════════════════

static int sensor_open(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    sensor_priv_t* priv = (sensor_priv_t*)dev->priv;
    char path[512];
    char buf[64];
    
    // Read scale value
    snprintf(path, sizeof(path), "%s/%s_scale", priv->iio_path,
             get_channel_prefix(priv->config.type));
    
    if (read_sysfs_value(path, buf, sizeof(buf)) == HAL_SUCCESS) {
        priv->scale_value = atof(buf);
    } else {
        priv->scale_value = 1.0f;  // Default scale
    }
    
    // Set sampling frequency if possible
    snprintf(path, sizeof(path), "%s/sampling_frequency", priv->iio_path);
    snprintf(buf, sizeof(buf), "%u", priv->config.sampling_rate_hz);
    write_sysfs_value(path, buf);  // Ignore errors
    
    dev->state = HAL_STATE_OPEN;
    printf("[sensor_hal] device %s opened (%s)\n", dev->name,
           sensor_hal_type_string(priv->config.type));
    
    return HAL_SUCCESS;
}

static int sensor_close(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    
    dev->state = HAL_STATE_CLOSED;
    printf("[sensor_hal] device %s closed\n", dev->name);
    
    return HAL_SUCCESS;
}

static int sensor_start(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_OPEN) return HAL_ERROR_INVALID;
    
    dev->state = HAL_STATE_ACTIVE;
    printf("[sensor_hal] device %s started\n", dev->name);
    
    return HAL_SUCCESS;
}

static int sensor_stop(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    
    dev->state = HAL_STATE_OPEN;
    printf("[sensor_hal] device %s stopped\n", dev->name);
    
    return HAL_SUCCESS;
}

static ssize_t sensor_read(hw_device_t* dev, void* buf, size_t size)
{
    if (!dev || !dev->priv || !buf) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_ACTIVE) return HAL_ERROR_INVALID;
    
    sensor_priv_t* priv = (sensor_priv_t*)dev->priv;
    
    // Check if sensor has 3 axes
    if (priv->config.type == SENSOR_TYPE_ACCEL ||
        priv->config.type == SENSOR_TYPE_GYRO ||
        priv->config.type == SENSOR_TYPE_MAGNET) {
        
        if (size < sizeof(sensor_data_3axis_t)) {
            return HAL_ERROR_INVALID;
        }
        
        sensor_data_3axis_t* data = (sensor_data_3axis_t*)buf;
        
        // Read all three axes
        if (read_axis_value(priv, "x", &data->x) != HAL_SUCCESS) return HAL_ERROR_IO;
        if (read_axis_value(priv, "y", &data->y) != HAL_SUCCESS) return HAL_ERROR_IO;
        if (read_axis_value(priv, "z", &data->z) != HAL_SUCCESS) return HAL_ERROR_IO;
        
        // Apply calibration offsets
        data->x += priv->offset_x;
        data->y += priv->offset_y;
        data->z += priv->offset_z;
        
        data->timestamp = get_timestamp_ns();
        
        return sizeof(sensor_data_3axis_t);
        
    } else {
        // Single-value sensor
        if (size < sizeof(sensor_data_1axis_t)) {
            return HAL_ERROR_INVALID;
        }
        
        sensor_data_1axis_t* data = (sensor_data_1axis_t*)buf;
        
        if (read_axis_value(priv, "input", &data->value) != HAL_SUCCESS) {
            return HAL_ERROR_IO;
        }
        
        data->timestamp = get_timestamp_ns();
        
        return sizeof(sensor_data_1axis_t);
    }
}

static ssize_t sensor_write(hw_device_t* dev, const void* buf, size_t size)
{
    // Sensors are read-only
    (void)dev; (void)buf; (void)size;
    return HAL_ERROR_NOT_SUPPORT;
}

static int sensor_control(hw_device_t* dev, uint32_t cmd, void* arg)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    sensor_priv_t* priv = (sensor_priv_t*)dev->priv;
    
    switch (cmd) {
        case SENSOR_CMD_SET_RATE: {
            if (!arg) return HAL_ERROR_INVALID;
            
            uint32_t rate = *(uint32_t*)arg;
            priv->config.sampling_rate_hz = rate;
            
            // Write to sysfs
            char path[512];
            char buf[64];
            snprintf(path, sizeof(path), "%s/sampling_frequency", priv->iio_path);
            snprintf(buf, sizeof(buf), "%u", rate);
            write_sysfs_value(path, buf);
            
            return HAL_SUCCESS;
        }
        
        case SENSOR_CMD_GET_RATE:
            if (!arg) return HAL_ERROR_INVALID;
            *(uint32_t*)arg = priv->config.sampling_rate_hz;
            return HAL_SUCCESS;
            
        case SENSOR_CMD_CALIBRATE:
            // Simple calibration: read current values as offsets
            if (priv->config.type == SENSOR_TYPE_ACCEL ||
                priv->config.type == SENSOR_TYPE_GYRO ||
                priv->config.type == SENSOR_TYPE_MAGNET) {
                
                float x, y, z;
                read_axis_value(priv, "x", &x);
                read_axis_value(priv, "y", &y);
                read_axis_value(priv, "z", &z);
                
                priv->offset_x = -x;
                priv->offset_y = -y;
                priv->offset_z = -z;
                
                printf("[sensor_hal] calibrated: offset x=%.3f y=%.3f z=%.3f\n",
                       priv->offset_x, priv->offset_y, priv->offset_z);
            }
            return HAL_SUCCESS;
            
        case SENSOR_CMD_GET_CONFIG:
            if (!arg) return HAL_ERROR_INVALID;
            memcpy(arg, &priv->config, sizeof(sensor_config_t));
            return HAL_SUCCESS;
            
        default:
            return HAL_ERROR_NOT_SUPPORT;
    }
}

static int sensor_get_info(hw_device_t* dev, void* info)
{
    if (!dev || !dev->priv || !info) return HAL_ERROR_INVALID;
    
    sensor_priv_t* priv = (sensor_priv_t*)dev->priv;
    sensor_info_t* sensor_info = (sensor_info_t*)info;
    
    size_t copy_len = strlen(priv->iio_path);
    if (copy_len >= sizeof(sensor_info->iio_device))
        copy_len = sizeof(sensor_info->iio_device) - 1;
    memcpy(sensor_info->iio_device, priv->iio_path, copy_len);
    sensor_info->iio_device[copy_len] = '\0';
    sensor_info->type = priv->config.type;
    sensor_info->sampling_rate_hz = priv->config.sampling_rate_hz;
    sensor_info->resolution = priv->scale_value;
    sensor_info->max_range = 0.0f;  // Unknown
    sensor_info->fifo_size = 0;     // Unknown
    
    return HAL_SUCCESS;
}

// Device operations
static const hw_device_ops_t sensor_ops = {
    .open     = sensor_open,
    .close    = sensor_close,
    .start    = sensor_start,
    .stop     = sensor_stop,
    .read     = sensor_read,
    .write    = sensor_write,
    .control  = sensor_control,
    .get_info = sensor_get_info,
};

// ══════════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

hw_device_t* sensor_hal_create(const char* name,
                               const char* iio_device,
                               const sensor_config_t* config)
{
    if (!name || !iio_device || !config) return NULL;
    
    // Allocate device
    hw_device_t* dev = malloc(sizeof(hw_device_t));
    if (!dev) return NULL;
    
    // Initialize device
    if (hal_device_init(dev, name, HAL_DEVICE_TYPE_SENSOR) != HAL_SUCCESS) {
        free(dev);
        return NULL;
    }
    
    // Allocate private data
    sensor_priv_t* priv = calloc(1, sizeof(sensor_priv_t));
    if (!priv) {
        hal_device_destroy(dev);
        free(dev);
        return NULL;
    }
    
    // Initialize private data
    snprintf(priv->iio_path, sizeof(priv->iio_path), "%s/%s", IIO_BASE_PATH, iio_device);
    memcpy(&priv->config, config, sizeof(sensor_config_t));
    priv->scale_value = 1.0f;
    priv->offset_x = priv->offset_y = priv->offset_z = 0.0f;
    
    // Set device operations
    dev->ops = &sensor_ops;
    dev->priv = priv;
    
    printf("[sensor_hal] created device %s for %s (%s)\n",
           name, iio_device, sensor_hal_type_string(config->type));
    
    return dev;
}

void sensor_hal_destroy(hw_device_t* dev)
{
    if (!dev) return;
    
    if (dev->state != HAL_STATE_CLOSED && dev->ops && dev->ops->close) {
        dev->ops->close(dev);
    }
    
    if (dev->priv) {
        free(dev->priv);
        dev->priv = NULL;
    }
    
    hal_device_destroy(dev);
    free(dev);
}

int sensor_hal_read_3axis(hw_device_t* dev, sensor_data_3axis_t* data)
{
    if (!dev || !data) return HAL_ERROR_INVALID;
    
    ssize_t bytes = dev->ops->read(dev, data, sizeof(sensor_data_3axis_t));
    if (bytes < 0) return (int)bytes;
    
    return HAL_SUCCESS;
}

int sensor_hal_read_1axis(hw_device_t* dev, sensor_data_1axis_t* data)
{
    if (!dev || !data) return HAL_ERROR_INVALID;
    
    ssize_t bytes = dev->ops->read(dev, data, sizeof(sensor_data_1axis_t));
    if (bytes < 0) return (int)bytes;
    
    return HAL_SUCCESS;
}