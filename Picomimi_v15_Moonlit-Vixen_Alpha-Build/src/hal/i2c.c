/**
 * PICOMIMI I2C HAL Implementation
 */
#include "hal/i2c.h"
#include "api/picomimi_kernel.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

pm_result_t pm_i2c_init(pm_i2c_t* i2c, const pm_i2c_config_t* config) {
    if (!i2c || !config || !config->inst) return PM_ERROR_INVALID;
    
    i2c->inst = config->inst;
    i2c->config = *config;
    i2c->owner = pm_task_get_current();
    
    i2c_init(i2c->inst, config->baudrate);
    
    gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(config->sda_pin);
    gpio_pull_up(config->scl_pin);
    
    i2c->initialized = true;
    return PM_OK;
}

pm_result_t pm_i2c_deinit(pm_i2c_t* i2c) {
    if (!i2c || !i2c->initialized) return PM_ERROR_INVALID;
    i2c_deinit(i2c->inst);
    i2c->initialized = false;
    return PM_OK;
}

int pm_i2c_write(pm_i2c_t* i2c, uint8_t addr, const uint8_t* data, size_t len, bool nostop) {
    if (!i2c || !i2c->initialized || !data) return -1;
    return i2c_write_blocking(i2c->inst, addr, data, len, nostop);
}

int pm_i2c_read(pm_i2c_t* i2c, uint8_t addr, uint8_t* data, size_t len, bool nostop) {
    if (!i2c || !i2c->initialized || !data) return -1;
    return i2c_read_blocking(i2c->inst, addr, data, len, nostop);
}

int pm_i2c_write_read(pm_i2c_t* i2c, uint8_t addr, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen) {
    if (!i2c || !i2c->initialized) return -1;
    int ret = i2c_write_blocking(i2c->inst, addr, wdata, wlen, true);
    if (ret < 0) return ret;
    return i2c_read_blocking(i2c->inst, addr, rdata, rlen, false);
}

int pm_i2c_write_reg(pm_i2c_t* i2c, uint8_t addr, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return pm_i2c_write(i2c, addr, data, 2, false);
}

int pm_i2c_read_reg(pm_i2c_t* i2c, uint8_t addr, uint8_t reg, uint8_t* value) {
    int ret = pm_i2c_write(i2c, addr, &reg, 1, true);
    if (ret < 0) return ret;
    return pm_i2c_read(i2c, addr, value, 1, false);
}
