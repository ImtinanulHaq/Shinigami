/**
 * @file gpio_hal.c
 * @brief GPIO HAL implementation — sysfs /sys/class/gpio backend.
 *
 * Security changes vs. original:
 *   - validate_gpio_pin() rejects pin numbers above GPIO_PIN_NUMBER_MAX
 *     before any sysfs write, preventing out-of-range kernel arguments.
 *   - control() validates arg pointer and alignment before dereference.
 *
 * Performance notes:
 *   - The value file (/sys/class/gpio/gpioN/value) is opened once in
 *     gpio_open() and held for the device lifetime.  Each read/write is
 *     then a single lseek+read or lseek+write rather than three syscalls
 *     (open, read/write, close) per access.
 *   - gpio_hal_wait_interrupt() uses poll(POLLPRI) which puts the thread
 *     to sleep in the kernel scheduler; CPU is zero while waiting.
 */

#define _DEFAULT_SOURCE
#include "gpio_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── path constants ───────────────────────────────────────────────────── */

#define GPIO_BASE_PATH "/sys/class/gpio"

/* ── private device data ──────────────────────────────────────────────── */

/**
 * @brief Internal state for a GPIO HAL device.
 */
typedef struct {
  uint32_t pin_number;
  gpio_config_t config;
  int value_fd;
  int exported;
} gpio_priv_t;

/* ── control-command specification table ──────────────────────────────── */

/**
 * @brief Maps a control command to its required argument size (0 = no arg).
 */
typedef struct {
  uint32_t command;
  size_t arg_size;
} gpio_cmd_spec_t;

static const gpio_cmd_spec_t GPIO_CMD_SPECS[] = {
    {GPIO_CMD_SET_DIRECTION, sizeof(gpio_direction_t)},
    {GPIO_CMD_GET_DIRECTION, sizeof(gpio_direction_t)},
    {GPIO_CMD_SET_VALUE, sizeof(gpio_value_t)},
    {GPIO_CMD_GET_VALUE, sizeof(gpio_value_t)},
    {GPIO_CMD_SET_EDGE, sizeof(gpio_edge_t)},
    {GPIO_CMD_GET_EDGE, sizeof(gpio_edge_t)},
    {GPIO_CMD_WAIT_EDGE, sizeof(uint32_t)},
};

/* ── security: pin number validation ─────────────────────────────────── */

/**
 * @brief Confirm @p pin_number is within the allowed range for this platform.
 *
 * Writing an out-of-range pin number to /sys/class/gpio/export passes an
 * unexpected value into the kernel's GPIO subsystem.  The range check is
 * performed before any file I/O.
 *
 * @param pin_number  Requested GPIO pin number.
 * @return 0 if valid, -1 if out of range.
 */
static int validate_gpio_pin(uint32_t pin_number) {
  return (pin_number <= GPIO_PIN_NUMBER_MAX) ? 0 : -1;
}

/* ── security: control arg validation ────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 * @param control_command  Command code from @ref gpio_cmd_t.
 * @param command_arg      Argument pointer supplied by caller.
 * @return 0 if valid, -1 if invalid or unknown command.
 */
static int validate_control_arg(uint32_t control_command,
                                const void *command_arg) {
  for (size_t i = 0; i < HAL_ARRAY_SIZE(GPIO_CMD_SPECS); i++) {
    if (GPIO_CMD_SPECS[i].command != control_command)
      continue;
    if (GPIO_CMD_SPECS[i].arg_size == 0)
      return 0;
    if (!command_arg)
      return -1;
    if ((uintptr_t)command_arg % sizeof(uint32_t) != 0)
      return -1;
    return 0;
  }
  return -1;
}

/* ── sysfs file helpers ───────────────────────────────────────────────── */

/**
 * @brief Write a string to a sysfs attribute file.
 * @return 0 on success, -1 on error.
 */
static int sysfs_write(const char *sysfs_path, const char *value) {
  int fd = open(sysfs_path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  ssize_t len = (ssize_t)strlen(value);
  ssize_t written = write(fd, value, (size_t)len);
  close(fd);

  return (written == len) ? 0 : -1;
}

/* ── vtable implementations ───────────────────────────────────────────── */

static int gpio_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;
  char path[256];
  char pin_str[16];

  snprintf(pin_str, sizeof(pin_str), "%u", priv->pin_number);

  /* Export the pin — creates /sys/class/gpio/gpioN/ directory. */
  if (sysfs_write(GPIO_BASE_PATH "/export", pin_str) != 0) {
    /*
     * Failure here usually means the pin was already exported by a
     * previous (possibly crashed) process.  Check whether the sysfs
     * directory exists; if it does we can proceed.
     */
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u", priv->pin_number);
    if (access(path, F_OK) != 0) {
      fprintf(stderr, "[gpio_hal] failed to export gpio%u: %s\n",
              priv->pin_number, strerror(errno));
      return HAL_ERROR_NO_DEVICE;
    }
  }
  priv->exported = 1;

  /* Set direction. */
  snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/direction",
           priv->pin_number);
  const char *dir_str =
      (priv->config.direction == GPIO_DIR_INPUT) ? "in" : "out";
  sysfs_write(path, dir_str);

  /* Set initial output value. */
  if (priv->config.direction == GPIO_DIR_OUTPUT) {
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/value",
             priv->pin_number);
    const char *val_str =
        (priv->config.initial_value == GPIO_VALUE_HIGH) ? "1" : "0";
    sysfs_write(path, val_str);
  }

  /* Configure edge detection for interrupt-capable inputs. */
  if (priv->config.edge != GPIO_EDGE_NONE) {
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/edge",
             priv->pin_number);
    const char *edge_str;
    switch (priv->config.edge) {
    case GPIO_EDGE_RISING:
      edge_str = "rising";
      break;
    case GPIO_EDGE_FALLING:
      edge_str = "falling";
      break;
    case GPIO_EDGE_BOTH:
      edge_str = "both";
      break;
    default:
      edge_str = "none";
      break;
    }
    sysfs_write(path, edge_str);
  }

  /*
   * Keep the value file open for the device's lifetime.  lseek + read/write
   * on a persistent fd costs one syscall each; open + read + close per
   * access would cost three — a 3× reduction for high-frequency toggling.
   */
  snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/value",
           priv->pin_number);
  priv->value_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (priv->value_fd < 0) {
    fprintf(stderr, "[gpio_hal] open value file gpio%u: %s\n", priv->pin_number,
            strerror(errno));
    return HAL_ERROR_IO;
  }

  device_ptr->fd = priv->value_fd;
  device_ptr->state = HAL_STATE_OPEN;
  printf("[gpio_hal] gpio%u opened (%s)\n", priv->pin_number, dir_str);
  return HAL_SUCCESS;
}

static int gpio_close(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;

  if (priv->value_fd >= 0) {
    close(priv->value_fd);
    priv->value_fd = -1;
  }

  /*
   * Unexport the pin so other processes and future HAL instances can
   * claim it.  Forgetting to unexport is a common embedded bug that
   * causes "Device or resource busy" errors on the next init cycle.
   */
  if (priv->exported) {
    char pin_str[16];
    snprintf(pin_str, sizeof(pin_str), "%u", priv->pin_number);
    sysfs_write(GPIO_BASE_PATH "/unexport", pin_str);
    priv->exported = 0;
  }

  device_ptr->fd = -1;
  device_ptr->state = HAL_STATE_CLOSED;
  printf("[gpio_hal] gpio%u closed\n", priv->pin_number);
  return HAL_SUCCESS;
}

static int gpio_start(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_OPEN)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_ACTIVE;
  return HAL_SUCCESS;
}

static int gpio_stop(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_OPEN;
  return HAL_SUCCESS;
}

/**
 * @brief Read the current pin logic level.
 *
 * lseek(SEEK_SET, 0) repositions within the sysfs virtual file so the
 * next read() returns the current hardware state, not a cached byte from
 * a prior read that left the position at EOF.
 *
 * @return 1 byte (0 or 1) on success, or negative HAL_ERROR_*.
 */
static ssize_t gpio_read(hw_device_t *device_ptr, void *data_buffer,
                         size_t buffer_size) {
  if (!device_ptr || !device_ptr->priv || !data_buffer || buffer_size < 1u)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;
  char level_char[4];

  lseek(priv->value_fd, 0, SEEK_SET);
  ssize_t bytes = read(priv->value_fd, level_char, sizeof(level_char) - 1u);
  if (bytes <= 0)
    return HAL_ERROR_IO;

  level_char[bytes] = '\0';
  ((uint8_t *)data_buffer)[0] = (level_char[0] == '1') ? 1u : 0u;

  return 1;
}

/**
 * @brief Drive an output pin by writing '0' or '1' to the value file.
 * @return 1 on success, or negative HAL_ERROR_*.
 */
static ssize_t gpio_write(hw_device_t *device_ptr, const void *data_buffer,
                          size_t data_size) {
  if (!device_ptr || !device_ptr->priv || !data_buffer || data_size < 1u)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;
  const char *level_str = (((const uint8_t *)data_buffer)[0] != 0u) ? "1" : "0";

  lseek(priv->value_fd, 0, SEEK_SET);
  ssize_t bytes = write(priv->value_fd, level_str, 1u);

  return (bytes == 1) ? 1 : HAL_ERROR_IO;
}

static int gpio_control(hw_device_t *device_ptr, uint32_t control_command,
                        void *command_arg) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;
  if (validate_control_arg(control_command, command_arg) != 0)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;
  char path[256];

  switch (control_command) {
  case GPIO_CMD_SET_DIRECTION: {
    gpio_direction_t dir = *(const gpio_direction_t *)command_arg;
    priv->config.direction = dir;
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/direction",
             priv->pin_number);
    sysfs_write(path, (dir == GPIO_DIR_INPUT) ? "in" : "out");
    return HAL_SUCCESS;
  }

  case GPIO_CMD_GET_DIRECTION:
    *(gpio_direction_t *)command_arg = priv->config.direction;
    return HAL_SUCCESS;

  case GPIO_CMD_SET_VALUE:
    return gpio_hal_set_value(device_ptr, *(const gpio_value_t *)command_arg);

  case GPIO_CMD_GET_VALUE:
    return gpio_hal_get_value(device_ptr, (gpio_value_t *)command_arg);

  case GPIO_CMD_SET_EDGE: {
    gpio_edge_t edge = *(const gpio_edge_t *)command_arg;
    priv->config.edge = edge;
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/edge",
             priv->pin_number);
    const char *edge_str;
    switch (edge) {
    case GPIO_EDGE_RISING:
      edge_str = "rising";
      break;
    case GPIO_EDGE_FALLING:
      edge_str = "falling";
      break;
    case GPIO_EDGE_BOTH:
      edge_str = "both";
      break;
    default:
      edge_str = "none";
      break;
    }
    sysfs_write(path, edge_str);
    return HAL_SUCCESS;
  }

  case GPIO_CMD_GET_EDGE:
    *(gpio_edge_t *)command_arg = priv->config.edge;
    return HAL_SUCCESS;

  case GPIO_CMD_WAIT_EDGE:
    return gpio_hal_wait_interrupt(device_ptr, *(const uint32_t *)command_arg);

  default:
    return HAL_ERROR_NOT_SUPPORT;
  }
}

static int gpio_get_info(hw_device_t *device_ptr, void *info_out) {
  if (!device_ptr || !device_ptr->priv || !info_out)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;
  gpio_info_t *gpio_info = (gpio_info_t *)info_out;

  gpio_info->pin_number = priv->pin_number;
  gpio_info->direction = priv->config.direction;
  gpio_info->edge = priv->config.edge;
  gpio_hal_get_value(device_ptr, &gpio_info->value);

  return HAL_SUCCESS;
}

/* ── cleanup callback ─────────────────────────────────────────────────── */

/**
 * @brief Free GPIO private data; registered as hw_device_t::cleanup.
 *
 * Closes the persistent value file descriptor and unexports the pin if
 * gpio_close() was not called before the last reference was dropped.
 *
 * @param device_ptr  Device whose priv is to be freed.
 */
static void gpio_priv_cleanup(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;

  if (priv->value_fd >= 0) {
    close(priv->value_fd);
    priv->value_fd = -1;
  }

  if (priv->exported) {
    char pin_str[16];
    snprintf(pin_str, sizeof(pin_str), "%u", priv->pin_number);
    sysfs_write(GPIO_BASE_PATH "/unexport", pin_str);
  }

  free(priv);
  device_ptr->priv = NULL;
}

/* ── vtable ───────────────────────────────────────────────────────────── */

static const hw_device_ops_t gpio_ops = {
    .open = gpio_open,
    .close = gpio_close,
    .start = gpio_start,
    .stop = gpio_stop,
    .read = gpio_read,
    .write = gpio_write,
    .control = gpio_control,
    .get_info = gpio_get_info,
};

/* ── public API ───────────────────────────────────────────────────────── */

gpio_config_t gpio_hal_default_config(uint32_t pin_number) {
  gpio_config_t cfg;
  cfg.pin_number = pin_number;
  cfg.direction = GPIO_DIR_OUTPUT;
  cfg.initial_value = GPIO_VALUE_LOW;
  cfg.edge = GPIO_EDGE_NONE;
  cfg.pull = GPIO_PULL_NONE;
  return cfg;
}

hw_device_t *gpio_hal_create(const char *device_name,
                             const gpio_config_t *gpio_config) {
  if (!device_name || !gpio_config)
    return NULL;

  if (validate_gpio_pin(gpio_config->pin_number) != 0) {
    fprintf(stderr, "[gpio_hal] pin %u exceeds GPIO_PIN_NUMBER_MAX (%u)\n",
            gpio_config->pin_number, GPIO_PIN_NUMBER_MAX);
    return NULL;
  }

  hw_device_t *dev = malloc(sizeof(hw_device_t));
  if (!dev)
    return NULL;

  if (hal_device_init(dev, device_name, HAL_DEVICE_TYPE_GPIO) != HAL_SUCCESS) {
    free(dev);
    return NULL;
  }

  gpio_priv_t *priv = calloc(1u, sizeof(gpio_priv_t));
  if (!priv) {
    hal_device_destroy(dev);
    free(dev);
    return NULL;
  }

  priv->pin_number = gpio_config->pin_number;
  memcpy(&priv->config, gpio_config, sizeof(gpio_config_t));
  priv->value_fd = -1;
  priv->exported = 0;

  dev->ops = &gpio_ops;
  dev->priv = priv;
  dev->cleanup = gpio_priv_cleanup;

  printf("[gpio_hal] created '%s' for gpio%u\n", device_name,
         gpio_config->pin_number);
  return dev;
}

void gpio_hal_destroy(hw_device_t *device_ptr) { hal_device_unref(device_ptr); }

int gpio_hal_set_value(hw_device_t *device_ptr, gpio_value_t new_value) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;

  uint8_t level = (new_value == GPIO_VALUE_HIGH) ? 1u : 0u;
  ssize_t rc = device_ptr->ops->write(device_ptr, &level, 1u);
  return (rc > 0) ? HAL_SUCCESS : HAL_ERROR_IO;
}

int gpio_hal_get_value(hw_device_t *device_ptr, gpio_value_t *value_out) {
  if (!device_ptr || !value_out)
    return HAL_ERROR_INVALID;

  uint8_t level;
  ssize_t rc = device_ptr->ops->read(device_ptr, &level, 1u);
  if (rc <= 0)
    return HAL_ERROR_IO;

  *value_out = (level != 0u) ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW;
  return HAL_SUCCESS;
}

int gpio_hal_toggle(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;

  gpio_value_t current;
  int rc = gpio_hal_get_value(device_ptr, &current);
  if (rc != HAL_SUCCESS)
    return rc;

  return gpio_hal_set_value(device_ptr, (current == GPIO_VALUE_HIGH)
                                            ? GPIO_VALUE_LOW
                                            : GPIO_VALUE_HIGH);
}

int gpio_hal_wait_interrupt(hw_device_t *device_ptr, uint32_t timeout_ms) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;

  /*
   * Perform a dummy read to drain any pending POLLPRI event that fired
   * before this call.  Without the drain, poll() would return immediately
   * on the stale event rather than waiting for the *next* edge.
   */
  char dummy[4];
  lseek(priv->value_fd, 0, SEEK_SET);
  read(priv->value_fd, dummy, sizeof(dummy));

  /*
   * POLLPRI is the event class the sysfs GPIO subsystem uses to signal
   * edge transitions.  poll() yields the calling thread to the scheduler
   * until the kernel delivers the event or the timeout expires — CPU
   * consumption during the wait is zero.
   */
  struct pollfd pfd;
  pfd.fd = priv->value_fd;
  pfd.events = POLLPRI | POLLERR;
  pfd.revents = 0;

  int poll_timeout = (timeout_ms == 0u) ? -1 : (int)timeout_ms;
  int ret = poll(&pfd, 1, poll_timeout);

  if (ret < 0)
    return HAL_ERROR_IO;
  if (ret == 0)
    return HAL_ERROR_TIMEOUT;

  return HAL_SUCCESS;
}
