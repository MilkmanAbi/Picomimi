/**
 * PICOMIMI I2C HAL
 */
#ifndef PICOMIMI_HAL_I2C_H
#define PICOMIMI_HAL_I2C_H

#include "api/picomimi_types.h"
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_inst_t* inst;
    uint baudrate;
    uint8_t sda_pin;
    uint8_t scl_pin;
} pm_i2c_config_t;

typedef struct {
    i2c_inst_t* inst;
    pm_i2c_config_t config;
    pm_task_id_t owner;
    bool initialized;
} pm_i2c_t;

pm_result_t pm_i2c_init(pm_i2c_t* i2c, const pm_i2c_config_t* config);
pm_result_t pm_i2c_deinit(pm_i2c_t* i2c);

int pm_i2c_write(pm_i2c_t* i2c, uint8_t addr, const uint8_t* data, size_t len, bool nostop);
int pm_i2c_read(pm_i2c_t* i2c, uint8_t addr, uint8_t* data, size_t len, bool nostop);
int pm_i2c_write_read(pm_i2c_t* i2c, uint8_t addr, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen);

// Register operations
int pm_i2c_write_reg(pm_i2c_t* i2c, uint8_t addr, uint8_t reg, uint8_t value);
int pm_i2c_read_reg(pm_i2c_t* i2c, uint8_t addr, uint8_t reg, uint8_t* value);

#ifdef __cplusplus
}
#endif

#endif
