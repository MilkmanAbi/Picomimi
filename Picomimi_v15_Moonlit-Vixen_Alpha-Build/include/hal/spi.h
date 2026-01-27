/**
 * PICOMIMI SPI HAL
 */
#ifndef PICOMIMI_HAL_SPI_H
#define PICOMIMI_HAL_SPI_H

#include "api/picomimi_types.h"
#include "hardware/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_inst_t* inst;
    uint baudrate;
    uint8_t sck_pin;
    uint8_t mosi_pin;
    uint8_t miso_pin;
    uint8_t cs_pin;
    bool use_dma;
} pm_spi_config_t;

typedef struct {
    spi_inst_t* inst;
    pm_spi_config_t config;
    pm_task_id_t owner;
    bool initialized;
} pm_spi_t;

pm_result_t pm_spi_init(pm_spi_t* spi, const pm_spi_config_t* config);
pm_result_t pm_spi_deinit(pm_spi_t* spi);
pm_result_t pm_spi_set_baudrate(pm_spi_t* spi, uint baudrate);

int pm_spi_write(pm_spi_t* spi, const uint8_t* data, size_t len);
int pm_spi_read(pm_spi_t* spi, uint8_t* data, size_t len);
int pm_spi_transfer(pm_spi_t* spi, const uint8_t* tx, uint8_t* rx, size_t len);

void pm_spi_cs_select(pm_spi_t* spi);
void pm_spi_cs_deselect(pm_spi_t* spi);

#ifdef __cplusplus
}
#endif

#endif
