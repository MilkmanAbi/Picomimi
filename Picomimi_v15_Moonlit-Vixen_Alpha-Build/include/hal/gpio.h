/**
 * PICOMIMI GPIO HAL
 */
#ifndef PICOMIMI_HAL_GPIO_H
#define PICOMIMI_HAL_GPIO_H

#include "api/picomimi_types.h"
#include "hardware/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO modes
typedef enum {
    PM_GPIO_MODE_INPUT,
    PM_GPIO_MODE_OUTPUT,
    PM_GPIO_MODE_INPUT_PULLUP,
    PM_GPIO_MODE_INPUT_PULLDOWN,
    PM_GPIO_MODE_ALT_FUNC
} pm_gpio_mode_t;

// GPIO functions
pm_result_t pm_gpio_init(uint gpio, pm_gpio_mode_t mode);
pm_result_t pm_gpio_deinit(uint gpio);
void pm_gpio_set(uint gpio, bool value);
bool pm_gpio_get(uint gpio);
void pm_gpio_toggle(uint gpio);
pm_result_t pm_gpio_set_irq(uint gpio, uint32_t events, gpio_irq_callback_t callback);

// Resource-aware GPIO (tracks ownership)
pm_result_t pm_gpio_claim(uint gpio, pm_task_id_t owner);
pm_result_t pm_gpio_release(uint gpio, pm_task_id_t owner);
pm_task_id_t pm_gpio_get_owner(uint gpio);

#ifdef __cplusplus
}
#endif

#endif
