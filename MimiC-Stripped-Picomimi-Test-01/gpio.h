/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC SDK - GPIO Hardware Interface                                      ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  pico-sdk compatible GPIO API that maps to MimiC syscalls                 ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef HARDWARE_GPIO_H
#define HARDWARE_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "mimic/syscall.h"

// ============================================================================
// GPIO DEFINITIONS
// ============================================================================

#define GPIO_OUT    1
#define GPIO_IN     0

#define GPIO_FUNC_XIP    0
#define GPIO_FUNC_SPI    1
#define GPIO_FUNC_UART   2
#define GPIO_FUNC_I2C    3
#define GPIO_FUNC_PWM    4
#define GPIO_FUNC_SIO    5
#define GPIO_FUNC_PIO0   6
#define GPIO_FUNC_PIO1   7
#define GPIO_FUNC_GPCK   8
#define GPIO_FUNC_USB    9
#define GPIO_FUNC_NULL   0x1f

// Number of GPIO pins
#ifdef MIMIC_TARGET_RP2350
  #define NUM_BANK0_GPIOS  48
#else
  #define NUM_BANK0_GPIOS  30
#endif

// ============================================================================
// GPIO API
// ============================================================================

/**
 * Initialize a GPIO pin
 */
static inline void gpio_init(uint gpio) {
    mimi_gpio_init(gpio);
}

/**
 * Initialize multiple GPIO pins
 */
static inline void gpio_init_mask(uint32_t mask) {
    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        if (mask & (1u << i)) {
            gpio_init(i);
        }
    }
}

/**
 * Set GPIO direction
 */
static inline void gpio_set_dir(uint gpio, bool out) {
    mimi_gpio_set_dir(gpio, out);
}

/**
 * Set direction for multiple GPIOs
 */
static inline void gpio_set_dir_out_masked(uint32_t mask) {
    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        if (mask & (1u << i)) {
            gpio_set_dir(i, true);
        }
    }
}

static inline void gpio_set_dir_in_masked(uint32_t mask) {
    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        if (mask & (1u << i)) {
            gpio_set_dir(i, false);
        }
    }
}

static inline void gpio_set_dir_masked(uint32_t mask, uint32_t value) {
    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        if (mask & (1u << i)) {
            gpio_set_dir(i, (value & (1u << i)) != 0);
        }
    }
}

/**
 * Set GPIO output level
 */
static inline void gpio_put(uint gpio, bool value) {
    mimi_gpio_put(gpio, value);
}

/**
 * Set multiple GPIOs at once
 */
static inline void gpio_put_masked(uint32_t mask, uint32_t value) {
    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        if (mask & (1u << i)) {
            gpio_put(i, (value & (1u << i)) != 0);
        }
    }
}

static inline void gpio_put_all(uint32_t value) {
    gpio_put_masked(0xFFFFFFFF, value);
}

/**
 * Get GPIO input level
 */
static inline bool gpio_get(uint gpio) {
    return mimi_gpio_get(gpio);
}

/**
 * Get all GPIO input levels
 */
static inline uint32_t gpio_get_all(void) {
    uint32_t result = 0;
    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        if (gpio_get(i)) {
            result |= (1u << i);
        }
    }
    return result;
}

/**
 * Configure pull-up/pull-down resistors
 */
static inline void gpio_pull_up(uint gpio) {
    mimi_gpio_set_pulls(gpio, true, false);
}

static inline void gpio_pull_down(uint gpio) {
    mimi_gpio_set_pulls(gpio, false, true);
}

static inline void gpio_disable_pulls(uint gpio) {
    mimi_gpio_set_pulls(gpio, false, false);
}

static inline void gpio_set_pulls(uint gpio, bool up, bool down) {
    mimi_gpio_set_pulls(gpio, up, down);
}

static inline bool gpio_is_pulled_up(uint gpio) {
    // Not directly queryable via syscall, would need kernel support
    return false;
}

static inline bool gpio_is_pulled_down(uint gpio) {
    return false;
}

/**
 * Set GPIO function (SIO, SPI, I2C, etc.)
 */
static inline void gpio_set_function(uint gpio, uint fn) {
    // In MimiC, function is set by the kernel when claiming resources
    // This is a no-op for user code - kernel handles multiplexing
    (void)gpio;
    (void)fn;
}

/**
 * Get current GPIO function
 */
static inline uint gpio_get_function(uint gpio) {
    // Default to SIO (GPIO)
    (void)gpio;
    return GPIO_FUNC_SIO;
}

/**
 * Set output drive strength
 */
typedef enum {
    GPIO_DRIVE_STRENGTH_2MA = 0,
    GPIO_DRIVE_STRENGTH_4MA = 1,
    GPIO_DRIVE_STRENGTH_8MA = 2,
    GPIO_DRIVE_STRENGTH_12MA = 3
} gpio_drive_strength;

static inline void gpio_set_drive_strength(uint gpio, gpio_drive_strength drive) {
    // Would need syscall support
    (void)gpio;
    (void)drive;
}

/**
 * Set slew rate
 */
typedef enum {
    GPIO_SLEW_RATE_SLOW = 0,
    GPIO_SLEW_RATE_FAST = 1
} gpio_slew_rate;

static inline void gpio_set_slew_rate(uint gpio, gpio_slew_rate slew) {
    (void)gpio;
    (void)slew;
}

/**
 * Configure input hysteresis
 */
static inline void gpio_set_input_hysteresis_enabled(uint gpio, bool enabled) {
    (void)gpio;
    (void)enabled;
}

/**
 * Configure input enable
 */
static inline void gpio_set_input_enabled(uint gpio, bool enabled) {
    (void)gpio;
    (void)enabled;
}

/**
 * Configure output enable
 */
static inline void gpio_set_oeover(uint gpio, uint value) {
    (void)gpio;
    (void)value;
}

// ============================================================================
// IRQ HANDLING (Limited in user mode)
// ============================================================================

typedef enum {
    GPIO_IRQ_LEVEL_LOW  = 0x1,
    GPIO_IRQ_LEVEL_HIGH = 0x2,
    GPIO_IRQ_EDGE_FALL  = 0x4,
    GPIO_IRQ_EDGE_RISE  = 0x8
} gpio_irq_level;

// IRQ handling would need special kernel support
// For now, these are stubs

static inline void gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled) {
    (void)gpio;
    (void)events;
    (void)enabled;
}

typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t events);

static inline void gpio_set_irq_callback(gpio_irq_callback_t callback) {
    (void)callback;
}

static inline void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events,
                                                       bool enabled, gpio_irq_callback_t callback) {
    (void)gpio;
    (void)events;
    (void)enabled;
    (void)callback;
}

#endif // HARDWARE_GPIO_H
