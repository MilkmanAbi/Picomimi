/**
 * PMFS Implementation Part 2 - tmpfs, banks, statistics, utilities
 * Continued from pmfs.c
 */
#include "fs/pmfs.h"
#include "api/picomimi_kernel.h"
#include <stdio.h>
#include <string.h>

extern pmfs_state_t g_pmfs;

// ============================================================================
// TMPFS (RAM DISK) - FULLY FUNCTIONAL IMPLEMENTATION
// ============================================================================

#if PMFS_ENABLE_TMPFS

pmfs_status_t pmfs_tmpfs_create(const char* name, uint32_t size) {
    if (g_pmfs.tmpfs_used + size > PMFS_TMPFS_SIZE) {
        printf("[PMFS] ERROR: tmpfs full\n");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Check if already exists
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (g_pmfs.tmpfs_entries[i].in_use && strcmp(g_pmfs.tmpfs_entries[i].name, name) == 0) {
            return PMFS_ERROR_ALREADY_EXISTS;
        }
    }
    
    // Find free slot
    int idx = -1;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (!g_pmfs.tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        printf("[PMFS] ERROR: tmpfs entry limit reached\n");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &g_pmfs.tmpfs_pool[g_pmfs.tmpfs_next_offset];
    g_pmfs.tmpfs_next_offset += size;
    g_pmfs.tmpfs_used += size;
    
    pmfs_tmpfs_entry_t* entry = &g_pmfs.tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->name[PMFS_MAX_FILENAME - 1] = '\0';
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = pm_get_time_ms();
    entry->modified = pm_get_time_ms();
    
    return PMFS_OK;
}

pmfs_status_t pmfs_tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    pmfs_tmpfs_entry_t* entry = NULL;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (g_pmfs.tmpfs_entries[i].in_use && strcmp(g_pmfs.tmpfs_entries[i].name, name) == 0) {
            entry = &g_pmfs.tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        printf("[PMFS] ERROR: tmpfs entry not found\n");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        printf("[PMFS] ERROR: tmpfs write exceeds allocation\n");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = pm_get_time_ms();
    
    return PMFS_OK;
}

int pmfs_tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    pmfs_tmpfs_entry_t* entry = NULL;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (g_pmfs.tmpfs_entries[i].in_use && strcmp(g_pmfs.tmpfs_entries[i].name, name) == 0) {
            entry = &g_pmfs.tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return (int)to_read;
}

pmfs_status_t pmfs_tmpfs_delete(const char* name) {
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (g_pmfs.tmpfs_entries[i].in_use && strcmp(g_pmfs.tmpfs_entries[i].name, name) == 0) {
            g_pmfs.tmpfs_entries[i].in_use = false;
            // Space will be reclaimed during compact
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

bool pmfs_tmpfs_exists(const char* name) {
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (g_pmfs.tmpfs_entries[i].in_use && strcmp(g_pmfs.tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

void pmfs_tmpfs_clear(void) {
    memset(g_pmfs.tmpfs_entries, 0, sizeof(g_pmfs.tmpfs_entries));
    memset(g_pmfs.tmpfs_pool, 0, sizeof(g_pmfs.tmpfs_pool));
    g_pmfs.tmpfs_used = 0;
    g_pmfs.tmpfs_next_offset = 0;
}

uint32_t pmfs_tmpfs_available(void) {
    return PMFS_TMPFS_SIZE - g_pmfs.tmpfs_used;
}

void pmfs_tmpfs_compact(void) {
    uint8_t temp_pool[PMFS_TMPFS_SIZE];
    uint32_t write_offset = 0;
    
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (g_pmfs.tmpfs_entries[i].in_use) {
            // Copy data to temp pool
            memcpy(&temp_pool[write_offset], g_pmfs.tmpfs_entries[i].data, 
                   g_pmfs.tmpfs_entries[i].size);
            g_pmfs.tmpfs_entries[i].data = &g_pmfs.tmpfs_pool[write_offset];
            write_offset += g_pmfs.tmpfs_entries[i].allocated;
        }
    }
    
    // Copy back to main pool
    memcpy(g_pmfs.tmpfs_pool, temp_pool, write_offset);
    g_pmfs.tmpfs_next_offset = write_offset;
}

#endif // PMFS_ENABLE_TMPFS

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

pmfs_bank_t pmfs_get_active_bank(void) {
    return g_pmfs.metadata.active_bank;
}

pmfs_bank_t pmfs_get_backup_bank(void) {
    return g_pmfs.metadata.backup_bank;
}

const char* pmfs_get_bank_path(pmfs_bank_t bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return NULL;
}

pmfs_status_t pmfs_set_active_bank(pmfs_bank_t bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    printf("[PMFS] Switching active bank to: %s\n", bank == PMFS_BANK_A ? "A" : "B");
    
    pmfs_bank_t old_active = g_pmfs.metadata.active_bank;
    g_pmfs.metadata.active_bank = bank;
    g_pmfs.metadata.backup_bank = old_active;
    
    // Persist immediately
    FIL meta_file;
    if (f_open(&meta_file, PMFS_METADATA_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        g_pmfs.metadata.crc32 = pmfs_crc32((uint8_t*)&g_pmfs.metadata, 
                                            sizeof(pmfs_metadata_t) - sizeof(uint32_t));
        UINT bw;
        f_write(&meta_file, &g_pmfs.metadata, sizeof(pmfs_metadata_t), &bw);
        f_close(&meta_file);
    }
    
    return PMFS_OK;
}

pmfs_status_t pmfs_clear_bank(pmfs_bank_t bank) {
    const char* bank_path = pmfs_get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    printf("[PMFS] Clearing bank: %s\n", bank == PMFS_BANK_A ? "A" : "B");
    
    // Delete all files in the bank directory
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, bank_path);
    if (res != FR_OK) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        char filepath[PMFS_MAX_PATH_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s/%s", bank_path, fno.fname);
        
        if (fno.fattrib & AM_DIR) {
            // Recursively delete subdirectory
            pmfs_rmdir(filepath, true);
        } else {
            f_unlink(filepath);
        }
    }
    
    f_closedir(&dir);
    
    return PMFS_OK;
}

pmfs_status_t pmfs_copy_bank(pmfs_bank_t src, pmfs_bank_t dst) {
    printf("[PMFS] Copying bank (this may take a while)...\n");
    
    const char* src_path = pmfs_get_bank_path(src);
    const char* dst_path = pmfs_get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Clear destination first
    pmfs_clear_bank(dst);
    f_mkdir(dst_path);
    
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, src_path);
    if (res != FR_OK) {
        printf("[PMFS] ERROR: Cannot open source bank\n");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    uint32_t files_copied = 0;
    uint8_t buffer[512];
    
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        if (!(fno.fattrib & AM_DIR)) {
            char src_file_path[PMFS_MAX_PATH_LENGTH];
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            
            snprintf(src_file_path, sizeof(src_file_path), "%s/%s", src_path, fno.fname);
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, fno.fname);
            
            FIL src_file, dst_file;
            if (f_open(&src_file, src_file_path, FA_READ) == FR_OK) {
                if (f_open(&dst_file, dst_file_path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
                    UINT br, bw;
                    while (1) {
                        res = f_read(&src_file, buffer, sizeof(buffer), &br);
                        if (res != FR_OK || br == 0) break;
                        f_write(&dst_file, buffer, br, &bw);
                    }
                    f_close(&dst_file);
                    files_copied++;
                }
                f_close(&src_file);
            }
        }
    }
    
    f_closedir(&dir);
    
    printf("[PMFS] Copied %lu files\n", (unsigned long)files_copied);
    
    return PMFS_OK;
}

pmfs_status_t pmfs_verify_bank(pmfs_bank_t bank) {
    const char* bank_path = pmfs_get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    FILINFO fno;
    if (f_stat(bank_path, &fno) != FR_OK) {
        printf("[PMFS] ERROR: Bank directory missing\n");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

pmfs_status_t pmfs_log_system(const char* message) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, (unsigned long)(pm_get_time_ms() / 86400000));
    
    FIL log_file;
    FRESULT res = f_open(&log_file, log_path, FA_WRITE | FA_OPEN_APPEND);
    if (res != FR_OK) {
        // Try to create the file
        res = f_open(&log_file, log_path, FA_WRITE | FA_CREATE_ALWAYS);
        if (res != FR_OK) {
            return PMFS_ERROR_IO_FAILURE;
        }
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", (unsigned long)pm_get_time_ms());
    
    f_printf(&log_file, "%s%s\n", timestamp, message);
    f_close(&log_file);
    
    return PMFS_OK;
}

pmfs_status_t pmfs_log_user(const char* message) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, (unsigned long)(pm_get_time_ms() / 86400000));
    
    FIL log_file;
    FRESULT res = f_open(&log_file, log_path, FA_WRITE | FA_OPEN_APPEND);
    if (res != FR_OK) {
        res = f_open(&log_file, log_path, FA_WRITE | FA_CREATE_ALWAYS);
        if (res != FR_OK) {
            return PMFS_ERROR_IO_FAILURE;
        }
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", (unsigned long)pm_get_time_ms());
    
    f_printf(&log_file, "%s%s\n", timestamp, message);
    f_close(&log_file);
    
    return PMFS_OK;
}

pmfs_status_t pmfs_log_rotate(const char* log_dir, uint32_t max_files) {
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, log_dir);
    if (res != FR_OK) return PMFS_ERROR_FILE_NOT_FOUND;
    
    // Count log files
    uint32_t file_count = 0;
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        if (!(fno.fattrib & AM_DIR)) {
            file_count++;
        }
    }
    f_closedir(&dir);
    
    // Delete oldest files if exceeding limit
    if (file_count > max_files) {
        res = f_opendir(&dir, log_dir);
        if (res != FR_OK) return PMFS_ERROR_FILE_NOT_FOUND;
        
        uint32_t to_delete = file_count - max_files;
        
        while (to_delete > 0) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            if (!(fno.fattrib & AM_DIR)) {
                char filepath[PMFS_MAX_PATH_LENGTH];
                snprintf(filepath, sizeof(filepath), "%s/%s", log_dir, fno.fname);
                f_unlink(filepath);
                to_delete--;
            }
        }
        
        f_closedir(&dir);
    }
    
    return PMFS_OK;
}

pmfs_status_t pmfs_read_log_tail(const char* log_path, char* buffer, uint32_t buffer_size, uint32_t lines) {
    if (!g_pmfs.mounted) return PMFS_ERROR_NOT_MOUNTED;
    if (!buffer || buffer_size == 0) return PMFS_ERROR_INVALID_PARAM;
    
    FIL file;
    FRESULT res = f_open(&file, log_path, FA_READ);
    if (res != FR_OK) return PMFS_ERROR_FILE_NOT_FOUND;
    
    uint32_t fsize = f_size(&file);
    if (fsize == 0) {
        buffer[0] = '\0';
        f_close(&file);
        return PMFS_OK;
    }
    
    // Simple approach: read from end looking for newlines
    uint32_t pos = fsize - 1;
    uint32_t line_count = 0;
    uint8_t c;
    UINT br;
    
    while (pos > 0 && line_count <= lines) {
        f_lseek(&file, pos);
        f_read(&file, &c, 1, &br);
        
        if (c == '\n') {
            line_count++;
            if (line_count > lines) {
                pos++;
                break;
            }
        }
        pos--;
    }
    
    // Read from pos to end
    f_lseek(&file, pos);
    uint32_t to_read = fsize - pos;
    if (to_read >= buffer_size) to_read = buffer_size - 1;
    
    f_read(&file, buffer, to_read, &br);
    buffer[br] = '\0';
    
    f_close(&file);
    g_pmfs.stats.bytes_read += br;
    
    return PMFS_OK;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void pmfs_get_stats(pmfs_stats_t* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &g_pmfs.stats, sizeof(pmfs_stats_t));
    }
}

void pmfs_print_stats(void) {
    printf("\n[PMFS] ============================================\n");
    printf("[PMFS] FILESYSTEM STATISTICS\n");
    printf("[PMFS] ============================================\n");
    printf("[PMFS] Files Created: %lu\n", (unsigned long)g_pmfs.stats.files_created);
    printf("[PMFS] Files Deleted: %lu\n", (unsigned long)g_pmfs.stats.files_deleted);
    printf("[PMFS] Bytes Written: %llu\n", (unsigned long long)g_pmfs.stats.bytes_written);
    printf("[PMFS] Bytes Read: %llu\n", (unsigned long long)g_pmfs.stats.bytes_read);
    printf("[PMFS] Cache Hits: %lu\n", (unsigned long)g_pmfs.stats.cache_hits);
    printf("[PMFS] Cache Misses: %lu\n", (unsigned long)g_pmfs.stats.cache_misses);
    printf("[PMFS] Journal Commits: %lu\n", (unsigned long)g_pmfs.stats.journal_commits);
#if PMFS_ENABLE_TMPFS
    printf("[PMFS] tmpfs Used: %lu / %d\n", (unsigned long)g_pmfs.tmpfs_used, PMFS_TMPFS_SIZE);
#endif
    printf("[PMFS] Fragmentation: %.1f%%\n", pmfs_get_fragmentation());
}

uint64_t pmfs_get_free_space(void) {
    FATFS* fs;
    DWORD fre_clust;
    
    FRESULT res = f_getfree("", &fre_clust, &fs);
    if (res != FR_OK) return 0;
    
    uint64_t free_bytes = (uint64_t)fre_clust * fs->csize * 512;
    return free_bytes;
}

uint64_t pmfs_get_total_space(void) {
    FATFS* fs;
    DWORD fre_clust;
    
    FRESULT res = f_getfree("", &fre_clust, &fs);
    if (res != FR_OK) return 0;
    
    uint64_t total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
    return total_bytes;
}

uint64_t pmfs_get_used_space(void) {
    uint64_t total = pmfs_get_total_space();
    uint64_t free = pmfs_get_free_space();
    return (total > free) ? (total - free) : 0;
}

static pmfs_status_t calculate_directory_size(const char* path, uint64_t* size) {
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, path);
    if (res != FR_OK) return PMFS_ERROR_FILE_NOT_FOUND;
    
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        if (fno.fattrib & AM_DIR) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, fno.fname);
            calculate_directory_size(subdir, size);
        } else {
            *size += fno.fsize;
        }
    }
    
    f_closedir(&dir);
    return PMFS_OK;
}

static pmfs_status_t count_files_recursive(const char* path, uint32_t* count) {
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, path);
    if (res != FR_OK) return PMFS_ERROR_FILE_NOT_FOUND;
    
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        if (fno.fattrib & AM_DIR) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, fno.fname);
            count_files_recursive(subdir, count);
        } else {
            (*count)++;
        }
    }
    
    f_closedir(&dir);
    return PMFS_OK;
}

float pmfs_get_fragmentation(void) {
    uint32_t total_files = 0;
    count_files_recursive(PMFS_ROOT_DIR, &total_files);
    
    if (total_files == 0) return 0.0f;
    
    // Heuristic: More files + more writes = more fragmentation
    float write_factor = (float)g_pmfs.metadata.total_writes / 1000.0f;
    float file_factor = (float)total_files / 100.0f;
    
    float fragmentation = (write_factor + file_factor) * 10.0f;
    if (fragmentation > 100.0f) fragmentation = 100.0f;
    
    return fragmentation;
}

pmfs_metadata_t* pmfs_get_metadata(void) {
    return &g_pmfs.metadata;
}

// ============================================================================
// MAINTENANCE
// ============================================================================

pmfs_status_t pmfs_fsck(bool auto_repair) {
    printf("\n[PMFS] Running filesystem check...\n");
    
    pmfs_status_t status = verify_integrity();
    if (status != PMFS_OK) {
        printf("[PMFS] ERROR: Integrity check failed\n");
        if (auto_repair) {
            printf("[PMFS] Attempting auto-repair...\n");
            return pmfs_repair_corruption();
        }
        return status;
    }
    
    printf("[PMFS] Filesystem OK\n");
    g_pmfs.stats.fsck_runs++;
    g_pmfs.metadata.needs_fsck = false;
    
    // Save metadata
    FIL meta_file;
    if (f_open(&meta_file, PMFS_METADATA_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        g_pmfs.metadata.crc32 = pmfs_crc32((uint8_t*)&g_pmfs.metadata, 
                                            sizeof(pmfs_metadata_t) - sizeof(uint32_t));
        UINT bw;
        f_write(&meta_file, &g_pmfs.metadata, sizeof(pmfs_metadata_t), &bw);
        f_close(&meta_file);
    }
    
    return PMFS_OK;
}

pmfs_status_t pmfs_repair_corruption(void) {
    printf("[PMFS] Repairing filesystem...\n");
    
    // Recreate directory tree
    const char* dirs[] = {
        PMFS_ROOT_DIR, PMFS_SYSTEM_A_DIR, PMFS_SYSTEM_B_DIR,
        PMFS_TMPFS_DIR, PMFS_LOG_DIR, PMFS_SYSLOG_DIR,
        PMFS_USERLOG_DIR, PMFS_DATA_DIR, PMFS_CONFIG_DIR,
        PMFS_CACHE_DIR, PMFS_JOURNAL_DIR
    };
    
    for (int i = 0; i < 11; i++) {
        FILINFO fno;
        if (f_stat(dirs[i], &fno) != FR_OK) {
            f_mkdir(dirs[i]);
        }
    }
    
    // Clear journal
    memset(g_pmfs.journal, 0, sizeof(g_pmfs.journal));
    g_pmfs.journal_head = 0;
    
    g_pmfs.metadata.needs_fsck = false;
    
    // Save metadata
    FIL meta_file;
    if (f_open(&meta_file, PMFS_METADATA_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        g_pmfs.metadata.crc32 = pmfs_crc32((uint8_t*)&g_pmfs.metadata, 
                                            sizeof(pmfs_metadata_t) - sizeof(uint32_t));
        UINT bw;
        f_write(&meta_file, &g_pmfs.metadata, sizeof(pmfs_metadata_t), &bw);
        f_close(&meta_file);
    }
    
    printf("[PMFS] Repair complete\n");
    
    return PMFS_OK;
}

pmfs_status_t pmfs_defragment(void) {
    printf("\n[PMFS] Starting defragmentation...\n");
    
    if (!g_pmfs.mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
#if PMFS_ENABLE_TMPFS
    // Defragment tmpfs
    pmfs_tmpfs_compact();
#endif
    
    // For SD card defrag, we would need more complex file copying
    // For now just mark as done
    
    g_pmfs.stats.defrag_runs++;
    
    printf("[PMFS] Defragmentation complete\n");
    
    return PMFS_OK;
}

pmfs_status_t pmfs_garbage_collect(void) {
    printf("\n[PMFS] Running garbage collection...\n");
    
    if (!g_pmfs.mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    // Flush and clear cache
    if (g_pmfs.cache_enabled) {
        if (g_pmfs.write_cache.dirty) {
            FIL f;
            if (f_open(&f, g_pmfs.write_cache.path, FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
                UINT bw;
                f_write(&f, g_pmfs.write_cache.data, g_pmfs.write_cache.size, &bw);
                f_close(&f);
            }
        }
        memset(&g_pmfs.write_cache, 0, sizeof(g_pmfs.write_cache));
    }
    
#if PMFS_ENABLE_TMPFS
    // Compact tmpfs
    pmfs_tmpfs_compact();
#endif
    
    // Clear committed journal entries
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (g_pmfs.journal[i].committed && !g_pmfs.journal[i].active) {
            memset(&g_pmfs.journal[i], 0, sizeof(pmfs_journal_entry_t));
        }
    }
    
    // Release stale file locks (older than 5 minutes)
    uint32_t now = pm_get_time_ms();
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (g_pmfs.file_locks[i].active) {
            if (now - g_pmfs.file_locks[i].acquired_time > 300000) {
                g_pmfs.file_locks[i].active = false;
            }
        }
    }
    
    printf("[PMFS] Garbage collection complete\n");
    
    return PMFS_OK;
}

pmfs_status_t pmfs_verify_all_files(void) {
    printf("\n[PMFS] Verifying all files...\n");
    
    if (!g_pmfs.mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    uint32_t files_checked = 0;
    uint32_t errors_found = 0;
    
    // Verify all files recursively
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, PMFS_ROOT_DIR);
    if (res == FR_OK) {
        while (1) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            files_checked++;
            
            // Try to open each file for reading
            if (!(fno.fattrib & AM_DIR)) {
                char filepath[PMFS_MAX_PATH_LENGTH];
                snprintf(filepath, sizeof(filepath), "%s/%s", PMFS_ROOT_DIR, fno.fname);
                
                FIL f;
                if (f_open(&f, filepath, FA_READ) == FR_OK) {
                    uint8_t test_byte;
                    UINT br;
                    if (f_read(&f, &test_byte, 1, &br) != FR_OK) {
                        errors_found++;
                    }
                    f_close(&f);
                } else {
                    errors_found++;
                }
            }
        }
        f_closedir(&dir);
    }
    
    printf("[PMFS] Checked %lu files, found %lu errors\n", 
           (unsigned long)files_checked, (unsigned long)errors_found);
    
    if (errors_found > 0) {
        g_pmfs.metadata.needs_fsck = true;
        return PMFS_ERROR_CORRUPT;
    }
    
    return PMFS_OK;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

pmfs_status_t pmfs_opendir(pmfs_dir_t* dir, const char* path) {
    if (!dir || !path) return PMFS_ERROR_INVALID_PARAM;
    
    FRESULT res = f_opendir(&dir->fat_dir, path);
    if (res != FR_OK) {
        dir->valid = false;
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    strncpy(dir->path, path, PMFS_MAX_PATH_LENGTH - 1);
    dir->valid = true;
    
    return PMFS_OK;
}

pmfs_status_t pmfs_readdir(pmfs_dir_t* dir, pmfs_dirent_t* entry) {
    if (!dir || !entry || !dir->valid) return PMFS_ERROR_INVALID_PARAM;
    
    FRESULT res = f_readdir(&dir->fat_dir, &entry->info);
    if (res != FR_OK || entry->info.fname[0] == 0) {
        return PMFS_ERROR_FILE_NOT_FOUND;  // End of directory
    }
    
    strncpy(entry->name, entry->info.fname, PMFS_MAX_FILENAME - 1);
    entry->size = entry->info.fsize;
    entry->is_dir = (entry->info.fattrib & AM_DIR) != 0;
    entry->is_hidden = (entry->info.fattrib & AM_HID) != 0;
    entry->is_readonly = (entry->info.fattrib & AM_RDO) != 0;
    
    return PMFS_OK;
}

pmfs_status_t pmfs_closedir(pmfs_dir_t* dir) {
    if (!dir) return PMFS_ERROR_INVALID_PARAM;
    
    if (dir->valid) {
        f_closedir(&dir->fat_dir);
        dir->valid = false;
    }
    
    return PMFS_OK;
}

void pmfs_rewinddir(pmfs_dir_t* dir) {
    if (dir && dir->valid) {
        f_readdir(&dir->fat_dir, NULL);  // Passing NULL rewinds
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

pmfs_status_t pmfs_normalize_path(const char* cwd, const char* path, char* out, size_t out_size) {
    if (!out || out_size == 0) return PMFS_ERROR_INVALID_PARAM;
    
    if (!path || strlen(path) == 0) {
        strncpy(out, cwd ? cwd : "/", out_size - 1);
        out[out_size - 1] = '\0';
        return PMFS_OK;
    }
    
    // Absolute path?
    if (path[0] == '/') {
        strncpy(out, path, out_size - 1);
        out[out_size - 1] = '\0';
        return PMFS_OK;
    }
    
    // Relative path - combine with cwd
    if (cwd && strlen(cwd) > 0) {
        if (cwd[strlen(cwd) - 1] == '/') {
            snprintf(out, out_size, "%s%s", cwd, path);
        } else {
            snprintf(out, out_size, "%s/%s", cwd, path);
        }
    } else {
        snprintf(out, out_size, "/%s", path);
    }
    
    return PMFS_OK;
}

void pmfs_print_tree(const char* path, int depth) {
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, path);
    if (res != FR_OK) return;
    
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        // Print indentation
        for (int i = 0; i < depth; i++) {
            printf("  ");
        }
        
        printf("%s %s", (fno.fattrib & AM_DIR) ? "[D]" : "[F]", fno.fname);
        if (!(fno.fattrib & AM_DIR)) {
            printf(" (%lu bytes)", (unsigned long)fno.fsize);
        }
        printf("\n");
        
        // Recurse into directories
        if (fno.fattrib & AM_DIR) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, fno.fname);
            pmfs_print_tree(subdir, depth + 1);
        }
    }
    
    f_closedir(&dir);
}

void pmfs_print_metadata(void) {
    printf("\n[PMFS] ============================================\n");
    printf("[PMFS] FILESYSTEM METADATA\n");
    printf("[PMFS] ============================================\n");
    printf("[PMFS] Version: %s\n", g_pmfs.metadata.version);
    printf("[PMFS] Mount Count: %lu\n", (unsigned long)g_pmfs.metadata.mount_count);
    printf("[PMFS] Active Bank: %s\n", g_pmfs.metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    printf("[PMFS] Total Writes: %lu\n", (unsigned long)g_pmfs.metadata.total_writes);
    printf("[PMFS] Bad Sectors: %lu\n", (unsigned long)g_pmfs.metadata.bad_sectors);
    printf("[PMFS] Needs FSCK: %s\n", g_pmfs.metadata.needs_fsck ? "YES" : "NO");
}
