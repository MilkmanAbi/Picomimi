/**
 * PICOMIMI PWM HAL Implementation
 */
#include "hal/pwm.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

pm_result_t pm_pwm_init(uint8_t gpio, uint32_t frequency, uint16_t duty) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);
    
    // Calculate divider and wrap for desired frequency
    uint32_t clock = clock_get_hz(clk_sys);
    uint32_t divider16 = clock / frequency / 4096 + (clock % (frequency * 4096) != 0);
    if (divider16 / 16 == 0) divider16 = 16;
    
    uint32_t wrap = clock * 16 / divider16 / frequency - 1;
    
    pwm_set_clkdiv_int_frac(slice, divider16/16, divider16 & 0xF);
    pwm_set_wrap(slice, (uint16_t)wrap);
    pwm_set_chan_level(slice, channel, duty * wrap / 65535);
    pwm_set_enabled(slice, true);
    
    return PM_OK;
}

pm_result_t pm_pwm_set_duty(uint8_t gpio, uint16_t duty) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);
    uint16_t wrap = pwm_hw->slice[slice].top;
    
    pwm_set_chan_level(slice, channel, duty * wrap / 65535);
    
    return PM_OK;
}

pm_result_t pm_pwm_set_duty_percent(uint8_t gpio, uint8_t percent) {
    if (percent > 100) percent = 100;
    return pm_pwm_set_duty(gpio, (uint16_t)(percent * 655));
}

pm_result_t pm_pwm_set_frequency(uint8_t gpio, uint32_t frequency) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    
    uint slice = pwm_gpio_to_slice_num(gpio);
    
    uint32_t clock = clock_get_hz(clk_sys);
    uint32_t divider16 = clock / frequency / 4096 + (clock % (frequency * 4096) != 0);
    if (divider16 / 16 == 0) divider16 = 16;
    
    uint32_t wrap = clock * 16 / divider16 / frequency - 1;
    
    pwm_set_clkdiv_int_frac(slice, divider16/16, divider16 & 0xF);
    pwm_set_wrap(slice, (uint16_t)wrap);
    
    return PM_OK;
}

pm_result_t pm_pwm_enable(uint8_t gpio, bool enable) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_enabled(slice, enable);
    
    return PM_OK;
}

pm_result_t pm_pwm_deinit(uint8_t gpio) {
    if (gpio >= PICOMIMI_GPIO_COUNT) return PM_ERROR_INVALID;
    
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_enabled(slice, false);
    gpio_set_function(gpio, GPIO_FUNC_NULL);
    
    return PM_OK;
}
