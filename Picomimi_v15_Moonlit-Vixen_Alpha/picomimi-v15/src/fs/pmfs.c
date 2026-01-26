/**
 * PICOMIMI FileSystem (PMFS) Implementation
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Full implementation with:
 * - Journaling and crash recovery
 * - tmpfs RAM disk
 * - A/B bank system
 * - Write caching
 * - File locking
 */
#include "fs/pmfs.h"
#include "api/picomimi_kernel.h"
#include "lib/sd/sd_card.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

static pmfs_state_t g_pmfs;

pmfs_state_t* pmfs_get_state(void) {
    return &g_pmfs;
}

// ============================================================================
// INTERNAL HELPERS - FORWARD DECLARATIONS
// ============================================================================

static pmfs_status_t create_directory_tree(void);
static pmfs_status_t load_metadata(void);
static pmfs_status_t save_metadata(void);
static pmfs_status_t verify_integrity(void);
static pmfs_status_t replay_journal(void);
static pmfs_status_t commit_journal(void);
static pmfs_status_t save_journal_to_disk(void);
static pmfs_status_t load_journal_from_disk(void);
static void init_journal(void);
static void init_tmpfs(void);
static pmfs_status_t journal_add(pmfs_journal_op_t op, const char* path, const char* path2);
static pmfs_status_t cache_write(const char* path, const uint8_t* data, uint32_t size);
static pmfs_status_t cache_flush(void);
static int find_free_handle(void);
static pmfs_file_handle_t* get_handle(int fd);
static bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
static void release_lock(const char* path);
static bool is_locked(const char* path, uint32_t task_id);
static pmfs_status_t recursive_delete(const char* path);
static pmfs_status_t count_files_recursive(const char* path, uint32_t* count);
static pmfs_status_t calculate_directory_size(const char* path, uint64_t* size);

// ============================================================================
// CRC32 IMPLEMENTATION
// ============================================================================

uint32_t pmfs_crc32(const uint8_t* data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

pmfs_status_t pmfs_init(uint8_t cs_pin) {
    (void)cs_pin;
    
    printf("[PMFS] Initializing PicoMimi FileSystem v%s\n", PMFS_VERSION);
    
    // Clear state
    memset(&g_pmfs, 0, sizeof(pmfs_state_t));
    g_pmfs.cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    strcpy(g_pmfs.cwd, "/");
    
    // Initialize SD card through FatFS
    FRESULT res = f_mount(&g_pmfs.fatfs, "", 1);
    if (res != FR_OK) {
        printf("[PMFS] ERROR: SD card mount failed (FR=%d)\n", res);
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    g_pmfs.fatfs_mounted = true;
    printf("[PMFS] SD card detected\n");
    
    pmfs_status_t status = pmfs_check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        printf("[PMFS] Root structure not found\n");
        printf("[PMFS] ==============================================\n");
        printf("[PMFS] FILESYSTEM NOT INITIALIZED\n");
        printf("[PMFS] ==============================================\n");
        printf("[PMFS] To initialize the filesystem, call:\n");
        printf("[PMFS]   pmfs_format_and_initialize()\n");
        printf("[PMFS] \n");
        printf("[PMFS] WARNING: This will create the PMFS structure\n");
        printf("[PMFS]          on your SD card.\n");
        printf("[PMFS] ==============================================\n");
        return PMFS_ERROR_NO_ROOT;
    }
    
    g_pmfs.initialized = true;
    return PMFS_OK;
}

pmfs_status_t pmfs_check_root_exists(void) {
    FILINFO fno;
    
    if (f_stat(PMFS_ROOT_DIR, &fno) != FR_OK) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (f_stat(PMFS_METADATA_FILE, &fno) != FR_OK) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

pmfs_status_t pmfs_format_and_initialize(void) {
    printf("\n[PMFS] ============================================\n");
    printf("[PMFS] INITIALIZING FILESYSTEM\n");
    printf("[PMFS] ============================================\n");
    
    printf("[PMFS] Creating directory tree...\n");
    pmfs_status_t status = create_directory_tree();
    if (status != PMFS_OK) {
        printf("[PMFS] ERROR: Failed to create directories\n");
        return status;
    }
    
    printf("[PMFS] Initializing metadata...\n");
    g_pmfs.metadata.magic = PMFS_MAGIC;
    strncpy(g_pmfs.metadata.version, PMFS_VERSION, sizeof(g_pmfs.metadata.version) - 1);
    g_pmfs.metadata.created_timestamp = pm_get_time_ms();
    g_pmfs.metadata.last_mount_timestamp = 0;
    g_pmfs.metadata.mount_count = 0;
    g_pmfs.metadata.active_bank = PMFS_BANK_A;
    g_pmfs.metadata.backup_bank = PMFS_BANK_B;
    g_pmfs.metadata.needs_fsck = false;
    g_pmfs.metadata.total_writes = 0;
    g_pmfs.metadata.total_sectors = 0;
    g_pmfs.metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        printf("[PMFS] ERROR: Failed to save metadata\n");
        return status;
    }
    
    // Create boot flag
    FIL boot_file;
    if (f_open(&boot_file, PMFS_BOOTFLAG_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        f_printf(&boot_file, "PMFS_INITIALIZED\n");
        f_close(&boot_file);
    }
    
    printf("[PMFS] ============================================\n");
    printf("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY\n");
    printf("[PMFS] ============================================\n");
    printf("[PMFS] You can now call pmfs_mount()\n");
    
    g_pmfs.initialized = true;
    return PMFS_OK;
}

static pmfs_status_t create_directory_tree(void) {
    const char* dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_TMPFS_DIR,
        PMFS_LOG_DIR,
        PMFS_SYSLOG_DIR,
        PMFS_USERLOG_DIR,
        PMFS_DATA_DIR,
        PMFS_CONFIG_DIR,
        PMFS_CACHE_DIR,
        PMFS_JOURNAL_DIR
    };
    
    for (int i = 0; i < 11; i++) {
        FILINFO fno;
        if (f_stat(dirs[i], &fno) != FR_OK) {
            printf("[PMFS]   Creating: %s\n", dirs[i]);
            
            FRESULT res = f_mkdir(dirs[i]);
            if (res != FR_OK && res != FR_EXIST) {
                printf("[PMFS]   ERROR: Failed to create %s (FR=%d)\n", dirs[i], res);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            printf("[PMFS]   Exists: %s\n", dirs[i]);
        }
    }
    
    return PMFS_OK;
}

// ============================================================================
// MOUNTING
// ============================================================================

pmfs_status_t pmfs_mount(void) {
    if (!g_pmfs.initialized) {
        printf("[PMFS] ERROR: Not initialized\n");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (g_pmfs.mounted) {
        printf("[PMFS] Already mounted\n");
        return PMFS_OK;
    }
    
    printf("\n[PMFS] Mounting filesystem...\n");
    
    pmfs_status_t status = load_metadata();
    if (status != PMFS_OK) {
        printf("[PMFS] ERROR: Failed to load metadata\n");
        return status;
    }
    
    printf("[PMFS] Verifying integrity...\n");
    status = verify_integrity();
    if (status != PMFS_OK) {
        printf("[PMFS] WARNING: Integrity check failed\n");
        g_pmfs.metadata.needs_fsck = true;
    }
    
#if PMFS_ENABLE_JOURNALING
    printf("[PMFS] Loading journal from disk...\n");
    load_journal_from_disk();
    
    printf("[PMFS] Replaying journal...\n");
    status = replay_journal();
    if (status != PMFS_OK) {
        printf("[PMFS] WARNING: Journal replay had errors\n");
    }
#endif
    
    init_journal();
    init_tmpfs();
    
    g_pmfs.metadata.mount_count++;
    g_pmfs.metadata.last_mount_timestamp = pm_get_time_ms();
    save_metadata();
    
    g_pmfs.mounted = true;
    
    printf("[PMFS] ============================================\n");
    printf("[PMFS] FILESYSTEM MOUNTED\n");
    printf("[PMFS] ============================================\n");
    printf("[PMFS] Active Bank: %s\n", g_pmfs.metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    printf("[PMFS] Mount Count: %lu\n", (unsigned long)g_pmfs.metadata.mount_count);
    printf("[PMFS] Free Space: %lu KB\n", (unsigned long)(pmfs_get_free_space() / 1024));
    
    return PMFS_OK;
}

pmfs_status_t pmfs_unmount(void) {
    if (!g_pmfs.mounted) {
        return PMFS_OK;
    }
    
    printf("\n[PMFS] Unmounting filesystem...\n");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (g_pmfs.file_handles[i].in_use) {
            pmfs_close(i);
        }
    }
    
    // Flush cache
    if (g_pmfs.cache_enabled) {
        cache_flush();
    }
    
#if PMFS_ENABLE_JOURNALING
    commit_journal();
    save_journal_to_disk();
#endif
    
#if PMFS_ENABLE_TMPFS
    pmfs_tmpfs_clear();
#endif
    
    save_metadata();
    
    g_pmfs.mounted = false;
    printf("[PMFS] Filesystem unmounted\n");
    
    return PMFS_OK;
}

pmfs_status_t pmfs_emergency_unmount(void) {
    // Emergency unmount for kernel panic - minimal operations
    
    // Close all files immediately
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (g_pmfs.file_handles[i].in_use) {
            f_sync(&g_pmfs.file_handles[i].fat_file);
            f_close(&g_pmfs.file_handles[i].fat_file);
        }
    }
    
    // Flush cache if dirty
    if (g_pmfs.cache_enabled && g_pmfs.write_cache.dirty) {
        FIL f;
        if (f_open(&f, g_pmfs.write_cache.path, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
            UINT bw;
            f_write(&f, g_pmfs.write_cache.data, g_pmfs.write_cache.size, &bw);
            f_close(&f);
        }
    }
    
#if PMFS_ENABLE_JOURNALING
    save_journal_to_disk();
#endif
    
    // Mark as needing fsck
    g_pmfs.metadata.needs_fsck = true;
    save_metadata();
    
    g_pmfs.mounted = false;
    
    return PMFS_OK;
}

bool pmfs_is_initialized(void) {
    return g_pmfs.initialized;
}

bool pmfs_is_mounted(void) {
    return g_pmfs.mounted;
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

static pmfs_status_t load_metadata(void) {
    FIL meta_file;
    FRESULT res = f_open(&meta_file, PMFS_METADATA_FILE, FA_READ);
    if (res != FR_OK) {
        printf("[PMFS] ERROR: Cannot open metadata file\n");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    UINT bytes_read;
    res = f_read(&meta_file, &g_pmfs.metadata, sizeof(pmfs_metadata_t), &bytes_read);
    f_close(&meta_file);
    
    if (res != FR_OK || bytes_read != sizeof(pmfs_metadata_t)) {
        printf("[PMFS] ERROR: Metadata size mismatch\n");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (g_pmfs.metadata.magic != PMFS_MAGIC) {
        printf("[PMFS] ERROR: Invalid metadata magic\n");
        return PMFS_ERROR_CORRUPT;
    }
    
    // Verify CRC
    uint32_t calculated_crc = pmfs_crc32((uint8_t*)&g_pmfs.metadata, 
                                          sizeof(pmfs_metadata_t) - sizeof(uint32_t));
    
    if (calculated_crc != g_pmfs.metadata.crc32) {
        printf("[PMFS] WARNING: Metadata CRC mismatch\n");
    }
    
    return PMFS_OK;
}

static pmfs_status_t save_metadata(void) {
    // Calculate CRC (excluding the CRC field itself)
    g_pmfs.metadata.crc32 = pmfs_crc32((uint8_t*)&g_pmfs.metadata, 
                                        sizeof(pmfs_metadata_t) - sizeof(uint32_t));
    
    FIL meta_file;
    FRESULT res = f_open(&meta_file, PMFS_METADATA_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        printf("[PMFS] ERROR: Cannot write metadata file\n");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    UINT bytes_written;
    res = f_write(&meta_file, &g_pmfs.metadata, sizeof(pmfs_metadata_t), &bytes_written);
    f_close(&meta_file);
    
    if (res != FR_OK || bytes_written != sizeof(pmfs_metadata_t)) {
        printf("[PMFS] ERROR: Metadata write incomplete\n");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

static pmfs_status_t verify_integrity(void) {
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    FILINFO fno;
    for (int i = 0; i < 5; i++) {
        if (f_stat(required_dirs[i], &fno) != FR_OK) {
            printf("[PMFS] ERROR: Missing directory: %s\n", required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

static void init_journal(void) {
    memset(g_pmfs.journal, 0, sizeof(g_pmfs.journal));
    g_pmfs.journal_head = 0;
}

static void init_tmpfs(void) {
#if PMFS_ENABLE_TMPFS
    memset(g_pmfs.tmpfs_entries, 0, sizeof(g_pmfs.tmpfs_entries));
    memset(g_pmfs.tmpfs_pool, 0, sizeof(g_pmfs.tmpfs_pool));
    g_pmfs.tmpfs_used = 0;
    g_pmfs.tmpfs_next_offset = 0;
#endif
}

static pmfs_status_t journal_add(pmfs_journal_op_t op, const char* path, const char* path2) {
#if !PMFS_ENABLE_JOURNALING
    return PMFS_OK;
#else
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!g_pmfs.journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full, commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    pmfs_journal_entry_t* entry = &g_pmfs.journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = pm_get_time_ms();
    entry->committed = false;
    
    return PMFS_OK;
#endif
}

static pmfs_status_t commit_journal(void) {
#if !PMFS_ENABLE_JOURNALING
    return PMFS_OK;
#else
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (g_pmfs.journal[i].active) {
            g_pmfs.journal[i].committed = true;
            g_pmfs.journal[i].active = false;
        }
    }
    
    g_pmfs.stats.journal_commits++;
    save_journal_to_disk();
    
    return PMFS_OK;
#endif
}

static pmfs_status_t save_journal_to_disk(void) {
    FIL journal_file;
    FRESULT res = f_open(&journal_file, PMFS_JOURNAL_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        printf("[PMFS] ERROR: Cannot write journal file\n");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    UINT bw;
    f_write(&journal_file, g_pmfs.journal, sizeof(g_pmfs.journal), &bw);
    f_close(&journal_file);
    
    return PMFS_OK;
}

static pmfs_status_t load_journal_from_disk(void) {
    FILINFO fno;
    if (f_stat(PMFS_JOURNAL_FILE, &fno) != FR_OK) {
        return PMFS_OK;  // No journal file is OK
    }
    
    FIL journal_file;
    FRESULT res = f_open(&journal_file, PMFS_JOURNAL_FILE, FA_READ);
    if (res != FR_OK) {
        printf("[PMFS] WARNING: Cannot read journal file\n");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    UINT br;
    res = f_read(&journal_file, g_pmfs.journal, sizeof(g_pmfs.journal), &br);
    f_close(&journal_file);
    
    if (br != sizeof(g_pmfs.journal)) {
        printf("[PMFS] WARNING: Journal size mismatch\n");
        return PMFS_ERROR_CORRUPT;
    }
    
    return PMFS_OK;
}

static pmfs_status_t replay_journal(void) {
    bool any_uncommitted = false;
    
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (g_pmfs.journal[i].active && !g_pmfs.journal[i].committed) {
            any_uncommitted = true;
            
            printf("[PMFS] Replaying journal entry: %s\n", g_pmfs.journal[i].path);
            
            switch (g_pmfs.journal[i].operation) {
                case JOURNAL_OP_CREATE:
                    // File was being created - nothing to replay
                    break;
                    
                case JOURNAL_OP_DELETE:
                    // Complete the deletion
                    f_unlink(g_pmfs.journal[i].path);
                    break;
                    
                case JOURNAL_OP_WRITE:
                    // File write was in progress - mark as potentially corrupt
                    printf("[PMFS] WARNING: Uncommitted write detected\n");
                    break;
                    
                case JOURNAL_OP_RENAME:
                    // Complete the rename if target doesn't exist
                    {
                        FILINFO fno;
                        if (f_stat(g_pmfs.journal[i].path2, &fno) != FR_OK &&
                            f_stat(g_pmfs.journal[i].path, &fno) == FR_OK) {
                            f_rename(g_pmfs.journal[i].path, g_pmfs.journal[i].path2);
                        }
                    }
                    break;
                    
                case JOURNAL_OP_MKDIR:
                    // Complete directory creation
                    {
                        FILINFO fno;
                        if (f_stat(g_pmfs.journal[i].path, &fno) != FR_OK) {
                            f_mkdir(g_pmfs.journal[i].path);
                        }
                    }
                    break;
                    
                default:
                    break;
            }
            
            g_pmfs.journal[i].active = false;
        }
    }
    
    if (any_uncommitted) {
        printf("[PMFS] Journal replay complete\n");
        g_pmfs.stats.journal_rollbacks++;
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

static int find_free_handle(void) {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!g_pmfs.file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

static pmfs_file_handle_t* get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return NULL;
    if (!g_pmfs.file_handles[fd].in_use) return NULL;
    return &g_pmfs.file_handles[fd];
}

int pmfs_open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!g_pmfs.mounted) return -1;
    
    if (is_locked(path, task_id)) {
        printf("[PMFS] ERROR: File is locked\n");
        return -1;
    }
    
    int fd = find_free_handle();
    if (fd < 0) {
        printf("[PMFS] ERROR: No free file handles\n");
        return -1;
    }
    
    pmfs_file_handle_t* handle = &g_pmfs.file_handles[fd];
    
    // Map PMFS flags to FatFS flags
    BYTE fat_mode = 0;
    if (flags & PMFS_MODE_READ) fat_mode |= FA_READ;
    if (flags & PMFS_MODE_WRITE) fat_mode |= FA_WRITE;
    if (flags & PMFS_MODE_CREATE) fat_mode |= FA_OPEN_ALWAYS;
    if (flags & PMFS_MODE_TRUNCATE) fat_mode |= FA_CREATE_ALWAYS;
    if (flags & PMFS_MODE_APPEND) fat_mode |= FA_OPEN_APPEND;
    
    if (fat_mode == 0) fat_mode = FA_READ;
    
    FRESULT res = f_open(&handle->fat_file, path, fat_mode);
    if (res != FR_OK) {
        if (flags & PMFS_MODE_CREATE) {
            res = f_open(&handle->fat_file, path, FA_WRITE | FA_CREATE_ALWAYS);
            if (res != FR_OK) return -1;
            f_close(&handle->fat_file);
            res = f_open(&handle->fat_file, path, fat_mode);
        }
        
        if (res != FR_OK) return -1;
    }
    
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = pm_get_time_ms();
    handle->locked = false;
    handle->current_position = 0;
    
#if PMFS_ENABLE_JOURNALING
    if (flags & PMFS_MODE_CREATE) {
        journal_add(JOURNAL_OP_CREATE, path, NULL);
    }
#endif
    
    g_pmfs.stats.files_created++;
    
    return fd;
}

pmfs_status_t pmfs_close(int fd) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    pmfs_flush(fd);
    
    f_close(&handle->fat_file);
    
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    memset(handle, 0, sizeof(pmfs_file_handle_t));
    
    return PMFS_OK;
}

int pmfs_read(int fd, uint8_t* buffer, uint32_t size) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    UINT bytes_read;
    FRESULT res = f_read(&handle->fat_file, buffer, size, &bytes_read);
    
    if (res != FR_OK) return -1;
    
    g_pmfs.stats.bytes_read += bytes_read;
    handle->last_access = pm_get_time_ms();
    handle->current_position += bytes_read;
    
    return (int)bytes_read;
}

int pmfs_write(int fd, const uint8_t* data, uint32_t size) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        printf("[PMFS] ERROR: File not opened for writing\n");
        return -1;
    }
    
    int written = 0;
    
#if PMFS_ENABLE_WRITE_CACHE
    if (g_pmfs.cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        pmfs_status_t status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            g_pmfs.stats.cache_hits++;
        } else {
            g_pmfs.stats.cache_misses++;
        }
    }
#endif
    
    if (written == 0) {
        UINT bw;
        FRESULT res = f_write(&handle->fat_file, data, size, &bw);
        if (res == FR_OK) {
            written = (int)bw;
        }
    }
    
    if (written > 0) {
        g_pmfs.stats.bytes_written += written;
        g_pmfs.metadata.total_writes++;
        handle->last_access = pm_get_time_ms();
        handle->current_position += written;
        
#if PMFS_ENABLE_JOURNALING
        journal_add(JOURNAL_OP_WRITE, handle->path, NULL);
#endif
    }
    
    return written;
}

pmfs_status_t pmfs_seek(int fd, uint32_t position) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    FRESULT res = f_lseek(&handle->fat_file, position);
    if (res != FR_OK) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    handle->current_position = position;
    return PMFS_OK;
}

uint32_t pmfs_tell(int fd) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle) return 0;
    
    return handle->current_position;
}

uint32_t pmfs_size(int fd) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle) return 0;
    
    return f_size(&handle->fat_file);
}

bool pmfs_eof(int fd) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle) return true;
    
    return f_eof(&handle->fat_file) != 0;
}

pmfs_status_t pmfs_flush(int fd) {
    pmfs_file_handle_t* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
#if PMFS_ENABLE_WRITE_CACHE
    if (g_pmfs.cache_enabled && strcmp(g_pmfs.write_cache.path, handle->path) == 0) {
        cache_flush();
    }
#endif
    
    f_sync(&handle->fat_file);
    
    return PMFS_OK;
}

pmfs_status_t pmfs_remove(const char* path) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
#if PMFS_ENABLE_JOURNALING
    journal_add(JOURNAL_OP_DELETE, path, NULL);
#endif
    
    FRESULT res = f_unlink(path);
    if (res != FR_OK) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    g_pmfs.stats.files_deleted++;
    
    return PMFS_OK;
}

pmfs_status_t pmfs_rename(const char* old_path, const char* new_path) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    
    FILINFO fno;
    if (f_stat(old_path, &fno) != FR_OK) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (f_stat(new_path, &fno) == FR_OK) {
        return PMFS_ERROR_ALREADY_EXISTS;
    }
    
    if (is_locked(old_path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
#if PMFS_ENABLE_JOURNALING
    journal_add(JOURNAL_OP_RENAME, old_path, new_path);
#endif
    
    FRESULT res = f_rename(old_path, new_path);
    if (res != FR_OK) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

pmfs_status_t pmfs_mkdir(const char* path) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    
    FILINFO fno;
    if (f_stat(path, &fno) == FR_OK) {
        return PMFS_ERROR_ALREADY_EXISTS;
    }
    
#if PMFS_ENABLE_JOURNALING
    journal_add(JOURNAL_OP_MKDIR, path, NULL);
#endif
    
    FRESULT res = f_mkdir(path);
    if (res != FR_OK && res != FR_EXIST) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    g_pmfs.stats.dirs_created++;
    
    return PMFS_OK;
}

pmfs_status_t pmfs_rmdir(const char* path, bool recursive) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (!(fno.fattrib & AM_DIR)) {
        return PMFS_ERROR_NOT_A_DIR;
    }
    
    if (recursive) {
        return recursive_delete(path);
    } else {
        FRESULT res = f_unlink(path);
        if (res != FR_OK) {
            return PMFS_ERROR_IO_FAILURE;
        }
    }
    
    g_pmfs.stats.dirs_deleted++;
    
    return PMFS_OK;
}

static pmfs_status_t recursive_delete(const char* path) {
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        char filepath[PMFS_MAX_PATH_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, fno.fname);
        
        if (fno.fattrib & AM_DIR) {
            recursive_delete(filepath);
        } else {
            f_unlink(filepath);
        }
    }
    
    f_closedir(&dir);
    
    res = f_unlink(path);
    return (res == FR_OK) ? PMFS_OK : PMFS_ERROR_IO_FAILURE;
}

bool pmfs_exists(const char* path) {
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK;
}

bool pmfs_is_file(const char* path) {
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return false;
    return !(fno.fattrib & AM_DIR);
}

bool pmfs_is_dir(const char* path) {
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return false;
    return (fno.fattrib & AM_DIR) != 0;
}

uint32_t pmfs_file_size(const char* path) {
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return 0;
    return fno.fsize;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

static pmfs_status_t cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // If different file in cache, flush first
    if (g_pmfs.write_cache.dirty && strcmp(g_pmfs.write_cache.path, path) != 0) {
        cache_flush();
    }
    
    strncpy(g_pmfs.write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(g_pmfs.write_cache.data, data, size);
    g_pmfs.write_cache.size = size;
    g_pmfs.write_cache.last_access = pm_get_time_ms();
    g_pmfs.write_cache.dirty = true;
    
    return PMFS_OK;
}

static pmfs_status_t cache_flush(void) {
    if (!g_pmfs.write_cache.dirty) {
        return PMFS_OK;
    }
    
    FIL f;
    FRESULT res = f_open(&f, g_pmfs.write_cache.path, FA_WRITE | FA_OPEN_APPEND);
    if (res != FR_OK) {
        printf("[PMFS] ERROR: Cache flush failed\n");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    UINT bw;
    f_write(&f, g_pmfs.write_cache.data, g_pmfs.write_cache.size, &bw);
    f_close(&f);
    
    g_pmfs.write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

static bool acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Check if already locked
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (g_pmfs.file_locks[i].active && strcmp(g_pmfs.file_locks[i].path, path) == 0) {
            if (g_pmfs.file_locks[i].exclusive || exclusive) {
                return false;
            }
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!g_pmfs.file_locks[i].active) {
            g_pmfs.file_locks[i].active = true;
            strncpy(g_pmfs.file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            g_pmfs.file_locks[i].owner_task_id = task_id;
            g_pmfs.file_locks[i].acquired_time = pm_get_time_ms();
            g_pmfs.file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;
}

static void release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (g_pmfs.file_locks[i].active && strcmp(g_pmfs.file_locks[i].path, path) == 0) {
            g_pmfs.file_locks[i].active = false;
            return;
        }
    }
}

static bool is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (g_pmfs.file_locks[i].active && strcmp(g_pmfs.file_locks[i].path, path) == 0) {
            if (g_pmfs.file_locks[i].owner_task_id == task_id) {
                return false;  // Caller owns the lock
            }
            return true;
        }
    }
    return false;
}

// ============================================================================
// STATUS STRING CONVERSION
// ============================================================================

const char* pmfs_status_to_string(pmfs_status_t status) {
    switch (status) {
        case PMFS_OK: return "OK";
        case PMFS_ERROR_NOT_INITIALIZED: return "Not Initialized";
        case PMFS_ERROR_SD_NOT_FOUND: return "SD Card Not Found";
        case PMFS_ERROR_NO_ROOT: return "Root Structure Missing";
        case PMFS_ERROR_CORRUPT: return "Filesystem Corrupt";
        case PMFS_ERROR_NO_SPACE: return "No Space";
        case PMFS_ERROR_FILE_NOT_FOUND: return "File Not Found";
        case PMFS_ERROR_ACCESS_DENIED: return "Access Denied";
        case PMFS_ERROR_FILE_LOCKED: return "File Locked";
        case PMFS_ERROR_INVALID_PARAM: return "Invalid Parameter";
        case PMFS_ERROR_IO_FAILURE: return "I/O Failure";
        case PMFS_ERROR_JOURNAL_FULL: return "Journal Full";
        case PMFS_ERROR_NOT_MOUNTED: return "Not Mounted";
        case PMFS_ERROR_ALREADY_EXISTS: return "Already Exists";
        case PMFS_ERROR_NOT_A_DIR: return "Not a Directory";
        case PMFS_ERROR_NOT_A_FILE: return "Not a File";
        case PMFS_ERROR_HANDLES_FULL: return "Handles Full";
        default: return "Unknown Error";
    }
}
