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
 *
 * PROPOSED CHANGES:
 *   - Added gpio_hal_get_info() public helper to fill gpio_info_t without
 *     going through the generic control() dispatch.
 *   - Added GPIO_CMD_TOGGLE convenience command for output pins.
 */

#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include "hal_interface.h"

/* ── GPIO pin limits ──────────────────────────────────────────────────── */

/**
 * @brief Maximum allowed GPIO pin number.
 *
 * Platform-specific.  Raspberry Pi 4 exposes pins 0–57.  Adjust for
 * your target SoC via -DGPIO_PIN_NUMBER_MAX=N at build time.
 */
#ifndef GPIO_PIN_NUMBER_MAX
#define GPIO_PIN_NUMBER_MAX 1023U
#endif

/* ── enumerations ─────────────────────────────────────────────────────── */

/**
 * @brief Whether the pin is configured as an input or output.
 *
 * @p GPIO_DIR_INPUT   Pin is high-impedance; reads hardware state.
 * @p GPIO_DIR_OUTPUT  Pin drives the bus low (0) or high (VCC).
 */
typedef enum {
  GPIO_DIR_INPUT = 0,
  GPIO_DIR_OUTPUT = 1,
} gpio_direction_t;

/**
 * @brief Logic level on the pin.
 *
 * @p GPIO_VALUE_LOW   Logic 0 / GND level.
 * @p GPIO_VALUE_HIGH  Logic 1 / VCC level.
 */
typedef enum {
  GPIO_VALUE_LOW = 0,
  GPIO_VALUE_HIGH = 1,
} gpio_value_t;

/**
 * @brief Edge-detection mode for interrupt-capable input pins.
 *
 * @p GPIO_EDGE_NONE     No edge detection; pin cannot trigger interrupts.
 * @p GPIO_EDGE_RISING   Interrupt fires on low→high transition.
 * @p GPIO_EDGE_FALLING  Interrupt fires on high→low transition.
 * @p GPIO_EDGE_BOTH     Interrupt fires on any level change.
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
 * @p GPIO_PULL_NONE  Floating; state is indeterminate when undriven.
 * @p GPIO_PULL_UP    Weak pull to VCC; pin reads HIGH when undriven.
 * @p GPIO_PULL_DOWN  Weak pull to GND; pin reads LOW when undriven.
 *
 * @note Not all SoCs allow pull configuration through sysfs.  This field
 *       is stored in the config but not applied in the current implementation.
 */
typedef enum {
  GPIO_PULL_NONE = 0,
  GPIO_PULL_UP = 1,
  GPIO_PULL_DOWN = 2,
} gpio_pull_t;

/* ── configuration ────────────────────────────────────────────────────── */

/**
 * @brief Full configuration for a GPIO pin.
 *
 * @p pin_number     Hardware GPIO number; validated against
 * GPIO_PIN_NUMBER_MAX.
 * @p direction      Input or output at open time.
 * @p initial_value  Logic level driven at open for output pins.
 * @p edge           Edge-detection mode for interrupt-capable input pins.
 * @p pull           Pull-resistor configuration (stored; may not be applied).
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
 *
 * @p pin_number  Hardware GPIO number.
 * @p direction   Current direction (may differ from config after control()).
 * @p value       Most recently sampled logic level.
 * @p edge        Active edge-detection mode.
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
 * @p GPIO_CMD_SET_DIRECTION  arg: const gpio_direction_t *
 * @p GPIO_CMD_GET_DIRECTION  arg: gpio_direction_t *
 * @p GPIO_CMD_SET_VALUE      arg: const gpio_value_t *
 * @p GPIO_CMD_GET_VALUE      arg: gpio_value_t *
 * @p GPIO_CMD_SET_EDGE       arg: const gpio_edge_t *
 * @p GPIO_CMD_GET_EDGE       arg: gpio_edge_t *
 * @p GPIO_CMD_WAIT_EDGE      arg: const uint32_t * (timeout ms; 0 = infinite)
 * @p GPIO_CMD_TOGGLE         [PROPOSED] arg: NULL; flips the output level.
 */
typedef enum {
  GPIO_CMD_SET_DIRECTION = 0x4000,
  GPIO_CMD_GET_DIRECTION = 0x4001,
  GPIO_CMD_SET_VALUE = 0x4002,
  GPIO_CMD_GET_VALUE = 0x4003,
  GPIO_CMD_SET_EDGE = 0x4004,
  GPIO_CMD_GET_EDGE = 0x4005,
  GPIO_CMD_WAIT_EDGE = 0x4006,
  GPIO_CMD_TOGGLE = 0x4007, /* [PROPOSED] */
} gpio_cmd_t;

/* ── public API ───────────────────────────────────────────────────────── */

/** @brief Allocate and initialise a GPIO HAL device. */
hw_device_t *gpio_hal_create(const char *device_name,
                             const gpio_config_t *gpio_config);

/** @brief Release all resources held by a GPIO device and unexport the pin. */
void gpio_hal_destroy(hw_device_t *device_ptr);

/** @brief Return the default GPIO configuration for a given pin number. */
gpio_config_t gpio_hal_default_config(uint32_t pin_number);

/** @brief Drive an output pin to the specified logic level. */
int gpio_hal_set_value(hw_device_t *device_ptr, gpio_value_t new_value);

/** @brief Sample the current logic level of a pin. */
int gpio_hal_get_value(hw_device_t *device_ptr, gpio_value_t *value_out);

/** @brief Toggle an output pin between HIGH and LOW. */
int gpio_hal_toggle(hw_device_t *device_ptr);

/** @brief Block until the configured edge event fires or timeout expires. */
int gpio_hal_wait_interrupt(hw_device_t *device_ptr, uint32_t timeout_ms);

/** @brief [PROPOSED] Fill a gpio_info_t with the current pin state. */
int gpio_hal_get_info(hw_device_t *device_ptr, gpio_info_t *info_out);

#endif /* GPIO_HAL_H */
