/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC FAT32 - Minimal FAT32 Implementation                               ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Raw SPI + FAT32 - no Arduino SD library bloat                            ║
 * ║  Designed for disk-buffered compilation workflow                          ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include <string.h>
#include <stdio.h>

#include "mimic.h"
#include "mimic_fat32.h"

// ============================================================================
// SPI CONFIGURATION
// ============================================================================

#define SD_SPI          spi0
#define SD_BAUDRATE_SLOW    250000      // 250 KHz for init
#define SD_BAUDRATE_FAST    25000000    // 25 MHz normal operation

// ============================================================================
// SD CARD STATE
// ============================================================================

static MimicVolume vol;
static MimicFile files[MIMIC_MAX_FILES];
static char current_dir[MIMIC_MAX_PATH] = "/";

// ============================================================================
// LOW-LEVEL SPI FUNCTIONS
// ============================================================================

static inline void sd_cs_select(void) {
    gpio_put(MIMIC_SD_CS, 0);
    sleep_us(1);
}

static inline void sd_cs_deselect(void) {
    gpio_put(MIMIC_SD_CS, 1);
    sleep_us(1);
}

static uint8_t sd_spi_transfer(uint8_t data) {
    uint8_t rx;
    spi_write_read_blocking(SD_SPI, &data, &rx, 1);
    return rx;
}

static void sd_spi_write(const uint8_t* data, size_t len) {
    spi_write_blocking(SD_SPI, data, len);
}

static void sd_spi_read(uint8_t* data, size_t len) {
    uint8_t dummy = 0xFF;
    for (size_t i = 0; i < len; i++) {
        spi_write_read_blocking(SD_SPI, &dummy, &data[i], 1);
    }
}

// Wait for card ready (not busy)
static bool sd_wait_ready(uint32_t timeout_ms) {
    uint64_t start = time_us_64();
    uint64_t timeout_us = timeout_ms * 1000ULL;
    
    while ((time_us_64() - start) < timeout_us) {
        if (sd_spi_transfer(0xFF) == 0xFF) return true;
    }
    return false;
}

// ============================================================================
// SD CARD COMMANDS
// ============================================================================

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
    // Wait for ready
    if (!sd_wait_ready(500)) return 0xFF;
    
    // Send command
    uint8_t buf[6];
    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;
    
    // CRC (only needed for CMD0 and CMD8)
    if (cmd == SD_CMD0) buf[5] = 0x95;
    else if (cmd == SD_CMD8) buf[5] = 0x87;
    else buf[5] = 0x01;  // Dummy CRC
    
    sd_spi_write(buf, 6);
    
    // Skip a byte for stop read commands
    if (cmd == SD_CMD12) sd_spi_transfer(0xFF);
    
    // Wait for response (up to 8 bytes)
    uint8_t resp;
    for (int i = 0; i < 8; i++) {
        resp = sd_spi_transfer(0xFF);
        if ((resp & 0x80) == 0) break;
    }
    
    return resp;
}

static uint8_t sd_send_acmd(uint8_t cmd, uint32_t arg) {
    sd_send_cmd(SD_CMD55, 0);
    return sd_send_cmd(cmd, arg);
}

// ============================================================================
// SD CARD INITIALIZATION
// ============================================================================

int mimic_sd_init(void) {
    // Initialize SPI pins
    spi_init(SD_SPI, SD_BAUDRATE_SLOW);
    gpio_set_function(MIMIC_SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(MIMIC_SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MIMIC_SD_MISO, GPIO_FUNC_SPI);
    
    // CS as GPIO output, high (deselected)
    gpio_init(MIMIC_SD_CS);
    gpio_set_dir(MIMIC_SD_CS, GPIO_OUT);
    gpio_put(MIMIC_SD_CS, 1);
    
    // Wait and send clock pulses
    sleep_ms(10);
    for (int i = 0; i < 10; i++) {
        sd_spi_transfer(0xFF);
    }
    
    vol.card_type = SD_TYPE_UNKNOWN;
    vol.initialized = false;
    
    // CMD0 - Go idle
    sd_cs_select();
    uint8_t resp = sd_send_cmd(SD_CMD0, 0);
    sd_cs_deselect();
    
    if (resp != 0x01) {
        return MIMIC_ERR_IO;
    }
    
    // CMD8 - Check voltage (SDv2)
    sd_cs_select();
    resp = sd_send_cmd(SD_CMD8, 0x1AA);
    
    if (resp == 0x01) {
        // SDv2 card
        uint8_t ocr[4];
        sd_spi_read(ocr, 4);
        sd_cs_deselect();
        
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) {
            return MIMIC_ERR_IO;
        }
        
        // ACMD41 with HCS bit
        uint64_t start = time_us_64();
        while ((time_us_64() - start) < 1000000) {  // 1 second timeout
            sd_cs_select();
            resp = sd_send_acmd(SD_ACMD41, 0x40000000);
            sd_cs_deselect();
            if (resp == 0x00) break;
            sleep_ms(10);
        }
        
        if (resp != 0x00) {
            return MIMIC_ERR_IO;
        }
        
        // CMD58 - Read OCR to check CCS (Card Capacity Status)
        sd_cs_select();
        resp = sd_send_cmd(SD_CMD58, 0);
        if (resp == 0x00) {
            sd_spi_read(ocr, 4);
            vol.card_type = (ocr[0] & 0x40) ? SD_TYPE_SDHC : SD_TYPE_SD2;
        }
        sd_cs_deselect();
        
    } else {
        sd_cs_deselect();
        
        // SDv1 or MMC
        sd_cs_select();
        resp = sd_send_acmd(SD_ACMD41, 0);
        sd_cs_deselect();
        
        if (resp <= 1) {
            // SDv1
            vol.card_type = SD_TYPE_SD1;
            
            uint64_t start = time_us_64();
            while ((time_us_64() - start) < 1000000) {
                sd_cs_select();
                resp = sd_send_acmd(SD_ACMD41, 0);
                sd_cs_deselect();
                if (resp == 0x00) break;
                sleep_ms(10);
            }
        } else {
            // MMC
            vol.card_type = SD_TYPE_MMC;
            
            uint64_t start = time_us_64();
            while ((time_us_64() - start) < 1000000) {
                sd_cs_select();
                resp = sd_send_cmd(SD_CMD1, 0);
                sd_cs_deselect();
                if (resp == 0x00) break;
                sleep_ms(10);
            }
        }
        
        if (resp != 0x00) {
            return MIMIC_ERR_IO;
        }
        
        // Set block size to 512 for non-SDHC
        sd_cs_select();
        sd_send_cmd(SD_CMD16, 512);
        sd_cs_deselect();
    }
    
    // Switch to fast SPI
    spi_set_baudrate(SD_SPI, SD_BAUDRATE_FAST);
    
    vol.initialized = true;
    return MIMIC_OK;
}

bool mimic_sd_present(void) {
    return vol.initialized;
}

uint8_t mimic_sd_get_type(void) {
    return vol.card_type;
}

// ============================================================================
// SECTOR READ/WRITE
// ============================================================================

int mimic_sd_read_sector(uint32_t sector, uint8_t* buf) {
    if (!vol.initialized) return MIMIC_ERR_IO;
    
    // Convert to byte address for non-SDHC
    uint32_t addr = (vol.card_type == SD_TYPE_SDHC) ? sector : sector * 512;
    
    sd_cs_select();
    
    uint8_t resp = sd_send_cmd(SD_CMD17, addr);
    if (resp != 0x00) {
        sd_cs_deselect();
        return MIMIC_ERR_IO;
    }
    
    // Wait for data token
    uint64_t start = time_us_64();
    while ((time_us_64() - start) < 200000) {  // 200ms timeout
        resp = sd_spi_transfer(0xFF);
        if (resp == 0xFE) break;
        if ((resp & 0xF0) == 0x00) {
            sd_cs_deselect();
            return MIMIC_ERR_IO;  // Error token
        }
    }
    
    if (resp != 0xFE) {
        sd_cs_deselect();
        return MIMIC_ERR_IO;
    }
    
    // Read data
    sd_spi_read(buf, 512);
    
    // Read and discard CRC
    sd_spi_transfer(0xFF);
    sd_spi_transfer(0xFF);
    
    sd_cs_deselect();
    return MIMIC_OK;
}

int mimic_sd_write_sector(uint32_t sector, const uint8_t* buf) {
    if (!vol.initialized) return MIMIC_ERR_IO;
    
    uint32_t addr = (vol.card_type == SD_TYPE_SDHC) ? sector : sector * 512;
    
    sd_cs_select();
    
    uint8_t resp = sd_send_cmd(SD_CMD24, addr);
    if (resp != 0x00) {
        sd_cs_deselect();
        return MIMIC_ERR_IO;
    }
    
    // Send data token
    sd_spi_transfer(0xFF);
    sd_spi_transfer(0xFE);
    
    // Write data
    sd_spi_write(buf, 512);
    
    // Dummy CRC
    sd_spi_transfer(0xFF);
    sd_spi_transfer(0xFF);
    
    // Check response
    resp = sd_spi_transfer(0xFF);
    if ((resp & 0x1F) != 0x05) {
        sd_cs_deselect();
        return MIMIC_ERR_IO;
    }
    
    // Wait for write to complete
    if (!sd_wait_ready(500)) {
        sd_cs_deselect();
        return MIMIC_ERR_IO;
    }
    
    sd_cs_deselect();
    return MIMIC_OK;
}

// ============================================================================
// FAT32 MOUNTING
// ============================================================================

// Read a sector with caching
static int fat32_read_sector(uint32_t sector) {
    if (vol.cached_sector == sector) return MIMIC_OK;
    
    // Flush dirty cache first
    if (vol.cache_dirty) {
        int err = mimic_sd_write_sector(vol.cached_sector, vol.sector_buf);
        if (err != MIMIC_OK) return err;
        vol.cache_dirty = false;
    }
    
    int err = mimic_sd_read_sector(sector, vol.sector_buf);
    if (err == MIMIC_OK) {
        vol.cached_sector = sector;
    }
    return err;
}

// Mark cache as dirty (needs write)
static void fat32_cache_dirty(void) {
    vol.cache_dirty = true;
}

// Flush cache
static int fat32_flush_cache(void) {
    if (vol.cache_dirty) {
        int err = mimic_sd_write_sector(vol.cached_sector, vol.sector_buf);
        if (err != MIMIC_OK) return err;
        vol.cache_dirty = false;
    }
    return MIMIC_OK;
}

int mimic_fat32_mount(void) {
    // Initialize SD card
    int err = mimic_sd_init();
    if (err != MIMIC_OK) return err;
    
    vol.cached_sector = 0xFFFFFFFF;
    vol.cache_dirty = false;
    
    // Read MBR / boot sector
    err = fat32_read_sector(0);
    if (err != MIMIC_OK) return err;
    
    // Check for MBR (partition table)
    uint32_t part_start = 0;
    if (vol.sector_buf[0x1FE] == 0x55 && vol.sector_buf[0x1FF] == 0xAA) {
        // Check first partition entry
        if (vol.sector_buf[0x1C2] == 0x0B || vol.sector_buf[0x1C2] == 0x0C) {
            // FAT32 partition
            part_start = *(uint32_t*)&vol.sector_buf[0x1C6];
        }
    }
    
    // Read boot sector
    err = fat32_read_sector(part_start);
    if (err != MIMIC_OK) return err;
    
    // Parse BPB
    Fat32BPB* bpb = (Fat32BPB*)vol.sector_buf;
    
    // Verify FAT32 signature
    if (bpb->bytes_per_sector != 512) return MIMIC_ERR_CORRUPT;
    if (bpb->root_entry_count != 0) return MIMIC_ERR_CORRUPT;  // Must be 0 for FAT32
    if (bpb->fat_size_16 != 0) return MIMIC_ERR_CORRUPT;       // Must be 0 for FAT32
    
    // Store geometry
    vol.sectors_per_cluster = bpb->sectors_per_cluster;
    vol.bytes_per_cluster = vol.sectors_per_cluster * 512;
    vol.fat_start_sector = part_start + bpb->reserved_sectors;
    vol.fat_sectors = bpb->fat_size_32;
    vol.root_cluster = bpb->root_cluster;
    vol.data_start_sector = vol.fat_start_sector + (bpb->num_fats * vol.fat_sectors);
    
    uint32_t total_sectors = bpb->total_sectors_32;
    uint32_t data_sectors = total_sectors - (bpb->reserved_sectors + bpb->num_fats * vol.fat_sectors);
    vol.total_clusters = data_sectors / vol.sectors_per_cluster;
    
    // Initialize file handles
    memset(files, 0, sizeof(files));
    strcpy(current_dir, "/");
    
    return MIMIC_OK;
}

void mimic_fat32_unmount(void) {
    fat32_flush_cache();
    
    // Close all files
    for (int i = 0; i < MIMIC_MAX_FILES; i++) {
        if (files[i].open) {
            mimic_fclose(i);
        }
    }
    
    vol.initialized = false;
}

bool mimic_fat32_mounted(void) {
    return vol.initialized;
}

// ============================================================================
// FAT TABLE OPERATIONS
// ============================================================================

// Get cluster number from FAT
static uint32_t fat32_get_fat_entry(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;
    
    if (fat32_read_sector(fat_sector) != MIMIC_OK) {
        return 0xFFFFFFFF;
    }
    
    return (*(uint32_t*)&vol.sector_buf[entry_offset]) & 0x0FFFFFFF;
}

// Set cluster number in FAT
static int fat32_set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;
    
    int err = fat32_read_sector(fat_sector);
    if (err != MIMIC_OK) return err;
    
    *(uint32_t*)&vol.sector_buf[entry_offset] = (value & 0x0FFFFFFF) | 
        (*(uint32_t*)&vol.sector_buf[entry_offset] & 0xF0000000);
    
    fat32_cache_dirty();
    return fat32_flush_cache();
}

// Convert cluster to sector
static uint32_t fat32_cluster_to_sector(uint32_t cluster) {
    return vol.data_start_sector + ((cluster - 2) * vol.sectors_per_cluster);
}

// Allocate a new cluster
static uint32_t fat32_alloc_cluster(void) {
    for (uint32_t cluster = 2; cluster < vol.total_clusters + 2; cluster++) {
        if (fat32_get_fat_entry(cluster) == FAT32_FREE) {
            // Mark as end of chain
            fat32_set_fat_entry(cluster, FAT32_EOC);
            return cluster;
        }
    }
    return 0;  // No free clusters
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

// Convert 8.3 name to normal string
static void fat32_name_to_str(const Fat32DirEntry* entry, char* str) {
    int i, j = 0;
    
    // Copy name (trim trailing spaces)
    for (i = 0; i < 8 && entry->name[i] != ' '; i++) {
        str[j++] = entry->name[i];
    }
    
    // Add extension if present
    if (entry->ext[0] != ' ') {
        str[j++] = '.';
        for (i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            str[j++] = entry->ext[i];
        }
    }
    
    str[j] = '\0';
}

// Convert string to 8.3 name
static void fat32_str_to_name(const char* str, char* name, char* ext) {
    memset(name, ' ', 8);
    memset(ext, ' ', 3);
    
    int i = 0, j = 0;
    
    // Copy until dot or end
    while (str[i] && str[i] != '.' && j < 8) {
        name[j++] = (str[i] >= 'a' && str[i] <= 'z') ? str[i] - 32 : str[i];
        i++;
    }
    
    // Skip dot
    if (str[i] == '.') i++;
    
    // Copy extension
    j = 0;
    while (str[i] && j < 3) {
        ext[j++] = (str[i] >= 'a' && str[i] <= 'z') ? str[i] - 32 : str[i];
        i++;
    }
}

// Find entry in directory
static int fat32_find_entry(uint32_t dir_cluster, const char* name, 
                            Fat32DirEntry* entry, uint32_t* entry_cluster,
                            uint32_t* entry_idx) {
    char search_name[8], search_ext[3];
    fat32_str_to_name(name, search_name, search_ext);
    
    uint32_t cluster = dir_cluster;
    
    while (cluster != 0 && cluster < FAT32_EOC) {
        uint32_t sector = fat32_cluster_to_sector(cluster);
        
        for (uint32_t s = 0; s < vol.sectors_per_cluster; s++) {
            if (fat32_read_sector(sector + s) != MIMIC_OK) {
                return MIMIC_ERR_IO;
            }
            
            Fat32DirEntry* entries = (Fat32DirEntry*)vol.sector_buf;
            
            for (int i = 0; i < 16; i++) {  // 16 entries per sector
                if (entries[i].name[0] == 0x00) {
                    // End of directory
                    return MIMIC_ERR_NOENT;
                }
                
                if (entries[i].name[0] == 0xE5) {
                    // Deleted entry
                    continue;
                }
                
                if (entries[i].attr == FAT_ATTR_LFN) {
                    // Long filename (skip for now)
                    continue;
                }
                
                // Compare name
                if (memcmp(entries[i].name, search_name, 8) == 0 &&
                    memcmp(entries[i].ext, search_ext, 3) == 0) {
                    *entry = entries[i];
                    if (entry_cluster) *entry_cluster = cluster;
                    if (entry_idx) *entry_idx = s * 16 + i;
                    return MIMIC_OK;
                }
            }
        }
        
        // Next cluster in chain
        cluster = fat32_get_fat_entry(cluster);
    }
    
    return MIMIC_ERR_NOENT;
}

// Parse path and find file/dir
static int fat32_resolve_path(const char* path, uint32_t* cluster, 
                               Fat32DirEntry* entry) {
    // Start from root or current directory
    uint32_t curr_cluster;
    if (path[0] == '/') {
        curr_cluster = vol.root_cluster;
        path++;
    } else {
        // TODO: resolve current_dir
        curr_cluster = vol.root_cluster;
    }
    
    char component[256];
    
    while (*path) {
        // Skip leading slashes
        while (*path == '/') path++;
        if (!*path) break;
        
        // Extract component
        int i = 0;
        while (*path && *path != '/' && i < 255) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        
        // Find in directory
        Fat32DirEntry found;
        int err = fat32_find_entry(curr_cluster, component, &found, NULL, NULL);
        if (err != MIMIC_OK) return err;
        
        // Get cluster
        curr_cluster = ((uint32_t)found.fst_clus_hi << 16) | found.fst_clus_lo;
        
        if (entry) *entry = found;
    }
    
    if (cluster) *cluster = curr_cluster;
    return MIMIC_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int mimic_fopen(const char* path, uint8_t mode) {
    // Find free file slot
    int fd = -1;
    for (int i = 0; i < MIMIC_MAX_FILES; i++) {
        if (!files[i].open) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return MIMIC_ERR_NOMEM;
    
    MimicFile* f = &files[fd];
    memset(f, 0, sizeof(MimicFile));
    
    // Resolve path
    Fat32DirEntry entry;
    int err = fat32_resolve_path(path, NULL, &entry);
    
    if (err == MIMIC_ERR_NOENT && (mode & MIMIC_FILE_CREATE)) {
        // TODO: Create new file
        return MIMIC_ERR_NOSYS;
    }
    
    if (err != MIMIC_OK) return err;
    
    // Check if directory
    if (entry.attr & FAT_ATTR_DIRECTORY) {
        return MIMIC_ERR_INVAL;  // Can't open directory as file
    }
    
    // Setup file handle
    f->open = true;
    f->mode = mode;
    f->first_cluster = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    f->current_cluster = f->first_cluster;
    f->cluster_offset = 0;
    f->file_size = entry.file_size;
    f->position = 0;
    strncpy(f->path, path, MIMIC_MAX_PATH - 1);
    
    // Handle append mode
    if (mode & MIMIC_FILE_APPEND) {
        mimic_fseek(fd, 0, MIMIC_SEEK_END);
    }
    
    // Handle truncate
    if (mode & MIMIC_FILE_TRUNC) {
        // TODO: Implement truncate
    }
    
    return fd;
}

int mimic_fclose(int fd) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return MIMIC_ERR_INVAL;
    if (!files[fd].open) return MIMIC_ERR_INVAL;
    
    fat32_flush_cache();
    files[fd].open = false;
    return MIMIC_OK;
}

int mimic_fread(int fd, void* buf, size_t size) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return MIMIC_ERR_INVAL;
    MimicFile* f = &files[fd];
    if (!f->open) return MIMIC_ERR_INVAL;
    if (!(f->mode & MIMIC_FILE_READ)) return MIMIC_ERR_PERM;
    
    uint8_t* out = (uint8_t*)buf;
    size_t bytes_read = 0;
    
    while (bytes_read < size && f->position < f->file_size) {
        // Check for end of cluster chain
        if (f->current_cluster == 0 || f->current_cluster >= FAT32_EOC) {
            break;
        }
        
        // Read current sector
        uint32_t sector_in_cluster = f->cluster_offset / 512;
        uint32_t offset_in_sector = f->cluster_offset % 512;
        uint32_t sector = fat32_cluster_to_sector(f->current_cluster) + sector_in_cluster;
        
        if (fat32_read_sector(sector) != MIMIC_OK) {
            return bytes_read > 0 ? (int)bytes_read : MIMIC_ERR_IO;
        }
        
        // Copy data
        uint32_t bytes_in_sector = 512 - offset_in_sector;
        uint32_t bytes_in_file = f->file_size - f->position;
        uint32_t bytes_to_copy = size - bytes_read;
        
        if (bytes_to_copy > bytes_in_sector) bytes_to_copy = bytes_in_sector;
        if (bytes_to_copy > bytes_in_file) bytes_to_copy = bytes_in_file;
        
        memcpy(out + bytes_read, vol.sector_buf + offset_in_sector, bytes_to_copy);
        
        bytes_read += bytes_to_copy;
        f->position += bytes_to_copy;
        f->cluster_offset += bytes_to_copy;
        
        // Move to next cluster if needed
        if (f->cluster_offset >= vol.bytes_per_cluster) {
            f->current_cluster = fat32_get_fat_entry(f->current_cluster);
            f->cluster_offset = 0;
        }
    }
    
    return (int)bytes_read;
}

int mimic_fwrite(int fd, const void* buf, size_t size) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return MIMIC_ERR_INVAL;
    MimicFile* f = &files[fd];
    if (!f->open) return MIMIC_ERR_INVAL;
    if (!(f->mode & MIMIC_FILE_WRITE)) return MIMIC_ERR_PERM;
    
    const uint8_t* in = (const uint8_t*)buf;
    size_t bytes_written = 0;
    
    while (bytes_written < size) {
        // Allocate cluster if needed
        if (f->current_cluster == 0 || f->current_cluster >= FAT32_EOC) {
            uint32_t new_cluster = fat32_alloc_cluster();
            if (new_cluster == 0) {
                return bytes_written > 0 ? (int)bytes_written : MIMIC_ERR_NOMEM;
            }
            
            if (f->first_cluster == 0) {
                f->first_cluster = new_cluster;
                // TODO: Update directory entry
            } else {
                // Link to previous cluster
                // (find last cluster and link)
            }
            
            f->current_cluster = new_cluster;
            f->cluster_offset = 0;
        }
        
        // Calculate write position
        uint32_t sector_in_cluster = f->cluster_offset / 512;
        uint32_t offset_in_sector = f->cluster_offset % 512;
        uint32_t sector = fat32_cluster_to_sector(f->current_cluster) + sector_in_cluster;
        
        // Read sector if partial write
        if (offset_in_sector != 0 || (size - bytes_written) < 512) {
            if (fat32_read_sector(sector) != MIMIC_OK) {
                return bytes_written > 0 ? (int)bytes_written : MIMIC_ERR_IO;
            }
        }
        
        // Copy data
        uint32_t bytes_in_sector = 512 - offset_in_sector;
        uint32_t bytes_to_copy = size - bytes_written;
        if (bytes_to_copy > bytes_in_sector) bytes_to_copy = bytes_in_sector;
        
        memcpy(vol.sector_buf + offset_in_sector, in + bytes_written, bytes_to_copy);
        fat32_cache_dirty();
        
        bytes_written += bytes_to_copy;
        f->position += bytes_to_copy;
        f->cluster_offset += bytes_to_copy;
        
        // Update file size
        if (f->position > f->file_size) {
            f->file_size = f->position;
            // TODO: Update directory entry
        }
        
        // Move to next cluster if needed
        if (f->cluster_offset >= vol.bytes_per_cluster) {
            uint32_t next = fat32_get_fat_entry(f->current_cluster);
            if (next >= FAT32_EOC) {
                // Need to allocate next cluster
                next = fat32_alloc_cluster();
                if (next == 0) {
                    fat32_flush_cache();
                    return (int)bytes_written;
                }
                fat32_set_fat_entry(f->current_cluster, next);
            }
            f->current_cluster = next;
            f->cluster_offset = 0;
        }
    }
    
    fat32_flush_cache();
    return (int)bytes_written;
}

int mimic_fseek(int fd, int32_t offset, int whence) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return MIMIC_ERR_INVAL;
    MimicFile* f = &files[fd];
    if (!f->open) return MIMIC_ERR_INVAL;
    
    int32_t new_pos;
    switch (whence) {
        case MIMIC_SEEK_SET:
            new_pos = offset;
            break;
        case MIMIC_SEEK_CUR:
            new_pos = f->position + offset;
            break;
        case MIMIC_SEEK_END:
            new_pos = f->file_size + offset;
            break;
        default:
            return MIMIC_ERR_INVAL;
    }
    
    if (new_pos < 0) new_pos = 0;
    if (new_pos > (int32_t)f->file_size) new_pos = f->file_size;
    
    // Recalculate cluster position
    f->position = new_pos;
    f->current_cluster = f->first_cluster;
    f->cluster_offset = 0;
    
    uint32_t clusters_to_skip = new_pos / vol.bytes_per_cluster;
    for (uint32_t i = 0; i < clusters_to_skip && f->current_cluster < FAT32_EOC; i++) {
        f->current_cluster = fat32_get_fat_entry(f->current_cluster);
    }
    f->cluster_offset = new_pos % vol.bytes_per_cluster;
    
    return MIMIC_OK;
}

int32_t mimic_ftell(int fd) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return MIMIC_ERR_INVAL;
    if (!files[fd].open) return MIMIC_ERR_INVAL;
    return files[fd].position;
}

int32_t mimic_fsize(int fd) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return MIMIC_ERR_INVAL;
    if (!files[fd].open) return MIMIC_ERR_INVAL;
    return files[fd].file_size;
}

bool mimic_feof(int fd) {
    if (fd < 0 || fd >= MIMIC_MAX_FILES) return true;
    if (!files[fd].open) return true;
    return files[fd].position >= files[fd].file_size;
}

int mimic_fflush(int fd) {
    (void)fd;
    return fat32_flush_cache();
}

// ============================================================================
// FILE UTILITIES
// ============================================================================

bool mimic_exists(const char* path) {
    Fat32DirEntry entry;
    return fat32_resolve_path(path, NULL, &entry) == MIMIC_OK;
}

bool mimic_is_dir(const char* path) {
    Fat32DirEntry entry;
    if (fat32_resolve_path(path, NULL, &entry) != MIMIC_OK) return false;
    return (entry.attr & FAT_ATTR_DIRECTORY) != 0;
}

// ============================================================================
// STREAMING I/O FOR COMPILER
// ============================================================================

int mimic_stream_open(MimicStream* stream, const char* path, uint8_t mode,
                       uint8_t* buffer, uint32_t buf_size) {
    stream->fd = mimic_fopen(path, mode);
    if (stream->fd < 0) return stream->fd;
    
    stream->buffer = buffer;
    stream->buf_size = buf_size;
    stream->buf_pos = 0;
    stream->buf_len = 0;
    stream->eof = false;
    
    return MIMIC_OK;
}

int mimic_stream_close(MimicStream* stream) {
    if (stream->fd < 0) return MIMIC_ERR_INVAL;
    
    // Flush write buffer
    if (stream->buf_pos > 0 && (files[stream->fd].mode & MIMIC_FILE_WRITE)) {
        mimic_fwrite(stream->fd, stream->buffer, stream->buf_pos);
    }
    
    int err = mimic_fclose(stream->fd);
    stream->fd = -1;
    return err;
}

int mimic_stream_getc(MimicStream* stream) {
    if (stream->buf_pos >= stream->buf_len) {
        // Refill buffer
        int n = mimic_fread(stream->fd, stream->buffer, stream->buf_size);
        if (n <= 0) {
            stream->eof = true;
            return -1;
        }
        stream->buf_len = n;
        stream->buf_pos = 0;
    }
    
    return stream->buffer[stream->buf_pos++];
}

int mimic_stream_putc(MimicStream* stream, int c) {
    if (stream->buf_pos >= stream->buf_size) {
        // Flush buffer
        int n = mimic_fwrite(stream->fd, stream->buffer, stream->buf_pos);
        if (n < (int)stream->buf_pos) return MIMIC_ERR_IO;
        stream->buf_pos = 0;
    }
    
    stream->buffer[stream->buf_pos++] = (uint8_t)c;
    return c;
}

bool mimic_stream_eof(MimicStream* stream) {
    return stream->eof;
}

int mimic_stream_flush(MimicStream* stream) {
    if (stream->buf_pos > 0 && (files[stream->fd].mode & MIMIC_FILE_WRITE)) {
        int n = mimic_fwrite(stream->fd, stream->buffer, stream->buf_pos);
        if (n < (int)stream->buf_pos) return MIMIC_ERR_IO;
        stream->buf_pos = 0;
    }
    return mimic_fflush(stream->fd);
}
