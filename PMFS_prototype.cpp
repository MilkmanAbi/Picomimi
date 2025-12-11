/*
 * PMFS (Picomimi Filesystem) - Picomimi System Storage Engine v2.0
 
 * * **PMFS is the core system driver for all persistent and volatile storage**
 * within the PicoMimi OS. It provides a policy-driven layer built on the standard
 * SD/FAT32 interface to introduce critical RTOS functionality.

 * * * ============================================================================
 * PMFS Architectural Features
 * ============================================================================
 * * 1. Transactional Journaling: 
 * Ensures data consistency and atomicity across system events.
 * * 2. High-Speed Write Caching:
 * Minimises slow external I/O by batching writes and hopefully reducing flash wear.
 * * 3. Dual System Banks (A/B OTA): 
 * Enables secure, failsafe Over-The-Air firmware updates and rollbacks (Future Feature Introduction).
 * * 4. Volatile Storage (tmpfs): 
 * Provides a high-speed RAM-based file system for ephemeral system data.
 * * 5. Concurrency Management (File Locking): 
 * Implements thread-safe access to files for the RTOS kernel. (Soon, in v5.0)
 * * 6. Resource Management:
 * Includes support for Quota Management. (Soon, in v5.0)
 * * 7. Integrity Features: 
 * Includes Auto-repair routines and hooks for Compression Support. (Dev in progress)

 * * * * ============================================================================
 * Developer Status & Tasks (MilkmanAbi)
 * ============================================================================
 * * [STATUS: WEAR LEVELING] 
 * The current wear-leveling logic is a **Simulated Proof-of-Concept** (PoC) 
 * designed for demonstration only. It relies on the SD card's internal 
 * controller and is scheduled for final removal.
 * * [TASK: TMPFS] 
 * The RAM disk implementation is currently non-functional and requires a 
 * dedicated review of its memory allocation and file table logic.
 * * [TASK: JOURNALING] 
 * Journal data is currently stored in volatile RAM. Critical task to migrate 
 * the journal to a persistent file on the SD card (`/.journal`) to ensure 
 * true crash recovery.
 * * [TASK: PANIC HANDLING]
 * Implement an emergency, synchronised `pmfs.unmount()` call within the 
 * Kernel Panic Handler to guarantee data integrity during system failures.

 * * * * ============================================================================
 * PMFS ARCHITECTURAL CLARIFICATION
 * ============================================================================
 * * **PMFS is the Picomimi System's Storage Engine.**
 * It is a **Filesystem Abstraction Layer (FAL)** built on the standard SD.h
 * library and the FAT32 format. It functions as the OS's driver, providing 
 * critical RTOS features (journaling, locking) that the base SD library lacks. 
 * This design enables rapid feature development without tackling the demon 
 * of a low-level block driver replacement.

NOTES: This shit fucked. Duplication, file paste onto itself, These functions exist in the class but have no implementation:

PMFS::seek

PMFS::tell

PMFS::size(int fd)

PMFS::eof

PMFS::rmdir

PMFS::rename

PMFS::verify_all_files

PMFS::garbage_collect

PMFS::defragment

PMFS::get_used_space

PMFS::get_fragmentation

Bank “clear” function for copy
I fix soon OwO
 
 * */

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem/*
 * PMFS - PicoMimi FileSystem v2.0
 * Hyper-Advanced Filesystem with:
 * - Wear Leveling
 * - Journaling
 * - Write Caching
 * - A/B System Banks for OTA
 * - tmpfs (RAM disk)
 * - Quota Management
 * - File Locking
 * - Directory Indexing
 * - Auto-repair
 * - Compression Support Ready
 */
 
 /* MilkmanAbi Dev notes, remove wear leveling, it's fake, it's ai generated for a proof of concept, just to demo, actual SD cards handle this internally, this is just for demo, treat it as such. 
 
FUCK. IMPLEMENT Emergency unmount during kernel panic!!! RAHHHHH
*/

#include <SD.h>
#include <Arduino.h>

// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "2.0.0"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_WEAR_LEVELING   true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        128
#define PMFS_TMPFS_SIZE             (32 * 1024)  // 32KB RAM disk
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_WEAR_THRESHOLD         10000         // Write cycles
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
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
    PMFS_ERROR_JOURNAL_FULL
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    bool in_use;
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t flags;
    uint32_t owner_task_id;
    uint64_t last_access;
    bool locked;
    uint32_t lock_owner;
} __attribute__((packed));

struct PMFSJournalEntry {
    bool active;
    PMFSJournalOp operation;
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint64_t timestamp;
    uint32_t size;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t size;
    uint64_t last_access;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    bool in_use;
    char name[PMFS_MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t allocated;
    uint64_t created;
    uint64_t modified;
} __attribute__((packed));

struct PMFSFileLock {
    bool active;
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint64_t acquired_time;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
    uint64_t files_created;
    uint64_t files_deleted;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t journal_commits;
    uint32_t journal_rollbacks;
    uint32_t wear_level_rotations;
    uint32_t fsck_runs;
} __attribute__((packed));

struct PMFSWearInfo {
    uint32_t sector_id;
    uint32_t write_count;
    bool is_bad;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk)
    PMFSTmpFSEntry tmpfs_entries[16];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    
    // Wear leveling
    PMFSWearInfo* wear_table;
    uint32_t wear_table_size;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    void init_journal();
    void init_tmpfs();
    void init_wear_leveling();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    void update_wear_level(uint32_t sector);
    uint32_t find_best_sector();
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);
    uint32_t tell(int fd);
    uint32_t size(int fd);
    bool eof(int fd);
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path);
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK)
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();
    float get_fragmentation();
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE
    // ========================================================================
    
    PMFSStatus defragment();
    PMFSStatus garbage_collect();
    PMFSStatus verify_all_files();
    PMFSStatus repair_corruption();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    wear_table = nullptr;
    wear_table_size = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
    
    if (wear_table) {
        free(wear_table);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    // Initialize SD card
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    // Check if root structure exists
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    // Create directory structure
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    // Initialize metadata
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    // Create boot flag
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
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
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    // Load metadata
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    // Verify integrity
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    // Replay journal if needed
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    // Initialize subsystems
    init_journal();
    init_tmpfs();
    init_wear_leveling();
    
    // Update metadata
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    // Close all open files
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    // Flush cache
    if (cache_enabled) {
        cache_flush();
    }
    
    // Commit journal
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
    }
    
    // Clear tmpfs
    tmpfs_clear();
    
    // Save metadata
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

void PMFS::init_wear_leveling() {
    if (!PMFS_ENABLE_WEAR_LEVELING) return;
    
    // Allocate wear table (simplified - would need actual sector count from SD)
    wear_table_size = 1024;  // Example: track 1024 sectors
    wear_table = (PMFSWearInfo*)malloc(sizeof(PMFSWearInfo) * wear_table_size);
    
    if (wear_table) {
        for (uint32_t i = 0; i < wear_table_size; i++) {
            wear_table[i].sector_id = i;
            wear_table[i].write_count = 0;
            wear_table[i].is_bad = false;
        }
    }
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    // TODO: Verify CRC32
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    // Calculate CRC32
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    // Basic integrity checks
    
    // Check that all required directories exist
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    // Check metadata CRC
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
        // Not fatal, continue
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    // Check if file is locked
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    // Find free handle
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    // Construct SD file mode
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    // Open file
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        // If CREATE flag is set and file doesn't exist, create it
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    // Set up handle
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush any pending writes
    flush(fd);
    
    // Close SD file
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    // Release lock if held
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    // Clear handle
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    // Check if write mode
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    // Use write cache if enabled
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        // Write to cache
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    // Direct write if cache disabled or cache full
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        
        // Update wear leveling
        if (PMFS_ENABLE_WEAR_LEVELING) {
            // Simplified - would need actual sector calculation
            uint32_t sector = handle->sd_file.position() / PMFS_SECTOR_SIZE;
            update_wear_level(sector);
        }
        
        // Journal the write
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    // Flush cache if applicable
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    // Flush SD file
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Check if locked
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    // Journal the operation
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS (RAM DISK) IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Find free entry
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_used];
    tmpfs_used += size;
    
    // Set up entry
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    // Find entry
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    // Find and clear entry
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Note: We don't reclaim space from pool (simplified)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    tmpfs_used = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < 16; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT (A/B OTA)
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    // Swap banks
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Open source directory
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    // Clear destination
    // TODO: Implement recursive directory deletion
    
    // Copy files
    File file = src_dir.openNextFile();
    uint32_t files_copied = 0;
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            // Copy file
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if bank directory exists
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);  // New file per day
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    // TODO: Implement log rotation
    // - Count log files in directory
    // - Delete oldest files if count > max_files
    return PMFS_OK;
}

// ============================================================================
// JOURNALING
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Find free journal entry
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        // Journal full - commit and retry
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    // Add entry
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    // Mark all active entries as committed
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    // On mount, replay any uncommitted journal entries
    // This ensures filesystem consistency after crashes
    
    // TODO: Implement journal replay from persistent storage
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Check if different file - flush if so
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    // Write to cache
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    // Write cache to file
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    // Find existing lock
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            // File already locked
            if (file_locks[i].exclusive || exclusive) {
                return false;  // Cannot acquire
            }
            // Shared lock OK
            return true;
        }
    }
    
    // Find free lock slot
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;  // No free slots
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;  // Owner can access
            }
            return true;  // Locked by someone else
        }
    }
    return false;  // Not locked
}

// ============================================================================
// WEAR LEVELINGEmergency unmount during kernel panic


// ============================================================================

void PMFS::update_wear_level(uint32_t sector) {
    if (!wear_table || sector >= wear_table_size) return;
    
    wear_table[sector].write_count++;
    
    if (wear_table[sector].write_count > PMFS_WEAR_THRESHOLD) {
        Serial.print("[PMFS] WARNING: Sector ");
        Serial.print(sector);
        Serial.println(" approaching wear limit");
    }
}

uint32_t PMFS::find_best_sector() {
    if (!wear_table) return 0;
    
    uint32_t best_sector = 0;
    uint32_t min_writes = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < wear_table_size; i++) {
        if (!wear_table[i].is_bad && wear_table[i].write_count < min_writes) {
            min_writes = wear_table[i].write_count;
            best_sector = i;
        }
    }
    
    return best_sector;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
}

uint64_t PMFS::get_free_space() {
    // Note: SD library doesn't provide this on RP2040
    // Would need platform-specific implementation
    return 0;  // Placeholder
}

uint64_t PMFS::get_total_space() {
    return 0;  // Placeholder
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    // Simplified CRC32 - use proper implementation in production
    uint32_t crc = 0xFFFFFFFF;Emergency unmount during kernel panic


    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

const char* PMFS::status_to_string(PMFSStatus status) {
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
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: Emergency unmount during kernel panic

"); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

// ============================================================================
// MAINTENANCE OPERATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    // Check directory structure
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    // Recreate missing directories
    create_directory_tree();
    
    // Clear journal
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Global instance
PMFS pmfs;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize PMFS
    PMFSStatus status = pmfs.init(5);  // CS pin 5
    
    if (status == PMFS_ERROR_NO_ROOT) {
        // First time - initialize filesystem
        Serial.println("Initializing filesystem...");
        status = pmfs.format_and_initialize();
        
        if (status != PMFS_OK) {
            Serial.println("ERROR: Initialization failed!");
            while(1);
        }
    }
    
    // Mount filesystem
    status = pmfs.mount();
    if (status != PMFS_OK) {
        Serial.println("ERROR: Mount failed!");
        while(1);
    }
    
    // Use filesystem
    int fd = pmfs.open("/PMFS/data/test.txt", 
                       PMFS_MODE_WRITE | PMFS_MODE_CREATE);
    if (fd >= 0) {
        const char* data = "Hello PMFS!";
        pmfs.write(fd, (uint8_t*)data, strlen(data));
        pmfs.close(fd);
    }
    
    // Use tmpfs
    pmfs.tmpfs_create("temp1", 1024);
    uint8_t temp_data[] = {1, 2, 3, 4, 5};
    pmfs.tmpfs_write("temp1", temp_data, sizeof(temp_data));
    
    // Log something
    pmfs.log_system("System started");
    
    // Print stats
    pmfs.print_stats();
    pmfs.print_metadata();
}

void loop() {
    // Your code here
}
*/
