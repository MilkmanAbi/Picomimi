/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  PICOMIMI FatFS Disk I/O Interface                                         ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Connects the custom SD card driver to FatFS filesystem                    ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include "ff.h"
#include "diskio.h"
#include "sd_card.h"
#include "pico/time.h"

// ============================================================================
// GLOBAL SD CARD STATE
// ============================================================================

static sd_state_t g_sd_state;
static bool g_sd_initialized = false;

// ============================================================================
// FATFS INTERFACE FUNCTIONS
// ============================================================================

/**
 * Initialize a Drive
 */
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    
    if (g_sd_initialized) {
        return 0;
    }
    
    // Get default configuration
    sd_config_t config;
    sd_get_default_config(&config);
    
    // Initialize SD card
    int result = sd_init(&g_sd_state, &config);
    if (result != SD_OK) {
        return STA_NOINIT;
    }
    
    g_sd_initialized = true;
    return 0;
}

/**
 * Get Drive Status
 */
DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    
    if (!g_sd_initialized || !sd_is_ready(&g_sd_state)) {
        return STA_NOINIT;
    }
    
    return 0;
}

/**
 * Read Sector(s)
 */
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !buff) {
        return RES_PARERR;
    }
    
    if (!g_sd_initialized || !sd_is_ready(&g_sd_state)) {
        return RES_NOTRDY;
    }
    
    int result = sd_read_blocks(&g_sd_state, sector, buff, count);
    if (result != SD_OK) {
        return RES_ERROR;
    }
    
    return RES_OK;
}

/**
 * Write Sector(s)
 */
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !buff) {
        return RES_PARERR;
    }
    
    if (!g_sd_initialized || !sd_is_ready(&g_sd_state)) {
        return RES_NOTRDY;
    }
    
    int result = sd_write_blocks(&g_sd_state, sector, buff, count);
    if (result != SD_OK) {
        return RES_ERROR;
    }
    
    return RES_OK;
}

/**
 * Miscellaneous Functions
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0) {
        return RES_PARERR;
    }
    
    if (!g_sd_initialized || !sd_is_ready(&g_sd_state)) {
        return RES_NOTRDY;
    }
    
    const sd_card_info_t* info = sd_get_info(&g_sd_state);
    
    switch (cmd) {
        case CTRL_SYNC:
            // Ensure pending writes are finished
            if (sd_sync(&g_sd_state) != SD_OK) {
                return RES_ERROR;
            }
            return RES_OK;
            
        case GET_SECTOR_COUNT:
            if (!buff) return RES_PARERR;
            *(LBA_t*)buff = info->num_sectors;
            return RES_OK;
            
        case GET_SECTOR_SIZE:
            if (!buff) return RES_PARERR;
            *(WORD*)buff = info->sector_size;
            return RES_OK;
            
        case GET_BLOCK_SIZE:
            if (!buff) return RES_PARERR;
            *(DWORD*)buff = 1;  // Erase block size in sectors (unknown, use 1)
            return RES_OK;
            
        case CTRL_TRIM:
            // Trim not supported yet
            return RES_OK;
            
        default:
            return RES_PARERR;
    }
}

/**
 * Get current time for FatFS timestamps
 */
DWORD get_fattime(void) {
    // Returns a packed timestamp:
    // bit31:25 = Year from 1980 (0..127)
    // bit24:21 = Month (1..12)
    // bit20:16 = Day (1..31)
    // bit15:11 = Hour (0..23)
    // bit10:5  = Minute (0..59)
    // bit4:0   = Second/2 (0..29)
    
    // For now, return a fixed time (2024-01-01 00:00:00)
    // TODO: Get actual RTC time if available
    return ((DWORD)(2024 - 1980) << 25) |   // Year 2024
           ((DWORD)1 << 21) |                // January
           ((DWORD)1 << 16) |                // 1st
           ((DWORD)0 << 11) |                // 00 hours
           ((DWORD)0 << 5) |                 // 00 minutes
           ((DWORD)0 << 0);                  // 00 seconds
}

// ============================================================================
// ADDITIONAL PICOMIMI-SPECIFIC FUNCTIONS
// ============================================================================

/**
 * Get direct access to SD card state (for advanced operations)
 */
sd_state_t* diskio_get_sd_state(void) {
    return g_sd_initialized ? &g_sd_state : NULL;
}

/**
 * Check if disk is initialized
 */
bool diskio_is_ready(void) {
    return g_sd_initialized && sd_is_ready(&g_sd_state);
}

/**
 * Get disk capacity in bytes
 */
uint64_t diskio_get_capacity(void) {
    if (!g_sd_initialized) return 0;
    return sd_get_capacity(&g_sd_state);
}

/**
 * Reinitialize the disk (after power cycle, etc.)
 */
int diskio_reinit(void) {
    if (g_sd_initialized) {
        sd_deinit(&g_sd_state);
        g_sd_initialized = false;
    }
    
    DSTATUS status = disk_initialize(0);
    return (status == 0) ? 0 : -1;
}

/**
 * Get SD card type string
 */
const char* diskio_get_card_type_string(void) {
    if (!g_sd_initialized) return "Not initialized";
    
    const sd_card_info_t* info = sd_get_info(&g_sd_state);
    switch (info->type) {
        case SD_TYPE_MMC:   return "MMC";
        case SD_TYPE_SD1:   return "SD v1.x";
        case SD_TYPE_SD2:   return "SD v2.x";
        case SD_TYPE_SDHC:  return "SDHC/SDXC";
        default:            return "Unknown";
    }
}
