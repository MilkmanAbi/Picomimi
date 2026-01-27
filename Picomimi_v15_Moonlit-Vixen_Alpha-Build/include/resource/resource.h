/**
 * PICOMIMI-AXISOS Resource Manager Header
 * v14.3.1 Resource-Owning Kernel Port
 */
#ifndef PICOMIMI_RESOURCE_H
#define PICOMIMI_RESOURCE_H

#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// INITIALIZATION
// ============================================================================

void pm_res_init(void);
void pm_res_tick(void);

// ============================================================================
// GENERIC CLAIM/RELEASE
// ============================================================================

pm_res_handle_t pm_res_claim(pm_resource_type_t type, uint8_t id, pm_resource_mode_t mode);
pm_result_t pm_res_release(pm_res_handle_t handle);

// ============================================================================
// GPIO
// ============================================================================

int pm_claim_gpio(uint8_t pin);
void pm_release_gpio(uint8_t pin);
bool pm_gpio_is_free(uint8_t pin);
bool pm_gpio_is_owned(uint8_t pin);

// ============================================================================
// SPI
// ============================================================================

int pm_claim_spi(uint8_t bus, uint8_t cs_pin);
void pm_release_spi(uint8_t bus);

// ============================================================================
// I2C
// ============================================================================

int pm_claim_i2c(uint8_t bus, uint8_t device_addr);
void pm_release_i2c(uint8_t bus, uint8_t device_addr);

// ============================================================================
// ADC
// ============================================================================

int pm_claim_adc(uint8_t channel);
void pm_release_adc(uint8_t channel);

// ============================================================================
// PWM
// ============================================================================

int pm_claim_pwm(uint8_t slice, uint8_t channel);
void pm_release_pwm(uint8_t slice, uint8_t channel);

// ============================================================================
// TASK CLEANUP
// ============================================================================

void pm_res_cleanup_task(pm_task_id_t task_id);

// ============================================================================
// VIOLATIONS
// ============================================================================

void pm_res_record_violation(pm_task_id_t task_id, pm_resource_type_t type, uint8_t id);

// ============================================================================
// QUERY
// ============================================================================

uint32_t pm_res_get_owner(pm_resource_type_t type, uint8_t id);
bool pm_res_is_claimed(pm_resource_type_t type, uint8_t id);
uint32_t pm_res_get_task_resource_count(pm_task_id_t task_id);

// ============================================================================
// STATS
// ============================================================================

void pm_res_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_RESOURCE_H
