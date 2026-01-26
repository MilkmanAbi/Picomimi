/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  PICOMIMI SD Card Library - Pure Pico-SDK Implementation                   ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Non-blocking SPI-based SD card driver                                     ║
 * ║  Supports: SD, SDHC, SDXC cards                                           ║
 * ║  Features: DMA transfers, async operations, power management               ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef PICOMIMI_SD_CARD_H
#define PICOMIMI_SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/spi.h"
#include "hardware/dma.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SD CARD CONSTANTS
// ============================================================================

// SD Card types
#define SD_TYPE_UNKNOWN     0
#define SD_TYPE_MMC         1
#define SD_TYPE_SD1         2       // SD v1.x
#define SD_TYPE_SD2         3       // SD v2.x (SDSC)
#define SD_TYPE_SDHC        4       // SDHC/SDXC

// SD Commands
#define SD_CMD0             0       // GO_IDLE_STATE
#define SD_CMD1             1       // SEND_OP_COND (MMC)
#define SD_CMD8             8       // SEND_IF_COND
#define SD_CMD9             9       // SEND_CSD
#define SD_CMD10            10      // SEND_CID
#define SD_CMD12            12      // STOP_TRANSMISSION
#define SD_CMD13            13      // SEND_STATUS
#define SD_CMD16            16      // SET_BLOCKLEN
#define SD_CMD17            17      // READ_SINGLE_BLOCK
#define SD_CMD18            18      // READ_MULTIPLE_BLOCK
#define SD_CMD23            23      // SET_BLOCK_COUNT (for CMD25)
#define SD_CMD24            24      // WRITE_BLOCK
#define SD_CMD25            25      // WRITE_MULTIPLE_BLOCK
#define SD_CMD55            55      // APP_CMD
#define SD_CMD58            58      // READ_OCR
#define SD_ACMD23           23      // SET_WR_BLK_ERASE_COUNT
#define SD_ACMD41           41      // SD_SEND_OP_COND

// R1 Response bits
#define SD_R1_IDLE          0x01
#define SD_R1_ERASE_RESET   0x02
#define SD_R1_ILLEGAL_CMD   0x04
#define SD_R1_CRC_ERROR     0x08
#define SD_R1_ERASE_SEQ     0x10
#define SD_R1_ADDRESS_ERROR 0x20
#define SD_R1_PARAM_ERROR   0x40

// Data tokens
#define SD_TOKEN_START_BLOCK        0xFE
#define SD_TOKEN_START_MULTI_WRITE  0xFC
#define SD_TOKEN_STOP_MULTI_WRITE   0xFD

// Data response
#define SD_DATA_ACCEPTED    0x05
#define SD_DATA_CRC_ERROR   0x0B
#define SD_DATA_WRITE_ERROR 0x0D

// Block size
#define SD_BLOCK_SIZE       512

// Timeouts (in ms)
#define SD_TIMEOUT_INIT     2000
#define SD_TIMEOUT_CMD      200
#define SD_TIMEOUT_READ     300
#define SD_TIMEOUT_WRITE    500
#define SD_TIMEOUT_ERASE    10000

// ============================================================================
// SD CARD CONFIGURATION
// ============================================================================

/**
 * SD card hardware configuration
 */
typedef struct {
    spi_inst_t* spi;            // SPI instance (spi0 or spi1)
    uint8_t pin_cs;             // Chip select pin
    uint8_t pin_sck;            // Clock pin
    uint8_t pin_mosi;           // MOSI pin
    uint8_t pin_miso;           // MISO pin
    uint32_t spi_freq_init;     // Init frequency (typically 400kHz)
    uint32_t spi_freq_fast;     // Normal frequency (up to 50MHz)
    bool use_dma;               // Enable DMA transfers
    int dma_tx_channel;         // DMA TX channel (-1 for auto)
    int dma_rx_channel;         // DMA RX channel (-1 for auto)
} sd_config_t;

/**
 * SD card information
 */
typedef struct {
    uint8_t type;               // Card type (SD_TYPE_xxx)
    uint64_t capacity;          // Total capacity in bytes
    uint32_t num_sectors;       // Total sectors
    uint16_t sector_size;       // Sector size (always 512)
    uint8_t csd_version;        // CSD structure version
    uint8_t cid[16];            // Card identification
    uint8_t csd[16];            // Card specific data
    uint32_t ocr;               // Operation conditions register
    uint32_t rca;               // Relative card address
    bool high_capacity;         // SDHC/SDXC flag
    bool initialized;           // Successfully initialized
} sd_card_info_t;

/**
 * SD card state
 */
typedef struct {
    sd_config_t config;
    sd_card_info_t info;
    
    // DMA channels
    int dma_tx;
    int dma_rx;
    
    // State tracking
    bool card_present;
    bool spi_initialized;
    uint32_t current_freq;
    
    // Statistics
    uint32_t read_count;
    uint32_t write_count;
    uint32_t error_count;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    
    // Async state
    bool async_busy;
    uint32_t async_sector;
    uint8_t* async_buffer;
    size_t async_count;
} sd_state_t;

/**
 * Async operation callback
 */
typedef void (*sd_async_callback_t)(int result, void* user_data);

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Get default configuration for common pin setups
 */
void sd_get_default_config(sd_config_t* config);

/**
 * Initialize SD card with given configuration
 * @return 0 on success, negative error code on failure
 */
int sd_init(sd_state_t* sd, const sd_config_t* config);

/**
 * Deinitialize SD card
 */
void sd_deinit(sd_state_t* sd);

/**
 * Check if card is present and initialized
 */
bool sd_is_ready(const sd_state_t* sd);

/**
 * Get card information
 */
const sd_card_info_t* sd_get_info(const sd_state_t* sd);

// ============================================================================
// BASIC BLOCK OPERATIONS (BLOCKING)
// ============================================================================

/**
 * Read single block (512 bytes)
 * @param sector Sector number (LBA)
 * @param buffer Buffer to read into (must be 512 bytes)
 * @return 0 on success, negative error code on failure
 */
int sd_read_block(sd_state_t* sd, uint32_t sector, uint8_t* buffer);

/**
 * Read multiple blocks
 * @param sector Starting sector number
 * @param buffer Buffer to read into
 * @param count Number of sectors to read
 * @return 0 on success, negative error code on failure
 */
int sd_read_blocks(sd_state_t* sd, uint32_t sector, uint8_t* buffer, uint32_t count);

/**
 * Write single block (512 bytes)
 * @param sector Sector number (LBA)
 * @param buffer Buffer to write from (must be 512 bytes)
 * @return 0 on success, negative error code on failure
 */
int sd_write_block(sd_state_t* sd, uint32_t sector, const uint8_t* buffer);

/**
 * Write multiple blocks
 * @param sector Starting sector number
 * @param buffer Buffer to write from
 * @param count Number of sectors to write
 * @return 0 on success, negative error code on failure
 */
int sd_write_blocks(sd_state_t* sd, uint32_t sector, const uint8_t* buffer, uint32_t count);

/**
 * Sync - ensure all writes are complete
 * @return 0 on success, negative error code on failure
 */
int sd_sync(sd_state_t* sd);

// ============================================================================
// NON-BLOCKING / ASYNC OPERATIONS
// ============================================================================

/**
 * Start async block read (non-blocking)
 * @param sector Sector number
 * @param buffer Buffer to read into
 * @param count Number of sectors
 * @param callback Completion callback (called from IRQ context)
 * @param user_data User data for callback
 * @return 0 if operation started, negative error code on failure
 */
int sd_read_async(sd_state_t* sd, uint32_t sector, uint8_t* buffer, 
                  uint32_t count, sd_async_callback_t callback, void* user_data);

/**
 * Start async block write (non-blocking)
 * @param sector Sector number
 * @param buffer Buffer to write from
 * @param count Number of sectors
 * @param callback Completion callback (called from IRQ context)
 * @param user_data User data for callback
 * @return 0 if operation started, negative error code on failure
 */
int sd_write_async(sd_state_t* sd, uint32_t sector, const uint8_t* buffer,
                   uint32_t count, sd_async_callback_t callback, void* user_data);

/**
 * Check if async operation is in progress
 */
bool sd_is_busy(const sd_state_t* sd);

/**
 * Poll async operation (call periodically if not using IRQ)
 * @return 0 if complete, 1 if still busy, negative on error
 */
int sd_poll_async(sd_state_t* sd);

/**
 * Cancel any pending async operation
 */
void sd_cancel_async(sd_state_t* sd);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Get card capacity in bytes
 */
uint64_t sd_get_capacity(const sd_state_t* sd);

/**
 * Get number of sectors
 */
uint32_t sd_get_sector_count(const sd_state_t* sd);

/**
 * Set SPI frequency
 * @param freq_hz New frequency in Hz
 * @return Actual frequency set
 */
uint32_t sd_set_frequency(sd_state_t* sd, uint32_t freq_hz);

/**
 * Power down SD card (for power saving)
 */
void sd_power_down(sd_state_t* sd);

/**
 * Power up and reinitialize SD card
 * @return 0 on success, negative error code on failure
 */
int sd_power_up(sd_state_t* sd);

/**
 * Get error string for error code
 */
const char* sd_error_string(int error);

// ============================================================================
// ERROR CODES
// ============================================================================

#define SD_OK                   0
#define SD_ERROR_NO_CARD       -1
#define SD_ERROR_INIT_FAILED   -2
#define SD_ERROR_CMD_FAILED    -3
#define SD_ERROR_TIMEOUT       -4
#define SD_ERROR_CRC           -5
#define SD_ERROR_WRITE_FAIL    -6
#define SD_ERROR_READ_FAIL     -7
#define SD_ERROR_INVALID_PARAM -8
#define SD_ERROR_BUSY          -9
#define SD_ERROR_NOT_READY    -10

// ============================================================================
// INTERNAL (for diskio.c integration)
// ============================================================================

/**
 * Low-level SPI byte transfer
 */
uint8_t sd_spi_transfer(sd_state_t* sd, uint8_t data);

/**
 * Low-level SPI multi-byte transfer
 */
void sd_spi_transfer_bulk(sd_state_t* sd, const uint8_t* tx, uint8_t* rx, size_t len);

/**
 * Select (CS low) / deselect (CS high) card
 */
void sd_select(sd_state_t* sd);
void sd_deselect(sd_state_t* sd);

/**
 * Wait for card to be ready
 */
bool sd_wait_ready(sd_state_t* sd, uint32_t timeout_ms);

/**
 * Send command and get R1 response
 */
uint8_t sd_send_command(sd_state_t* sd, uint8_t cmd, uint32_t arg);

/**
 * Send application command (CMD55 + ACMDxx)
 */
uint8_t sd_send_acmd(sd_state_t* sd, uint8_t acmd, uint32_t arg);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_SD_CARD_H
