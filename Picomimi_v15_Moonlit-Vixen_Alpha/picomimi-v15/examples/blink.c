/**
 * PICOMIMI Example: LED Blink
 * 
 * Demonstrates basic task creation and GPIO usage
 */
#include "api/picomimi_kernel.h"
#include "hal/gpio.h"
#include "pico/stdlib.h"

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

static void blink_task(void* arg) {
    (void)arg;
    static bool led_state = false;
    static uint32_t last_toggle = 0;
    
    uint32_t now = pm_get_time_ms();
    if (now - last_toggle >= 500) {
        led_state = !led_state;
        pm_gpio_set(PICO_DEFAULT_LED_PIN, led_state);
        last_toggle = now;
    }
}

void example_blink_init(void) {
    // Initialize LED GPIO
    pm_gpio_init(PICO_DEFAULT_LED_PIN, PM_GPIO_MODE_OUTPUT);
    
    // Create blink task
    pm_task_id_t id = pm_task_create("blink", blink_task, NULL,
                                      5, PICOMIMI_TASK_TYPE_APPLICATION,
                                      CORE_AFFINITY_ANY);
    
    if (id != PM_INVALID_TASK) {
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Blink task started (ID %d)", id);
    }
}
