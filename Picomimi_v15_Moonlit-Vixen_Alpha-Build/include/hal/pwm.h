/**
 * PICOMIMI PWM HAL Header
 */
#ifndef PICOMIMI_HAL_PWM_H
#define PICOMIMI_HAL_PWM_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

pm_result_t pm_pwm_init(uint8_t gpio, uint32_t frequency, uint16_t duty);
pm_result_t pm_pwm_deinit(uint8_t gpio);
pm_result_t pm_pwm_set_duty(uint8_t gpio, uint16_t duty);
pm_result_t pm_pwm_set_frequency(uint8_t gpio, uint32_t frequency);
pm_result_t pm_pwm_enable(uint8_t gpio, bool enable);
pm_result_t pm_pwm_set_duty_percent(uint8_t gpio, uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_HAL_PWM_H
