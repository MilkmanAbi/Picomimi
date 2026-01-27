/**
 * PICOMIMI ADC HAL
 */
#ifndef PICOMIMI_HAL_ADC_H
#define PICOMIMI_HAL_ADC_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

pm_result_t pm_adc_init(void);
pm_result_t pm_adc_init_pin(uint8_t gpio);
uint16_t pm_adc_read(uint8_t channel);
float pm_adc_read_voltage(uint8_t channel);
float pm_adc_read_temperature(void);
pm_result_t pm_adc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_HAL_ADC_H
