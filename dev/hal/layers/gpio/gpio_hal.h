/**
 * @file gpio_hal.h
 * @brief GPIO HAL — sysfs General Purpose I/O interface.
 *
 * Controls GPIO pins through the legacy /sys/class/gpio interface.
 * Edge-interrupt waiting is implemented with poll(POLLPRI) so the calling
 * thread blocks in the kernel scheduler rather than busy-looping.
 *
 * @note The sysfs GPIO interface was deprecated in Linux 4.8 (2016).
 *       A future revision of this HAL should target libgpiod
 *       (/dev/gpiochipN + ioctl) which supports atomic multi-pin
 *       operations and per-consumer ownership semantics.
 */

#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include "hal_interface.h"

/* ── GPIO pin limits ──────────────────────────────────────────────────── */

/**
 * @brief Maximum allowed GPIO pin number.
 *
 * Platform-specific.  Raspberry Pi 4 exposes pins 0–57.  Adjust for
 * your target SoC.
 */
#define GPIO_PIN_NUMBER_MAX 1023U

/* ── enumerations ─────────────────────────────────────────────────────── */

/**
 * @brief Whether the pin is configured as input or output.
 */
typedef enum {
  GPIO_DIR_INPUT = 0,
  GPIO_DIR_OUTPUT = 1,
} gpio_direction_t;

/**
 * @brief Logic level on the pin.
 */
typedef enum {
  GPIO_VALUE_LOW = 0,
  GPIO_VALUE_HIGH = 1,
} gpio_value_t;

/**
 * @brief Edge-detection mode for interrupt-capable input pins.
 */
typedef enum {
  GPIO_EDGE_NONE = 0,
  GPIO_EDGE_RISING = 1,
  GPIO_EDGE_FALLING = 2,
  GPIO_EDGE_BOTH = 3,
} gpio_edge_t;

/**
 * @brief Pull-resistor configuration.
 *
 * @note Not all SoCs allow pull configuration via sysfs; this field
 *       is stored but not applied in the current implementation.
 */
typedef enum {
  GPIO_PULL_NONE = 0,
  GPIO_PULL_UP = 1,
  GPIO_PULL_DOWN = 2,
} gpio_pull_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Full configuration for a GPIO pin.
 */
typedef struct {
  uint32_t pin_number;
  gpio_direction_t direction;
  gpio_value_t initial_value;
  gpio_edge_t edge;
  gpio_pull_t pull;
} gpio_config_t;

/* ── device info ──────────────────────────────────────────────────────── */

/**
 * @brief Runtime snapshot of GPIO pin state.
 */
typedef struct {
  uint32_t pin_number;
  gpio_direction_t direction;
  gpio_value_t value;
  gpio_edge_t edge;
} gpio_info_t;

/* ── control commands ─────────────────────────────────────────────────── */

/**
 * @brief Commands accepted by the GPIO device control() operation.
 *
 *   GPIO_CMD_SET_DIRECTION — arg: const gpio_direction_t *
 *   GPIO_CMD_GET_DIRECTION — arg: gpio_direction_t *
 *   GPIO_CMD_SET_VALUE     — arg: const gpio_value_t *
 *   GPIO_CMD_GET_VALUE     — arg: gpio_value_t *
 *   GPIO_CMD_SET_EDGE      — arg: const gpio_edge_t *
 *   GPIO_CMD_GET_EDGE      — arg: gpio_edge_t *
 *   GPIO_CMD_WAIT_EDGE     — arg: const uint32_t *  (timeout ms; 0=infinite)
 */
typedef enum {
  GPIO_CMD_SET_DIRECTION = 0x4000,
  GPIO_CMD_GET_DIRECTION = 0x4001,
  GPIO_CMD_SET_VALUE = 0x4002,
  GPIO_CMD_GET_VALUE = 0x4003,
  GPIO_CMD_SET_EDGE = 0x4004,
  GPIO_CMD_GET_EDGE = 0x4005,
  GPIO_CMD_WAIT_EDGE = 0x4006,
} gpio_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise a GPIO HAL device.
 *
 * Validates that @p gpio_config->pin_number is within
 * [0, GPIO_PIN_NUMBER_MAX] before any sysfs operations.
 *
 * @param device_name  Human-readable name for registry lookup.
 * @param gpio_config  Pin parameters; a copy is stored internally.
 * @return Initialised hw_device_t with ref_count=1, or NULL on error.
 */
hw_device_t *gpio_hal_create(const char *device_name,
                             const gpio_config_t *gpio_config);

/**
 * @brief Release all resources held by a GPIO device.
 *
 * Unexports the GPIO pin from sysfs so other processes can claim it.
 *
 * @param device_ptr  Device returned by @ref gpio_hal_create.
 */
void gpio_hal_destroy(hw_device_t *device_ptr);

/**
 * @brief Return the default GPIO configuration for a pin number.
 *
 * Output, initially LOW, no edge detection, no pull resistor.
 *
 * @param pin_number  GPIO pin number.
 * @return Populated gpio_config_t; no heap allocation.
 */
gpio_config_t gpio_hal_default_config(uint32_t pin_number);

/**
 * @brief Drive an output pin to the specified logic level.
 *
 * @param device_ptr  Open GPIO device configured as output.
 * @param new_value   Desired logic level.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int gpio_hal_set_value(hw_device_t *device_ptr, gpio_value_t new_value);

/**
 * @brief Sample the current logic level of a pin.
 *
 * @param device_ptr  Open GPIO device.
 * @param value_out   Receives the current logic level.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int gpio_hal_get_value(hw_device_t *device_ptr, gpio_value_t *value_out);

/**
 * @brief Toggle an output pin between HIGH and LOW.
 *
 * Reads the current state and writes the opposite.
 *
 * @param device_ptr  Open GPIO device configured as output.
 * @return HAL_SUCCESS or HAL_ERROR_*.
 */
int gpio_hal_toggle(hw_device_t *device_ptr);

/**
 * @brief Block until the configured edge event fires or timeout expires.
 *
 * Uses poll(POLLPRI) to sleep in the kernel scheduler; CPU usage is zero
 * while waiting.  The caller must have configured a non-NONE edge in
 * @ref gpio_config_t before calling open().
 *
 * @param device_ptr  Open GPIO input device with edge detection configured.
 * @param timeout_ms  Maximum wait in milliseconds; 0 means wait forever.
 * @return HAL_SUCCESS on edge, HAL_ERROR_TIMEOUT, or HAL_ERROR_IO.
 */
int gpio_hal_wait_interrupt(hw_device_t *device_ptr, uint32_t timeout_ms);

#endif /* GPIO_HAL_H */
