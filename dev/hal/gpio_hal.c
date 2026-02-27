#include "gpio_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#define GPIO_BASE_PATH  "/sys/class/gpio"

// Private GPIO device data
typedef struct {
    uint32_t pin_number;
    gpio_config_t config;
    int value_fd;           // File descriptor for value file
    int exported;           // Whether pin is exported
} gpio_priv_t;

// ══════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

gpio_config_t gpio_hal_default_config(uint32_t pin_number)
{
    gpio_config_t config;
    config.pin_number = pin_number;
    config.direction = GPIO_DIR_OUTPUT;
    config.initial_value = GPIO_VALUE_LOW;
    config.edge = GPIO_EDGE_NONE;
    config.pull = GPIO_PULL_NONE;
    return config;
}

static int write_file(const char* path, const char* value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return HAL_ERROR_IO;
    
    ssize_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    close(fd);
    
    return (written == len) ? HAL_SUCCESS : HAL_ERROR_IO;
}

#if 0  /* Currently unused - kept for future use */
static int read_file(const char* path, char* buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return HAL_ERROR_IO;
    
    ssize_t bytes = read(fd, buf, size - 1);
    close(fd);
    
    if (bytes <= 0) return HAL_ERROR_IO;
    
    buf[bytes] = '\0';
    if (bytes > 0 && buf[bytes - 1] == '\n') {
        buf[bytes - 1] = '\0';
    }
    
    return HAL_SUCCESS;
}
#endif

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE OPERATIONS
// ══════════════════════════════════════════════════════════════════════════════

static int gpio_open(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    char path[256];
    char buf[16];
    
    // Export GPIO pin
    snprintf(buf, sizeof(buf), "%u", priv->pin_number);
    if (write_file(GPIO_BASE_PATH "/export", buf) != HAL_SUCCESS) {
        // Pin might already be exported, check if it exists
        snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u", priv->pin_number);
        if (access(path, F_OK) != 0) {
            fprintf(stderr, "[gpio_hal] failed to export gpio%u\n", priv->pin_number);
            return HAL_ERROR_NO_DEVICE;
        }
    }
    priv->exported = 1;
    
    // Set direction
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/direction", priv->pin_number);
    const char* dir_str = (priv->config.direction == GPIO_DIR_INPUT) ? "in" : "out";
    write_file(path, dir_str);
    
    // Set initial value for output
    if (priv->config.direction == GPIO_DIR_OUTPUT) {
        snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/value", priv->pin_number);
        const char* val_str = (priv->config.initial_value == GPIO_VALUE_HIGH) ? "1" : "0";
        write_file(path, val_str);
    }
    
    // Set edge detection
    if (priv->config.edge != GPIO_EDGE_NONE) {
        snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/edge", priv->pin_number);
        const char* edge_str;
        switch (priv->config.edge) {
            case GPIO_EDGE_RISING:  edge_str = "rising"; break;
            case GPIO_EDGE_FALLING: edge_str = "falling"; break;
            case GPIO_EDGE_BOTH:    edge_str = "both"; break;
            default:                edge_str = "none"; break;
        }
        write_file(path, edge_str);
    }
    
    // Open value file for fast access
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/value", priv->pin_number);
    priv->value_fd = open(path, O_RDWR | O_NONBLOCK);
    if (priv->value_fd < 0) {
        fprintf(stderr, "[gpio_hal] failed to open value file\n");
        return HAL_ERROR_IO;
    }
    
    dev->fd = priv->value_fd;
    dev->state = HAL_STATE_OPEN;
    printf("[gpio_hal] gpio%u opened (%s)\n", priv->pin_number, dir_str);
    
    return HAL_SUCCESS;
}

static int gpio_close(hw_device_t* dev)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    
    if (priv->value_fd >= 0) {
        close(priv->value_fd);
        priv->value_fd = -1;
    }
    
    // Unexport GPIO
    if (priv->exported) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", priv->pin_number);
        write_file(GPIO_BASE_PATH "/unexport", buf);
        priv->exported = 0;
    }
    
    dev->fd = -1;
    dev->state = HAL_STATE_CLOSED;
    printf("[gpio_hal] gpio%u closed\n", priv->pin_number);
    
    return HAL_SUCCESS;
}

static int gpio_start(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    if (dev->state != HAL_STATE_OPEN) return HAL_ERROR_INVALID;
    
    dev->state = HAL_STATE_ACTIVE;
    return HAL_SUCCESS;
}

static int gpio_stop(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    dev->state = HAL_STATE_OPEN;
    return HAL_SUCCESS;
}

static ssize_t gpio_read(hw_device_t* dev, void* buf, size_t size)
{
    if (!dev || !dev->priv || !buf || size < 1) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    char value_str[4];
    
    lseek(priv->value_fd, 0, SEEK_SET);
    ssize_t bytes = read(priv->value_fd, value_str, sizeof(value_str) - 1);
    if (bytes <= 0) return HAL_ERROR_IO;
    
    value_str[bytes] = '\0';
    ((uint8_t*)buf)[0] = (value_str[0] == '1') ? 1 : 0;
    
    return 1;
}

static ssize_t gpio_write(hw_device_t* dev, const void* buf, size_t size)
{
    if (!dev || !dev->priv || !buf || size < 1) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    const char* value_str = (((uint8_t*)buf)[0] != 0) ? "1" : "0";
    
    lseek(priv->value_fd, 0, SEEK_SET);
    ssize_t bytes = write(priv->value_fd, value_str, 1);
    
    return (bytes == 1) ? 1 : HAL_ERROR_IO;
}

static int gpio_control(hw_device_t* dev, uint32_t cmd, void* arg)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    (void)priv;
    
    switch (cmd) {
        case GPIO_CMD_SET_VALUE:
            if (!arg) return HAL_ERROR_INVALID;
            return gpio_hal_set_value(dev, *(gpio_value_t*)arg);
            
        case GPIO_CMD_GET_VALUE:
            if (!arg) return HAL_ERROR_INVALID;
            return gpio_hal_get_value(dev, (gpio_value_t*)arg);
            
        case GPIO_CMD_WAIT_EDGE:
            if (!arg) return HAL_ERROR_INVALID;
            return gpio_hal_wait_interrupt(dev, *(uint32_t*)arg);
            
        default:
            return HAL_ERROR_NOT_SUPPORT;
    }
}

static int gpio_get_info(hw_device_t* dev, void* info)
{
    if (!dev || !dev->priv || !info) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    gpio_info_t* gpio_info = (gpio_info_t*)info;
    
    gpio_info->pin_number = priv->pin_number;
    gpio_info->direction = priv->config.direction;
    gpio_info->edge = priv->config.edge;
    gpio_hal_get_value(dev, &gpio_info->value);
    
    return HAL_SUCCESS;
}

static const hw_device_ops_t gpio_ops = {
    .open     = gpio_open,
    .close    = gpio_close,
    .start    = gpio_start,
    .stop     = gpio_stop,
    .read     = gpio_read,
    .write    = gpio_write,
    .control  = gpio_control,
    .get_info = gpio_get_info,
};

// ══════════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

hw_device_t* gpio_hal_create(const char* name, const gpio_config_t* config)
{
    if (!name || !config) return NULL;
    
    hw_device_t* dev = malloc(sizeof(hw_device_t));
    if (!dev) return NULL;
    
    if (hal_device_init(dev, name, HAL_DEVICE_TYPE_GPIO) != HAL_SUCCESS) {
        free(dev);
        return NULL;
    }
    
    gpio_priv_t* priv = calloc(1, sizeof(gpio_priv_t));
    if (!priv) {
        hal_device_destroy(dev);
        free(dev);
        return NULL;
    }
    
    priv->pin_number = config->pin_number;
    memcpy(&priv->config, config, sizeof(gpio_config_t));
    priv->value_fd = -1;
    priv->exported = 0;
    
    dev->ops = &gpio_ops;
    dev->priv = priv;
    
    printf("[gpio_hal] created device %s for gpio%u\n", name, config->pin_number);
    
    return dev;
}

void gpio_hal_destroy(hw_device_t* dev)
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

int gpio_hal_set_value(hw_device_t* dev, gpio_value_t value)
{
    if (!dev) return HAL_ERROR_INVALID;
    
    uint8_t val = (value == GPIO_VALUE_HIGH) ? 1 : 0;
    return (dev->ops->write(dev, &val, 1) > 0) ? HAL_SUCCESS : HAL_ERROR_IO;
}

int gpio_hal_get_value(hw_device_t* dev, gpio_value_t* value)
{
    if (!dev || !value) return HAL_ERROR_INVALID;
    
    uint8_t val;
    if (dev->ops->read(dev, &val, 1) <= 0) {
        return HAL_ERROR_IO;
    }
    
    *value = (val != 0) ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW;
    return HAL_SUCCESS;
}

int gpio_hal_toggle(hw_device_t* dev)
{
    if (!dev) return HAL_ERROR_INVALID;
    
    gpio_value_t current;
    if (gpio_hal_get_value(dev, &current) != HAL_SUCCESS) {
        return HAL_ERROR_IO;
    }
    
    gpio_value_t new_val = (current == GPIO_VALUE_HIGH) ? GPIO_VALUE_LOW : GPIO_VALUE_HIGH;
    return gpio_hal_set_value(dev, new_val);
}

int gpio_hal_wait_interrupt(hw_device_t* dev, uint32_t timeout_ms)
{
    if (!dev || !dev->priv) return HAL_ERROR_INVALID;
    
    gpio_priv_t* priv = (gpio_priv_t*)dev->priv;
    
    // Dummy read to clear any pending interrupt
    char dummy[4];
    lseek(priv->value_fd, 0, SEEK_SET);
    ssize_t dummy_read = read(priv->value_fd, dummy, sizeof(dummy));
    (void)dummy_read;
    
    // Use poll() to wait for edge
    struct pollfd pfd;
    pfd.fd = priv->value_fd;
    pfd.events = POLLPRI | POLLERR;
    pfd.revents = 0;
    
    int timeout = (timeout_ms == 0) ? -1 : (int)timeout_ms;
    int ret = poll(&pfd, 1, timeout);
    
    if (ret < 0) {
        return HAL_ERROR_IO;
    } else if (ret == 0) {
        return HAL_ERROR_TIMEOUT;
    }
    
    return HAL_SUCCESS;
}