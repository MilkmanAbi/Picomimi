/**
 * PICOMIMI-AXISOS v15.0.0-Alpha Resource Manager
 * Complete port from v14.3.1 "Quiet Otter"
 * 
 * DESIGN PHILOSOPHY:
 *   - Claim/Release = kernel tracks ownership (one-time overhead)
 *   - GPIO operations = DIRECT hardware access (ZERO overhead after claim)
 *   - Pre-kill cleanup = kernel resets GPIOs BEFORE task termination
 *   - Shadow state = kernel tracks expected pin states for clean teardown
 *
 * This is NOT a HAL that intercepts every GPIO write. That would kill performance.
 * Instead, we track OWNERSHIP and EXPECTED STATE, then clean up on task death.
 */
#include "api/picomimi_kernel.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

extern pm_kernel_state_t g_kernel;

// ============================================================================
// CONSTANTS
// ============================================================================

#define RES_HANDLE_MAGIC 0xA000

static const char* const RES_TYPE_NAMES[] = {
    "GPIO", "SPI", "I2C", "ADC", "PWM", "PIO", "UART", "DMA", "TIMER"
};

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Generate a unique handle for a resource
static pm_res_handle_t res_generate_handle(pm_resource_type_t type, uint8_t id) {
    g_kernel.next_handle_id++;
    uint16_t handle = RES_HANDLE_MAGIC | ((type & 0x0F) << 8) | (id & 0xFF);
    return handle;
}

// Decode a handle to type and id
static bool res_handle_decode(pm_res_handle_t handle, pm_resource_type_t* type, uint8_t* id) {
    if ((handle & 0xF000) != RES_HANDLE_MAGIC) {
        return false;
    }
    if (type) *type = (pm_resource_type_t)((handle >> 8) & 0x0F);
    if (id) *id = handle & 0xFF;
    return true;
}

// Get resource descriptor by type and id
static pm_resource_desc_t* res_get_by_type_id(pm_resource_type_t type, uint8_t id) {
    switch (type) {
        case RES_TYPE_GPIO:
            if (id < PICOMIMI_RES_GPIO_COUNT) return &g_kernel.gpio_resources[id];
            break;
        case RES_TYPE_SPI:
            if (id < PICOMIMI_RES_SPI_COUNT) return &g_kernel.spi_resources[id];
            break;
        case RES_TYPE_I2C:
            if (id < PICOMIMI_RES_I2C_COUNT * 16) return &g_kernel.i2c_resources[id];
            break;
        case RES_TYPE_ADC:
            if (id < PICOMIMI_RES_ADC_COUNT) return &g_kernel.adc_resources[id];
            break;
        case RES_TYPE_PWM:
            if (id < PICOMIMI_RES_PWM_CHANNEL_COUNT) return &g_kernel.pwm_resources[id];
            break;
        case RES_TYPE_PIO:
            if (id < PICOMIMI_RES_PIO_COUNT * PICOMIMI_RES_PIO_SM_COUNT) 
                return &g_kernel.pio_resources[id];
            break;
        case RES_TYPE_UART:
            if (id < PICOMIMI_RES_UART_COUNT) return &g_kernel.uart_resources[id];
            break;
        case RES_TYPE_DMA:
            if (id < PICOMIMI_RES_DMA_COUNT) return &g_kernel.dma_resources[id];
            break;
        case RES_TYPE_TIMER:
            if (id < PICOMIMI_RES_TIMER_ALARM_COUNT) return &g_kernel.timer_resources[id];
            break;
        default:
            break;
    }
    return NULL;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void pm_res_init(void) {
    mutex_init(&g_kernel.res_lock);
    
    // Clear all resource descriptors
    memset(g_kernel.gpio_resources, 0, sizeof(g_kernel.gpio_resources));
    memset(g_kernel.gpio_info, 0, sizeof(g_kernel.gpio_info));
    memset(g_kernel.spi_resources, 0, sizeof(g_kernel.spi_resources));
    memset(g_kernel.spi_info, 0, sizeof(g_kernel.spi_info));
    memset(g_kernel.i2c_resources, 0, sizeof(g_kernel.i2c_resources));
    memset(g_kernel.i2c_info, 0, sizeof(g_kernel.i2c_info));
    memset(g_kernel.adc_resources, 0, sizeof(g_kernel.adc_resources));
    memset(g_kernel.pwm_resources, 0, sizeof(g_kernel.pwm_resources));
    memset(g_kernel.pwm_info, 0, sizeof(g_kernel.pwm_info));
    memset(g_kernel.pio_resources, 0, sizeof(g_kernel.pio_resources));
    memset(g_kernel.pio_info, 0, sizeof(g_kernel.pio_info));
    memset(g_kernel.uart_resources, 0, sizeof(g_kernel.uart_resources));
    memset(g_kernel.dma_resources, 0, sizeof(g_kernel.dma_resources));
    memset(g_kernel.timer_resources, 0, sizeof(g_kernel.timer_resources));
    
    // Clear violations
    memset(g_kernel.violations, 0, sizeof(g_kernel.violations));
    g_kernel.violation_head = 0;
    g_kernel.violation_count = 0;
    g_kernel.total_violations = 0;
    
    // Clear transactions
    memset(g_kernel.transactions, 0, sizeof(g_kernel.transactions));
    
    // Reset stats
    g_kernel.res_total_claims = 0;
    g_kernel.res_total_releases = 0;
    g_kernel.res_total_conflicts = 0;
    g_kernel.res_total_auto_releases = 0;
    g_kernel.next_handle_id = 1;
    
    // Initialize all resources as free
    for (int i = 0; i < PICOMIMI_RES_GPIO_COUNT; i++) {
        g_kernel.gpio_resources[i].state = RES_STATE_FREE;
        g_kernel.gpio_resources[i].type = RES_TYPE_GPIO;
        g_kernel.gpio_resources[i].id = i;
    }
    
    for (int i = 0; i < PICOMIMI_RES_SPI_COUNT; i++) {
        g_kernel.spi_resources[i].state = RES_STATE_FREE;
        g_kernel.spi_resources[i].type = RES_TYPE_SPI;
        g_kernel.spi_resources[i].id = i;
    }
    
    for (int i = 0; i < PICOMIMI_RES_I2C_COUNT * 16; i++) {
        g_kernel.i2c_resources[i].state = RES_STATE_FREE;
        g_kernel.i2c_resources[i].type = RES_TYPE_I2C;
        g_kernel.i2c_resources[i].id = i;
    }
    
    for (int i = 0; i < PICOMIMI_RES_ADC_COUNT; i++) {
        g_kernel.adc_resources[i].state = RES_STATE_FREE;
        g_kernel.adc_resources[i].type = RES_TYPE_ADC;
        g_kernel.adc_resources[i].id = i;
    }
    
    for (int i = 0; i < PICOMIMI_RES_PWM_CHANNEL_COUNT; i++) {
        g_kernel.pwm_resources[i].state = RES_STATE_FREE;
        g_kernel.pwm_resources[i].type = RES_TYPE_PWM;
        g_kernel.pwm_resources[i].id = i;
    }
    
    g_kernel.res_manager_initialized = true;
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Resource manager initialized");
}

// ============================================================================
// CLAIM/RELEASE - CORE API
// ============================================================================

pm_res_handle_t pm_res_claim(pm_resource_type_t type, uint8_t id, pm_resource_mode_t mode) {
    pm_resource_desc_t* res = res_get_by_type_id(type, id);
    if (!res) {
        pm_klog(PICOMIMI_LOG_LEVEL_ERROR, "Invalid resource: %s[%d]", 
                RES_TYPE_NAMES[type], id);
        return PM_INVALID_HANDLE;
    }
    
    mutex_enter_blocking(&g_kernel.res_lock);
    
    uint32_t task_id = g_kernel.current_task;
    
    // Check if already claimed
    if (res->state == RES_STATE_CLAIMED) {
        if (res->owner_task_id == task_id) {
            // Already own it, return existing handle
            mutex_exit(&g_kernel.res_lock);
            return res->handle;
        }
        
        // Claimed by another task
        g_kernel.res_total_conflicts++;
        mutex_exit(&g_kernel.res_lock);
        pm_klog(PICOMIMI_LOG_LEVEL_WARN, "Resource conflict: %s[%d] owned by task %lu",
                RES_TYPE_NAMES[type], id, (unsigned long)res->owner_task_id);
        return PM_INVALID_HANDLE;
    }
    
    // Claim the resource
    res->owner_task_id = task_id;
    res->claim_time_ms = g_kernel.uptime_ms;
    res->last_access_ms = g_kernel.uptime_ms;
    res->access_count = 0;
    res->state = RES_STATE_CLAIMED;
    res->mode = mode;
    res->handle = res_generate_handle(type, id);
    
    g_kernel.res_total_claims++;
    
    mutex_exit(&g_kernel.res_lock);
    
    return res->handle;
}

pm_result_t pm_res_release(pm_res_handle_t handle) {
    pm_resource_type_t type;
    uint8_t id;
    
    if (!res_handle_decode(handle, &type, &id)) {
        return PM_ERROR_INVALID;
    }
    
    pm_resource_desc_t* res = res_get_by_type_id(type, id);
    if (!res) return PM_ERROR_NOTFOUND;
    
    mutex_enter_blocking(&g_kernel.res_lock);
    
    // Verify ownership
    if (res->owner_task_id != g_kernel.current_task) {
        mutex_exit(&g_kernel.res_lock);
        return PM_ERROR_DENIED;
    }
    
    // Release
    res->owner_task_id = 0;
    res->state = RES_STATE_FREE;
    res->handle = PM_INVALID_HANDLE;
    res->configured = false;
    
    g_kernel.res_total_releases++;
    
    mutex_exit(&g_kernel.res_lock);
    
    return PM_OK;
}

// ============================================================================
// GPIO-SPECIFIC CLAIMS
// ============================================================================

int pm_claim_gpio(uint8_t pin) {
    if (pin >= PICOMIMI_RES_GPIO_COUNT) return -1;
    
    pm_res_handle_t handle = pm_res_claim(RES_TYPE_GPIO, pin, RES_MODE_EXCLUSIVE);
    if (handle == PM_INVALID_HANDLE) return -1;
    
    // Initialize the GPIO
    gpio_init(pin);
    g_kernel.gpio_resources[pin].configured = true;
    
    return (int)pin;
}

void pm_release_gpio(uint8_t pin) {
    if (pin >= PICOMIMI_RES_GPIO_COUNT) return;
    
    pm_resource_desc_t* res = &g_kernel.gpio_resources[pin];
    if (res->owner_task_id != g_kernel.current_task) return;
    
    // Reset GPIO to safe state (input, no pulls)
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);
    
    pm_res_release(res->handle);
}

bool pm_gpio_is_free(uint8_t pin) {
    if (pin >= PICOMIMI_RES_GPIO_COUNT) return false;
    return g_kernel.gpio_resources[pin].state == RES_STATE_FREE;
}

bool pm_gpio_is_owned(uint8_t pin) {
    if (pin >= PICOMIMI_RES_GPIO_COUNT) return false;
    pm_resource_desc_t* res = &g_kernel.gpio_resources[pin];
    return (res->state == RES_STATE_CLAIMED && 
            res->owner_task_id == g_kernel.current_task);
}

// ============================================================================
// SPI CLAIMS
// ============================================================================

int pm_claim_spi(uint8_t bus, uint8_t cs_pin) {
    if (bus >= PICOMIMI_RES_SPI_COUNT) return -1;
    
    // Claim SPI bus
    pm_res_handle_t handle = pm_res_claim(RES_TYPE_SPI, bus, RES_MODE_EXCLUSIVE);
    if (handle == PM_INVALID_HANDLE) return -1;
    
    // Also claim CS pin
    if (cs_pin < PICOMIMI_RES_GPIO_COUNT) {
        if (pm_claim_gpio(cs_pin) < 0) {
            pm_res_release(handle);
            return -1;
        }
        g_kernel.spi_info[bus].cs_pin = cs_pin;
    }
    
    return (int)bus;
}

void pm_release_spi(uint8_t bus) {
    if (bus >= PICOMIMI_RES_SPI_COUNT) return;
    
    pm_resource_desc_t* res = &g_kernel.spi_resources[bus];
    if (res->owner_task_id != g_kernel.current_task) return;
    
    // Release CS pin too
    uint8_t cs_pin = g_kernel.spi_info[bus].cs_pin;
    if (cs_pin < PICOMIMI_RES_GPIO_COUNT) {
        pm_release_gpio(cs_pin);
    }
    
    pm_res_release(res->handle);
}

// ============================================================================
// I2C CLAIMS
// ============================================================================

int pm_claim_i2c(uint8_t bus, uint8_t device_addr) {
    if (bus >= PICOMIMI_RES_I2C_COUNT) return -1;
    
    // Each I2C bus can have multiple devices (by address)
    uint8_t id = bus * 16 + (device_addr & 0x0F);
    
    pm_res_handle_t handle = pm_res_claim(RES_TYPE_I2C, id, RES_MODE_EXCLUSIVE);
    if (handle == PM_INVALID_HANDLE) return -1;
    
    g_kernel.i2c_info[id].device_addr = device_addr;
    g_kernel.i2c_info[id].is_master = true;
    
    return (int)id;
}

void pm_release_i2c(uint8_t bus, uint8_t device_addr) {
    if (bus >= PICOMIMI_RES_I2C_COUNT) return;
    
    uint8_t id = bus * 16 + (device_addr & 0x0F);
    pm_resource_desc_t* res = &g_kernel.i2c_resources[id];
    
    if (res->owner_task_id != g_kernel.current_task) return;
    
    pm_res_release(res->handle);
}

// ============================================================================
// ADC CLAIMS
// ============================================================================

int pm_claim_adc(uint8_t channel) {
    if (channel >= PICOMIMI_RES_ADC_COUNT) return -1;
    
    pm_res_handle_t handle = pm_res_claim(RES_TYPE_ADC, channel, RES_MODE_EXCLUSIVE);
    if (handle == PM_INVALID_HANDLE) return -1;
    
    return (int)channel;
}

void pm_release_adc(uint8_t channel) {
    if (channel >= PICOMIMI_RES_ADC_COUNT) return;
    
    pm_resource_desc_t* res = &g_kernel.adc_resources[channel];
    if (res->owner_task_id != g_kernel.current_task) return;
    
    pm_res_release(res->handle);
}

// ============================================================================
// PWM CLAIMS
// ============================================================================

int pm_claim_pwm(uint8_t slice, uint8_t channel) {
    uint8_t id = slice * 2 + channel;
    if (id >= PICOMIMI_RES_PWM_CHANNEL_COUNT) return -1;
    
    pm_res_handle_t handle = pm_res_claim(RES_TYPE_PWM, id, RES_MODE_EXCLUSIVE);
    if (handle == PM_INVALID_HANDLE) return -1;
    
    g_kernel.pwm_info[id].slice = slice;
    g_kernel.pwm_info[id].channel = channel;
    
    return (int)id;
}

void pm_release_pwm(uint8_t slice, uint8_t channel) {
    uint8_t id = slice * 2 + channel;
    if (id >= PICOMIMI_RES_PWM_CHANNEL_COUNT) return;
    
    pm_resource_desc_t* res = &g_kernel.pwm_resources[id];
    if (res->owner_task_id != g_kernel.current_task) return;
    
    // Disable PWM
    pwm_set_enabled(slice, false);
    
    pm_res_release(res->handle);
}

// ============================================================================
// TASK RESOURCE CLEANUP
// ============================================================================

// Called BEFORE task termination - resets all resources owned by task
void pm_res_cleanup_task(pm_task_id_t task_id) {
    mutex_enter_blocking(&g_kernel.res_lock);
    
    uint32_t released = 0;
    
    // Release all GPIO
    for (int i = 0; i < PICOMIMI_RES_GPIO_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.gpio_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            // Reset GPIO to safe state (high-Z input)
            gpio_set_dir(i, GPIO_IN);
            gpio_disable_pulls(i);
            
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all SPI
    for (int i = 0; i < PICOMIMI_RES_SPI_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.spi_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all I2C
    for (int i = 0; i < PICOMIMI_RES_I2C_COUNT * 16; i++) {
        pm_resource_desc_t* res = &g_kernel.i2c_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all ADC
    for (int i = 0; i < PICOMIMI_RES_ADC_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.adc_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all PWM
    for (int i = 0; i < PICOMIMI_RES_PWM_CHANNEL_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.pwm_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            // Disable PWM
            pwm_set_enabled(g_kernel.pwm_info[i].slice, false);
            
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all PIO
    for (int i = 0; i < PICOMIMI_RES_PIO_COUNT * PICOMIMI_RES_PIO_SM_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.pio_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all UART
    for (int i = 0; i < PICOMIMI_RES_UART_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.uart_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all DMA
    for (int i = 0; i < PICOMIMI_RES_DMA_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.dma_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    // Release all timers
    for (int i = 0; i < PICOMIMI_RES_TIMER_ALARM_COUNT; i++) {
        pm_resource_desc_t* res = &g_kernel.timer_resources[i];
        if (res->state == RES_STATE_CLAIMED && res->owner_task_id == task_id) {
            res->state = RES_STATE_FREE;
            res->owner_task_id = 0;
            released++;
        }
    }
    
    g_kernel.res_total_auto_releases += released;
    
    mutex_exit(&g_kernel.res_lock);
    
    if (released > 0) {
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Task %lu: released %lu resources",
                (unsigned long)task_id, (unsigned long)released);
    }
}

// ============================================================================
// VIOLATION TRACKING
// ============================================================================

void pm_res_record_violation(pm_task_id_t task_id, pm_resource_type_t type, uint8_t id) {
    mutex_enter_blocking(&g_kernel.res_lock);
    
    // Find existing violation for this task
    for (int i = 0; i < PICOMIMI_MAX_RESOURCE_VIOLATIONS; i++) {
        if (g_kernel.violations[i].task_id == task_id) {
            g_kernel.violations[i].violation_count++;
            g_kernel.violations[i].timestamp_ms = g_kernel.uptime_ms;
            g_kernel.violations[i].resource_type = type;
            g_kernel.violations[i].resource_id = id;
            g_kernel.total_violations++;
            mutex_exit(&g_kernel.res_lock);
            return;
        }
    }
    
    // Add new violation
    pm_resource_violation_t* v = &g_kernel.violations[g_kernel.violation_head];
    v->task_id = task_id;
    v->timestamp_ms = g_kernel.uptime_ms;
    v->resource_type = type;
    v->resource_id = id;
    v->violation_count = 1;
    v->warned = false;
    
    g_kernel.violation_head = (g_kernel.violation_head + 1) % PICOMIMI_MAX_RESOURCE_VIOLATIONS;
    if (g_kernel.violation_count < PICOMIMI_MAX_RESOURCE_VIOLATIONS) {
        g_kernel.violation_count++;
    }
    g_kernel.total_violations++;
    
    mutex_exit(&g_kernel.res_lock);
    
    pm_klog(PICOMIMI_LOG_LEVEL_WARN, "Resource violation: task %lu accessed %s[%d]",
            (unsigned long)task_id, RES_TYPE_NAMES[type], id);
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

uint32_t pm_res_get_owner(pm_resource_type_t type, uint8_t id) {
    pm_resource_desc_t* res = res_get_by_type_id(type, id);
    if (!res || res->state != RES_STATE_CLAIMED) return 0;
    return res->owner_task_id;
}

bool pm_res_is_claimed(pm_resource_type_t type, uint8_t id) {
    pm_resource_desc_t* res = res_get_by_type_id(type, id);
    return res && res->state == RES_STATE_CLAIMED;
}

uint32_t pm_res_get_task_resource_count(pm_task_id_t task_id) {
    uint32_t count = 0;
    
    for (int i = 0; i < PICOMIMI_RES_GPIO_COUNT; i++) {
        if (g_kernel.gpio_resources[i].state == RES_STATE_CLAIMED &&
            g_kernel.gpio_resources[i].owner_task_id == task_id) count++;
    }
    
    for (int i = 0; i < PICOMIMI_RES_SPI_COUNT; i++) {
        if (g_kernel.spi_resources[i].state == RES_STATE_CLAIMED &&
            g_kernel.spi_resources[i].owner_task_id == task_id) count++;
    }
    
    for (int i = 0; i < PICOMIMI_RES_I2C_COUNT * 16; i++) {
        if (g_kernel.i2c_resources[i].state == RES_STATE_CLAIMED &&
            g_kernel.i2c_resources[i].owner_task_id == task_id) count++;
    }
    
    for (int i = 0; i < PICOMIMI_RES_ADC_COUNT; i++) {
        if (g_kernel.adc_resources[i].state == RES_STATE_CLAIMED &&
            g_kernel.adc_resources[i].owner_task_id == task_id) count++;
    }
    
    for (int i = 0; i < PICOMIMI_RES_PWM_CHANNEL_COUNT; i++) {
        if (g_kernel.pwm_resources[i].state == RES_STATE_CLAIMED &&
            g_kernel.pwm_resources[i].owner_task_id == task_id) count++;
    }
    
    return count;
}

// ============================================================================
// STATISTICS
// ============================================================================

void pm_res_print_stats(void) {
    printf("\n=== Resource Manager Stats ===\n");
    printf("Total claims: %lu\n", (unsigned long)g_kernel.res_total_claims);
    printf("Total releases: %lu\n", (unsigned long)g_kernel.res_total_releases);
    printf("Auto releases: %lu\n", (unsigned long)g_kernel.res_total_auto_releases);
    printf("Conflicts: %lu\n", (unsigned long)g_kernel.res_total_conflicts);
    printf("Violations: %lu\n", (unsigned long)g_kernel.total_violations);
    
    // Count claimed resources
    uint32_t gpio_claimed = 0, spi_claimed = 0, i2c_claimed = 0;
    uint32_t adc_claimed = 0, pwm_claimed = 0;
    
    for (int i = 0; i < PICOMIMI_RES_GPIO_COUNT; i++) {
        if (g_kernel.gpio_resources[i].state == RES_STATE_CLAIMED) gpio_claimed++;
    }
    for (int i = 0; i < PICOMIMI_RES_SPI_COUNT; i++) {
        if (g_kernel.spi_resources[i].state == RES_STATE_CLAIMED) spi_claimed++;
    }
    for (int i = 0; i < PICOMIMI_RES_I2C_COUNT * 16; i++) {
        if (g_kernel.i2c_resources[i].state == RES_STATE_CLAIMED) i2c_claimed++;
    }
    for (int i = 0; i < PICOMIMI_RES_ADC_COUNT; i++) {
        if (g_kernel.adc_resources[i].state == RES_STATE_CLAIMED) adc_claimed++;
    }
    for (int i = 0; i < PICOMIMI_RES_PWM_CHANNEL_COUNT; i++) {
        if (g_kernel.pwm_resources[i].state == RES_STATE_CLAIMED) pwm_claimed++;
    }
    
    printf("\n--- Currently Claimed ---\n");
    printf("GPIO: %lu/%d\n", (unsigned long)gpio_claimed, PICOMIMI_RES_GPIO_COUNT);
    printf("SPI: %lu/%d\n", (unsigned long)spi_claimed, PICOMIMI_RES_SPI_COUNT);
    printf("I2C: %lu/%d\n", (unsigned long)i2c_claimed, PICOMIMI_RES_I2C_COUNT * 16);
    printf("ADC: %lu/%d\n", (unsigned long)adc_claimed, PICOMIMI_RES_ADC_COUNT);
    printf("PWM: %lu/%d\n", (unsigned long)pwm_claimed, PICOMIMI_RES_PWM_CHANNEL_COUNT);
}

// ============================================================================
// RESOURCE TICK (Called from main loop)
// ============================================================================

void pm_res_tick(void) {
    // Check for transaction timeouts
    for (int i = 0; i < PICOMIMI_MAX_TRANSACTIONS; i++) {
        pm_resource_transaction_t* t = &g_kernel.transactions[i];
        if (t->active && t->timeout_ms > 0) {
            if (g_kernel.uptime_ms - t->start_time_ms > t->timeout_ms) {
                // Transaction timed out - release all handles
                for (int j = 0; j < t->handle_count; j++) {
                    pm_res_release(t->handles[j]);
                }
                t->active = false;
                pm_klog(PICOMIMI_LOG_LEVEL_WARN, "Transaction timeout: task %lu",
                        (unsigned long)t->owner_task_id);
            }
        }
    }
}
