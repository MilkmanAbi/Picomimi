/**
 * PICOMIMI GPIO HAL Implementation
 */
#include "hal/gpio.h"
#include "api/picomimi_kernel.h"
#include "hardware/gpio.h"

// GPIO ownership tracking
static pm_task_id_t gpio_owners[PICOMIMI_GPIO_COUNT];

pm_result_t pm_gpio_init(uint gpio, pm_gpio_mode_t mode) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    
    gpio_init(gpio);
    
    switch (mode) {
        case PM_GPIO_MODE_INPUT:
            gpio_set_dir(gpio, GPIO_IN);
            gpio_disable_pulls(gpio);
            break;
        case PM_GPIO_MODE_OUTPUT:
            gpio_set_dir(gpio, GPIO_OUT);
            break;
        case PM_GPIO_MODE_INPUT_PULLUP:
            gpio_set_dir(gpio, GPIO_IN);
            gpio_pull_up(gpio);
            break;
        case PM_GPIO_MODE_INPUT_PULLDOWN:
            gpio_set_dir(gpio, GPIO_IN);
            gpio_pull_down(gpio);
            break;
        case PM_GPIO_MODE_ALT_FUNC:
            // Caller should set function separately
            break;
    }
    
    return PM_OK;
}

pm_result_t pm_gpio_deinit(uint gpio) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    gpio_deinit(gpio);
    gpio_owners[gpio] = PM_INVALID_TASK;
    return PM_OK;
}

void pm_gpio_set(uint gpio, bool value) {
    gpio_put(gpio, value);
}

bool pm_gpio_get(uint gpio) {
    return gpio_get(gpio);
}

void pm_gpio_toggle(uint gpio) {
    gpio_xor_mask(1u << gpio);
}

pm_result_t pm_gpio_set_irq(uint gpio, uint32_t events, gpio_irq_callback_t callback) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    gpio_set_irq_enabled_with_callback(gpio, events, true, callback);
    return PM_OK;
}

pm_result_t pm_gpio_claim(uint gpio, pm_task_id_t owner) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    if (gpio_owners[gpio] != PM_INVALID_TASK && gpio_owners[gpio] != owner) {
        return PM_ERROR_BUSY;
    }
    gpio_owners[gpio] = owner;
    return PM_OK;
}

pm_result_t pm_gpio_release(uint gpio, pm_task_id_t owner) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    if (gpio_owners[gpio] != owner) return PM_ERROR_DENIED;
    gpio_owners[gpio] = PM_INVALID_TASK;
    return PM_OK;
}

pm_task_id_t pm_gpio_get_owner(uint gpio) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_INVALID_TASK;
    return gpio_owners[gpio];
}
