#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include "hal_interface.h"

// ══════════════════════════════════════════════════════════════════════════════
// GPIO HAL - General Purpose I/O Implementation
// 
// Purpose: Provides GPIO pin control using sysfs interface
// Features:
// - Pin input/output configuration
// - Digital read/write
// - Interrupt on edge detection
// - Pull-up/pull-down configuration
// ══════════════════════════════════════════════════════════════════════════════

// GPIO direction
typedef enum {
    GPIO_DIR_INPUT  = 0,  // Input pin
    GPIO_DIR_OUTPUT = 1,  // Output pin
} gpio_direction_t;

// GPIO value
typedef enum {
    GPIO_VALUE_LOW  = 0,  // Logic low (0V)
    GPIO_VALUE_HIGH = 1,  // Logic high (3.3V or 5V)
} gpio_value_t;

// GPIO edge detection (for interrupts)
typedef enum {
    GPIO_EDGE_NONE    = 0,  // No interrupt
    GPIO_EDGE_RISING  = 1,  // Rising edge (low to high)
    GPIO_EDGE_FALLING = 2,  // Falling edge (high to low)
    GPIO_EDGE_BOTH    = 3,  // Both edges
} gpio_edge_t;

// GPIO pull resistor configuration
typedef enum {
    GPIO_PULL_NONE = 0,  // No pull resistor
    GPIO_PULL_UP   = 1,  // Pull-up resistor
    GPIO_PULL_DOWN = 2,  // Pull-down resistor
} gpio_pull_t;

// GPIO configuration
typedef struct {
    uint32_t pin_number;        // GPIO pin number
    gpio_direction_t direction; // Pin direction
    gpio_value_t initial_value; // Initial value (for output)
    gpio_edge_t edge;           // Edge detection (for input)
    gpio_pull_t pull;           // Pull resistor configuration
} gpio_config_t;

// GPIO info
typedef struct {
    uint32_t pin_number;        // GPIO pin number
    gpio_direction_t direction; // Current direction
    gpio_value_t value;         // Current value
    gpio_edge_t edge;           // Edge detection setting
} gpio_info_t;

// GPIO control commands
typedef enum {
    GPIO_CMD_SET_DIRECTION = 0x4000,  // arg: gpio_direction_t*
    GPIO_CMD_GET_DIRECTION = 0x4001,  // arg: gpio_direction_t*
    GPIO_CMD_SET_VALUE     = 0x4002,  // arg: gpio_value_t*
    GPIO_CMD_GET_VALUE     = 0x4003,  // arg: gpio_value_t*
    GPIO_CMD_SET_EDGE      = 0x4004,  // arg: gpio_edge_t*
    GPIO_CMD_GET_EDGE      = 0x4005,  // arg: gpio_edge_t*
    GPIO_CMD_WAIT_EDGE     = 0x4006,  // arg: uint32_t* (timeout ms)
} gpio_cmd_t;

// ══════════════════════════════════════════════════════════════════════════════
// GPIO HAL FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// Create GPIO device
// name: device name (user-friendly, like "gpio_led")
// config: GPIO configuration
// Returns: device pointer on success, NULL on failure
hw_device_t* gpio_hal_create(const char* name, const gpio_config_t* config);

// Destroy GPIO device
void gpio_hal_destroy(hw_device_t* dev);

// Helper: Get default GPIO configuration
gpio_config_t gpio_hal_default_config(uint32_t pin_number);

// Helper: Set GPIO value (shortcut)
int gpio_hal_set_value(hw_device_t* dev, gpio_value_t value);

// Helper: Get GPIO value (shortcut)
int gpio_hal_get_value(hw_device_t* dev, gpio_value_t* value);

// Helper: Toggle GPIO value (for output pins)
int gpio_hal_toggle(hw_device_t* dev);

// Helper: Wait for edge interrupt
// timeout_ms: timeout in milliseconds (0 = infinite)
// Returns: 0 on success, negative error code on failure/timeout
int gpio_hal_wait_interrupt(hw_device_t* dev, uint32_t timeout_ms);

#endif // GPIO_HAL_H