/**
 * @file gpio_hal.c
 * @brief GPIO HAL implementation — sysfs /sys/class/gpio backend.
 *
 * Security hardening applied:
 *   - validate_gpio_pin() rejects numbers above GPIO_PIN_NUMBER_MAX before
 *     any sysfs write, preventing out-of-range values from reaching the
 *     kernel GPIO subsystem.
 *   - All sysfs paths are constructed with snprintf + known prefix strings;
 *     no user-supplied data appears in a path component.
 *   - control() validates arg pointer and alignment before any dereference.
 *   - O_CLOEXEC is set on every open() call to prevent fd inheritance.
 *
 * Performance:
 *   - The value file (/sys/class/gpio/gpioN/value) is opened once in
 *     gpio_open() and held for the device lifetime.  Each read/write uses
 *     lseek(0) + read/write rather than three syscalls (open/read/close)
 *     per access — a 3× reduction for high-frequency toggling.
 *   - gpio_hal_wait_interrupt() uses poll(POLLPRI) which yields the thread
 *     to the kernel scheduler; CPU usage during the wait is zero.
 *
 * PROPOSED additions implemented:
 *   - GPIO_CMD_TOGGLE convenience command.
 *   - gpio_hal_get_info() public typed helper.
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
 *
 * @p pin_number  Hardware GPIO number from the config.
 * @p config      Copy of the active GPIO configuration.
 * @p value_fd    Persistent fd to /sys/class/gpio/gpioN/value; -1 if closed.
 * @p exported    Non-zero if the pin has been exported into sysfs.
 */
typedef struct {
  uint32_t pin_number;
  gpio_config_t config;
  int value_fd;
  int exported;
} gpio_priv_t;

/* ── control-command specification table ──────────────────────────────── */

/**
 * @brief Maps each control command to the byte size of its argument.
 *
 * @p command   Control command code from gpio_cmd_t.
 * @p arg_size  Required argument size in bytes; 0 if no argument.
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
    {GPIO_CMD_TOGGLE, 0}, /* [PROPOSED] no argument */
};

/* ── security: pin number validation ─────────────────────────────────── */

/**
 * @brief Confirm that @p pin_number is within the allowed platform range.
 *
 * Writing an out-of-range pin number to /sys/class/gpio/export passes an
 * unexpected integer to the kernel GPIO subsystem, which may enable
 * unintended hardware or produce undefined behaviour.  The range check is
 * performed before any file I/O.
 *
 * @param pin_number  Requested GPIO pin number.
 * @return 0 if valid (≤ GPIO_PIN_NUMBER_MAX), -1 if out of range.
 */
static int validate_gpio_pin(uint32_t pin_number) {
  return (pin_number <= GPIO_PIN_NUMBER_MAX) ? 0 : -1;
}

/* ── security: control arg validation ────────────────────────────────── */

/**
 * @brief Validate a control command's argument before dereferencing it.
 *
 * Checks the command against GPIO_CMD_SPECS to determine whether an
 * argument is required, then verifies the pointer is non-NULL and
 * naturally aligned to uint32_t.
 *
 * @param control_command  Command code from gpio_cmd_t.
 * @param command_arg      Argument pointer supplied by caller.
 * @return 0 if valid, -1 if the argument is missing, misaligned, or
 *         the command is unknown.
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
 *
 * Opens with O_CLOEXEC so the fd is not inherited by child processes.
 * Used to write direction, edge, initial value, export, and unexport.
 *
 * @param sysfs_path  Absolute sysfs path to write.
 * @param value       Null-terminated string value.
 * @return 0 on success, -1 on open or short-write failure.
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

/**
 * @brief Export the pin to sysfs, configure direction and edge, then open
 *        the value file for persistent read/write access.
 *
 * Export creates /sys/class/gpio/gpioN/.  If the directory already exists
 * (e.g. from a previous process that crashed without unexport), we proceed
 * rather than failing, because the hardware is still controllable.
 *
 * The value file is opened once with O_RDWR | O_NONBLOCK | O_CLOEXEC and
 * kept in priv->value_fd for the lifetime of the device.  This means each
 * read/write needs only lseek+read or lseek+write rather than three separate
 * syscalls per access — a meaningful saving at high toggle frequencies.
 *
 * @param device_ptr  GPIO device in HAL_STATE_CLOSED.
 * @return HAL_SUCCESS, HAL_ERROR_NO_DEVICE, or HAL_ERROR_IO.
 */
static int gpio_open(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;
  char path[256];
  char pin_str[16];

  snprintf(pin_str, sizeof(pin_str), "%u", priv->pin_number);

  if (sysfs_write(GPIO_BASE_PATH "/export", pin_str) != 0) {
    /*
     * Failure here usually means the pin was already exported.
     * Check whether the sysfs directory exists; if so, proceed.
     */
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u", priv->pin_number);
    if (access(path, F_OK) != 0) {
      fprintf(stderr, "[gpio_hal] failed to export gpio%u: %s\n",
              priv->pin_number, strerror(errno));
      return HAL_ERROR_NO_DEVICE;
    }
  }
  priv->exported = 1;

  const char *dir_str =
      (priv->config.direction == GPIO_DIR_INPUT) ? "in" : "out";
  snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/direction",
           priv->pin_number);
  sysfs_write(path, dir_str);

  if (priv->config.direction == GPIO_DIR_OUTPUT) {
    snprintf(path, sizeof(path), GPIO_BASE_PATH "/gpio%u/value",
             priv->pin_number);
    sysfs_write(path,
                (priv->config.initial_value == GPIO_VALUE_HIGH) ? "1" : "0");
  }

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

/**
 * @brief Close the value file descriptor and unexport the pin from sysfs.
 *
 * Unexport releases the pin so other processes and future HAL instances
 * can claim it.  Forgetting to unexport is a common embedded bug that
 * causes "Device or resource busy" on the next init cycle.
 *
 * @param device_ptr  GPIO device.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int gpio_close(hw_device_t *device_ptr) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;

  if (priv->value_fd >= 0) {
    close(priv->value_fd);
    priv->value_fd = -1;
  }

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

/**
 * @brief Transition from OPEN to ACTIVE state.
 *
 * @param device_ptr  GPIO device in HAL_STATE_OPEN.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int gpio_start(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  if (device_ptr->state != HAL_STATE_OPEN)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_ACTIVE;
  return HAL_SUCCESS;
}

/**
 * @brief Transition back to OPEN state.
 *
 * @param device_ptr  GPIO device.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
static int gpio_stop(hw_device_t *device_ptr) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;
  device_ptr->state = HAL_STATE_OPEN;
  return HAL_SUCCESS;
}

/**
 * @brief Read the current pin logic level from the persistent value fd.
 *
 * lseek(SEEK_SET, 0) repositions within the sysfs virtual file so the
 * subsequent read() returns the current hardware state, not a cached byte
 * from a prior read that left the file position at EOF.
 *
 * @param device_ptr   Open GPIO device.
 * @param data_buffer  Receives one uint8_t: 0 for LOW, 1 for HIGH.
 * @param buffer_size  Must be at least 1 byte.
 * @return 1 on success, negative HAL_ERROR_* on failure.
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
 *
 * lseek(SEEK_SET, 0) is required before write for the same reason as in
 * gpio_read: sysfs virtual files do not auto-rewind between I/O calls.
 *
 * @param device_ptr   Open GPIO output device.
 * @param data_buffer  Source: first byte interpreted as 0=LOW, non-zero=HIGH.
 * @param data_size    Must be at least 1 byte.
 * @return 1 on success, negative HAL_ERROR_* on failure.
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

/**
 * @brief Execute a GPIO-specific control command.
 *
 * All argument pointers are validated for NULL and alignment before any
 * dereference.  GPIO_CMD_TOGGLE [PROPOSED] calls gpio_hal_toggle() which
 * reads the current level and writes its complement — useful for LED
 * blink patterns without the caller tracking state.
 *
 * @param device_ptr      GPIO device.
 * @param control_command One of gpio_cmd_t.
 * @param command_arg     Typed argument; see gpio_cmd_t for requirements.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
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

  case GPIO_CMD_TOGGLE: /* [PROPOSED] */
    return gpio_hal_toggle(device_ptr);

  default:
    return HAL_ERROR_NOT_SUPPORT;
  }
}

/**
 * @brief Fill a gpio_info_t with the current pin runtime state.
 *
 * Calls gpio_hal_get_value() internally to sample the live hardware level.
 *
 * @param device_ptr  GPIO device.
 * @param info_out    Caller-allocated gpio_info_t to fill.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
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
 * Called by hal_device_unref() when the reference count reaches zero.
 * Closes the persistent value fd and unexports the pin if gpio_close()
 * was not called before the last reference was dropped — ensures the pin
 * is always released even in error paths.
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
    .reset = NULL,
};

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Return the default GPIO configuration for a pin number.
 *
 * Output direction, initially LOW, no edge detection, no pull resistor.
 * Suitable as a starting point for LED or relay outputs.
 *
 * @param pin_number  GPIO pin number.
 * @return Populated gpio_config_t; no heap allocation.
 */
gpio_config_t gpio_hal_default_config(uint32_t pin_number) {
  gpio_config_t cfg;
  cfg.pin_number = pin_number;
  cfg.direction = GPIO_DIR_OUTPUT;
  cfg.initial_value = GPIO_VALUE_LOW;
  cfg.edge = GPIO_EDGE_NONE;
  cfg.pull = GPIO_PULL_NONE;
  return cfg;
}

/**
 * @brief Allocate and initialise a GPIO HAL device.
 *
 * Validates gpio_config->pin_number against GPIO_PIN_NUMBER_MAX before
 * any sysfs operations.  No kernel resources are acquired until
 * dev->ops->open() is called.
 *
 * [PROPOSED] Sets capabilities = HAL_CAP_READ | HAL_CAP_WRITE |
 * HAL_CAP_CONTROL.
 *
 * @param device_name  Human-readable name for registry lookup.
 * @param gpio_config  Pin parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
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
  dev->capabilities =
      HAL_CAP_READ | HAL_CAP_WRITE | HAL_CAP_CONTROL; /* [PROPOSED] */

  printf("[gpio_hal] created '%s' for gpio%u\n", device_name,
         gpio_config->pin_number);
  return dev;
}

/**
 * @brief Release all resources held by a GPIO device and unexport the pin.
 *
 * Delegates to hal_device_unref() which drives the full teardown chain.
 *
 * @param device_ptr  Device returned by gpio_hal_create().
 */
void gpio_hal_destroy(hw_device_t *device_ptr) { hal_device_unref(device_ptr); }

/**
 * @brief Drive an output pin to the specified logic level.
 *
 * Writes a single-byte level (1 = HIGH, 0 = LOW) through the generic
 * write vtable slot.
 *
 * @param device_ptr  Open GPIO device configured as output.
 * @param new_value   Desired logic level from gpio_value_t.
 * @return HAL_SUCCESS or HAL_ERROR_IO.
 */
int gpio_hal_set_value(hw_device_t *device_ptr, gpio_value_t new_value) {
  if (!device_ptr)
    return HAL_ERROR_INVALID;

  uint8_t level = (new_value == GPIO_VALUE_HIGH) ? 1u : 0u;
  ssize_t rc = device_ptr->ops->write(device_ptr, &level, 1u);
  return (rc > 0) ? HAL_SUCCESS : HAL_ERROR_IO;
}

/**
 * @brief Sample the current logic level of a pin.
 *
 * Reads one byte through the generic read vtable slot and converts the
 * raw byte to a gpio_value_t enum constant.
 *
 * @param device_ptr  Open GPIO device.
 * @param value_out   Receives GPIO_VALUE_LOW or GPIO_VALUE_HIGH.
 * @return HAL_SUCCESS or HAL_ERROR_IO.
 */
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

/**
 * @brief Toggle an output pin between HIGH and LOW.
 *
 * Reads the current state with gpio_hal_get_value() and immediately writes
 * the complement with gpio_hal_set_value().  The read-modify-write is not
 * atomic at the hardware level; use with care in concurrent contexts.
 *
 * @param device_ptr  Open GPIO device configured as output.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
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

/**
 * @brief Block until the configured edge event fires or the timeout expires.
 *
 * A dummy read is performed first to drain any pending POLLPRI event that
 * may have fired before this call, preventing an immediate spurious return
 * on the stale event instead of waiting for the next edge.
 *
 * poll(POLLPRI) then yields the calling thread to the kernel scheduler.
 * CPU consumption during the wait is zero.  With timeout_ms == 0 poll()
 * waits indefinitely.
 *
 * @param device_ptr  Open GPIO input device with edge detection configured.
 * @param timeout_ms  Maximum wait in milliseconds; 0 = wait forever.
 * @return HAL_SUCCESS on edge, HAL_ERROR_TIMEOUT, or HAL_ERROR_IO.
 */
int gpio_hal_wait_interrupt(hw_device_t *device_ptr, uint32_t timeout_ms) {
  if (!device_ptr || !device_ptr->priv)
    return HAL_ERROR_INVALID;

  gpio_priv_t *priv = (gpio_priv_t *)device_ptr->priv;

  /* Drain any stale pending event before arming the poll. */
  char dummy[4];
  lseek(priv->value_fd, 0, SEEK_SET);
  read(priv->value_fd, dummy, sizeof(dummy));

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

/**
 * @brief [PROPOSED] Fill a gpio_info_t with the current pin runtime state.
 *
 * Public typed alternative to calling dev->ops->get_info() directly.
 * Samples the live hardware level via gpio_hal_get_value().
 *
 * @param device_ptr  Open GPIO device.
 * @param info_out    Caller-allocated gpio_info_t to fill.
 * @return HAL_SUCCESS or HAL_ERROR_INVALID.
 */
int gpio_hal_get_info(hw_device_t *device_ptr, gpio_info_t *info_out) {
  if (!device_ptr || !info_out)
    return HAL_ERROR_INVALID;
  return gpio_get_info(device_ptr, info_out);
}
