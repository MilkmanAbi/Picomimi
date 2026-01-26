/**
 * PICOMIMI SPI HAL Implementation
 */
#include "hal/spi.h"
#include "api/picomimi_kernel.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

pm_result_t pm_spi_init(pm_spi_t* spi, const pm_spi_config_t* config) {
    if (!spi || !config || !config->inst) return PM_ERROR_INVALID;
    
    spi->inst = config->inst;
    spi->config = *config;
    spi->owner = pm_task_get_current();
    
    // Initialize SPI
    spi_init(spi->inst, config->baudrate);
    
    // Configure pins
    gpio_set_function(config->sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->mosi_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->miso_pin, GPIO_FUNC_SPI);
    
    // CS pin is manually controlled
    gpio_init(config->cs_pin);
    gpio_set_dir(config->cs_pin, GPIO_OUT);
    gpio_put(config->cs_pin, 1);  // Deselected
    
    spi->initialized = true;
    return PM_OK;
}

pm_result_t pm_spi_deinit(pm_spi_t* spi) {
    if (!spi || !spi->initialized) return PM_ERROR_INVALID;
    spi_deinit(spi->inst);
    spi->initialized = false;
    return PM_OK;
}

pm_result_t pm_spi_set_baudrate(pm_spi_t* spi, uint baudrate) {
    if (!spi || !spi->initialized) return PM_ERROR_INVALID;
    spi_set_baudrate(spi->inst, baudrate);
    spi->config.baudrate = baudrate;
    return PM_OK;
}

int pm_spi_write(pm_spi_t* spi, const uint8_t* data, size_t len) {
    if (!spi || !spi->initialized || !data) return -1;
    return spi_write_blocking(spi->inst, data, len);
}

int pm_spi_read(pm_spi_t* spi, uint8_t* data, size_t len) {
    if (!spi || !spi->initialized || !data) return -1;
    return spi_read_blocking(spi->inst, 0xFF, data, len);
}

int pm_spi_transfer(pm_spi_t* spi, const uint8_t* tx, uint8_t* rx, size_t len) {
    if (!spi || !spi->initialized) return -1;
    return spi_write_read_blocking(spi->inst, tx, rx, len);
}

void pm_spi_cs_select(pm_spi_t* spi) {
    if (spi && spi->initialized) {
        gpio_put(spi->config.cs_pin, 0);
    }
}

void pm_spi_cs_deselect(pm_spi_t* spi) {
    if (spi && spi->initialized) {
        gpio_put(spi->config.cs_pin, 1);
    }
}
