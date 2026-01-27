/**
 * PICOMIMI ADC HAL Implementation
 */
#include "hal/adc.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

static bool adc_initialized = false;

pm_result_t pm_adc_init(void) {
    if (!adc_initialized) {
        adc_init();
        adc_set_temp_sensor_enabled(true);
        adc_initialized = true;
    }
    return PM_OK;
}

pm_result_t pm_adc_init_pin(uint8_t gpio) {
    // ADC pins are 26-29
    if (gpio < 26 || gpio > 29) return PM_ERROR_INVALID;
    
    adc_gpio_init(gpio);
    return PM_OK;
}

uint16_t pm_adc_read(uint8_t channel) {
    if (channel > 4) return 0;  // 0-3 for GPIO, 4 for temp sensor
    
    adc_select_input(channel);
    return adc_read();
}

float pm_adc_read_voltage(uint8_t channel) {
    uint16_t raw = pm_adc_read(channel);
    return raw * 3.3f / 4096.0f;
}

float pm_adc_read_temperature(void) {
    adc_select_input(4);
    uint16_t raw = adc_read();
    float voltage = raw * 3.3f / 4096.0f;
    return 27.0f - (voltage - 0.706f) / 0.001721f;
}

pm_result_t pm_adc_deinit(void) {
    // ADC doesn't really need cleanup
    return PM_OK;
}
