/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC FAT32 - Minimal FAT32 Implementation for SD Cards                  ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  No Arduino bloat - raw SPI + FAT32 from scratch                          ║
 * ║  Designed for disk-buffered compilation workflow                          ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef MIMIC_FAT32_H
#define MIMIC_FAT32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// SD CARD PHYSICAL LAYER
// ============================================================================

// SD Card SPI pins (configurable)
#ifndef MIMIC_SD_CS
  #define MIMIC_SD_CS       5
#endif
#ifndef MIMIC_SD_MOSI
  #define MIMIC_SD_MOSI     19
#endif
#ifndef MIMIC_SD_MISO
  #define MIMIC_SD_MISO     16
#endif
#ifndef MIMIC_SD_SCK
  #define MIMIC_SD_SCK      18
#endif

// SD Card commands
#define SD_CMD0             0       // GO_IDLE_STATE
#define SD_CMD1             1       // SEND_OP_COND (MMC)
#define SD_CMD8             8       // SEND_IF_COND (SDC V2)
#define SD_CMD9             9       // SEND_CSD
#define SD_CMD10            10      // SEND_CID
#define SD_CMD12            12      // STOP_TRANSMISSION
#define SD_CMD16            16      // SET_BLOCKLEN
#define SD_CMD17            17      // READ_SINGLE_BLOCK
#define SD_CMD18            18      // READ_MULTIPLE_BLOCK
#define SD_CMD24            24      // WRITE_BLOCK
#define SD_CMD25            25      // WRITE_MULTIPLE_BLOCK
#define SD_CMD55            55      // APP_CMD
#define SD_CMD58            58      // READ_OCR
#define SD_ACMD41           41      // SD_SEND_OP_COND

// SD Card types
#define SD_TYPE_UNKNOWN     0
#define SD_TYPE_MMC         1       // MMC
#define SD_TYPE_SD1         2       // SD V1
#define SD_TYPE_SD2         3       // SD V2
#define SD_TYPE_SDHC        4       // SDHC/SDXC

// Sector size
#define SD_SECTOR_SIZE      512

// ============================================================================
// FAT32 STRUCTURES
// ============================================================================

// Boot sector (BPB - BIOS Parameter Block)
typedef struct __attribute__((packed)) {
    uint8_t     jump_boot[3];
    char        oem_name[8];
    uint16_t    bytes_per_sector;
    uint8_t     sectors_per_cluster;
    uint16_t    reserved_sectors;
    uint8_t     num_fats;
    uint16_t    root_entry_count;       // 0 for FAT32
    uint16_t    total_sectors_16;       // 0 for FAT32
    uint8_t     media_type;
    uint16_t    fat_size_16;            // 0 for FAT32
    uint16_t    sectors_per_track;
    uint16_t    num_heads;
    uint32_t    hidden_sectors;
    uint32_t    total_sectors_32;
    
    // FAT32 specific
    uint32_t    fat_size_32;
    uint16_t    ext_flags;
    uint16_t    fs_version;
    uint32_t    root_cluster;
    uint16_t    fs_info;
    uint16_t    backup_boot_sector;
    uint8_t     reserved[12];
    uint8_t     drive_number;
    uint8_t     reserved1;
    uint8_t     boot_sig;
    uint32_t    volume_id;
    char        volume_label[11];
    char        fs_type[8];
} Fat32BPB;

// Directory entry (32 bytes)
typedef struct __attribute__((packed)) {
    char        name[8];                // Short filename
    char        ext[3];                 // Extension
    uint8_t     attr;                   // Attributes
    uint8_t     nt_res;
    uint8_t     crt_time_tenth;
    uint16_t    crt_time;
    uint16_t    crt_date;
    uint16_t    lst_acc_date;
    uint16_t    fst_clus_hi;            // High word of cluster
    uint16_t    wrt_time;
    uint16_t    wrt_date;
    uint16_t    fst_clus_lo;            // Low word of cluster
    uint32_t    file_size;
} Fat32DirEntry;

// Long filename entry (32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t     order;
    uint16_t    name1[5];               // Characters 1-5
    uint8_t     attr;                   // Always 0x0F
    uint8_t     type;
    uint8_t     checksum;
    uint16_t    name2[6];               // Characters 6-11
    uint16_t    fst_clus_lo;            // Always 0
    uint16_t    name3[2];               // Characters 12-13
} Fat32LfnEntry;

// File attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F        // Long filename marker

// Special cluster values
#define FAT32_EOC           0x0FFFFFF8  // End of chain
#define FAT32_FREE          0x00000000  // Free cluster
#define FAT32_BAD           0x0FFFFFF7  // Bad cluster

// ============================================================================
// FILE SYSTEM STATE
// ============================================================================

// Volume info
typedef struct {
    // SD card
    uint8_t     card_type;
    bool        initialized;
    
    // FAT32 geometry
    uint32_t    sectors_per_cluster;
    uint32_t    bytes_per_cluster;
    uint32_t    fat_start_sector;       // First FAT sector
    uint32_t    fat_sectors;            // Sectors per FAT
    uint32_t    root_cluster;           // Root directory cluster
    uint32_t    data_start_sector;      // First data sector
    uint32_t    total_clusters;
    
    // Cached sector
    uint32_t    cached_sector;
    uint8_t     sector_buf[SD_SECTOR_SIZE];
    bool        cache_dirty;
} MimicVolume;

// Open file handle
#define MIMIC_MAX_FILES     8
#define MIMIC_MAX_PATH      256

typedef struct {
    bool        open;
    bool        is_dir;
    uint8_t     mode;                   // Read/Write/Append
    uint32_t    dir_cluster;            // Directory containing this file
    uint32_t    dir_entry_idx;          // Index in directory
    uint32_t    first_cluster;          // First cluster of file
    uint32_t    current_cluster;
    uint32_t    cluster_offset;         // Byte offset within cluster
    uint32_t    file_size;
    uint32_t    position;               // Current position
    char        path[MIMIC_MAX_PATH];
} MimicFile;

// File open modes
#define MIMIC_FILE_READ     0x01
#define MIMIC_FILE_WRITE    0x02
#define MIMIC_FILE_APPEND   0x04
#define MIMIC_FILE_CREATE   0x08
#define MIMIC_FILE_TRUNC    0x10

// Seek modes
#define MIMIC_SEEK_SET      0
#define MIMIC_SEEK_CUR      1
#define MIMIC_SEEK_END      2

// ============================================================================
// SD CARD LOW-LEVEL API
// ============================================================================

// Initialize SD card
int mimic_sd_init(void);

// Check if card is present
bool mimic_sd_present(void);

// Read a sector
int mimic_sd_read_sector(uint32_t sector, uint8_t* buf);

// Write a sector
int mimic_sd_write_sector(uint32_t sector, const uint8_t* buf);

// Read multiple sectors
int mimic_sd_read_sectors(uint32_t sector, uint8_t* buf, uint32_t count);

// Write multiple sectors
int mimic_sd_write_sectors(uint32_t sector, const uint8_t* buf, uint32_t count);

// Get card info
uint8_t mimic_sd_get_type(void);
uint64_t mimic_sd_get_size(void);

// ============================================================================
// FAT32 API
// ============================================================================

// Mount/unmount
int mimic_fat32_mount(void);
void mimic_fat32_unmount(void);
bool mimic_fat32_mounted(void);

// File operations
int mimic_fopen(const char* path, uint8_t mode);         // Returns fd or error
int mimic_fclose(int fd);
int mimic_fread(int fd, void* buf, size_t size);
int mimic_fwrite(int fd, const void* buf, size_t size);
int mimic_fseek(int fd, int32_t offset, int whence);
int32_t mimic_ftell(int fd);
int mimic_fflush(int fd);
int32_t mimic_fsize(int fd);
bool mimic_feof(int fd);

// Directory operations
int mimic_mkdir(const char* path);
int mimic_rmdir(const char* path);
int mimic_chdir(const char* path);
int mimic_getcwd(char* buf, size_t size);

// File management
int mimic_unlink(const char* path);                      // Delete file
int mimic_rename(const char* old_path, const char* new_path);
int mimic_stat(const char* path, uint32_t* size, uint8_t* attr);
bool mimic_exists(const char* path);
bool mimic_is_dir(const char* path);

// Directory listing
typedef struct {
    char        name[256];
    uint32_t    size;
    uint8_t     attr;
    bool        is_dir;
} MimicDirEntry;

int mimic_opendir(const char* path);                     // Returns dir handle
int mimic_readdir(int dir_handle, MimicDirEntry* entry);
int mimic_closedir(int dir_handle);

// Convenience
int mimic_read_file(const char* path, void* buf, size_t max_size);
int mimic_write_file(const char* path, const void* buf, size_t size);
int mimic_append_file(const char* path, const void* buf, size_t size);

// ============================================================================
// FILESYSTEM INFO
// ============================================================================

typedef struct {
    uint64_t    total_bytes;
    uint64_t    free_bytes;
    uint64_t    used_bytes;
    uint32_t    total_clusters;
    uint32_t    free_clusters;
    uint32_t    cluster_size;
    uint32_t    sector_size;
} MimicFSInfo;

int mimic_fs_info(MimicFSInfo* info);

// ============================================================================
// STREAMING I/O FOR COMPILER
// ============================================================================
// Special functions optimized for disk-buffered compilation

// Stream interface (for large files)
typedef struct {
    int         fd;
    uint8_t*    buffer;
    uint32_t    buf_size;
    uint32_t    buf_pos;
    uint32_t    buf_len;
    bool        eof;
} MimicStream;

int mimic_stream_open(MimicStream* stream, const char* path, uint8_t mode, 
                      uint8_t* buffer, uint32_t buf_size);
int mimic_stream_close(MimicStream* stream);
int mimic_stream_getc(MimicStream* stream);
int mimic_stream_ungetc(MimicStream* stream, int c);
int mimic_stream_putc(MimicStream* stream, int c);
int mimic_stream_puts(MimicStream* stream, const char* s);
int mimic_stream_read(MimicStream* stream, void* buf, size_t size);
int mimic_stream_write(MimicStream* stream, const void* buf, size_t size);
int mimic_stream_flush(MimicStream* stream);
bool mimic_stream_eof(MimicStream* stream);

#endif // MIMIC_FAT32_H
