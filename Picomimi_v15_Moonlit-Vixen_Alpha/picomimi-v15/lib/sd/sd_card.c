/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  PICOMIMI SD Card Library - Implementation                                 ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Non-blocking SPI-based SD card driver for Pico-SDK                        ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include "sd_card.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "pico/time.h"
#include <string.h>

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * Calculate CRC7 for SD commands
 */
static uint8_t sd_crc7(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if ((byte & 0x80) ^ (crc & 0x80)) {
                crc ^= 0x09;
            }
            byte <<= 1;
        }
    }
    return (crc << 1) | 1;  // Add stop bit
}

/**
 * Calculate CRC16 for SD data blocks
 */
static uint16_t sd_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) | (crc << 8);
        crc ^= data[i];
        crc ^= (crc & 0xFF) >> 4;
        crc ^= crc << 12;
        crc ^= (crc & 0xFF) << 5;
    }
    return crc;
}

/**
 * Delay microseconds
 */
static inline void sd_delay_us(uint32_t us) {
    busy_wait_us(us);
}

/**
 * Get time in milliseconds
 */
static inline uint32_t sd_get_time_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ============================================================================
// SPI OPERATIONS
// ============================================================================

void sd_select(sd_state_t* sd) {
    gpio_put(sd->config.pin_cs, 0);
    sd_delay_us(1);
}

void sd_deselect(sd_state_t* sd) {
    gpio_put(sd->config.pin_cs, 1);
    sd_delay_us(1);
    // Send extra clocks to release MISO
    spi_write_blocking(sd->config.spi, (const uint8_t[]){0xFF}, 1);
}

uint8_t sd_spi_transfer(sd_state_t* sd, uint8_t data) {
    uint8_t rx;
    spi_write_read_blocking(sd->config.spi, &data, &rx, 1);
    return rx;
}

void sd_spi_transfer_bulk(sd_state_t* sd, const uint8_t* tx, uint8_t* rx, size_t len) {
    if (tx && rx) {
        spi_write_read_blocking(sd->config.spi, tx, rx, len);
    } else if (tx) {
        spi_write_blocking(sd->config.spi, tx, len);
    } else if (rx) {
        spi_read_blocking(sd->config.spi, 0xFF, rx, len);
    }
}

bool sd_wait_ready(sd_state_t* sd, uint32_t timeout_ms) {
    uint32_t start = sd_get_time_ms();
    uint8_t resp;
    
    do {
        resp = sd_spi_transfer(sd, 0xFF);
        if (resp == 0xFF) {
            return true;
        }
    } while ((sd_get_time_ms() - start) < timeout_ms);
    
    return false;
}

// ============================================================================
// COMMAND INTERFACE
// ============================================================================

uint8_t sd_send_command(sd_state_t* sd, uint8_t cmd, uint32_t arg) {
    uint8_t buf[6];
    uint8_t resp;
    int retry;
    
    // Wait for card to be ready
    if (cmd != SD_CMD0 && cmd != SD_CMD12) {
        if (!sd_wait_ready(sd, SD_TIMEOUT_CMD)) {
            return 0xFF;
        }
    }
    
    // Build command
    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;
    buf[5] = sd_crc7(buf, 5);
    
    // Special case CRCs for init commands
    if (cmd == SD_CMD0) buf[5] = 0x95;
    if (cmd == SD_CMD8) buf[5] = 0x87;
    
    // Send command
    sd_spi_transfer_bulk(sd, buf, NULL, 6);
    
    // Skip stuff byte for CMD12
    if (cmd == SD_CMD12) {
        sd_spi_transfer(sd, 0xFF);
    }
    
    // Wait for response (R1: MSB = 0)
    retry = 10;
    do {
        resp = sd_spi_transfer(sd, 0xFF);
    } while ((resp & 0x80) && --retry);
    
    return resp;
}

uint8_t sd_send_acmd(sd_state_t* sd, uint8_t acmd, uint32_t arg) {
    uint8_t resp;
    
    // Send CMD55 first
    resp = sd_send_command(sd, SD_CMD55, 0);
    if (resp > 1) {
        return resp;
    }
    
    // Send application command
    return sd_send_command(sd, acmd, arg);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void sd_get_default_config(sd_config_t* config) {
    config->spi = spi0;
    config->pin_cs = PICOMIMI_SD_CS_PIN;
    config->pin_sck = PICOMIMI_SD_SCK_PIN;
    config->pin_mosi = PICOMIMI_SD_MOSI_PIN;
    config->pin_miso = PICOMIMI_SD_MISO_PIN;
    config->spi_freq_init = 400000;     // 400 kHz for init
    config->spi_freq_fast = 25000000;   // 25 MHz for normal ops
    config->use_dma = true;
    config->dma_tx_channel = -1;        // Auto-allocate
    config->dma_rx_channel = -1;
}

static int sd_init_card(sd_state_t* sd) {
    uint8_t resp;
    uint32_t start;
    int retry;
    
    // Initialize info structure
    memset(&sd->info, 0, sizeof(sd->info));
    sd->info.type = SD_TYPE_UNKNOWN;
    sd->info.sector_size = SD_BLOCK_SIZE;
    
    // Set slow speed for initialization
    spi_set_baudrate(sd->config.spi, sd->config.spi_freq_init);
    sd->current_freq = sd->config.spi_freq_init;
    
    // Power on sequence: >74 clocks with CS high
    sd_deselect(sd);
    uint8_t dummy[10];
    memset(dummy, 0xFF, sizeof(dummy));
    spi_write_blocking(sd->config.spi, dummy, sizeof(dummy));
    
    // CMD0: Go to idle state
    sd_select(sd);
    retry = 10;
    do {
        resp = sd_send_command(sd, SD_CMD0, 0);
    } while (resp != SD_R1_IDLE && --retry);
    
    if (resp != SD_R1_IDLE) {
        sd_deselect(sd);
        return SD_ERROR_INIT_FAILED;
    }
    
    // CMD8: Check for SD v2
    resp = sd_send_command(sd, SD_CMD8, 0x1AA);
    if (resp == SD_R1_IDLE) {
        // SD v2.0 or later
        uint8_t ocr[4];
        sd_spi_transfer_bulk(sd, NULL, ocr, 4);
        
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) {
            sd_deselect(sd);
            return SD_ERROR_INIT_FAILED;
        }
        
        // ACMD41 with HCS bit
        start = sd_get_time_ms();
        do {
            resp = sd_send_acmd(sd, SD_ACMD41, 0x40000000);
            if ((sd_get_time_ms() - start) > SD_TIMEOUT_INIT) {
                sd_deselect(sd);
                return SD_ERROR_TIMEOUT;
            }
        } while (resp != 0);
        
        // CMD58: Read OCR to check CCS
        resp = sd_send_command(sd, SD_CMD58, 0);
        if (resp != 0) {
            sd_deselect(sd);
            return SD_ERROR_INIT_FAILED;
        }
        sd_spi_transfer_bulk(sd, NULL, ocr, 4);
        sd->info.ocr = ((uint32_t)ocr[0] << 24) | ((uint32_t)ocr[1] << 16) |
                       ((uint32_t)ocr[2] << 8) | ocr[3];
        
        if (ocr[0] & 0x40) {
            sd->info.type = SD_TYPE_SDHC;
            sd->info.high_capacity = true;
        } else {
            sd->info.type = SD_TYPE_SD2;
        }
    } else {
        // SD v1.x or MMC
        resp = sd_send_acmd(sd, SD_ACMD41, 0);
        if (resp <= 1) {
            // SD v1.x
            sd->info.type = SD_TYPE_SD1;
            start = sd_get_time_ms();
            do {
                resp = sd_send_acmd(sd, SD_ACMD41, 0);
                if ((sd_get_time_ms() - start) > SD_TIMEOUT_INIT) {
                    sd_deselect(sd);
                    return SD_ERROR_TIMEOUT;
                }
            } while (resp != 0);
        } else {
            // MMC
            sd->info.type = SD_TYPE_MMC;
            start = sd_get_time_ms();
            do {
                resp = sd_send_command(sd, SD_CMD1, 0);
                if ((sd_get_time_ms() - start) > SD_TIMEOUT_INIT) {
                    sd_deselect(sd);
                    return SD_ERROR_TIMEOUT;
                }
            } while (resp != 0);
        }
        
        // Set block size for non-SDHC
        resp = sd_send_command(sd, SD_CMD16, SD_BLOCK_SIZE);
        if (resp != 0) {
            sd_deselect(sd);
            return SD_ERROR_INIT_FAILED;
        }
    }
    
    // Read CSD register
    resp = sd_send_command(sd, SD_CMD9, 0);
    if (resp != 0) {
        sd_deselect(sd);
        return SD_ERROR_INIT_FAILED;
    }
    
    // Wait for data token
    start = sd_get_time_ms();
    do {
        resp = sd_spi_transfer(sd, 0xFF);
        if ((sd_get_time_ms() - start) > SD_TIMEOUT_READ) {
            sd_deselect(sd);
            return SD_ERROR_TIMEOUT;
        }
    } while (resp != SD_TOKEN_START_BLOCK);
    
    // Read CSD data
    sd_spi_transfer_bulk(sd, NULL, sd->info.csd, 16);
    sd_spi_transfer(sd, 0xFF);  // Skip CRC
    sd_spi_transfer(sd, 0xFF);
    
    // Calculate capacity from CSD
    sd->info.csd_version = (sd->info.csd[0] >> 6) & 0x03;
    if (sd->info.csd_version == 1) {
        // CSD v2.0 (SDHC/SDXC)
        uint32_t c_size = ((uint32_t)(sd->info.csd[7] & 0x3F) << 16) |
                          ((uint32_t)sd->info.csd[8] << 8) |
                          sd->info.csd[9];
        sd->info.capacity = (uint64_t)(c_size + 1) * 512 * 1024;
        sd->info.num_sectors = (c_size + 1) * 1024;
    } else {
        // CSD v1.0
        uint32_t c_size = ((uint32_t)(sd->info.csd[6] & 0x03) << 10) |
                          ((uint32_t)sd->info.csd[7] << 2) |
                          ((sd->info.csd[8] >> 6) & 0x03);
        uint32_t c_size_mult = ((sd->info.csd[9] & 0x03) << 1) |
                               ((sd->info.csd[10] >> 7) & 0x01);
        uint32_t read_bl_len = sd->info.csd[5] & 0x0F;
        uint32_t mult = 1 << (c_size_mult + 2);
        uint32_t block_nr = (c_size + 1) * mult;
        uint32_t block_len = 1 << read_bl_len;
        sd->info.capacity = (uint64_t)block_nr * block_len;
        sd->info.num_sectors = sd->info.capacity / SD_BLOCK_SIZE;
    }
    
    // Read CID register
    resp = sd_send_command(sd, SD_CMD10, 0);
    if (resp == 0) {
        start = sd_get_time_ms();
        do {
            resp = sd_spi_transfer(sd, 0xFF);
            if ((sd_get_time_ms() - start) > SD_TIMEOUT_READ) break;
        } while (resp != SD_TOKEN_START_BLOCK);
        
        if (resp == SD_TOKEN_START_BLOCK) {
            sd_spi_transfer_bulk(sd, NULL, sd->info.cid, 16);
            sd_spi_transfer(sd, 0xFF);
            sd_spi_transfer(sd, 0xFF);
        }
    }
    
    sd_deselect(sd);
    
    // Switch to fast SPI speed
    spi_set_baudrate(sd->config.spi, sd->config.spi_freq_fast);
    sd->current_freq = sd->config.spi_freq_fast;
    
    sd->info.initialized = true;
    sd->card_present = true;
    
    return SD_OK;
}

int sd_init(sd_state_t* sd, const sd_config_t* config) {
    if (!sd || !config) {
        return SD_ERROR_INVALID_PARAM;
    }
    
    // Copy configuration
    memcpy(&sd->config, config, sizeof(sd_config_t));
    
    // Initialize state
    sd->card_present = false;
    sd->spi_initialized = false;
    sd->async_busy = false;
    sd->dma_tx = -1;
    sd->dma_rx = -1;
    sd->read_count = 0;
    sd->write_count = 0;
    sd->error_count = 0;
    sd->crc_errors = 0;
    sd->timeout_errors = 0;
    
    // Initialize GPIO pins
    gpio_init(sd->config.pin_cs);
    gpio_set_dir(sd->config.pin_cs, GPIO_OUT);
    gpio_put(sd->config.pin_cs, 1);  // Deselect
    
    // Initialize SPI
    spi_init(sd->config.spi, sd->config.spi_freq_init);
    spi_set_format(sd->config.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    
    // Configure SPI pins
    gpio_set_function(sd->config.pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(sd->config.pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function(sd->config.pin_miso, GPIO_FUNC_SPI);
    
    // Enable pull-up on MISO
    gpio_pull_up(sd->config.pin_miso);
    
    sd->spi_initialized = true;
    sd->current_freq = sd->config.spi_freq_init;
    
    // Allocate DMA channels if requested
    if (sd->config.use_dma) {
        if (sd->config.dma_tx_channel >= 0) {
            sd->dma_tx = sd->config.dma_tx_channel;
        } else {
            sd->dma_tx = dma_claim_unused_channel(false);
        }
        if (sd->config.dma_rx_channel >= 0) {
            sd->dma_rx = sd->config.dma_rx_channel;
        } else {
            sd->dma_rx = dma_claim_unused_channel(false);
        }
    }
    
    // Give card time to power up
    sd_delay_us(10000);
    
    // Initialize the card
    return sd_init_card(sd);
}

void sd_deinit(sd_state_t* sd) {
    if (!sd) return;
    
    // Release DMA channels
    if (sd->dma_tx >= 0) {
        dma_channel_unclaim(sd->dma_tx);
        sd->dma_tx = -1;
    }
    if (sd->dma_rx >= 0) {
        dma_channel_unclaim(sd->dma_rx);
        sd->dma_rx = -1;
    }
    
    // Deinitialize SPI
    if (sd->spi_initialized) {
        spi_deinit(sd->config.spi);
        sd->spi_initialized = false;
    }
    
    // Reset state
    sd->card_present = false;
    sd->info.initialized = false;
}

bool sd_is_ready(const sd_state_t* sd) {
    return sd && sd->info.initialized && sd->card_present;
}

const sd_card_info_t* sd_get_info(const sd_state_t* sd) {
    return sd ? &sd->info : NULL;
}

// ============================================================================
// BLOCK READ/WRITE
// ============================================================================

int sd_read_block(sd_state_t* sd, uint32_t sector, uint8_t* buffer) {
    if (!sd_is_ready(sd) || !buffer) {
        return SD_ERROR_NOT_READY;
    }
    
    uint8_t resp;
    uint32_t addr = sd->info.high_capacity ? sector : sector * SD_BLOCK_SIZE;
    
    sd_select(sd);
    
    // Send read command
    resp = sd_send_command(sd, SD_CMD17, addr);
    if (resp != 0) {
        sd_deselect(sd);
        sd->error_count++;
        return SD_ERROR_CMD_FAILED;
    }
    
    // Wait for data token
    uint32_t start = sd_get_time_ms();
    do {
        resp = sd_spi_transfer(sd, 0xFF);
        if ((sd_get_time_ms() - start) > SD_TIMEOUT_READ) {
            sd_deselect(sd);
            sd->timeout_errors++;
            sd->error_count++;
            return SD_ERROR_TIMEOUT;
        }
    } while (resp == 0xFF);
    
    if (resp != SD_TOKEN_START_BLOCK) {
        sd_deselect(sd);
        sd->error_count++;
        return SD_ERROR_READ_FAIL;
    }
    
    // Read data block
    sd_spi_transfer_bulk(sd, NULL, buffer, SD_BLOCK_SIZE);
    
    // Read and verify CRC
    uint8_t crc_hi = sd_spi_transfer(sd, 0xFF);
    uint8_t crc_lo = sd_spi_transfer(sd, 0xFF);
    (void)crc_hi; (void)crc_lo;  // TODO: verify CRC
    
    sd_deselect(sd);
    sd->read_count++;
    
    return SD_OK;
}

int sd_read_blocks(sd_state_t* sd, uint32_t sector, uint8_t* buffer, uint32_t count) {
    if (!sd_is_ready(sd) || !buffer || count == 0) {
        return SD_ERROR_NOT_READY;
    }
    
    if (count == 1) {
        return sd_read_block(sd, sector, buffer);
    }
    
    uint8_t resp;
    uint32_t addr = sd->info.high_capacity ? sector : sector * SD_BLOCK_SIZE;
    
    sd_select(sd);
    
    // Send multi-block read command
    resp = sd_send_command(sd, SD_CMD18, addr);
    if (resp != 0) {
        sd_deselect(sd);
        sd->error_count++;
        return SD_ERROR_CMD_FAILED;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        // Wait for data token
        uint32_t start = sd_get_time_ms();
        do {
            resp = sd_spi_transfer(sd, 0xFF);
            if ((sd_get_time_ms() - start) > SD_TIMEOUT_READ) {
                sd_send_command(sd, SD_CMD12, 0);
                sd_deselect(sd);
                sd->timeout_errors++;
                sd->error_count++;
                return SD_ERROR_TIMEOUT;
            }
        } while (resp == 0xFF);
        
        if (resp != SD_TOKEN_START_BLOCK) {
            sd_send_command(sd, SD_CMD12, 0);
            sd_deselect(sd);
            sd->error_count++;
            return SD_ERROR_READ_FAIL;
        }
        
        // Read block
        sd_spi_transfer_bulk(sd, NULL, buffer + i * SD_BLOCK_SIZE, SD_BLOCK_SIZE);
        
        // Skip CRC
        sd_spi_transfer(sd, 0xFF);
        sd_spi_transfer(sd, 0xFF);
    }
    
    // Send stop transmission
    sd_send_command(sd, SD_CMD12, 0);
    sd_deselect(sd);
    
    sd->read_count += count;
    return SD_OK;
}

int sd_write_block(sd_state_t* sd, uint32_t sector, const uint8_t* buffer) {
    if (!sd_is_ready(sd) || !buffer) {
        return SD_ERROR_NOT_READY;
    }
    
    uint8_t resp;
    uint32_t addr = sd->info.high_capacity ? sector : sector * SD_BLOCK_SIZE;
    
    sd_select(sd);
    
    // Send write command
    resp = sd_send_command(sd, SD_CMD24, addr);
    if (resp != 0) {
        sd_deselect(sd);
        sd->error_count++;
        return SD_ERROR_CMD_FAILED;
    }
    
    // Wait for card ready
    if (!sd_wait_ready(sd, SD_TIMEOUT_WRITE)) {
        sd_deselect(sd);
        sd->timeout_errors++;
        sd->error_count++;
        return SD_ERROR_TIMEOUT;
    }
    
    // Send start block token
    sd_spi_transfer(sd, SD_TOKEN_START_BLOCK);
    
    // Send data
    sd_spi_transfer_bulk(sd, buffer, NULL, SD_BLOCK_SIZE);
    
    // Send dummy CRC
    sd_spi_transfer(sd, 0xFF);
    sd_spi_transfer(sd, 0xFF);
    
    // Get data response
    resp = sd_spi_transfer(sd, 0xFF);
    if ((resp & 0x1F) != SD_DATA_ACCEPTED) {
        sd_deselect(sd);
        sd->error_count++;
        return SD_ERROR_WRITE_FAIL;
    }
    
    // Wait for write to complete
    if (!sd_wait_ready(sd, SD_TIMEOUT_WRITE)) {
        sd_deselect(sd);
        sd->timeout_errors++;
        sd->error_count++;
        return SD_ERROR_TIMEOUT;
    }
    
    sd_deselect(sd);
    sd->write_count++;
    
    return SD_OK;
}

int sd_write_blocks(sd_state_t* sd, uint32_t sector, const uint8_t* buffer, uint32_t count) {
    if (!sd_is_ready(sd) || !buffer || count == 0) {
        return SD_ERROR_NOT_READY;
    }
    
    if (count == 1) {
        return sd_write_block(sd, sector, buffer);
    }
    
    uint8_t resp;
    uint32_t addr = sd->info.high_capacity ? sector : sector * SD_BLOCK_SIZE;
    
    sd_select(sd);
    
    // Pre-erase blocks (optional, can improve speed)
    if (sd->info.type != SD_TYPE_MMC) {
        sd_send_acmd(sd, SD_ACMD23, count);
    }
    
    // Send multi-block write command
    resp = sd_send_command(sd, SD_CMD25, addr);
    if (resp != 0) {
        sd_deselect(sd);
        sd->error_count++;
        return SD_ERROR_CMD_FAILED;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        // Wait for ready
        if (!sd_wait_ready(sd, SD_TIMEOUT_WRITE)) {
            sd_spi_transfer(sd, SD_TOKEN_STOP_MULTI_WRITE);
            sd_deselect(sd);
            sd->timeout_errors++;
            sd->error_count++;
            return SD_ERROR_TIMEOUT;
        }
        
        // Send start multi-write token
        sd_spi_transfer(sd, SD_TOKEN_START_MULTI_WRITE);
        
        // Send data
        sd_spi_transfer_bulk(sd, buffer + i * SD_BLOCK_SIZE, NULL, SD_BLOCK_SIZE);
        
        // Send dummy CRC
        sd_spi_transfer(sd, 0xFF);
        sd_spi_transfer(sd, 0xFF);
        
        // Get data response
        resp = sd_spi_transfer(sd, 0xFF);
        if ((resp & 0x1F) != SD_DATA_ACCEPTED) {
            sd_spi_transfer(sd, SD_TOKEN_STOP_MULTI_WRITE);
            sd_deselect(sd);
            sd->error_count++;
            return SD_ERROR_WRITE_FAIL;
        }
    }
    
    // Wait for last write
    if (!sd_wait_ready(sd, SD_TIMEOUT_WRITE)) {
        sd_spi_transfer(sd, SD_TOKEN_STOP_MULTI_WRITE);
        sd_deselect(sd);
        sd->timeout_errors++;
        sd->error_count++;
        return SD_ERROR_TIMEOUT;
    }
    
    // Send stop token
    sd_spi_transfer(sd, SD_TOKEN_STOP_MULTI_WRITE);
    
    // Wait for card to finish
    sd_wait_ready(sd, SD_TIMEOUT_WRITE);
    
    sd_deselect(sd);
    sd->write_count += count;
    
    return SD_OK;
}

int sd_sync(sd_state_t* sd) {
    if (!sd_is_ready(sd)) {
        return SD_ERROR_NOT_READY;
    }
    
    sd_select(sd);
    bool ready = sd_wait_ready(sd, SD_TIMEOUT_WRITE);
    sd_deselect(sd);
    
    return ready ? SD_OK : SD_ERROR_TIMEOUT;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

uint64_t sd_get_capacity(const sd_state_t* sd) {
    return sd && sd->info.initialized ? sd->info.capacity : 0;
}

uint32_t sd_get_sector_count(const sd_state_t* sd) {
    return sd && sd->info.initialized ? sd->info.num_sectors : 0;
}

uint32_t sd_set_frequency(sd_state_t* sd, uint32_t freq_hz) {
    if (!sd || !sd->spi_initialized) return 0;
    sd->current_freq = spi_set_baudrate(sd->config.spi, freq_hz);
    return sd->current_freq;
}

void sd_power_down(sd_state_t* sd) {
    if (!sd) return;
    sd_deselect(sd);
    sd->card_present = false;
}

int sd_power_up(sd_state_t* sd) {
    if (!sd || !sd->spi_initialized) {
        return SD_ERROR_NOT_READY;
    }
    return sd_init_card(sd);
}

const char* sd_error_string(int error) {
    switch (error) {
        case SD_OK:                 return "OK";
        case SD_ERROR_NO_CARD:      return "No card detected";
        case SD_ERROR_INIT_FAILED:  return "Initialization failed";
        case SD_ERROR_CMD_FAILED:   return "Command failed";
        case SD_ERROR_TIMEOUT:      return "Timeout";
        case SD_ERROR_CRC:          return "CRC error";
        case SD_ERROR_WRITE_FAIL:   return "Write failed";
        case SD_ERROR_READ_FAIL:    return "Read failed";
        case SD_ERROR_INVALID_PARAM: return "Invalid parameter";
        case SD_ERROR_BUSY:         return "Device busy";
        case SD_ERROR_NOT_READY:    return "Not ready";
        default:                    return "Unknown error";
    }
}

// ============================================================================
// ASYNC OPERATIONS (STUB - full implementation requires IRQ setup)
// ============================================================================

int sd_read_async(sd_state_t* sd, uint32_t sector, uint8_t* buffer, 
                  uint32_t count, sd_async_callback_t callback, void* user_data) {
    // For now, fall back to blocking
    int result = sd_read_blocks(sd, sector, buffer, count);
    if (callback) {
        callback(result, user_data);
    }
    return result;
}

int sd_write_async(sd_state_t* sd, uint32_t sector, const uint8_t* buffer,
                   uint32_t count, sd_async_callback_t callback, void* user_data) {
    // For now, fall back to blocking
    int result = sd_write_blocks(sd, sector, buffer, count);
    if (callback) {
        callback(result, user_data);
    }
    return result;
}

bool sd_is_busy(const sd_state_t* sd) {
    return sd ? sd->async_busy : false;
}

int sd_poll_async(sd_state_t* sd) {
    return sd && sd->async_busy ? 1 : 0;
}

void sd_cancel_async(sd_state_t* sd) {
    if (sd) sd->async_busy = false;
}
