/**
 * PICOMIMI FileSystem (PMFS) v3.0
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - Journaling filesystem with crash recovery
 * - tmpfs (RAM disk) for fast temp storage
 * - A/B bank system for OTA updates
 * - Write caching
 * - File locking
 * - Comprehensive statistics
 */
#ifndef PMFS_H
#define PMFS_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"
#include "ff.h"  // FatFS
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION                "3.0"
#define PMFS_MAGIC                  0x504D4653  // "PMFS"

// Paths
#define PMFS_ROOT_DIR               "/PICOMIMI"
#define PMFS_SYSTEM_A_DIR           "/PICOMIMI/SYSTEM_A"
#define PMFS_SYSTEM_B_DIR           "/PICOMIMI/SYSTEM_B"
#define PMFS_TMPFS_DIR              "/PICOMIMI/TMP"
#define PMFS_LOG_DIR                "/PICOMIMI/LOG"
#define PMFS_SYSLOG_DIR             "/PICOMIMI/LOG/SYSTEM"
#define PMFS_USERLOG_DIR            "/PICOMIMI/LOG/USER"
#define PMFS_DATA_DIR               "/PICOMIMI/DATA"
#define PMFS_CONFIG_DIR             "/PICOMIMI/CONFIG"
#define PMFS_CACHE_DIR              "/PICOMIMI/CACHE"
#define PMFS_JOURNAL_DIR            "/PICOMIMI/JOURNAL"

#define PMFS_METADATA_FILE          "/PICOMIMI/metadata.bin"
#define PMFS_BOOTFLAG_FILE          "/PICOMIMI/BOOTFLAG"
#define PMFS_JOURNAL_FILE           "/PICOMIMI/JOURNAL/journal.bin"

// Limits
#define PMFS_MAX_OPEN_FILES         PICOMIMI_PMFS_MAX_OPEN_FILES
#define PMFS_MAX_PATH_LENGTH        PICOMIMI_PMFS_MAX_PATH_LENGTH
#define PMFS_MAX_FILENAME           PICOMIMI_PMFS_MAX_FILENAME
#define PMFS_JOURNAL_ENTRIES        16
#define PMFS_MAX_LOCKS              8
#define PMFS_MAX_TMPFS_ENTRIES      16
#define PMFS_TMPFS_SIZE             PICOMIMI_TMPFS_SIZE
#define PMFS_WRITE_CACHE_SIZE       PICOMIMI_PMFS_WRITE_CACHE

// Features
#define PMFS_ENABLE_JOURNALING      1
#define PMFS_ENABLE_WRITE_CACHE     1
#define PMFS_ENABLE_TMPFS           (PMFS_TMPFS_SIZE > 0)

// ============================================================================
// PMFS STATUS CODES
// ============================================================================

typedef enum {
    PMFS_OK = 0,
    PMFS_ERROR_NOT_INITIALIZED,
    PMFS_ERROR_SD_NOT_FOUND,
    PMFS_ERROR_NO_ROOT,
    PMFS_ERROR_CORRUPT,
    PMFS_ERROR_NO_SPACE,
    PMFS_ERROR_FILE_NOT_FOUND,
    PMFS_ERROR_ACCESS_DENIED,
    PMFS_ERROR_FILE_LOCKED,
    PMFS_ERROR_INVALID_PARAM,
    PMFS_ERROR_IO_FAILURE,
    PMFS_ERROR_JOURNAL_FULL,
    PMFS_ERROR_NOT_MOUNTED,
    PMFS_ERROR_ALREADY_EXISTS,
    PMFS_ERROR_NOT_A_DIR,
    PMFS_ERROR_NOT_A_FILE,
    PMFS_ERROR_HANDLES_FULL,
} pmfs_status_t;

// ============================================================================
// PMFS FILE FLAGS
// ============================================================================

#define PMFS_MODE_READ              0x01
#define PMFS_MODE_WRITE             0x02
#define PMFS_MODE_APPEND            0x04
#define PMFS_MODE_CREATE            0x08
#define PMFS_MODE_TRUNCATE          0x10
#define PMFS_MODE_EXCLUSIVE         0x20

// ============================================================================
// PMFS SYSTEM BANKS
// ============================================================================

typedef enum {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1
} pmfs_bank_t;

// ============================================================================
// JOURNAL OPERATIONS
// ============================================================================

typedef enum {
    JOURNAL_OP_NONE = 0,
    JOURNAL_OP_CREATE,
    JOURNAL_OP_DELETE,
    JOURNAL_OP_WRITE,
    JOURNAL_OP_RENAME,
    JOURNAL_OP_MKDIR,
    JOURNAL_OP_RMDIR
} pmfs_journal_op_t;

// ============================================================================
// STRUCTURES
// ============================================================================

// PMFS Metadata (stored on disk)
typedef struct {
    uint32_t magic;
    char version[16];
    uint32_t created_timestamp;
    uint32_t last_mount_timestamp;
    uint32_t mount_count;
    pmfs_bank_t active_bank;
    pmfs_bank_t backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} PICOMIMI_PACKED pmfs_metadata_t;

// PMFS Statistics
typedef struct {
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint32_t files_created;
    uint32_t files_deleted;
    uint32_t dirs_created;
    uint32_t dirs_deleted;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t fsck_runs;
    uint32_t defrag_runs;
} pmfs_stats_t;

// Journal entry
typedef struct {
    pmfs_journal_op_t operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];
    uint32_t timestamp;
    bool active;
    bool committed;
} pmfs_journal_entry_t;

// Write cache
typedef struct {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint32_t last_access;
    bool dirty;
} pmfs_write_cache_t;

// File handle
typedef struct {
    FIL fat_file;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t flags;
    uint32_t owner_task_id;
    uint32_t current_position;
    uint32_t last_access;
    bool in_use;
    bool locked;
} pmfs_file_handle_t;

// File lock
typedef struct {
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint32_t acquired_time;
    bool active;
    bool exclusive;
} pmfs_file_lock_t;

// tmpfs entry
typedef struct {
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint32_t created;
    uint32_t modified;
    bool in_use;
} pmfs_tmpfs_entry_t;

// ============================================================================
// PMFS STATE
// ============================================================================

typedef struct {
    // Core state
    bool initialized;
    bool mounted;
    pmfs_metadata_t metadata;
    pmfs_stats_t stats;
    
    // FatFS state
    FATFS fatfs;
    bool fatfs_mounted;
    
    // File management
    pmfs_file_handle_t file_handles[PMFS_MAX_OPEN_FILES];
    pmfs_file_lock_t file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    pmfs_journal_entry_t journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    pmfs_write_cache_t write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
#if PMFS_ENABLE_TMPFS
    pmfs_tmpfs_entry_t tmpfs_entries[PMFS_MAX_TMPFS_ENTRIES];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    uint32_t tmpfs_next_offset;
#endif

    // Current working directory
    char cwd[PMFS_MAX_PATH_LENGTH];
} pmfs_state_t;

// ============================================================================
// INITIALIZATION & MOUNTING
// ============================================================================

pmfs_status_t pmfs_init(uint8_t cs_pin);
pmfs_status_t pmfs_check_root_exists(void);
pmfs_status_t pmfs_format_and_initialize(void);
pmfs_status_t pmfs_mount(void);
pmfs_status_t pmfs_unmount(void);
pmfs_status_t pmfs_emergency_unmount(void);
pmfs_status_t pmfs_fsck(bool auto_repair);

bool pmfs_is_initialized(void);
bool pmfs_is_mounted(void);

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int pmfs_open(const char* path, uint32_t flags, uint32_t task_id);
pmfs_status_t pmfs_close(int fd);
int pmfs_read(int fd, uint8_t* buffer, uint32_t size);
int pmfs_write(int fd, const uint8_t* data, uint32_t size);
pmfs_status_t pmfs_seek(int fd, uint32_t position);
uint32_t pmfs_tell(int fd);
uint32_t pmfs_size(int fd);
bool pmfs_eof(int fd);
pmfs_status_t pmfs_flush(int fd);

pmfs_status_t pmfs_remove(const char* path);
pmfs_status_t pmfs_rename(const char* old_path, const char* new_path);
pmfs_status_t pmfs_mkdir(const char* path);
pmfs_status_t pmfs_rmdir(const char* path, bool recursive);

bool pmfs_exists(const char* path);
bool pmfs_is_file(const char* path);
bool pmfs_is_dir(const char* path);
uint32_t pmfs_file_size(const char* path);

// ============================================================================
// TMPFS (RAM DISK)
// ============================================================================

#if PMFS_ENABLE_TMPFS
pmfs_status_t pmfs_tmpfs_create(const char* name, uint32_t size);
pmfs_status_t pmfs_tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
int pmfs_tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
pmfs_status_t pmfs_tmpfs_delete(const char* name);
bool pmfs_tmpfs_exists(const char* name);
void pmfs_tmpfs_clear(void);
uint32_t pmfs_tmpfs_available(void);
void pmfs_tmpfs_compact(void);
#endif

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

pmfs_bank_t pmfs_get_active_bank(void);
pmfs_bank_t pmfs_get_backup_bank(void);
pmfs_status_t pmfs_set_active_bank(pmfs_bank_t bank);
pmfs_status_t pmfs_copy_bank(pmfs_bank_t src, pmfs_bank_t dst);
pmfs_status_t pmfs_clear_bank(pmfs_bank_t bank);
pmfs_status_t pmfs_verify_bank(pmfs_bank_t bank);
const char* pmfs_get_bank_path(pmfs_bank_t bank);

// ============================================================================
// LOGGING
// ============================================================================

pmfs_status_t pmfs_log_system(const char* message);
pmfs_status_t pmfs_log_user(const char* message);
pmfs_status_t pmfs_log_rotate(const char* log_dir, uint32_t max_files);
pmfs_status_t pmfs_read_log_tail(const char* log_path, char* buffer, uint32_t buffer_size, uint32_t lines);

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void pmfs_get_stats(pmfs_stats_t* out_stats);
void pmfs_print_stats(void);
uint64_t pmfs_get_free_space(void);
uint64_t pmfs_get_total_space(void);
uint64_t pmfs_get_used_space(void);
float pmfs_get_fragmentation(void);
pmfs_metadata_t* pmfs_get_metadata(void);

// ============================================================================
// MAINTENANCE
// ============================================================================

pmfs_status_t pmfs_defragment(void);
pmfs_status_t pmfs_garbage_collect(void);
pmfs_status_t pmfs_verify_all_files(void);
pmfs_status_t pmfs_repair_corruption(void);

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

typedef struct {
    DIR fat_dir;
    char path[PMFS_MAX_PATH_LENGTH];
    bool valid;
} pmfs_dir_t;

typedef struct {
    FILINFO info;
    char name[PMFS_MAX_FILENAME];
    uint32_t size;
    bool is_dir;
    bool is_hidden;
    bool is_readonly;
} pmfs_dirent_t;

pmfs_status_t pmfs_opendir(pmfs_dir_t* dir, const char* path);
pmfs_status_t pmfs_readdir(pmfs_dir_t* dir, pmfs_dirent_t* entry);
pmfs_status_t pmfs_closedir(pmfs_dir_t* dir);
void pmfs_rewinddir(pmfs_dir_t* dir);

// ============================================================================
// UTILITY
// ============================================================================

const char* pmfs_status_to_string(pmfs_status_t status);
void pmfs_print_tree(const char* path, int depth);
void pmfs_print_metadata(void);
pmfs_status_t pmfs_normalize_path(const char* cwd, const char* path, char* out, size_t out_size);
uint32_t pmfs_crc32(const uint8_t* data, uint32_t length);

// ============================================================================
// GLOBAL STATE ACCESS
// ============================================================================

pmfs_state_t* pmfs_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // PMFS_H
