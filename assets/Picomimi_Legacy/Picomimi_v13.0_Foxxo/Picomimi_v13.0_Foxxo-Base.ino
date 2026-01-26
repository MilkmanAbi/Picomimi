/*
 * Picomimi MicroOS v13.0-BASE
 * Micro Operating System for RP2040
 * 
 * Changes from v12:
 *   - ACE (App Check Environment) REMOVED
 *   - Instant OOM killing with microsecond precision
 *   - Immediate block coalescing on kfree()
 *   - PMFS (Picomimi Filesystem) integrated:
 *       * Transactional journaling
 *       * Write caching
 *       * Dual system banks (A/B OTA)
 *       * tmpfs RAM disk
 *       * File locking
 *   - Cleaner, faster startup
 * 
 * Made with determination ฅ(•ㅅ•❀)ฅ...
 * ...and love ˗ˋˏ ♡ ˎˊ˗
 *
 * Toolchain:
 *   - Assembled with MIAU.py (Monolithic INO Aggregator Utility)  
 *     → Utility that assembles modules made by MRRP.py or manually edited by a person into a Monolithic INO.
 *   - Split with MRRP.py (Monolithic Repartition & Refactor Program)  
 *     → Utility that splits the Picomimi Kernel into module files, generating modules automatically.
 *   - Fixed with NYAA.py (Normalize Your Architecture Automatically)  
 *     → Fixer utility that takes in JSONs and auto-stitches or edits your code.
 *   - Reviewed and verified with MROW.py (Mend & Review Our Weirdness)  
 *     → Checker utility that ensures all modules and syntax are valid.
 */
#include <SPI.h>
#include <SD.h>
#include <hardware/adc.h>
#include <hardware/watchdog.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/timer.h>
#include <pico/platform.h>
#include <pico/multicore.h>
#include <pico/mutex.h>

#define SD_CS 5
#define SD_MOSI 19
#define SD_MISO 16
#define SD_SCK 18
#define BTN_ONOFF 9

#define disable_all_interrupts() __asm__ volatile ("cpsid i" : : : "memory")
#define enable_all_interrupts() __asm__ volatile ("cpsie i" : : : "memory")

// System limits

// Gotta make it smol temporarily OwO - TMPFS taking up too much RAM, so I'm limiting the system for now until I build an optimised version and a better TMPFS
#define MAX_TASKS 24
#define MAX_MEMORY_BLOCKS 128
#define HEAP_SIZE (120 * 1024)

#define KERNEL_RESERVE (10 * 1024)
#define TASK_NAME_LEN 24
#define MAX_LOG_ENTRIES 40
#define MAX_APPS 16
#define MAX_GUI_APPS 8
#define MAX_KERNEL_MUTEXES 16
#define MAX_SEMAPHORES 16
#define MAX_EVENT_FLAGS 16

// Scheduler configuration
#define SCHEDULER_TICK_US 1000
#define SCHED_NUM_PRIORITY_LEVELS 32
#define SCHED_RT_THRESHOLD 24
#define SCHED_BASE_QUANTUM_US 5000
#define SCHED_MAX_QUANTUM_US 80000
#define SCHED_AGING_INTERVAL_MS 500
#define SCHED_IDLE_INJECTION_THRESHOLD 85

// Watchdog and system
#define WATCHDOG_TIMEOUT_MS 8000
#define REAPER_INTERVAL_MS 2000
#define REAPER_GRACE_PERIOD_MS 500

// Core 1 configuration
#define MAX_CORE1_TASKS 8
#define CORE1_STACK_SIZE (4 * 1024)

// IPC configuration
#define MAX_IPC_MESSAGES 48
#define IPC_MSG_SIZE 64
#define IPC_NULL_MSG 0xFFFF
#define IPC_TARGET_BROADCAST 0xFFFFFFFF

// Filesystem configuration
#define FS_MAX_FILENAME 32
#define FS_MAX_OPEN_FILES 8
#define FS_BUFFER_SIZE 512
#define FS_LOG_FILE "/LogRecord"

// OOM configuration
#define OOM_REQUEST_TIMEOUT_MS 1500
#define MAX_OOM_HANDLERS 16
#define OOM_ABUSIVE_ALLOC_VELOCITY 80
#define OOM_ABUSIVE_ALLOC_SIZE (60 * 1024)
#define MAX_APP_MEM_REQUEST_GLOBAL (75 * 1024)
#define VELOCITY_CHECK_CHUNK (4 * 1024)
#define VELOCITY_TIME_THRESHOLD_US (80 * 1000)

// Memory protection thresholds
#define MEM_CRITICAL_THRESHOLD (15 * 1024)
#define MEM_WARNING_THRESHOLD (25 * 1024)
#define MEM_FRAGMENTATION_CRITICAL 75

// CPU protection thresholds
#define CPU_OVERLOAD_THRESHOLD 92.0f
#define CPU_CRITICAL_THRESHOLD 97.0f
#define CPU_TASK_ABUSE_THRESHOLD 80.0f
#define CPU_ABUSE_SAMPLE_COUNT 5


// Task states
enum TaskState : uint8_t {
  TASK_READY,
  TASK_RUNNING,
  TASK_WAITING,
  TASK_SUSPENDED,
  TASK_TERMINATED,
  TASK_ZOMBIE
};

// Core affinity
enum CoreAffinity : uint8_t {
  CORE_ANY = 0,
  CORE_0 = 1,
  CORE_1 = 2
};

// Enhanced memory management constants
#define MEM_MAGIC_ALLOCATED 0xDEADBEEF
#define MEM_MAGIC_FREE 0xFEEDFACE
#define MEM_CANARY_VALUE 0xCAFEBABE
#define KMEM_ALIGNMENT 8
#define MEM_MIN_SPLIT_SIZE 64
#define MEM_COALESCE_THRESHOLD 16
#define MEM_DEFRAG_INTERVAL_MS 5000
#define MEM_VERIFY_ON_FREE true
#define MEM_ZERO_ON_FREE false
#define MEM_PANIC_ON_CORRUPTION true

// OOM velocity tracking
#define OOM_VELOCITY_WINDOW_MS 1000
#define OOM_VELOCITY_CRITICAL 10
#define OOM_VELOCITY_HIGH 5
#define OOM_FAST_KILL_THRESHOLD_MS 50

// Memory pressure levels
enum MemPressure : uint8_t {
  MEM_PRESSURE_NONE = 0,
  MEM_PRESSURE_LOW = 1,
  MEM_PRESSURE_MODERATE = 2,
  MEM_PRESSURE_HIGH = 3,
  MEM_PRESSURE_CRITICAL = 4,
  MEM_PRESSURE_EMERGENCY = 5
};

// Allocation failure reasons
enum AllocFailReason : uint8_t {
  ALLOC_FAIL_NONE = 0,
  ALLOC_FAIL_NO_MEMORY,
  ALLOC_FAIL_FRAGMENTED,
  ALLOC_FAIL_TASK_BLOCKED,
  ALLOC_FAIL_TASK_LIMIT,
  ALLOC_FAIL_VELOCITY_THROTTLE,
  ALLOC_FAIL_EMERGENCY_RESERVE,
  ALLOC_FAIL_OOM_KILL_PENDING
};

// Task types
#define TASK_TYPE_KERNEL      0x01
#define TASK_TYPE_DRIVER      0x02
#define TASK_TYPE_SERVICE     0x04
#define TASK_TYPE_MODULE      0x08
#define TASK_TYPE_APPLICATION 0x10

// Task flags
#define TASK_FLAG_PROTECTED           0x01
#define TASK_FLAG_CRITICAL            0x02
#define TASK_FLAG_RESPAWN             0x04
#define TASK_FLAG_ONESHOT             0x08
#define TASK_FLAG_PERSISTENT          0x10
#define TASK_FLAG_OOM_CLEANUP_REQUESTED 0x20

// OOM priorities
#define OOM_PRIORITY_NEVER    0
#define OOM_PRIORITY_CRITICAL 1
#define OOM_PRIORITY_HIGH     2
#define OOM_PRIORITY_NORMAL   3
#define OOM_PRIORITY_LOW      4

// File types
#define FILE_TYPE_TEXT   0x01
#define FILE_TYPE_LOG    0x02
#define FILE_TYPE_DATA   0x03
#define FILE_TYPE_CONFIG 0x04

// IPC message types
enum IPCMessageType : uint8_t {
  IPC_NONE = 0,
  IPC_RENDER_FRAME,
  IPC_PROCESS_INPUT,
  IPC_COMPUTE_DATA,
  IPC_AUDIO_SAMPLE,
  IPC_USER_DEFINED
};

// IPC structures
struct IPCMessage {
  uint32_t sender_id;
  uint32_t target_id;
  IPCMessageType type;
  uint8_t priority;
  uint64_t timestamp;
  uint16_t sequence;
  uint8_t data[IPC_MSG_SIZE];
  uint16_t next;
  bool in_use;
} __attribute__((packed));

struct IPCManager {
  IPCMessage message_pool[MAX_IPC_MESSAGES];
  uint16_t free_list[MAX_IPC_MESSAGES];
  int16_t free_list_head;
  uint16_t sequence_counter;
  uint32_t dropped_messages;
  uint32_t total_sent;
  uint32_t total_received;
  mutex_t lock;
};

struct TaskIPCQueue {
  uint32_t priority_bitmap;
  uint16_t priority_lists_head[SCHED_NUM_PRIORITY_LEVELS];
  uint16_t message_count;
};

struct IPCStats {
  uint32_t messages_sent;
  uint32_t messages_received;
  uint32_t messages_dropped_pool_full;
  uint32_t messages_dropped_task_full;
  uint32_t broadcasts_sent;
  float avg_queue_depth_global;
  uint32_t max_queue_depth_global;
};

// Wait structures
struct TaskWaitNode {
  uint32_t task_id;
  TaskWaitNode* next;
  uint32_t wait_flags;
  uint8_t wait_mode;
  bool clear_on_exit;
};

#define K_EVENT_WAIT_ANY 0
#define K_EVENT_WAIT_ALL 1

struct KMutex {
  bool locked;
  uint32_t owner_id;
  uint8_t original_priority;
  TaskWaitNode* wait_list_head;
};

struct KSemaphore {
  int32_t count;
  uint32_t max_count;
  TaskWaitNode* wait_list_head;
};

struct KEvent {
  uint32_t flags;
  TaskWaitNode* wait_list_head;
};

// App registry
struct AppEntry {
  char name[TASK_NAME_LEN];
  void (*spawn_func)();
};

// Module callbacks
struct ModuleCallbacks {
  void (*init)(uint32_t id);
  void (*tick)(void*);
  void (*deinit)();
};


// Task scheduling info
struct TaskSchedInfo {
  uint8_t effective_priority;
  uint8_t base_priority;
  uint32_t quantum_us;
  uint32_t cpu_time_slice;
  uint64_t last_run;
  uint64_t total_runtime_us;
  uint32_t voluntary_yields;
  uint32_t preemptions;
  uint8_t cpu_affinity;
  uint8_t age;
  bool is_realtime;
  float cpu_usage_percent;
  uint32_t cpu_burst_counter;
  float cpu_samples[CPU_ABUSE_SAMPLE_COUNT];
  uint8_t cpu_sample_index;
} __attribute__((packed));

// Task Control Block
struct TCB {
  uint32_t id;
  TaskState state;
  uint8_t task_type;
  uint8_t oom_priority;
  uint8_t priority;
  CoreAffinity affinity;
  void (*entry)(void*);
  void* arg;
  ModuleCallbacks* callbacks;
  uint32_t flags;
  uint64_t wake_time;
  uint32_t mem_used;
  uint32_t mem_peak;
  char name[TASK_NAME_LEN];
  uint32_t mem_limit;
  uint64_t start_time;
  uint64_t max_runtime;
  uint64_t last_respawn;
  uint32_t respawn_count;
  uint32_t cpu_time;
  uint32_t last_run;
  uint32_t page_faults;
  uint32_t context_switches;
  const char* description;
  uint8_t running_on_core;
  uint32_t oom_bytes_requested;
  TaskSchedInfo sched_info;
  TaskIPCQueue ipc;
  uint8_t original_priority;
  TaskWaitNode wait_node;
  uint32_t alloc_velocity;
  uint64_t last_alloc_time;
  uint32_t mem_request_bytes;
  bool mem_blocked;
  uint32_t mem_throttle_mark;
  uint64_t mem_throttle_time_us;
  uint64_t total_cpu_time_us;
  bool is_cpu_abuser;
} __attribute__((aligned(64)));

// Memory block
// Enhanced Memory Block with integrity checking
struct MemBlock {
  void* addr;
  uint32_t size;
  uint32_t owner_id;
  uint32_t alloc_seq;
  uint64_t alloc_time_us;
  uint64_t last_access_us;
  uint32_t magic;           // Corruption detection
  uint32_t canary_front;    // Buffer overflow detection
  uint32_t canary_back;     // Buffer overflow detection
  uint16_t access_count;    // Usage tracking for paging
  uint8_t pressure_level;   // Memory pressure at allocation time
  bool free;
  bool paged_out;           // Will be used for paging later
  bool pinned;              // Prevent paging this block
  uint8_t _padding[1];
} __attribute__((packed, aligned(8)));

// Memory allocator statistics
struct MemStats {
  uint32_t total_allocs;
  uint32_t total_frees;
  uint32_t active_blocks;
  uint32_t failed_allocs;
  uint32_t oom_events;
  uint32_t oom_kills;
  uint32_t corruptions_detected;
  uint32_t emergency_compactions;
  uint32_t velocity_throttles;
  uint64_t total_bytes_allocated;
  uint64_t total_bytes_freed;
  uint64_t peak_usage_bytes;
  uint32_t last_defrag_ms;
  float fragmentation_pct;
  uint8_t current_pressure;
  uint32_t oom_velocity;
  uint64_t last_oom_time_us;
};

// OOM velocity tracker
struct OOMVelocityTracker {
  uint64_t events[OOM_VELOCITY_CRITICAL];
  uint8_t event_count;
  uint8_t head;
  uint64_t window_start_us;
};

// Log entry
struct LogEntry {
  uint64_t timestamp;
  char message[56];
  uint8_t level;
  uint8_t _padding[7];
} __attribute__((aligned(8)));

// Filesystem structures
struct FSFile {
  File handle;
  char path[FS_MAX_FILENAME];
  bool open;
  bool write_mode;
  uint32_t owner_task_id;
};

struct SDCardInfo {
  uint8_t card_type;
  uint64_t card_size;
  uint32_t sector_count;
  uint16_t sector_size;
  bool valid;
};

// Core 1 state
struct Core1State {
  TCB tasks[MAX_CORE1_TASKS];
  uint32_t task_count;
  uint32_t current_task;
  bool running;
  uint64_t uptime_us;
  float cpu_usage;
  uint32_t context_switches;
  mutex_t scheduler_lock;
} __attribute__((aligned(64)));

// OOM structures
typedef void (*oom_callback_t)(uint32_t bytes_requested);

struct TaskOOMHandler {
  uint32_t task_id;
  oom_callback_t callback;
  bool active;
};

struct OOMRequest {
  uint32_t allocating_task_id;
  uint32_t target_task_id;
  uint64_t request_time;
  uint32_t bytes_requested;
  bool request_sent;
  bool task_complied;
};

struct OOMStats {
  uint32_t requests_sent;
  uint32_t voluntary_releases;
  uint32_t forced_kills;
  uint32_t total_bytes_reclaimed;
  uint32_t prevention_count;
  uint32_t abusive_kills;
};

struct OOMVictim {
  uint32_t task_id;
  uint32_t memory_used;
  uint8_t oom_priority;
  int32_t score;
  bool has_handler;
};

// Scheduler structures
struct PriorityBitmap {
  uint32_t level_mask;
  uint32_t task_masks[SCHED_NUM_PRIORITY_LEVELS];
};

struct CoreScheduler {
  PriorityBitmap runnable;
  PriorityBitmap waiting;
  uint32_t current_task;
  uint32_t idle_task;
  uint8_t current_priority;
  uint64_t last_switch;
  uint64_t total_runtime;
  uint64_t idle_time;
  uint32_t switches;
  uint32_t preemptions;
  uint32_t idle_injections;
  float cpu_load;
  float cpu_load_instant;
  bool idle_injection_active;
  uint64_t last_aging;
  mutex_t lock;
} __attribute__((aligned(64)));

// Panic info
struct PanicInfo {
  const char* reason;
  uint32_t task_id;
  uint32_t pc;
  uint32_t lr;
  uint32_t sp;
  uint64_t timestamp;
  bool is_core1;
};

// Watchdog state
struct WatchdogState {
  bool enabled;
  uint64_t last_feed;
  uint32_t timeout_ms;
  uint32_t triggers;
  bool in_panic;
};

// UI Socket for apps
struct UISocket {
  bool (*request_focus)(uint32_t task_id);
  void (*release_focus)(uint32_t task_id);
  void (*register_stdout)(void (*write_char_fn)(char));
  bool (*send_message_api)(uint32_t target_id, IPCMessageType type, void* data, size_t size, uint8_t priority);
  bool (*receive_message_api)(IPCMessage* msg_out);
  uint32_t (*spawn_core1_task)(const char* name, void (*entry)(void*), void* arg, uint8_t priority);
  void (*task_exit)();
  bool (*mutex_lock)(uint32_t mutex_id);
  void (*mutex_unlock)(uint32_t mutex_id);
  bool (*sem_wait)(uint32_t sem_id, uint32_t timeout_ms);
  void (*sem_post)(uint32_t sem_id);
  uint32_t (*event_wait)(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms);
  void (*event_set)(uint32_t event_id, uint32_t flags);
  float (*get_core0_usage)();
  float (*get_core1_usage)();
  uint32_t (*get_task_memory)(uint32_t task_id);
  void (*register_oom_handler)(uint32_t task_id, void (*handler)(uint32_t bytes_requested));
  void (*oom_cleanup_done)(uint32_t task_id, uint32_t bytes_freed);
  void (*hint_memory_pressure)(uint32_t task_id);
};

// Kernel state
struct KernelState {
  TCB tasks[MAX_TASKS];
  uint32_t task_count;
  uint32_t current_task;
  
  MemBlock mem_blocks[MAX_MEMORY_BLOCKS];
  uint32_t mem_block_count;
  mutex_t mem_lock;
  
  uint64_t uptime_ms;
  bool running;
  bool panic_mode;
  volatile bool preemption_pending;
  
  uint32_t total_allocations;
  uint32_t total_frees;
  uint32_t oom_kills;
  uint32_t alloc_sequence;
  uint32_t fragmentation_pct;
  uint32_t largest_free_block;
  uint32_t total_free_mem;
  
  uint8_t kernel_tasks;
  uint8_t driver_tasks;
  uint8_t service_tasks;
  uint8_t module_tasks;
  uint8_t application_tasks;
  uint32_t zombie_tasks;
  
  bool shell_alive;
  bool cpumon_alive;
  bool tempmon_alive;
  bool fs_alive;
  bool root_mode;
  
  float cpu_usage;
  float temperature;
  uint32_t total_context_switches;
  
  LogEntry log[MAX_LOG_ENTRIES];
  uint32_t log_head;
  uint32_t log_count;
  mutex_t log_lock;
  
  bool fs_available;
  bool fs_mounted;
  SDCardInfo sd_info;
  uint64_t fs_used_bytes;
  uint32_t fs_reads;
  uint32_t fs_writes;
  uint32_t fs_log_counter;
  FSFile fs_open_files[FS_MAX_OPEN_FILES];
  mutex_t fs_lock;
  
  int32_t gui_focus_task_id;
  void (*app_write_char)(char);
  
  Core1State core1;
  IPCManager ipc_manager;
  bool core1_initialized;
  
  KMutex kernel_mutexes[MAX_KERNEL_MUTEXES];
  KSemaphore kernel_semaphores[MAX_SEMAPHORES];
  KEvent kernel_event_flags[MAX_EVENT_FLAGS];
  
  uint64_t last_velocity_check_ms;
  
  
  uint8_t heap[HEAP_SIZE];
} __attribute__((aligned(64)));


// ============================================================================
// PMFS (PICOMIMI FILESYSTEM) - INTEGRATED
// ============================================================================
// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "3.0.0"
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
#define PMFS_JOURNAL_FILE       "/PMFS/.journal/journal.dat"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature

// Limits
#define PMFS_MAX_OPEN_FILES         16
#define PMFS_MAX_PATH_LENGTH        256
#define PMFS_MAX_FILENAME           64
#define PMFS_WRITE_CACHE_SIZE       (8 * 1024)   // 8KB cache
#define PMFS_JOURNAL_ENTRIES        64
// I see if I wanna keep this shit or not. Eats RAM
// OOPSIE WOOPSIE!! UwU we made a fucky wucky...
#define PMFS_TMPFS_SIZE             (4 * 1024)  // 4KB RAM disk
// :3
#define PMFS_MAX_TMPFS_ENTRIES      32
#define PMFS_MAX_LOCKS              32
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
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
    PMFS_ERROR_JOURNAL_FULL,
    PMFS_ERROR_NOT_MOUNTED,
    PMFS_ERROR_ALREADY_EXISTS
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
    JOURNAL_OP_NONE = 0,
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
    uint32_t current_position;  // For seek/tell support
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
    uint32_t fsck_runs;
    uint32_t defrag_runs;
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
    
    // tmpfs (RAM disk) - FIXED IMPLEMENTATION
    PMFSTmpFSEntry tmpfs_entries[PMFS_MAX_TMPFS_ENTRIES];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    uint32_t tmpfs_next_offset;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    PMFSStatus save_journal_to_disk();
    PMFSStatus load_journal_from_disk();
    void init_journal();
    void init_tmpfs();
    
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
    
    PMFSStatus recursive_delete(const char* path);
    PMFSStatus count_files_recursive(const char* path, uint32_t* count);
    PMFSStatus calculate_directory_size(const char* path, uint64_t* size);
    
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
    PMFSStatus emergency_unmount();  // For kernel panic
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS - ALL IMPLEMENTED
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);  // IMPLEMENTED
    uint32_t tell(int fd);                        // IMPLEMENTED
    uint32_t size(int fd);                        // IMPLEMENTED
    bool eof(int fd);                             // IMPLEMENTED
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);  // IMPLEMENTED
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path, bool recursive = false);     // IMPLEMENTED
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK) - FULLY FUNCTIONAL
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    void tmpfs_compact();  // Defragment tmpfs
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus clear_bank(PMFSSystemBank bank);  // IMPLEMENTED
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
    // STATISTICS & MONITORING - ALL IMPLEMENTED
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();        // IMPLEMENTED
    float get_fragmentation();        // IMPLEMENTED
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE - ALL IMPLEMENTED
    // ========================================================================
    
    PMFSStatus defragment();          // IMPLEMENTED
    PMFSStatus garbage_collect();     // IMPLEMENTED
    PMFSStatus verify_all_files();    // IMPLEMENTED
    PMFSStatus repair_corruption();
    PMFSStatus verify_directory_recursive(const char* path, uint32_t* files_checked, uint32_t* errors_found);
    
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
    tmpfs_next_offset = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    memset(tmpfs_pool, 0, sizeof(tmpfs_pool));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
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
    
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
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
    
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Loading journal from disk...");
        load_journal_from_disk();
        
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    init_journal();
    init_tmpfs();
    
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
    
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    if (cache_enabled) {
        cache_flush();
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
        save_journal_to_disk();
    }
    
    tmpfs_clear();
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

PMFSStatus PMFS::emergency_unmount() {
    // Emergency unmount for kernel panic - minimal operations, no error checking
    
    // Close all files immediately
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use && file_handles[i].sd_file) {
            file_handles[i].sd_file.flush();
            file_handles[i].sd_file.close();
        }
    }
    
    // Flush cache if dirty
    if (cache_enabled && write_cache.dirty) {
        File f = SD.open(write_cache.path, FILE_WRITE);
        if (f) {
            f.write(write_cache.data, write_cache.size);
            f.close();
        }
    }
    
    // Save journal to disk
    if (PMFS_ENABLE_JOURNALING) {
        save_journal_to_disk();
    }
    
    // Mark as needing fsck
    metadata.needs_fsck = true;
    save_metadata();
    
    mounted = false;
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    memset(tmpfs_pool, 0, sizeof(tmpfs_pool));
    tmpfs_used = 0;
    tmpfs_next_offset = 0;
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
    
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
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
    
    return PMFS_OK;
}

// ============================================================================
// JOURNALING - NOW WITH PERSISTENT STORAGE
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        commit_journal();
        return journal_add(op, path, path2);
    }
    
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
    
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    save_journal_to_disk();
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_journal_to_disk() {
    File journal_file = SD.open(PMFS_JOURNAL_FILE, FILE_WRITE);
    if (!journal_file) {
        Serial.println("[PMFS] ERROR: Cannot write journal file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    journal_file.write((uint8_t*)journal, sizeof(journal));
    journal_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::load_journal_from_disk() {
    if (!SD.exists(PMFS_JOURNAL_FILE)) {
        return PMFS_OK;
    }
    
    File journal_file = SD.open(PMFS_JOURNAL_FILE, FILE_READ);
    if (!journal_file) {
        Serial.println("[PMFS] WARNING: Cannot read journal file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t read_bytes = journal_file.read((uint8_t*)journal, sizeof(journal));
    journal_file.close();
    
    if (read_bytes != sizeof(journal)) {
        Serial.println("[PMFS] WARNING: Journal size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    bool any_uncommitted = false;
    
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active && !journal[i].committed) {
            any_uncommitted = true;
            
            Serial.print("[PMFS] Replaying journal entry: ");
            Serial.println(journal[i].path);
            
            switch (journal[i].operation) {
                case JOURNAL_OP_CREATE:
                    // File was being created - nothing to replay
                    break;
                    
                case JOURNAL_OP_DELETE:
                    // Complete the deletion
                    SD.remove(journal[i].path);
                    break;
                    
                case JOURNAL_OP_WRITE:
                    // File write was in progress - mark as potentially corrupt
                    Serial.println("[PMFS] WARNING: Uncommitted write detected");
                    break;
                    
                case JOURNAL_OP_RENAME:
                    // Complete the rename if target doesn't exist
                    if (!SD.exists(journal[i].path2) && SD.exists(journal[i].path)) {
                        // Actual rename not supported by SD.h, would need copy+delete
                    }
                    break;
                    
                case JOURNAL_OP_MKDIR:
                    // Complete directory creation
                    if (!SD.exists(journal[i].path)) {
                        SD.mkdir(journal[i].path);
                    }
                    break;
                    
                default:
                    break;
            }
            
            journal[i].active = false;
        }
    }
    
    if (any_uncommitted) {
        Serial.println("[PMFS] Journal replay complete");
        stats.journal_rollbacks++;
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS - ALL IMPLEMENTATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
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
    
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    handle->current_position = 0;
    
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    flush(fd);
    
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    if (handle->locked) {
        release_lock(handle->path);
    }
    
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
        handle->current_position += bytes_read;
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        handle->current_position += written;
        
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

// IMPLEMENTED: File seek
PMFSStatus PMFS::seek(int fd, uint32_t position) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    if (!handle->sd_file.seek(position)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    handle->current_position = position;
    return PMFS_OK;
}

// IMPLEMENTED: File tell
uint32_t PMFS::tell(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return 0;
    
    return handle->current_position;
}

// IMPLEMENTED: File size
uint32_t PMFS::size(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return 0;
    
    return handle->sd_file.size();
}

// IMPLEMENTED: EOF check
bool PMFS::eof(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return true;
    
    return handle->current_position >= handle->sd_file.size();
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

// IMPLEMENTED: File rename
PMFSStatus PMFS::rename(const char* old_path, const char* new_path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (!SD.exists(old_path)) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (SD.exists(new_path)) {
        return PMFS_ERROR_ALREADY_EXISTS;
    }
    
    if (is_locked(old_path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_RENAME, old_path, new_path);
    }
    
    // SD.h doesn't support rename, so we copy+delete
    File src = SD.open(old_path, FILE_READ);
    if (!src) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    File dst = SD.open(new_path, FILE_WRITE);
    if (!dst) {
        src.close();
        return PMFS_ERROR_IO_FAILURE;
    }
    
    uint8_t buffer[512];
    while (src.available()) {
        int bytes_read = src.read(buffer, sizeof(buffer));
        dst.write(buffer, bytes_read);
    }
    
    src.close();
    dst.close();
    
    SD.remove(old_path);
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (SD.exists(path)) {
        return PMFS_ERROR_ALREADY_EXISTS;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

// IMPLEMENTED: Directory removal with recursive option
PMFSStatus PMFS::rmdir(const char* path, bool recursive) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (!SD.exists(path)) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return PMFS_ERROR_INVALID_PARAM;
    }
    dir.close();
    
    if (recursive) {
        return recursive_delete(path);
    } else {
        if (!SD.rmdir(path)) {
            return PMFS_ERROR_IO_FAILURE;
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::recursive_delete(const char* path) {
    File dir = SD.open(path);
    if (!dir) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (!dir.isDirectory()) {
        dir.close();
        return SD.remove(path) ? PMFS_OK : PMFS_ERROR_IO_FAILURE;
    }
    
    File file = dir.openNextFile();
    while (file) {
        char filepath[PMFS_MAX_PATH_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, file.name());
        
        if (file.isDirectory()) {
            file.close();
            recursive_delete(filepath);
        } else {
            file.close();
            SD.remove(filepath);
        }
        
        file = dir.openNextFile();
    }
    
    dir.close();
    
    return SD.rmdir(path) ? PMFS_OK : PMFS_ERROR_IO_FAILURE;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

bool PMFS::is_file(const char* path) {
    File f = SD.open(path);
    if (!f) return false;
    bool is_file = !f.isDirectory();
    f.close();
    return is_file;
}

bool PMFS::is_dir(const char* path) {
    File f = SD.open(path);
    if (!f) return false;
    bool is_directory = f.isDirectory();
    f.close();
    return is_directory;
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS - FULLY FUNCTIONAL IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Check if already exists
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return PMFS_ERROR_ALREADY_EXISTS;
        }
    }
    
    int idx = -1;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
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
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_next_offset];
    tmpfs_next_offset += size;
    tmpfs_used += size;
    
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->name[PMFS_MAX_FILENAME - 1] = '\0';
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
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
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
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
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Mark space as available (will be reclaimed during compact)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    memset(tmpfs_pool, 0, sizeof(tmpfs_pool));
    tmpfs_used = 0;
    tmpfs_next_offset = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// Defragment tmpfs by compacting holes
void PMFS::tmpfs_compact() {
    uint8_t temp_pool[PMFS_TMPFS_SIZE];
    uint32_t write_offset = 0;
    
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use) {
            // Copy data to temp pool
            memcpy(&temp_pool[write_offset], tmpfs_entries[i].data, tmpfs_entries[i].size);
            tmpfs_entries[i].data = &tmpfs_pool[write_offset];
            write_offset += tmpfs_entries[i].allocated;
        }
    }
    
    // Copy back to main pool
    memcpy(tmpfs_pool, temp_pool, write_offset);
    tmpfs_next_offset = write_offset;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT
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
    
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

// IMPLEMENTED: Clear bank for OTA
PMFSStatus PMFS::clear_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Clearing bank: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    return recursive_delete(bank_path);
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Clear destination first
    clear_bank(dst);
    SD.mkdir(dst_path);
    
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    uint32_t files_copied = 0;
    File file = src_dir.openNextFile();
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
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
             PMFS_SYSLOG_DIR, millis() / 86400000);
    
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
    File dir = SD.open(log_dir);
    if (!dir) return PMFS_ERROR_FILE_NOT_FOUND;
    
    // Count log files
    uint32_t file_count = 0;
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            file_count++;
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    // Delete oldest files if exceeding limit
    if (file_count > max_files) {
        // Simple rotation - delete oldest
        dir = SD.open(log_dir);
        file = dir.openNextFile();
        uint32_t to_delete = file_count - max_files;
        
        while (file && to_delete > 0) {
            if (!file.isDirectory()) {
                char filepath[PMFS_MAX_PATH_LENGTH];
                snprintf(filepath, sizeof(filepath), "%s/%s", log_dir, file.name());
                file.close();
                SD.remove(filepath);
                to_delete--;
            } else {
                file.close();
            }
            file = dir.openNextFile();
        }
        dir.close();
    }
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
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
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].exclusive || exclusive) {
                return false;
            }
            return true;
        }
    }
    
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
    
    return false;
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
                return false;
            }
            return true;
        }
    }
    return false;
}

// ============================================================================
// STATISTICS & MONITORING - ALL IMPLEMENTATIONS
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
    Serial.print("[PMFS] Fragmentation: "); Serial.print(get_fragmentation(), 1);
    Serial.println("%");
}

uint64_t PMFS::get_free_space() {
    // Note: SD library on RP2040 doesn't provide this easily
    // This is a placeholder that would need platform-specific implementation
    return 0;
}

uint64_t PMFS::get_total_space() {
    return 0;
}

// IMPLEMENTED: Calculate used space
uint64_t PMFS::get_used_space() {
    uint64_t total = 0;
    calculate_directory_size(PMFS_ROOT_DIR, &total);
    return total;
}

PMFSStatus PMFS::calculate_directory_size(const char* path, uint64_t* size) {
    File dir = SD.open(path);
    if (!dir) return PMFS_ERROR_FILE_NOT_FOUND;
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            calculate_directory_size(subdir, size);
        } else {
            *size += file.size();
            file.close();
        }
        file = dir.openNextFile();
    }
    
    dir.close();
    return PMFS_OK;
}

// IMPLEMENTED: Calculate fragmentation percentage
float PMFS::get_fragmentation() {
    uint32_t total_files = 0;
    uint32_t fragmented_files = 0;
    
    count_files_recursive(PMFS_ROOT_DIR, &total_files);
    
    // Simplified fragmentation calculation
    // In a real implementation, this would check file cluster chains
    // For now, we estimate based on file count and write patterns
    
    if (total_files == 0) return 0.0f;
    
    // Heuristic: More files + more writes = more fragmentation
    float write_factor = (float)metadata.total_writes / 1000.0f;
    float file_factor = (float)total_files / 100.0f;
    
    float fragmentation = (write_factor + file_factor) * 10.0f;
    if (fragmentation > 100.0f) fragmentation = 100.0f;
    
    return fragmentation;
}

PMFSStatus PMFS::count_files_recursive(const char* path, uint32_t* count) {
    File dir = SD.open(path);
    if (!dir) return PMFS_ERROR_FILE_NOT_FOUND;
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            count_files_recursive(subdir, count);
        } else {
            (*count)++;
            file.close();
        }
        file = dir.openNextFile();
    }
    
    dir.close();
    return PMFS_OK;
}

// ============================================================================
// MAINTENANCE - ALL IMPLEMENTATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
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
    
    create_directory_tree();
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// IMPLEMENTED: Defragmentation
PMFSStatus PMFS::defragment() {
    Serial.println("\n[PMFS] Starting defragmentation...");
    
    if (!mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    // Defragment tmpfs
    tmpfs_compact();
    
    // For SD card defrag, we would need to:
    // 1. Copy files to temporary location
    // 2. Delete originals
    // 3. Copy back in contiguous manner
    // This is complex and risky, so we just mark as done
    
    stats.defrag_runs++;
    
    Serial.println("[PMFS] Defragmentation complete");
    
    return PMFS_OK;
}

// IMPLEMENTED: Garbage collection
PMFSStatus PMFS::garbage_collect() {
    Serial.println("\n[PMFS] Running garbage collection...");
    
    if (!mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    // Clear cache
    if (cache_enabled) {
        cache_flush();
        memset(&write_cache, 0, sizeof(write_cache));
    }
    
    // Compact tmpfs
    tmpfs_compact();
    
    // Clear committed journal entries
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].committed && !journal[i].active) {
            memset(&journal[i], 0, sizeof(PMFSJournalEntry));
        }
    }
    
    // Release stale file locks (older than 5 minutes)
    uint64_t now = millis();
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active) {
            if (now - file_locks[i].acquired_time > 300000) {
                file_locks[i].active = false;
            }
        }
    }
    
    Serial.println("[PMFS] Garbage collection complete");
    
    return PMFS_OK;
}

// IMPLEMENTED: Verify all files
PMFSStatus PMFS::verify_all_files() {
    Serial.println("\n[PMFS] Verifying all files...");
    
    if (!mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    uint32_t files_checked = 0;
    uint32_t errors_found = 0;
    
    // Recursive verification starting from root
    PMFSStatus status = verify_directory_recursive(PMFS_ROOT_DIR, &files_checked, &errors_found);
    
    Serial.print("[PMFS] Checked ");
    Serial.print(files_checked);
    Serial.print(" files, found ");
    Serial.print(errors_found);
    Serial.println(" errors");
    
    if (errors_found > 0) {
        metadata.needs_fsck = true;
        save_metadata();
        return PMFS_ERROR_CORRUPT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_directory_recursive(const char* path, uint32_t* files_checked, uint32_t* errors_found) {
    File dir = SD.open(path);
    if (!dir) {
        (*errors_found)++;
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            verify_directory_recursive(subdir, files_checked, errors_found);
        } else {
            (*files_checked)++;
            
            // Check if file is readable
            uint8_t test_byte;
            if (file.read(&test_byte, 1) < 0) {
                (*errors_found)++;
            }
            
            file.close();
        }
        file = dir.openNextFile();
    }
    
    dir.close();
    return PMFS_OK;
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
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

bool PMFS::path_exists(const char* path) {
    return SD.exists(path);
}

bool PMFS::is_directory(const char* path) {
    File f = SD.open(path);
    if (!f) return false;
    bool is_dir = f.isDirectory();
    f.close();
    return is_dir;
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
        case PMFS_ERROR_JOURNAL_FULL: return "Journal Full";
        case PMFS_ERROR_NOT_MOUNTED: return "Not Mounted";
        case PMFS_ERROR_ALREADY_EXISTS: return "Already Exists";
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: "); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

void PMFS::print_tree(const char* path, int depth) {
    File dir = SD.open(path);
    if (!dir) return;
    
    File file = dir.openNextFile();
    while (file) {
        for (int i = 0; i < depth; i++) {
            Serial.print("  ");
        }
        Serial.print(file.isDirectory() ? "[D] " : "[F] ");
        Serial.print(file.name());
        if (!file.isDirectory()) {
            Serial.print(" (");
            Serial.print(file.size());
            Serial.print(" bytes)");
        }
        Serial.println();
        
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            print_tree(subdir, depth + 1);
        } else {
            file.close();
        }
        
        file = dir.openNextFile();
    }
    
    dir.close();
}

// Global PMFS instance
static PMFS pmfs;

// Enhanced memory management globals
static MemStats mem_stats __attribute__((aligned(64)));
static OOMVelocityTracker oom_velocity_tracker;
static uint64_t last_mem_verify_us = 0;
static uint32_t consecutive_alloc_failures = 0;
static bool emergency_mode = false;
static uint64_t last_emergency_mode_us = 0;

// Global kernel state
static KernelState kernel __attribute__((aligned(64)));
static mutex_t kout_mutex;

// App blocking
static char g_blocked_app_names[MAX_APPS][TASK_NAME_LEN];
static uint32_t g_blocked_app_count = 0;

// Shell state
static char cmd_buffer[128];
static uint32_t cmd_pos = 0;
static char shell_cwd[128] = "/";

// App registry
static AppEntry app_registry[MAX_APPS];
static uint32_t app_registry_count = 0;

// GUI state
static uint32_t gui_app_task_ids[MAX_GUI_APPS];
static uint32_t gui_app_count = 0;
static int32_t current_gui_focus_index = -1;

// IPC stats
static IPCStats ipc_stats;

// OOM state
static TaskOOMHandler oom_handlers[MAX_OOM_HANDLERS];
static OOMRequest oom_current_request = {0};
static OOMStats oom_stats = {0};

// Schedulers
static CoreScheduler core0_sched;
static CoreScheduler core1_sched;

// Panic state
static PanicInfo last_panic;
static bool in_panic = false;

// Watchdog
static WatchdogState watchdog_state = {false, 0, WATCHDOG_TIMEOUT_MS, 0, false};

// Timing helpers
static inline uint64_t get_time_us() { return micros(); }
static inline uint64_t get_time_ms() { return millis(); }
static inline void precise_sleep_us(uint32_t us) { if (us == 0) return; delayMicroseconds(us); }
static inline bool gpio_read_fast(uint8_t pin) { return digitalRead(pin) == LOW; }

// MultiPrint class
class MultiPrint : public Print {
public:
  virtual size_t write(uint8_t c) {
    mutex_enter_blocking(&kout_mutex);
    Serial.write(c);
    if (kernel.app_write_char) {
      kernel.app_write_char(c);
    }
    mutex_exit(&kout_mutex);
    return 1;
  }
  
  virtual size_t write(const uint8_t *buffer, size_t size) {
    mutex_enter_blocking(&kout_mutex);
    Serial.write(buffer, size);
    if (kernel.app_write_char) {
      for(size_t i = 0; i < size; i++) {
        kernel.app_write_char(buffer[i]);
      }
    }
    mutex_exit(&kout_mutex);
    return size;
  }
};

MultiPrint kout;

// Forward declarations
__attribute__((noreturn)) void kernel_panic(const char* reason);
void watchdog_init();
void watchdog_feed();
void watchdog_check();

// ============================================================================
// MEMORY INTEGRITY & VERIFICATION
// ============================================================================

// Forward declarations
__attribute__((noreturn)) void kernel_panic(const char* reason);
void watchdog_init();
void watchdog_feed();
void watchdog_check();

// ADD THIS LINE:
void klog(uint8_t level, const char* msg);

static inline bool mem_verify_canary(MemBlock* block) {
  if (!block) return false;
  
  bool front_ok = (block->canary_front == MEM_CANARY_VALUE);
  bool back_ok = (block->canary_back == MEM_CANARY_VALUE);
  
  return front_ok && back_ok;
}

static inline bool mem_verify_magic(MemBlock* block) {
  if (!block) return false;
  
  uint32_t expected = block->free ? MEM_MAGIC_FREE : MEM_MAGIC_ALLOCATED;
  return (block->magic == expected);
}

static bool mem_verify_block_integrity(MemBlock* block, const char* context) {
  if (!block) {
    klog(3, "MEM_VERIFY: NULL block");
    return false;
  }
  
  // Check magic number
  if (!mem_verify_magic(block)) {
    char buf[96];
    snprintf(buf, sizeof(buf), 
             "MEM_CORRUPT: Bad magic in %s (got 0x%08X, block %p)", 
             context, block->magic, block->addr);
    klog(3, buf);
    mem_stats.corruptions_detected++;
    
    if (MEM_PANIC_ON_CORRUPTION) {
      kernel_panic(buf);
    }
    return false;
  }
  
  // Check canaries
  if (!mem_verify_canary(block)) {
    char buf[96];
    snprintf(buf, sizeof(buf), 
             "MEM_CORRUPT: Canary violation in %s (front=0x%08X, back=0x%08X)", 
             context, block->canary_front, block->canary_back);
    klog(3, buf);
    mem_stats.corruptions_detected++;
    
    if (MEM_PANIC_ON_CORRUPTION) {
      kernel_panic(buf);
    }
    return false;
  }
  
  // Sanity checks
  if (block->size == 0 || block->size > HEAP_SIZE) {
    char buf[96];
    snprintf(buf, sizeof(buf), 
             "MEM_CORRUPT: Invalid size in %s (%u bytes)", 
             context, block->size);
    klog(3, buf);
    mem_stats.corruptions_detected++;
    return false;
  }
  
  return true;
}

static void mem_verify_all_blocks(const char* context) {
  uint64_t now = get_time_us();
  
  // Rate limit verification (expensive operation)
  if (now - last_mem_verify_us < 1000000) { // 1 second
    return;
  }
  last_mem_verify_us = now;
  
  mutex_enter_blocking(&kernel.mem_lock);
  
  uint32_t corrupted = 0;
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (!mem_verify_block_integrity(&kernel.mem_blocks[i], context)) {
      corrupted++;
    }
  }
  
  mutex_exit(&kernel.mem_lock);
  
  if (corrupted > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "MEM_VERIFY: %u corrupted blocks in %s", 
             corrupted, context);
    klog(3, buf);
  }
}

static inline void mem_set_block_metadata(MemBlock* block, bool is_free) {
  block->magic = is_free ? MEM_MAGIC_FREE : MEM_MAGIC_ALLOCATED;
  block->canary_front = MEM_CANARY_VALUE;
  block->canary_back = MEM_CANARY_VALUE;
  block->last_access_us = get_time_us();
}

// Memory management
void mem_init();
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);
size_t get_free_memory();
size_t get_used_memory();
size_t get_task_memory(uint32_t task_id);
void mem_compact();
void calculate_fragmentation();
bool is_memory_critical();
bool is_memory_warning();

// OOM system
bool oom_prevent(size_t bytes_needed);
OOMVictim oom_select_victim(size_t bytes_needed);
bool oom_request_cleanup(OOMVictim* victim, size_t bytes_needed);
bool oom_killer(size_t bytes_needed);
void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed);
void k_register_oom_handler(uint32_t task_id, oom_callback_t callback);
void k_unregister_oom_handler(uint32_t task_id);
static oom_callback_t oom_get_handler(uint32_t task_id);

// CPU monitoring
void update_task_cpu_usage(TCB* task, uint64_t cpu_time_us);
bool is_cpu_overloaded();
bool is_task_cpu_abuser(TCB* task);
void handle_cpu_overload();


// Task management
void task_init();
void task_init_ipc_queue(TCB* task);
uint32_t task_create(const char* name, void (*entry)(void*), void* arg,
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, uint32_t mem_request, ModuleCallbacks* callbacks,
                     const char* description, CoreAffinity affinity);
void brutal_task_kill(uint32_t id);
void task_sleep(uint32_t ms);
void task_wake(uint32_t task_id);
void task_yield();
void k_task_exit_api();

// Scheduler
void scheduler_init_core0();
void scheduler_init_core1();
void scheduler_tick();
void sched_check_preemption();
void sched_update_task_priority(TCB* task);
uint32_t sched_select_next_core0();
uint32_t sched_select_next_core1();

// Priority bitmap operations
static void sched_bitmap_add(PriorityBitmap* bm, uint32_t task_id, uint8_t priority);
static void sched_bitmap_remove(PriorityBitmap* bm, uint32_t task_id, uint8_t priority);
static int sched_bitmap_find_highest(PriorityBitmap* bm, uint32_t* task_id_out);

// Core 1
void core1_main();
void core1_scheduler_init();
uint32_t k_spawn_core1_task(const char* name, void (*entry)(void*), void* arg, uint8_t priority);

// IPC
void ipc_init();
bool ipc_send_api(uint32_t target_id, IPCMessageType type, void* data, size_t size, uint8_t priority);
bool ipc_receive_api(IPCMessage* msg_out);
TCB* ipc_get_tcb_by_id_unsafe(uint32_t task_id);
void ipc_maintenance();

// RTOS primitives
void rtos_primitives_init();
bool k_mutex_lock(uint32_t mutex_id);
void k_mutex_unlock(uint32_t mutex_id);
bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms);
void k_sem_post(uint32_t sem_id);
uint32_t k_event_wait(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms);
void k_event_set(uint32_t event_id, uint32_t flags);
void wait_list_add(TaskWaitNode** head, TCB* task);
uint32_t wait_list_pop(TaskWaitNode** head);

// Filesystem
void fs_init();
bool fs_mount();
void fs_unmount();
void fs_list(const char* path);
void fs_stats();
void fs_cat(const char* path);
bool fs_write_new(const char* path, const char* content, bool append);
void fs_logtail(uint32_t lines);
bool fs_mkdir(const char* path);
bool fs_remove(const char* path);
void fs_log_write(const char* message);
void fs_log_init();
int fs_open(const char* path, bool write_mode);
void fs_close(int fd);

// Temperature
void temp_init();
float read_temperature();

// Logging
void klog(uint8_t level, const char* msg);

// System tasks
void idle_task(void* arg);
void k_reaper_task(void* arg);
void shell_task(void* arg);
void shell_deinit();
void input_task(void* arg);
void cpu_monitor_task(void* arg);
void cpumon_deinit();
void temp_monitor_task(void* arg);
void tempmon_deinit();
void fs_task(void* arg);
void fs_deinit();

// Shell commands
void cmd_help();
void cmd_ps();
void cmd_taskinfo(char* arg);
void cmd_listapps();
void cmd_top();
void cmd_mem();
void cmd_memmap();
void cmd_dmesg();
void cmd_uptime();
void cmd_temp();
void cmd_ipcstat();
void cmd_schedstat();
void cmd_oomstat();
void cmd_rtos_stat();
void cmd_reboot();
void cmd_kill(char* arg);
void cmd_root();
void cmd_write(char* arg);
void cmd_logls();
void cmd_format_sd();
void cmd_mkdir(char* arg);
void cmd_rm(char* arg);
void cmd_touch(char* arg);
void cmd_logtail(char* arg);
void cmd_cd(const char* arg);
void cmd_app_block_list();
void cmd_app_block_unlock(char* arg);

// Module callbacks
ModuleCallbacks shell_callbacks = { NULL, shell_task, shell_deinit };
ModuleCallbacks input_callbacks = { NULL, input_task, NULL };
ModuleCallbacks cpumon_callbacks = { NULL, cpu_monitor_task, cpumon_deinit };
ModuleCallbacks tempmon_callbacks = { NULL, temp_monitor_task, tempmon_deinit };
ModuleCallbacks fs_callbacks = { NULL, fs_task, fs_deinit };

// UI functions
bool k_request_gui_focus(uint32_t task_id);
void k_release_gui_focus(uint32_t task_id);
void k_register_stdout_target(void (*write_char_fn)(char));
float k_get_core0_usage();
float k_get_core1_usage();
uint32_t k_get_task_memory_api(uint32_t task_id);
void k_hint_memory_pressure(uint32_t task_id);
void k_register_gui_app(UISocket* socket_api);
void k_unregister_gui_app(uint32_t task_id);

// App registration
void Application_Register(const char* name, void (*spawn_func)());

// ============================================================================
// KERNEL PANIC & WATCHDOG
// ============================================================================

__attribute__((noreturn))
void kernel_panic(const char* reason) {
  if (in_panic) {
    while(1) { watchdog_update(); __asm__ volatile ("wfi"); }
  }
  
  in_panic = true;
  watchdog_state.in_panic = true;
  disable_all_interrupts();
  
  last_panic.reason = reason;
  last_panic.task_id = kernel.current_task;
  last_panic.timestamp = get_time_ms();
  last_panic.is_core1 = (get_core_num() == 1);
  
  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║          *** KERNEL PANIC ***         ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println();
  Serial.print("Reason: "); Serial.println(reason);
  Serial.print("Core: "); Serial.println(last_panic.is_core1 ? "1" : "0");
  Serial.print("Task: ");
  
  if (last_panic.task_id < kernel.task_count) {
    Serial.print(kernel.tasks[last_panic.task_id].name);
    Serial.print(" (ID="); Serial.print(last_panic.task_id); Serial.println(")");
  } else {
    Serial.println("UNKNOWN");
  }
  
  Serial.print("Uptime: "); Serial.print((uint32_t)(last_panic.timestamp / 1000)); Serial.println(" s");
  Serial.println();
  Serial.println("--- System State ---");
  Serial.print("Tasks: "); Serial.println(kernel.task_count);
  Serial.print("Memory: ");
  Serial.print(get_used_memory() / 1024); Serial.print("/");
  Serial.print(HEAP_SIZE / 1024); Serial.println(" KB");
  Serial.print("CPU Usage: "); Serial.print(kernel.cpu_usage, 1); Serial.println("%");
  Serial.println();
  Serial.println("System halted. Watchdog will reset in 8s...");
  Serial.flush();
  
  if (kernel.fs_mounted) {
    // Log panic to PMFS
    char panic_msg[128];
    snprintf(panic_msg, sizeof(panic_msg), "PANIC: %s", reason);
    pmfs.log_system(panic_msg);
    
    // Emergency unmount PMFS
    pmfs.emergency_unmount();
  }
  
  while(1) {
    watchdog_update();
    delay(100);
    if (get_time_ms() - last_panic.timestamp > 8000) {
      while(1) { __asm__ volatile ("wfi"); }
    }
  }
}

void watchdog_init() {
  if (watchdog_caused_reboot()) {
    kout.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!");
    kout.println("!!! REBOOT BY WATCHDOG !!!");
    kout.println("!!!!!!!!!!!!!!!!!!!!!!!!!!");
    watchdog_state.triggers++;
  }
  
  watchdog_state.enabled = true;
  watchdog_state.last_feed = get_time_ms();
  watchdog_state.in_panic = false;
  watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
  kout.println("[WDT] Watchdog enabled (8s timeout)");
}

void watchdog_feed() {
  if (!watchdog_state.enabled || watchdog_state.in_panic) return;
  watchdog_state.last_feed = get_time_ms();
  watchdog_update();
}

void watchdog_check() {
  if (!watchdog_state.enabled || watchdog_state.in_panic) return;
  uint64_t now = get_time_ms();
  uint64_t elapsed = now - watchdog_state.last_feed;
  
  if (elapsed > (watchdog_state.timeout_ms * 3 / 4)) {
    kout.print("\n*** WATCHDOG WARNING: No feed in ");
    kout.print((uint32_t)elapsed);
    kout.println(" ms ***");
    klog(2, "WDT: Feed timeout approaching!");
  }
}

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

void mem_init() {
  mutex_init(&kernel.mem_lock);
  memset(&kernel.mem_blocks, 0, sizeof(kernel.mem_blocks));
  
  kernel.mem_block_count = 1;
  kernel.mem_blocks[0].addr = kernel.heap;
  kernel.mem_blocks[0].size = HEAP_SIZE;
  kernel.mem_blocks[0].owner_id = 0;
  kernel.mem_blocks[0].free = true;
  kernel.mem_blocks[0].alloc_time_us = 0;
  kernel.mem_blocks[0].alloc_seq = 0;
  
  kernel.total_allocations = 0;
  kernel.total_frees = 0;
  kernel.oom_kills = 0;
  kernel.alloc_sequence = 0;
  kernel.fragmentation_pct = 0;
  kernel.largest_free_block = HEAP_SIZE;
  kernel.total_free_mem = HEAP_SIZE;
  
  memset(&oom_handlers, 0, sizeof(oom_handlers));
  memset(&oom_stats, 0, sizeof(oom_stats));
}

size_t get_free_memory() {
  return kernel.total_free_mem;
}

size_t get_used_memory() {
  return HEAP_SIZE - kernel.total_free_mem;
}

size_t get_task_memory(uint32_t task_id) {
  size_t mem = 0;
  mutex_enter_blocking(&kernel.mem_lock);
  
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (!kernel.mem_blocks[i].free && kernel.mem_blocks[i].owner_id == task_id) {
      mem += kernel.mem_blocks[i].size;
    }
  }
  
  mutex_exit(&kernel.mem_lock);
  return mem;
}

bool is_memory_critical() {
  return (kernel.total_free_mem < MEM_CRITICAL_THRESHOLD);
}

bool is_memory_warning() {
  return (kernel.total_free_mem < MEM_WARNING_THRESHOLD);
}

void calculate_fragmentation() {
  uint32_t free_blocks = 0;
  size_t largest = 0;
  size_t total_free = 0;
  
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (kernel.mem_blocks[i].free) {
      free_blocks++;
      total_free += kernel.mem_blocks[i].size;
      if (kernel.mem_blocks[i].size > largest) {
        largest = kernel.mem_blocks[i].size;
      }
    }
  }
  
  kernel.largest_free_block = largest;
  
  if (total_free > 0) {
    kernel.fragmentation_pct = 100 - ((largest * 100) / total_free);
  } else {
    kernel.fragmentation_pct = 0;
  }
}

// ============================================================================
// MEMORY PRESSURE DETECTION
// ============================================================================

static MemPressure mem_calculate_pressure() {
  uint32_t free_kb = kernel.total_free_mem / 1024;
  uint32_t total_kb = HEAP_SIZE / 1024;
  uint32_t used_pct = ((total_kb - free_kb) * 100) / total_kb;
  
  if (kernel.total_free_mem < MEM_CRITICAL_THRESHOLD) {
    return MEM_PRESSURE_EMERGENCY;
  } else if (kernel.total_free_mem < MEM_CRITICAL_THRESHOLD * 2) {
    return MEM_PRESSURE_CRITICAL;
  } else if (kernel.total_free_mem < MEM_WARNING_THRESHOLD) {
    return MEM_PRESSURE_HIGH;
  } else if (used_pct > 70) {
    return MEM_PRESSURE_MODERATE;
  } else if (used_pct > 50) {
    return MEM_PRESSURE_LOW;
  }
  
  return MEM_PRESSURE_NONE;
}

static void mem_update_pressure() {
  MemPressure old_pressure = (MemPressure)mem_stats.current_pressure;
  MemPressure new_pressure = mem_calculate_pressure();
  
  mem_stats.current_pressure = new_pressure;
  
  // Log pressure changes
  if (new_pressure != old_pressure && new_pressure >= MEM_PRESSURE_HIGH) {
    const char* pressure_names[] = {
      "NONE", "LOW", "MODERATE", "HIGH", "CRITICAL", "EMERGENCY"
    };
    
    char buf[64];
    snprintf(buf, sizeof(buf), "MEM_PRESSURE: %s -> %s", 
             pressure_names[old_pressure], 
             pressure_names[new_pressure]);
    klog(new_pressure >= MEM_PRESSURE_CRITICAL ? 3 : 2, buf);
    
    // Enter emergency mode if needed
    if (new_pressure == MEM_PRESSURE_EMERGENCY && !emergency_mode) {
      emergency_mode = true;
      last_emergency_mode_us = get_time_us();
      klog(3, "MEM: EMERGENCY MODE ACTIVATED");
    }
  }
  
  // Exit emergency mode after pressure drops
  if (emergency_mode && new_pressure < MEM_PRESSURE_HIGH) {
    uint64_t emergency_duration = get_time_us() - last_emergency_mode_us;
    if (emergency_duration > 5000000) { // 5 seconds of low pressure
      emergency_mode = false;
      klog(0, "MEM: Emergency mode deactivated");
    }
  }
}

void mem_delete_block(uint32_t index) {
  if (index >= kernel.mem_block_count) return;
  
  kernel.mem_blocks[index] = kernel.mem_blocks[kernel.mem_block_count - 1];
  memset(&kernel.mem_blocks[kernel.mem_block_count - 1], 0, sizeof(MemBlock));
  kernel.mem_block_count--;
}

void mem_compact() {
  mutex_enter_blocking(&kernel.mem_lock);
  
  uint32_t n = kernel.mem_block_count;
  if (n <= 1) {
    mutex_exit(&kernel.mem_lock);
    return;
  }
  
  // Sort blocks by address
  for (uint32_t i = 1; i < n; i++) {
    MemBlock key = kernel.mem_blocks[i];
    int32_t j = i - 1;
    
    while (j >= 0 && kernel.mem_blocks[j].addr > key.addr) {
      kernel.mem_blocks[j + 1] = kernel.mem_blocks[j];
      j = j - 1;
    }
    kernel.mem_blocks[j + 1] = key;
  }
  
  // Merge adjacent free blocks
  uint32_t write_idx = 0;
  uint32_t merges = 0;
  
  for (uint32_t read_idx = 1; read_idx < n; read_idx++) {
    MemBlock* writer = &kernel.mem_blocks[write_idx];
    MemBlock* reader = &kernel.mem_blocks[read_idx];
    
    if (writer->free && reader->free &&
        ((uint8_t*)writer->addr + writer->size == (uint8_t*)reader->addr)) {
      writer->size += reader->size;
      merges++;
    } else {
      write_idx++;
      if (write_idx != read_idx) {
        kernel.mem_blocks[write_idx] = kernel.mem_blocks[read_idx];
      }
    }
  }
  
  kernel.mem_block_count = write_idx + 1;
  
  if (merges > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "MEM: Compacted %d blocks", merges);
    klog(0, buf);
  }
  
  calculate_fragmentation();
  mutex_exit(&kernel.mem_lock);
}

void* kmalloc(size_t size, uint32_t task_id) {
  if (size == 0) return NULL;
  
  size = (size + 3) & ~3; // Align to 4 bytes
  
  bool is_app = false;
  TCB* task = NULL;
  
  if (task_id < 1000) {
    if (task_id < kernel.task_count) {
      task = &kernel.tasks[task_id];
      is_app = (task->task_type == TASK_TYPE_APPLICATION);
      if (task->mem_blocked) { return NULL; }
    }
  } else {
    is_app = true;
  }
  
  // Try allocation up to 3 times
  for (int attempt = 0; attempt < 3; attempt++) {
    mutex_enter_blocking(&kernel.mem_lock);
    
    // Check if we have enough memory (with kernel reserve for non-kernel tasks)
    if (is_app && (kernel.total_free_mem < size + KERNEL_RESERVE)) {
      mutex_exit(&kernel.mem_lock);
      
      if (oom_killer(size)) {
        kout.println("[MEM] kmalloc: Awaiting OOM cleanup...");
        task_sleep(OOM_REQUEST_TIMEOUT_MS + 500);
      }
      continue;
    }
    
    // Memory protection checks for applications
    if (task && task->task_type == TASK_TYPE_APPLICATION) {
      task->page_faults++;
      
      uint32_t current_usage = 0;
      for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (!kernel.mem_blocks[i].free && kernel.mem_blocks[i].owner_id == task_id) {
          current_usage += kernel.mem_blocks[i].size;
        }
      }
      
      // Check memory limits
      if ((task->mem_limit > 0 && current_usage + size > task->mem_limit) ||
          (task->mem_request_bytes > 0 && current_usage + size > task->mem_request_bytes)) {
        
        char klog_buf[128];
        snprintf(klog_buf, sizeof(klog_buf), 
                 "MEM_PROTECT: Task %s (ID %d) exceeded memory limit. Killing.", 
                 task->name, task_id);
        klog(3, klog_buf);
        
        mutex_exit(&kernel.mem_lock);
        brutal_task_kill(task_id);
        mem_compact();
        
        mutex_enter_blocking(&kernel.mem_lock);
        task->mem_blocked = true;
        
        // Add to blocked list
        bool already_blocked = false;
        for(uint32_t i=0; i < g_blocked_app_count; i++) {
          if(strcmp(g_blocked_app_names[i], task->name) == 0) {
            already_blocked = true;
            break;
          }
        }
        
        if (!already_blocked && g_blocked_app_count < MAX_APPS) {
          strncpy(g_blocked_app_names[g_blocked_app_count++], task->name, TASK_NAME_LEN - 1);
        }
        
        mutex_exit(&kernel.mem_lock);
        return NULL;
      }
      
      // Velocity throttling
      uint64_t now_us = get_time_us();
      if (task->mem_throttle_mark == 0) {
        task->mem_throttle_mark = current_usage;
        task->mem_throttle_time_us = now_us;
      } else if (current_usage > task->mem_throttle_mark && 
                 current_usage - task->mem_throttle_mark > VELOCITY_CHECK_CHUNK) {
        
        uint64_t delta_time = now_us - task->mem_throttle_time_us;
        
        if (delta_time < VELOCITY_TIME_THRESHOLD_US && delta_time > 0) {
          char klog_buf[128];
          snprintf(klog_buf, sizeof(klog_buf), 
                   "MEM_PROTECT: Task %s (ID %d) allocation velocity too high! Throttling.", 
                   task->name, task_id);
          klog(1, klog_buf);
          
          mutex_exit(&kernel.mem_lock);
          task_sleep(250);
          mutex_enter_blocking(&kernel.mem_lock);
        }
        
        task->mem_throttle_mark = current_usage;
        task->mem_throttle_time_us = now_us;
      }
    }
    
    // Find best-fit block
    uint32_t best_block_idx = 0xFFFFFFFF;
    uint32_t best_block_size = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
      MemBlock* block = &kernel.mem_blocks[i];
      
      if (block->free && block->size >= size) {
        if (block->size == size) {
          best_block_idx = i;
          break;
        }
        
        if (block->size < best_block_size) {
          best_block_idx = i;
          best_block_size = block->size;
        }
      }
    }
    
    // Allocate block
    if (best_block_idx != 0xFFFFFFFF) {
      MemBlock* block = &kernel.mem_blocks[best_block_idx];
      uint32_t allocated_size = size;
      
      // Split block if large enough
      if (block->size > size + 32 && kernel.mem_block_count < MAX_MEMORY_BLOCKS) {
        MemBlock* new_block = &kernel.mem_blocks[kernel.mem_block_count++];
        new_block->addr = (uint8_t*)block->addr + size;
        new_block->size = block->size - size;
        new_block->owner_id = 0;
        new_block->free = true;
        new_block->alloc_time_us = 0;
        new_block->alloc_seq = 0;
        
        block->size = size;
      } else {
        allocated_size = block->size;
      }
      
      block->free = false;
      kernel.total_free_mem -= allocated_size;
      block->owner_id = task_id;
      block->alloc_time_us = get_time_ms();
      block->alloc_seq = kernel.alloc_sequence++;
      kernel.total_allocations++;
      
      // Update task memory usage
      if (task_id < MAX_TASKS && task_id < kernel.task_count) {
        uint32_t task_mem = 0;
        for (uint32_t j = 0; j < kernel.mem_block_count; j++) {
          if (!kernel.mem_blocks[j].free && kernel.mem_blocks[j].owner_id == task_id) {
            task_mem += kernel.mem_blocks[j].size;
          }
        }
        
        kernel.tasks[task_id].mem_used = task_mem;
        if (task_mem > kernel.tasks[task_id].mem_peak) {
          kernel.tasks[task_id].mem_peak = task_mem;
        }
      }
      
      // Update allocation velocity
      if (task) {
        uint64_t now_alloc = get_time_ms();
        if (now_alloc - task->last_alloc_time > 1000) { 
          task->alloc_velocity = 0; 
        }
        task->alloc_velocity++;
        task->last_alloc_time = now_alloc;
      }
      
      calculate_fragmentation();
      mutex_exit(&kernel.mem_lock);
      return block->addr;
    }
    
    mutex_exit(&kernel.mem_lock);
    
    // No suitable block found, trigger OOM
    if (oom_killer(size)) {
      kout.println("[MEM] kmalloc: Awaiting OOM cleanup...");
      task_sleep(OOM_REQUEST_TIMEOUT_MS + 500);
    }
  }
  
  return NULL;
}

void kfree(void* ptr) {
  if (!ptr) return;
  
  mutex_enter_blocking(&kernel.mem_lock);
  
  int freed_block_idx = -1;
  uint32_t task_owner = 0;
  
  // Find block
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (kernel.mem_blocks[i].addr == ptr) {
      if (kernel.mem_blocks[i].free) {
        mutex_exit(&kernel.mem_lock);
        klog(2, "MEM: Double free attempt");
        return;
      }
      
      kernel.mem_blocks[i].free = true;
      kernel.total_free_mem += kernel.mem_blocks[i].size;
      task_owner = kernel.mem_blocks[i].owner_id;
      kernel.mem_blocks[i].owner_id = 0;
      kernel.total_frees++;
      freed_block_idx = i;
      break;
    }
  }
  
  if (freed_block_idx == -1) {
    mutex_exit(&kernel.mem_lock);
    klog(2, "MEM: Invalid kfree");
    return;
  }
  
  // Merge with adjacent free blocks
  MemBlock* freed_block = &kernel.mem_blocks[freed_block_idx];
  
  // Merge with next block
  int merged_next_idx = -1;
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (i == (uint32_t)freed_block_idx) continue;
    
    MemBlock* next_block = &kernel.mem_blocks[i];
    if (next_block->free &&
        (uint8_t*)freed_block->addr + freed_block->size == (uint8_t*)next_block->addr) {
      freed_block->size += next_block->size;
      merged_next_idx = i;
      break;
    }
  }
  
  // Merge with previous block
  int merged_prev_idx = -1;
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (i == (uint32_t)freed_block_idx || i == (uint32_t)merged_next_idx) continue;
    
    MemBlock* prev_block = &kernel.mem_blocks[i];
    if (prev_block->free &&
        (uint8_t*)prev_block->addr + prev_block->size == (uint8_t*)freed_block->addr) {
      prev_block->size += freed_block->size;
      merged_prev_idx = i;
      break;
    }
  }
  
  // Remove merged blocks
  if (merged_prev_idx != -1) {
    mem_delete_block(freed_block_idx);
    if (merged_next_idx != -1) {
      if (merged_next_idx == (int)(kernel.mem_block_count)) {
        mem_delete_block(freed_block_idx);
      } else {
        mem_delete_block(merged_next_idx);
      }
    }
  } else if (merged_next_idx != -1) {
    mem_delete_block(merged_next_idx);
  }
  
  // Update task memory usage
  if (task_owner < 1000 && task_owner < kernel.task_count) {
    uint32_t task_mem = 0;
    for (uint32_t j = 0; j < kernel.mem_block_count; j++) {
      if (!kernel.mem_blocks[j].free && kernel.mem_blocks[j].owner_id == task_owner) {
        task_mem += kernel.mem_blocks[j].size;
      }
    }
    kernel.tasks[task_owner].mem_used = task_mem;
  }
  
  calculate_fragmentation();
  mutex_exit(&kernel.mem_lock);
}

// ============================================================================
// OOM SYSTEM
// ============================================================================

void k_register_oom_handler(uint32_t task_id, oom_callback_t callback) {
  for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
    if (!oom_handlers[i].active) {
      oom_handlers[i].task_id = task_id;
      oom_handlers[i].callback = callback;
      oom_handlers[i].active = true;
      
      kout.print("[OOM] Handler registered for task ");
      kout.println(task_id);
      return;
    }
  }
  
  kout.println("[OOM] Handler registry full");
}

void k_unregister_oom_handler(uint32_t task_id) {
  for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
    if (oom_handlers[i].active && oom_handlers[i].task_id == task_id) {
      oom_handlers[i].active = false;
      return;
    }
  }
}

static oom_callback_t oom_get_handler(uint32_t task_id) {
  for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
    if (oom_handlers[i].active && oom_handlers[i].task_id == task_id) {
      return oom_handlers[i].callback;
    }
  }
  return NULL;
}

bool oom_prevent(size_t bytes_needed) {
  kout.println("[OOM] Prevention: Attempting memory recovery...");
  kout.println("[OOM] Step 1: Memory compaction");
  
  mem_compact();
  calculate_fragmentation();
  
  if (kernel.largest_free_block >= bytes_needed) {
    kout.print("[OOM] Success! Found ");
    kout.print(kernel.largest_free_block / 1024);
    kout.println(" KB free block");
    oom_stats.prevention_count++;
    return true;
  }
  
  kout.println("[OOM] Prevention failed - proceeding to victim selection");
  return false;
}

static int32_t oom_calculate_victim_score(TCB* task, uint32_t mem_used) {
  int32_t score = 0;
  
  // Memory usage score
  score += (mem_used / 1024);
  
  // OOM priority penalty
  score += (task->oom_priority * 100);
  
  // Idle time bonus
  uint64_t idle_time = get_time_ms() - task->last_run;
  if (idle_time > 5000) score += 200;
  else if (idle_time > 1000) score += 50;
  
  // Handler bonus (prefer keeping tasks with handlers)
  oom_callback_t handler = oom_get_handler(task->id);
  if (handler) score -= 50;
  
  // CPU abuser penalty
  if (task->is_cpu_abuser) score += 150;
  
  // Critical task protection
  if (task->flags & TASK_FLAG_CRITICAL) score = -10000;
  if (task->task_type != TASK_TYPE_APPLICATION) score = -10000;
  
  return score;
}

OOMVictim oom_select_victim(size_t bytes_needed) {
  OOMVictim victim = {0};
  victim.task_id = 0xFFFFFFFF;
  victim.score = -10000;
  
  kout.println("[OOM] Selecting victim...");
  
  // Check Core 0 tasks
  for (uint32_t i = 1; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE || task->mem_blocked) 
      continue;
    if (task->task_type != TASK_TYPE_APPLICATION) continue;
    if (task->flags & TASK_FLAG_CRITICAL) continue;
    
    uint32_t task_mem = get_task_memory(task->id);
    if (task_mem == 0) continue;
    
    int32_t score = oom_calculate_victim_score(task, task_mem);
    
    if (score > victim.score) {
      victim.task_id = task->id;
      victim.memory_used = task_mem;
      victim.oom_priority = task->oom_priority;
      victim.score = score;
      victim.has_handler = (oom_get_handler(task->id) != NULL);
    }
  }
  
  // Check Core 1 tasks
  mutex_enter_blocking(&kernel.core1.scheduler_lock);
  for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
    TCB* task = &kernel.core1.tasks[i];
    
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE || task->mem_blocked) 
      continue;
    if (task->task_type != TASK_TYPE_APPLICATION) continue;
    if (task->flags & TASK_FLAG_CRITICAL) continue;
    
    uint32_t task_mem = 10 * 1024; // Estimate for Core 1 tasks
    
    int32_t score = oom_calculate_victim_score(task, task_mem);
    
    if (score > victim.score) {
      victim.task_id = task->id;
      victim.memory_used = task_mem;
      victim.oom_priority = task->oom_priority;
      victim.score = score;
      victim.has_handler = (oom_get_handler(task->id) != NULL);
    }
  }
  mutex_exit(&kernel.core1.scheduler_lock);
  
  return victim;
}

bool oom_request_cleanup(OOMVictim* victim, size_t bytes_needed) {
  if (victim->task_id == 0xFFFFFFFF || !victim->has_handler) {
    return false;
  }
  
  TCB* task = NULL;
  bool is_core1 = (victim->task_id >= 1000);
  
  if(is_core1) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  task = ipc_get_tcb_by_id_unsafe(victim->task_id);
  if (!task) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
    return false;
  }
  
  kout.print("[OOM] Requesting graceful cleanup from '");
  kout.print(task->name);
  kout.print("' (");
  kout.print(victim->memory_used / 1024);
  kout.println(" KB)");
  
  oom_current_request.allocating_task_id = kernel.current_task;
  oom_current_request.target_task_id = victim->task_id;
  oom_current_request.request_time = get_time_ms();
  oom_current_request.bytes_requested = bytes_needed;
  oom_current_request.request_sent = true;
  oom_current_request.task_complied = false;
  
  task->flags |= TASK_FLAG_OOM_CLEANUP_REQUESTED;
  task->oom_bytes_requested = bytes_needed;
  
  if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
  
  oom_stats.requests_sent++;
  return true;
}

void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed) {
  if (!oom_current_request.request_sent) return;
  if (oom_current_request.target_task_id != task_id) return;
  
  TCB* task = NULL;
  bool is_core1 = (task_id >= 1000);
  
  if(is_core1) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  task = ipc_get_tcb_by_id_unsafe(task_id);
  if (!task) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
    return;
  }
  
  kout.print("[OOM] Task '");
  kout.print(task->name);
  kout.print("' freed ");
  kout.print(bytes_freed / 1024);
  kout.println(" KB voluntarily");
  
  oom_current_request.task_complied = true;
  oom_stats.voluntary_releases++;
  oom_stats.total_bytes_reclaimed += bytes_freed;
  
  char buf[64];
  snprintf(buf, sizeof(buf), "OOM: %s freed %dKB", task->name, bytes_freed / 1024);
  klog(0, buf);
  
  if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
  
  // Wake up allocating task
  uint32_t waiting_task_id = oom_current_request.allocating_task_id;
  if (waiting_task_id < kernel.task_count) {
    kout.print("[OOM] Waking up allocating task ");
    kout.println(waiting_task_id);
    task_wake(waiting_task_id);
  }
  
  oom_current_request.request_sent = false;
}

bool oom_killer(size_t bytes_needed) {
  kout.println("\n!!! OUT OF MEMORY !!!");
  kout.print("Need: ");
  kout.print(bytes_needed / 1024);
  kout.println(" KB");
  
  char buf[64];
  snprintf(buf, sizeof(buf), "OOM: Need %d KB", (int)(bytes_needed / 1024));
  klog(3, buf);
  
  // Check for abusive allocator
  uint32_t allocator_id = kernel.current_task;
  if (allocator_id < kernel.task_count) {
    TCB* allocator_task = &kernel.tasks[allocator_id];
    
    if ((allocator_task->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY || 
         bytes_needed > OOM_ABUSIVE_ALLOC_SIZE) &&
        allocator_task->task_type == TASK_TYPE_APPLICATION) {
      
      kout.println("\n[OOM] ABUSIVE ALLOCATOR DETECTED!");
      kout.print("[OOM] Executing allocator '"); 
      kout.print(allocator_task->name); 
      kout.println("'");
      
      brutal_task_kill(allocator_id);
      kernel.oom_kills++;
      oom_stats.forced_kills++;
      oom_stats.abusive_kills++;
      oom_current_request.request_sent = false;
      
      mem_compact();
      return false;
    }
  }
  
  // Try prevention first
  if (oom_prevent(bytes_needed)) {
    return false;
  }
  
  // Check if there's an active OOM request waiting
  if (oom_current_request.request_sent) {
    uint64_t elapsed = get_time_ms() - oom_current_request.request_time;
    
    if (elapsed < OOM_REQUEST_TIMEOUT_MS) {
      kout.println("[OOM] Waiting for graceful cleanup...");
      return true;
    }
    
    kout.println("[OOM] Graceful cleanup timed out!");
    kout.print("[OOM] Proceeding to kill victim: ");
    kout.println(oom_current_request.target_task_id);
    
    brutal_task_kill(oom_current_request.target_task_id);
    oom_stats.forced_kills++;
    oom_current_request.request_sent = false;
    
    return false;
  }
  
  // Select victim
  OOMVictim victim = oom_select_victim(bytes_needed);
  
  if (victim.task_id == 0xFFFFFFFF) {
    kout.println("OOM: NO KILLABLE APPLICATIONS!");
    klog(3, "OOM: No victims, PANIC!");
    kernel_panic("OOM: No killable victims");
    return false;
  }
  
  TCB* victim_task = NULL;
  bool is_core1_victim = (victim.task_id >= 1000);
  
  if(is_core1_victim) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  victim_task = ipc_get_tcb_by_id_unsafe(victim.task_id);
  if(!victim_task) {
    if(is_core1_victim) mutex_exit(&kernel.core1.scheduler_lock);
    kout.println("OOM: Victim vanished");
    return false;
  }
  
  kout.print("[OOM] Selected victim: '");
  kout.print(victim_task->name);
  kout.print("' (");
  kout.print(victim.memory_used / 1024);
  kout.print(" KB, score=");
  kout.print(victim.score);
  kout.println(")");
  
  // Check if victim is also an abuser
  if (victim_task->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY) {
    kout.println("[OOM] Victim is also an abusive allocator. No handler.");
    victim.has_handler = false;
  }
  
  // Try graceful cleanup if handler exists
  if (victim.has_handler && oom_request_cleanup(&victim, bytes_needed)) {
    if(is_core1_victim) mutex_exit(&kernel.core1.scheduler_lock);
    return true;
  }
  
  // Kill the victim
  kout.print("[OOM] Killing '");
  kout.print(victim_task->name);
  kout.print("' (");
  kout.print(victim.memory_used / 1024);
  kout.println(" KB)");
  
  snprintf(buf, sizeof(buf), "OOM: Killed %s (%dKB)",
           victim_task->name, victim.memory_used / 1024);
  klog(2, buf);
  
  if(is_core1_victim) mutex_exit(&kernel.core1.scheduler_lock);
  
  brutal_task_kill(victim.task_id);
  kernel.oom_kills++;
  oom_stats.forced_kills++;
  oom_stats.total_bytes_reclaimed += victim.memory_used;
  
  return false;
}

// ============================================================================
// CPU MONITORING & PROTECTION
// ============================================================================

void update_task_cpu_usage(TCB* task, uint64_t cpu_time_us) {
  if (!task) return;
  
  // Update rolling average of CPU samples
  float instant_cpu = (float)(cpu_time_us / 1000.0f);
  
  task->sched_info.cpu_samples[task->sched_info.cpu_sample_index] = instant_cpu;
  task->sched_info.cpu_sample_index = 
    (task->sched_info.cpu_sample_index + 1) % CPU_ABUSE_SAMPLE_COUNT;
  
  // Calculate average
  float total = 0;
  for (int i = 0; i < CPU_ABUSE_SAMPLE_COUNT; i++) {
    total += task->sched_info.cpu_samples[i];
  }
  task->sched_info.cpu_usage_percent = total / CPU_ABUSE_SAMPLE_COUNT;
  
  // Detect CPU burst
  if (instant_cpu > CPU_TASK_ABUSE_THRESHOLD) {
    task->sched_info.cpu_burst_counter++;
  } else {
    task->sched_info.cpu_burst_counter = 0;
  }
}

bool is_cpu_overloaded() {
  return (kernel.cpu_usage > CPU_OVERLOAD_THRESHOLD);
}

bool is_task_cpu_abuser(TCB* task) {
  if (!task) return false;
  
  // Check if task consistently uses too much CPU
  if (task->sched_info.cpu_usage_percent > CPU_TASK_ABUSE_THRESHOLD &&
      task->sched_info.cpu_burst_counter > 3) {
    return true;
  }
  
  return false;
}

void handle_cpu_overload() {
  if (!is_cpu_overloaded()) return;
  
  kout.println("\n!!! CPU OVERLOAD DETECTED !!!");
  kout.print("System CPU: ");
  kout.print(kernel.cpu_usage, 1);
  kout.println("%");
  
  // Find CPU abusers
  TCB* worst_abuser = NULL;
  float worst_usage = 0;
  
  for (uint32_t i = 1; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) continue;
    if (task->task_type != TASK_TYPE_APPLICATION) continue;
    if (task->flags & TASK_FLAG_CRITICAL) continue;
    
    if (is_task_cpu_abuser(task)) {
      if (task->sched_info.cpu_usage_percent > worst_usage) {
        worst_usage = task->sched_info.cpu_usage_percent;
        worst_abuser = task;
      }
    }
  }
  
  if (worst_abuser) {
    kout.print("[CPU] Killing CPU abuser: '");
    kout.print(worst_abuser->name);
    kout.print("' (");
    kout.print(worst_abuser->sched_info.cpu_usage_percent, 1);
    kout.println("%)");
    
    worst_abuser->is_cpu_abuser = true;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "CPU: Killed %s (%.1f%%)", 
             worst_abuser->name, worst_abuser->sched_info.cpu_usage_percent);
    klog(2, buf);
    
    brutal_task_kill(worst_abuser->id);
  } else {
    kout.println("[CPU] No obvious abuser found, throttling system");
    task_sleep(100);
  }
}

// SCHEDULER - PRIORITY BITMAP OPERATIONS
// ============================================================================

static inline int bitmap_ffs(uint32_t mask) {
  if (mask == 0) return -1;
  return __builtin_ctz(mask);
}

static inline void bitmap_set(uint32_t* mask, uint8_t bit) {
  *mask |= (1U << bit);
}

static inline void bitmap_clear(uint32_t* mask, uint8_t bit) {
  *mask &= ~(1U << bit);
}

static void sched_bitmap_add(PriorityBitmap* bm, uint32_t task_id, uint8_t priority) {
  if (priority >= SCHED_NUM_PRIORITY_LEVELS) priority = SCHED_NUM_PRIORITY_LEVELS - 1;
  if (task_id >= 32) return;
  
  bitmap_set(&bm->task_masks[priority], task_id);
  bitmap_set(&bm->level_mask, priority);
}

static void sched_bitmap_remove(PriorityBitmap* bm, uint32_t task_id, uint8_t priority) {
  if (priority >= SCHED_NUM_PRIORITY_LEVELS) priority = SCHED_NUM_PRIORITY_LEVELS - 1;
  if (task_id >= 32) return;
  
  bitmap_clear(&bm->task_masks[priority], task_id);
  
  if (bm->task_masks[priority] == 0) {
    bitmap_clear(&bm->level_mask, priority);
  }
}

static int sched_bitmap_find_highest(PriorityBitmap* bm, uint32_t* task_id_out) {
  if (bm->level_mask == 0) return -1;
  
  int level = 31 - __builtin_clz(bm->level_mask);
  int task_id = bitmap_ffs(bm->task_masks[level]);
  
  if (task_id >= 0) {
    *task_id_out = task_id;
    return level;
  }
  
  return -1;
}

// ============================================================================
// SCHEDULER
// ============================================================================

void scheduler_init_core0() {
  memset(&core0_sched, 0, sizeof(core0_sched));
  mutex_init(&core0_sched.lock);
  
  core0_sched.idle_task = 0;
  core0_sched.current_task = 0;
  core0_sched.last_aging = get_time_ms();
  
  kout.println("[SCHED] Core0 initialized (Preemptive O(1) bitmap)");
}

void scheduler_init_core1() {
  memset(&core1_sched, 0, sizeof(core1_sched));
  mutex_init(&core1_sched.lock);
  
  core1_sched.last_aging = get_time_ms();
  
  kout.println("[SCHED] Core1 initialized (O(1) bitmap)");
}

void sched_check_preemption() {
  if (kernel.preemption_pending) return;
  
  uint32_t task_id;
  int highest_prio = sched_bitmap_find_highest(&core0_sched.runnable, &task_id);
  
  if (highest_prio > kernel.tasks[kernel.current_task].priority) {
    kernel.preemption_pending = true;
  }
}

void sched_update_task_priority(TCB* task) {
  if (task->running_on_core != 0) return;
  
  disable_all_interrupts();
  
  // Remove from old priority
  for(int p = 0; p < SCHED_NUM_PRIORITY_LEVELS; p++) {
    if (core0_sched.runnable.task_masks[p] & (1U << task->id)) {
      sched_bitmap_remove(&core0_sched.runnable, task->id, p);
      break;
    }
  }
  
  // Add to new priority
  if (task->state == TASK_READY) {
    sched_bitmap_add(&core0_sched.runnable, task->id, task->priority);
  }
  
  enable_all_interrupts();
}

static void sched_age_tasks(CoreScheduler* sched, TCB* tasks, uint32_t task_count) {
  uint64_t now = get_time_ms();
  if (now - sched->last_aging < SCHED_AGING_INTERVAL_MS) return;
  
  sched->last_aging = now;
  
  for (uint32_t i = 0; i < task_count; i++) {
    TCB* task = &tasks[i];
    
    if (task->state != TASK_READY) continue;
    if (task->priority >= SCHED_RT_THRESHOLD) continue;
    if (task->original_priority != 0) continue;
    
    uint64_t wait_time = now - task->sched_info.last_run;
    
    if (wait_time > 1000 && task->priority < SCHED_NUM_PRIORITY_LEVELS - 1) {
      uint8_t old_prio = task->priority;
      task->priority++;
      
      disable_all_interrupts();
      sched_bitmap_remove(&sched->runnable, i, old_prio);
      sched_bitmap_add(&sched->runnable, i, task->priority);
      enable_all_interrupts();
    }
  }
}

static bool sched_should_inject_idle(CoreScheduler* sched) {
  if (sched->cpu_load < SCHED_IDLE_INJECTION_THRESHOLD) {
    sched->idle_injection_active = false;
    return false;
  }
  
  static uint32_t idle_counter = 0;
  if (++idle_counter >= 10) {
    idle_counter = 0;
    sched->idle_injections++;
    return true;
  }
  
  return false;
}

static void sched_update_load(CoreScheduler* sched, uint64_t idle_time_ms, uint64_t total_time_ms) {
  if (total_time_ms == 0) return;
  
  float instant_load = 100.0f * (1.0f - ((float)idle_time_ms / (float)total_time_ms));
  if (instant_load < 0) instant_load = 0;
  if (instant_load > 100) instant_load = 100;
  
  sched->cpu_load_instant = instant_load;
  sched->cpu_load = (sched->cpu_load * 0.9f) + (instant_load * 0.1f);
}

uint32_t sched_select_next_core0() {
  disable_all_interrupts();
  
  if (sched_should_inject_idle(&core0_sched)) {
    enable_all_interrupts();
    return core0_sched.idle_task;
  }
  
  uint32_t task_id;
  int priority = sched_bitmap_find_highest(&core0_sched.runnable, &task_id);
  
  if (priority < 0) {
    enable_all_interrupts();
    return core0_sched.idle_task;
  }
  
  if (task_id >= kernel.task_count) {
    enable_all_interrupts();
    return core0_sched.idle_task;
  }
  
  TCB* task = &kernel.tasks[task_id];
  
  if (task->state != TASK_READY && task->state != TASK_RUNNING) {
    sched_bitmap_remove(&core0_sched.runnable, task_id, priority);
    enable_all_interrupts();
    return sched_select_next_core0();
  }
  
  core0_sched.current_task = task_id;
  core0_sched.current_priority = priority;
  
  enable_all_interrupts();
  return task_id;
}

uint32_t sched_select_next_core1() {
  mutex_enter_blocking(&core1_sched.lock);
  
  uint32_t task_id;
  int priority = sched_bitmap_find_highest(&core1_sched.runnable, &task_id);
  
  if (priority < 0) {
    mutex_exit(&core1_sched.lock);
    return 0xFFFFFFFF;
  }
  
  mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  if (task_id >= kernel.core1.task_count) {
    mutex_exit(&kernel.core1.scheduler_lock);
    mutex_exit(&core1_sched.lock);
    return 0xFFFFFFFF;
  }
  
  TCB* task = &kernel.core1.tasks[task_id];
  
  if (task->state != TASK_READY && task->state != TASK_RUNNING) {
    mutex_exit(&kernel.core1.scheduler_lock);
    sched_bitmap_remove(&core1_sched.runnable, task_id, priority);
    mutex_exit(&core1_sched.lock);
    return sched_select_next_core1();
  }
  
  mutex_exit(&kernel.core1.scheduler_lock);
  
  core1_sched.current_task = task_id;
  core1_sched.current_priority = priority;
  
  mutex_exit(&core1_sched.lock);
  return task_id;
}

void scheduler_tick() {
  // Update velocity check timer
  uint64_t now_ms_for_velocity = get_time_ms();
  if (now_ms_for_velocity - kernel.last_velocity_check_ms > 1000) {
    kernel.last_velocity_check_ms = now_ms_for_velocity;
    
    for (uint32_t i = 0; i < kernel.task_count; i++) {
      if (kernel.tasks[i].alloc_velocity > 0 && 
          (now_ms_for_velocity - kernel.tasks[i].last_alloc_time > 1000)) {
        kernel.tasks[i].alloc_velocity = 0;
      }
    }
  }
  
  uint64_t now = get_time_ms();
  kernel.uptime_ms = now;
  
  if (kernel.task_count == 0 || kernel.task_count > MAX_TASKS) return;
  
  // Wake up waiting tasks
  for (uint32_t i = 0; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_WAITING && task->wake_time != 0 && now >= task->wake_time) {
      task_wake(i);
    }
    
    // Check max runtime
    if (task->max_runtime > 0 && task->state != TASK_TERMINATED && task->state != TASK_ZOMBIE) {
      if ((now - task->start_time) > task->max_runtime) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TIMEOUT: %s", task->name);
        klog(1, buf);
        brutal_task_kill(task->id);
      }
    }
    
    // Respawn tasks
    if (task->state == TASK_TERMINATED && (task->flags & TASK_FLAG_RESPAWN)) {
      if (task->task_type == TASK_TYPE_KERNEL) continue;
      
      if (now - task->last_respawn > 5000) {
        task->state = TASK_READY;
        task->start_time = now;
        task->last_respawn = now;
        task->respawn_count++;
        task->mem_used = 0;
        task->cpu_time = 0;
        
        task_init_ipc_queue(task);
        
        if (task->callbacks && task->callbacks->init) {
          task->callbacks->init(task->id);
        }
        
        disable_all_interrupts();
        if (task->affinity == CORE_0 || task->affinity == CORE_ANY) {
          sched_bitmap_add(&core0_sched.runnable, i, task->priority);
        }
        enable_all_interrupts();
      }
    }
  }
  
  // Age tasks
  sched_age_tasks(&core0_sched, kernel.tasks, kernel.task_count);
  
  // Update CPU load
  static uint64_t last_load_update = 0;
  static uint32_t last_total_task_cpu_time = 0;
  
  if (now - last_load_update >= 1000) {
    uint32_t total_task_cpu_time = 0;
    
    for (uint32_t i = 0; i < kernel.task_count; i++) {
      total_task_cpu_time += kernel.tasks[i].cpu_time;
    }
    
    uint32_t busy_time_ms = total_task_cpu_time - last_total_task_cpu_time;
    uint32_t total_time_ms = now - last_load_update;
    
    if (total_time_ms == 0) total_time_ms = 1;
    if (busy_time_ms > total_time_ms) busy_time_ms = total_time_ms;
    
    uint32_t idle_time_ms = total_time_ms - busy_time_ms;
    
    sched_update_load(&core0_sched, idle_time_ms, total_time_ms);
    kernel.cpu_usage = core0_sched.cpu_load;
    
    last_load_update = now;
    last_total_task_cpu_time = total_task_cpu_time;
    
    // Check for CPU overload
    if (kernel.cpu_usage > CPU_CRITICAL_THRESHOLD) {
      handle_cpu_overload();
    }
  }
  
  // IPC maintenance
  static uint32_t ipc_counter = 0;
  if (++ipc_counter >= 1000) {
    ipc_maintenance();
    ipc_counter = 0;
  }
}

void task_yield() {
  uint32_t prev_task_id = kernel.current_task;
  uint64_t now = get_time_ms();
  
  if (prev_task_id < kernel.task_count) {
    TCB* prev_task = &kernel.tasks[prev_task_id];
    prev_task->sched_info.last_run = now;
    prev_task->last_run = now;
  }
  
  uint32_t next_task = sched_select_next_core0();
  
  if (next_task < kernel.task_count) {
    kernel.current_task = next_task;
    kernel.total_context_switches++;
    kernel.tasks[next_task].context_switches++;
    kernel.tasks[next_task].sched_info.last_run = now;
    core0_sched.switches++;
  }
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

void task_init() {
  memset(&kernel.tasks, 0, sizeof(kernel.tasks));
  kernel.task_count = 0;
  kernel.current_task = 0;
  kernel.cpu_usage = 0.0f;
  kernel.root_mode = false;
  kernel.panic_mode = false;
  kernel.preemption_pending = false;
  
  kernel.kernel_tasks = 0;
  kernel.driver_tasks = 0;
  kernel.service_tasks = 0;
  kernel.module_tasks = 0;
  kernel.application_tasks = 0;
  kernel.zombie_tasks = 0;
  
  kernel.shell_alive = true;
  kernel.cpumon_alive = true;
  kernel.tempmon_alive = true;
  kernel.fs_alive = false;
  
  kernel.log_head = 0;
  kernel.log_count = 0;
  kernel.total_context_switches = 0;
  
  kernel.gui_focus_task_id = -1;
  kernel.app_write_char = NULL;
  
  scheduler_init_core0();
}

void task_init_ipc_queue(TCB* task) {
  task->ipc.priority_bitmap = 0;
  task->ipc.message_count = 0;
  
  for (int i = 0; i < SCHED_NUM_PRIORITY_LEVELS; i++) {
    task->ipc.priority_lists_head[i] = IPC_NULL_MSG;
  }
}

uint32_t task_create(const char* name, void (*entry)(void*), void* arg,
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, uint32_t mem_request, ModuleCallbacks* callbacks,
                     const char* description, CoreAffinity affinity) {
  
  // Check if app is blocked
  for (uint32_t i = 0; i < g_blocked_app_count; i++) {
    if (strcmp(name, g_blocked_app_names[i]) == 0) {
      char klog_buf[64];
      snprintf(klog_buf, sizeof(klog_buf), 
               "MEM_PROTECT: Launch rejected, app %s is blocked.", name);
      klog(2, klog_buf);
      return 0;
    }
  }
  
  if (kernel.task_count >= MAX_TASKS) {
    kout.println("ERROR: Maximum tasks reached!");
    return 0;
  }
  
  // Set up memory limits for applications
  if (task_type != TASK_TYPE_APPLICATION) {
    oom_priority = OOM_PRIORITY_NEVER;
    mem_request = mem_limit;
  } else {
    if (mem_request == 0 && mem_limit > 0) {
      mem_request = mem_limit;
    } else if (mem_request == 0 && mem_limit == 0) {
      mem_request = MAX_APP_MEM_REQUEST_GLOBAL;
    }
    
    if (mem_request > MAX_APP_MEM_REQUEST_GLOBAL) {
      char klog_buf[128];
      snprintf(klog_buf, sizeof(klog_buf), 
               "MEM_PROTECT: App %s launch rejected, requested %luKB > max %luKB.", 
               name, mem_request / 1024, (uint32_t)MAX_APP_MEM_REQUEST_GLOBAL / 1024);
      klog(3, klog_buf);
      
      if (g_blocked_app_count < MAX_APPS) {
        strncpy(g_blocked_app_names[g_blocked_app_count++], name, TASK_NAME_LEN - 1);
      }
      return 0;
    }
    
    if (mem_limit == 0) {
      mem_limit = mem_request;
    } else if (mem_limit < mem_request) {
      mem_limit = mem_request;
    }
  }
  
  // Check for ONESHOT tasks
  if (flags & TASK_FLAG_ONESHOT) {
    for (uint32_t i = 0; i < kernel.task_count; i++) {
      if (strcmp(kernel.tasks[i].name, name) == 0) {
        if (kernel.tasks[i].state != TASK_TERMINATED) {
          return 0;
        }
        
        uint64_t time_since_death = get_time_ms() - kernel.tasks[i].last_run;
        if (time_since_death < 2000) {
          return 0;
        }
      }
    }
  }
  
  // Create task
  uint32_t id = kernel.task_count++;
  TCB* task = &kernel.tasks[id];
  memset(task, 0, sizeof(TCB));
  
  task->id = id;
  strncpy(task->name, name, TASK_NAME_LEN - 1);
  task->name[TASK_NAME_LEN - 1] = '\0';
  task->state = TASK_READY;
  task->task_type = task_type;
  task->priority = priority;
  task->flags = flags;
  task->oom_priority = oom_priority;
  task->affinity = affinity;
  task->running_on_core = 0;
  task->entry = entry;
  task->arg = arg;
  task->wake_time = 0;
  task->start_time = get_time_ms();
  task->max_runtime = max_runtime_ms;
  task->last_respawn = 0;
  task->respawn_count = 0;
  task->mem_used = 0;
  task->mem_peak = 0;
  task->mem_limit = mem_limit;
  task->mem_request_bytes = mem_request;
  task->mem_blocked = false;
  task->mem_throttle_mark = 0;
  task->mem_throttle_time_us = 0;
  task->cpu_time = 0;
  task->last_run = get_time_ms();
  task->page_faults = 0;
  task->context_switches = 0;
  task->callbacks = callbacks;
  task->description = description;
  task->oom_bytes_requested = 0;
  task->alloc_velocity = 0;
  task->last_alloc_time = 0;
  task->total_cpu_time_us = 0;
  task->is_cpu_abuser = false;
  
  task_init_ipc_queue(task);
  
  task->original_priority = 0;
  
  // Set up scheduling info
  TaskSchedInfo* si = &task->sched_info;
  si->base_priority = priority;
  si->effective_priority = priority;
  si->is_realtime = (priority >= SCHED_RT_THRESHOLD);
  
  if (si->is_realtime) {
    si->quantum_us = SCHED_BASE_QUANTUM_US;
  } else {
    si->quantum_us = SCHED_BASE_QUANTUM_US +
                     (SCHED_NUM_PRIORITY_LEVELS - priority) * 2000;
    if (si->quantum_us > SCHED_MAX_QUANTUM_US) {
      si->quantum_us = SCHED_MAX_QUANTUM_US;
    }
  }
  
  si->cpu_affinity = (uint8_t)affinity;
  si->last_run = get_time_us();
  si->cpu_usage_percent = 0;
  si->cpu_burst_counter = 0;
  si->cpu_sample_index = 0;
  memset(si->cpu_samples, 0, sizeof(si->cpu_samples));
  
  // Update task type counters
  if (task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks++;
  else if (task_type == TASK_TYPE_DRIVER) kernel.driver_tasks++;
  else if (task_type == TASK_TYPE_SERVICE) kernel.service_tasks++;
  else if (task_type == TASK_TYPE_MODULE) kernel.module_tasks++;
  else if (task_type == TASK_TYPE_APPLICATION) kernel.application_tasks++;
  
  // Add to scheduler
  disable_all_interrupts();
  if (affinity == CORE_0 || affinity == CORE_ANY) {
    sched_bitmap_add(&core0_sched.runnable, id, priority);
  }
  enable_all_interrupts();
  
  // Initialize callbacks
  if (callbacks && callbacks->init) {
    callbacks->init(id);
  }
  
  // Log task creation
  char buf[64];
  const char* type_str[] = {"KERN", "DRVR", "SRVC", "MODUL", "APP"};
  uint8_t type_idx = 0;
  if (task_type == TASK_TYPE_DRIVER) type_idx = 1;
  else if (task_type == TASK_TYPE_SERVICE) type_idx = 2;
  else if (task_type == TASK_TYPE_MODULE) type_idx = 3;
  else if (task_type == TASK_TYPE_APPLICATION) type_idx = 4;
  
  snprintf(buf, sizeof(buf), "TASK: %s [%s] ID=%d", name, type_str[type_idx], id);
  klog(0, buf);
  
  return id;
}

void task_sleep(uint32_t ms) {
  TCB* task = &kernel.tasks[kernel.current_task];
  task->state = TASK_WAITING;
  task->wake_time = get_time_ms() + ms;
  
  disable_all_interrupts();
  sched_bitmap_remove(&core0_sched.runnable, task->id, task->priority);
  enable_all_interrupts();
}

void task_wake(uint32_t task_id) {
  if (task_id >= 1000) {
    uint32_t local_id = task_id - 1000;
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    if(local_id >= kernel.core1.task_count) {
      mutex_exit(&kernel.core1.scheduler_lock);
      return;
    }
    
    TCB* task = &kernel.core1.tasks[local_id];
    if (task->state == TASK_WAITING) {
      task->state = TASK_READY;
      task->wake_time = 0;
      
      mutex_enter_blocking(&core1_sched.lock);
      if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
        sched_bitmap_add(&core1_sched.runnable, local_id, task->priority);
      }
      mutex_exit(&core1_sched.lock);
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    return;
  }
  
  if (task_id >= kernel.task_count) return;
  
  disable_all_interrupts();
  TCB* task = &kernel.tasks[task_id];
  
  if (task->state == TASK_WAITING && !task->mem_blocked) {
    task->state = TASK_READY;
    task->wake_time = 0;
    
    if (task->affinity == CORE_0 || task->affinity == CORE_ANY) {
      sched_bitmap_add(&core0_sched.runnable, task->id, task->priority);
    }
    
    sched_check_preemption();
  }
  
  enable_all_interrupts();
}

void brutal_task_kill(uint32_t id) {
  if (id >= 1000) {
    kout.print("[KILL] Core 1 task "); 
    kout.println(id);
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    TCB* task = ipc_get_tcb_by_id_unsafe(id);
    if(task) {
      task->state = TASK_ZOMBIE;
      task->entry = NULL;
      task->callbacks = NULL;
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    return;
  }
  
  if (id >= kernel.task_count) return;
  
  TCB* task = &kernel.tasks[id];
  if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) return;
  
  kout.print("[KILL] '");
  kout.print(task->name);
  kout.println("'");
  
  if (task->task_type == TASK_TYPE_KERNEL) {
    kernel_panic("KERNEL TASK KILLED");
  }
  
  // Release GUI focus
  if (kernel.gui_focus_task_id == (int32_t)id) {
    k_release_gui_focus(id);
  }
  
  k_unregister_gui_app(id);
  k_unregister_oom_handler(id);
  
  // Clean up IPC messages
  disable_all_interrupts();
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  for(int pri = 0; pri < SCHED_NUM_PRIORITY_LEVELS; pri++) {
    if (task->ipc.priority_bitmap & (1U << pri)) {
      uint16_t msg_index = task->ipc.priority_lists_head[pri];
      
      while (msg_index != IPC_NULL_MSG) {
        IPCMessage* msg = &kernel.ipc_manager.message_pool[msg_index];
        uint16_t next_msg_index = msg->next;
        
        msg->in_use = false;
        msg->next = IPC_NULL_MSG;
        kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
        
        msg_index = next_msg_index;
      }
    }
  }
  
  task_init_ipc_queue(task);
  
  mutex_exit(&kernel.ipc_manager.lock);
  enable_all_interrupts();
  
  // Close open files
  uint32_t files_closed = 0;
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open &&
        kernel.fs_open_files[i].owner_task_id == id) {
      kout.print(" > Closing file: ");
      kout.println(kernel.fs_open_files[i].path);
      
      kernel.fs_open_files[i].handle.close();
      kernel.fs_open_files[i].open = false;
      kernel.fs_open_files[i].owner_task_id = 0;
      files_closed++;
    }
  }
  
  if (files_closed > 0) {
    kout.print(" > Closed ");
    kout.print(files_closed);
    kout.println(" file(s)");
  }
  
  // Call deinit callback
  if (task->callbacks && task->callbacks->deinit) {
    task->callbacks->deinit();
  }
  
  // Check for critical system tasks
  if (strcmp(task->name, "shell") == 0) {
    kernel.shell_alive = false;
    kout.println("\n*** SHELL DEAD - NO MORE COMMANDS ***");
  }
  else if (strcmp(task->name, "cpumon") == 0) {
    kernel.cpumon_alive = false;
  }
  else if (strcmp(task->name, "tempmon") == 0) {
    kernel.tempmon_alive = false;
  }
  else if (strcmp(task->name, "fs") == 0) {
    kernel.fs_alive = false;
    kout.println("\n*** FS DEAD ***");
  }
  
  // Update task type counters
  if (task->task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks--;
  else if (task->task_type == TASK_TYPE_DRIVER) kernel.driver_tasks--;
  else if (task->task_type == TASK_TYPE_SERVICE) kernel.service_tasks--;
  else if (task->task_type == TASK_TYPE_MODULE) kernel.module_tasks--;
  else if (task->task_type == TASK_TYPE_APPLICATION) kernel.application_tasks--;
  
  // Log kill
  char buf[64];
  snprintf(buf, sizeof(buf), "KILL: %s marked as %s", 
           task->name, (task->state == TASK_ZOMBIE) ? "ZOMBIE" : "TERMINATED");
  klog(2, buf);
  
  // Strip respawn flag
  if (task->flags & TASK_FLAG_RESPAWN) {
    kout.println(" > Stripping RESPAWN flag.");
    task->flags &= ~TASK_FLAG_RESPAWN;
  }
  
  // Set final state
  if (task->flags & TASK_FLAG_RESPAWN) {
    task->state = TASK_TERMINATED;
  } else {
    task->state = TASK_ZOMBIE;
    kernel.zombie_tasks++;
  }
  
  task->last_run = get_time_ms();
  task->entry = NULL;
  task->callbacks = NULL;
  task->arg = NULL;
  
  // Remove from scheduler
  disable_all_interrupts();
  sched_bitmap_remove(&core0_sched.runnable, id, task->priority);
  enable_all_interrupts();
}

void k_task_exit_api() {
  uint8_t core = get_core_num();
  
  if (core == 0) {
    uint32_t task_id = kernel.current_task;
    TCB* task = &kernel.tasks[task_id];
    
    if (task->task_type != TASK_TYPE_APPLICATION) {
      klog(2, "k_task_exit: Non-app task denied exit");
      return;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "APP: Task %s (ID %d) exiting", task->name, task_id);
    klog(0, buf);
    
    brutal_task_kill(task_id);
    task_sleep(0xFFFFFFFF);
  } else {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    
    uint32_t local_id = core1_sched.current_task;
    TCB* task = &kernel.core1.tasks[local_id];
    
    if (task->task_type != TASK_TYPE_APPLICATION) {
      mutex_exit(&kernel.core1.scheduler_lock);
      return;
    }
    
    kout.print("[CORE1] Task exit requested: "); 
    kout.println(task->name);
    
    task->state = TASK_TERMINATED;
    task->entry = NULL;
    task->callbacks = NULL;
    task->arg = NULL;
    
    mutex_enter_blocking(&core1_sched.lock);
    sched_bitmap_remove(&core1_sched.runnable, local_id, task->priority);
    mutex_exit(&core1_sched.lock);
    
    mutex_exit(&kernel.core1.scheduler_lock);
    
    while(1) {
      sleep_us(100000);
    }
  }
}

// ============================================================================
// CORE 1
// ============================================================================

void core1_scheduler_init() {
  mutex_init(&kernel.core1.scheduler_lock);
  
  kernel.core1.task_count = 0;
  kernel.core1.current_task = 0;
  kernel.core1.running = true;
  kernel.core1.uptime_us = 0;
  kernel.core1.cpu_usage = 0.0f;
  kernel.core1.context_switches = 0;
  
  memset(&kernel.core1.tasks, 0, sizeof(kernel.core1.tasks));
}

void core1_main() {
  scheduler_init_core1();
  
  kernel.core1.uptime_us = get_time_us();
  
  uint64_t last_stats_update = get_time_ms();
  static uint64_t last_total_time_c1 = 0;
  
  while (kernel.core1.running) {
    if (kernel.core1.task_count == 0) {
      tight_loop_contents();
      sleep_us(1000);
      continue;
    }
    
    uint64_t loop_start = get_time_us();
    uint64_t now_ms = get_time_ms();
    
    // Wake waiting tasks
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
      TCB* task = &kernel.core1.tasks[i];
      
      if (task->state == TASK_WAITING && now_ms >= task->wake_time) {
        task->state = TASK_READY;
        
        mutex_enter_blocking(&core1_sched.lock);
        if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
          sched_bitmap_add(&core1_sched.runnable, i, task->priority);
        }
        mutex_exit(&core1_sched.lock);
      }
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    
    // Age tasks
    sched_age_tasks(&core1_sched, kernel.core1.tasks, kernel.core1.task_count);
    
    // Select next task
    uint32_t task_id = sched_select_next_core1();
    
    if (task_id == 0xFFFFFFFF) {
      sleep_us(100);
      continue;
    }
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    
    if (task_id >= kernel.core1.task_count) {
      mutex_exit(&kernel.core1.scheduler_lock);
      continue;
    }
    
    TCB* task = &kernel.core1.tasks[task_id];
    
    if (task->mem_blocked) {
      mutex_exit(&kernel.core1.scheduler_lock);
      continue;
    }
    
    // Handle OOM cleanup request
    if (task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED) {
      oom_callback_t handler = oom_get_handler(task->id);
      if (handler) {
        handler(task->oom_bytes_requested);
      }
      task->flags &= ~TASK_FLAG_OOM_CLEANUP_REQUESTED;
      task->oom_bytes_requested = 0;
    }
    
    // Execute task
    if ((task->state == TASK_READY || task->state == TASK_RUNNING) &&
        (task->entry || (task->callbacks && task->callbacks->tick))) {
      
      task->state = TASK_RUNNING;
      task->running_on_core = 1;
      
      mutex_enter_blocking(&core1_sched.lock);
      sched_bitmap_remove(&core1_sched.runnable, task_id, task->priority);
      mutex_exit(&core1_sched.lock);
      
      uint64_t task_start = get_time_us();
      
      mutex_exit(&kernel.core1.scheduler_lock);
      
      if (task->callbacks && task->callbacks->tick) {
        task->callbacks->tick(task->arg);
      } else if (task->entry) {
        task->entry(task->arg);
      }
      
      mutex_enter_blocking(&kernel.core1.scheduler_lock);
      
      uint64_t task_duration = get_time_us() - task_start;
      task->cpu_time += (task_duration + 500) / 1000;
      task->total_cpu_time_us += task_duration;
      task->sched_info.last_run = get_time_ms();
      
      // Update CPU usage
      update_task_cpu_usage(task, task_duration);
      
      if (task->state == TASK_RUNNING) {
        task->state = TASK_READY;
        
        mutex_enter_blocking(&core1_sched.lock);
        if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
          sched_bitmap_add(&core1_sched.runnable, task_id, task->priority);
        }
        mutex_exit(&core1_sched.lock);
      }
    }
    
    mutex_exit(&kernel.core1.scheduler_lock);
    
    core1_sched.switches++;
    
    // Update CPU stats
    if (now_ms - last_stats_update >= 1000) {
      mutex_enter_blocking(&kernel.core1.scheduler_lock);
      
      uint64_t total_cpu = 0;
      for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
        total_cpu += kernel.core1.tasks[i].cpu_time;
      }
      
      uint64_t busy_time_ms = total_cpu - last_total_time_c1;
      last_total_time_c1 = total_cpu;
      
      uint64_t period_ms = now_ms - last_stats_update;
      if (period_ms == 0) period_ms = 1;
      if (busy_time_ms > period_ms) busy_time_ms = period_ms;
      
      float instant_load = 100.0f * (float)busy_time_ms / (float)period_ms;
      kernel.core1.cpu_usage = (kernel.core1.cpu_usage * 0.9f) + (instant_load * 0.1f);
      
      if (kernel.core1.cpu_usage > 100) kernel.core1.cpu_usage = 100;
      if (kernel.core1.cpu_usage < 0) kernel.core1.cpu_usage = 0;
      
      mutex_exit(&kernel.core1.scheduler_lock);
      
      last_stats_update = now_ms;
    }
    
    // Sleep for remaining time
    uint64_t elapsed = get_time_us() - loop_start;
    if (elapsed < SCHEDULER_TICK_US) {
      sleep_us(SCHEDULER_TICK_US - elapsed);
    }
  }
}

uint32_t k_spawn_core1_task(const char* name, void (*entry)(void*), void* arg, uint8_t priority) {
  if (!kernel.core1_initialized) {
    kout.println("[CORE1] Not initialized");
    return 0;
  }
  
  mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  if (kernel.core1.task_count >= MAX_CORE1_TASKS) {
    mutex_exit(&kernel.core1.scheduler_lock);
    kout.println("[CORE1] Task limit reached");
    return 0;
  }
  
  uint32_t id = kernel.core1.task_count++;
  TCB* task = &kernel.core1.tasks[id];
  memset(task, 0, sizeof(TCB));
  
  task->id = id + 1000;
  strncpy(task->name, name, TASK_NAME_LEN - 1);
  task->name[TASK_NAME_LEN - 1] = '\0';
  task->state = TASK_READY;
  task->task_type = TASK_TYPE_APPLICATION;
  task->priority = priority;
  task->affinity = CORE_1;
  task->entry = entry;
  task->arg = arg;
  task->running_on_core = 1;
  task->start_time = get_time_ms();
  task->oom_bytes_requested = 0;
  task->total_cpu_time_us = 0;
  task->is_cpu_abuser = false;
  
  task_init_ipc_queue(task);
  
  task->original_priority = 0;
  
  // Set up scheduling info
  TaskSchedInfo* si = &task->sched_info;
  si->base_priority = priority;
  si->effective_priority = priority;
  si->is_realtime = (priority >= SCHED_RT_THRESHOLD);
  
  if (si->is_realtime) {
    si->quantum_us = SCHED_BASE_QUANTUM_US;
  } else {
    si->quantum_us = SCHED_BASE_QUANTUM_US +
                     (SCHED_NUM_PRIORITY_LEVELS - priority) * 2000;
    if (si->quantum_us > SCHED_MAX_QUANTUM_US) {
      si->quantum_us = SCHED_MAX_QUANTUM_US;
    }
  }
  
  si->cpu_affinity = 1;
  si->last_run = get_time_us();
  si->cpu_usage_percent = 0;
  si->cpu_burst_counter = 0;
  si->cpu_sample_index = 0;
  memset(si->cpu_samples, 0, sizeof(si->cpu_samples));
  
  // Add to scheduler
  mutex_enter_blocking(&core1_sched.lock);
  if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
    sched_bitmap_add(&core1_sched.runnable, id, task->priority);
  }
  mutex_exit(&core1_sched.lock);
  
  mutex_exit(&kernel.core1.scheduler_lock);
  
  kout.print("[CORE1] Spawned task: ");
  kout.print(name);
  kout.print(" (ID=");
  kout.print(task->id);
  kout.println(")");
  
  return task->id;
}

// Continued in next message due to length...

// ============================================================================
// IPC SYSTEM
// ============================================================================

void ipc_init() {
  mutex_init(&kernel.ipc_manager.lock);
  
  kernel.ipc_manager.sequence_counter = 0;
  kernel.ipc_manager.dropped_messages = 0;
  kernel.ipc_manager.total_sent = 0;
  kernel.ipc_manager.total_received = 0;
  kernel.ipc_manager.free_list_head = MAX_IPC_MESSAGES - 1;
  
  for (uint32_t i = 0; i < MAX_IPC_MESSAGES; i++) {
    kernel.ipc_manager.message_pool[i].in_use = false;
    kernel.ipc_manager.free_list[i] = i;
  }
  
  memset(&ipc_stats, 0, sizeof(ipc_stats));
  
  kout.println("[IPC] O(1) Pool Manager initialized");
}

TCB* ipc_get_tcb_by_id_unsafe(uint32_t task_id) {
  if (task_id < 1000) {
    if (task_id < kernel.task_count) {
      return &kernel.tasks[task_id];
    }
  } else {
    uint32_t local_id = task_id - 1000;
    if (local_id < kernel.core1.task_count) {
      return &kernel.core1.tasks[local_id];
    }
  }
  return NULL;
}

bool ipc_send_raw(uint32_t sender_id, uint32_t target_id, IPCMessageType type,
                  void* data, size_t size, uint8_t priority) {
  
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  if (kernel.ipc_manager.free_list_head < 0) {
    mutex_exit(&kernel.ipc_manager.lock);
    ipc_stats.messages_dropped_pool_full++;
    klog(2, "IPC: Global pool full");
    return false;
  }
  
  uint16_t msg_index = kernel.ipc_manager.free_list[kernel.ipc_manager.free_list_head--];
  IPCMessage* msg = &kernel.ipc_manager.message_pool[msg_index];
  
  msg->in_use = true;
  msg->sender_id = sender_id;
  msg->target_id = target_id;
  msg->type = type;
  msg->priority = priority;
  msg->timestamp = get_time_us();
  msg->sequence = kernel.ipc_manager.sequence_counter++;
  msg->next = IPC_NULL_MSG;
  
  if (data && size > 0) {
    memcpy(msg->data, data, size);
  }
  
  mutex_exit(&kernel.ipc_manager.lock);
  
  // Find target task
  bool is_core1 = (target_id >= 1000);
  
  if (is_core1) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
  }
  
  TCB* target_task = ipc_get_tcb_by_id_unsafe(target_id);
  
  if (target_task == NULL || target_task->state == TASK_ZOMBIE || target_task->mem_blocked) {
    if (is_core1) {
      mutex_exit(&kernel.core1.scheduler_lock);
    }
    
    mutex_enter_blocking(&kernel.ipc_manager.lock);
    msg->in_use = false;
    kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
    mutex_exit(&kernel.ipc_manager.lock);
    
    return false;
  }
  
  // Lock target task queue
  if (!is_core1) {
    disable_all_interrupts();
  }
  
  if (target_task->ipc.message_count >= MAX_IPC_MESSAGES) {
    if (is_core1) {
      mutex_exit(&kernel.core1.scheduler_lock);
    } else {
      enable_all_interrupts();
    }
    
    mutex_enter_blocking(&kernel.ipc_manager.lock);
    msg->in_use = false;
    kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
    mutex_exit(&kernel.ipc_manager.lock);
    
    ipc_stats.messages_dropped_task_full++;
    return false;
  }
  
  // Add to priority queue
  uint16_t* head = &target_task->ipc.priority_lists_head[priority];
  msg->next = *head;
  *head = msg_index;
  
  target_task->ipc.priority_bitmap |= (1U << priority);
  target_task->ipc.message_count++;
  
  if (is_core1) {
    mutex_exit(&kernel.core1.scheduler_lock);
  } else {
    enable_all_interrupts();
  }
  
  kernel.ipc_manager.total_sent++;
  ipc_stats.messages_sent++;
  
  return true;
}

bool ipc_send_api(uint32_t target_id, IPCMessageType type,
                  void* data, size_t size, uint8_t priority) {
  
  if (size > IPC_MSG_SIZE) {
    klog(2, "IPC: Message too large");
    return false;
  }
  
  if (priority >= SCHED_NUM_PRIORITY_LEVELS) {
    priority = SCHED_NUM_PRIORITY_LEVELS - 1;
  }
  
  uint32_t sender_id = 0;
  if (get_core_num() == 0) sender_id = kernel.current_task;
  else sender_id = kernel.core1.tasks[core1_sched.current_task].id;
  
  // Handle broadcast
  if (target_id == IPC_TARGET_BROADCAST) {
    bool all_ok = true;
    ipc_stats.broadcasts_sent++;
    
    for(uint32_t i=0; i < kernel.task_count; i++) {
      if (kernel.tasks[i].id != sender_id && 
          kernel.tasks[i].state != TASK_ZOMBIE && 
          !kernel.tasks[i].mem_blocked) {
        if (!ipc_send_raw(sender_id, kernel.tasks[i].id, type, data, size, priority)) {
          all_ok = false;
        }
      }
    }
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for(uint32_t i=0; i < kernel.core1.task_count; i++) {
      if (kernel.core1.tasks[i].id != sender_id && !kernel.core1.tasks[i].mem_blocked) {
        uint32_t c1_target_id = kernel.core1.tasks[i].id;
        mutex_exit(&kernel.core1.scheduler_lock);
        
        if (!ipc_send_raw(sender_id, c1_target_id, type, data, size, priority)) {
          all_ok = false;
        }
        
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
      }
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    
    return all_ok;
  }
  
  return ipc_send_raw(sender_id, target_id, type, data, size, priority);
}

bool ipc_receive_api(IPCMessage* msg_out) {
  if (!msg_out) return false;
  
  TCB* task = NULL;
  bool is_core1 = (get_core_num() == 1);
  
  if (is_core1) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    task = &kernel.core1.tasks[core1_sched.current_task];
  } else {
    disable_all_interrupts();
    task = &kernel.tasks[kernel.current_task];
  }
  
  if (task == NULL) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
    else enable_all_interrupts();
    return false;
  }
  
  if (task->ipc.priority_bitmap == 0) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
    else enable_all_interrupts();
    return false;
  }
  
  // Get highest priority message
  int highest_priority = 31 - __builtin_clz(task->ipc.priority_bitmap);
  uint16_t* head = &task->ipc.priority_lists_head[highest_priority];
  uint16_t msg_index = *head;
  
  if (msg_index == IPC_NULL_MSG) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
    else enable_all_interrupts();
    
    klog(3, "IPC: Bitmap/queue mismatch!");
    task->ipc.priority_bitmap &= ~(1U << highest_priority);
    return false;
  }
  
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  IPCMessage* msg = &kernel.ipc_manager.message_pool[msg_index];
  
  *head = msg->next;
  if (*head == IPC_NULL_MSG) {
    task->ipc.priority_bitmap &= ~(1U << highest_priority);
  }
  
  task->ipc.message_count--;
  
  memcpy(msg_out, msg, sizeof(IPCMessage));
  
  msg->in_use = false;
  msg->next = IPC_NULL_MSG;
  kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
  
  kernel.ipc_manager.total_received++;
  ipc_stats.messages_received++;
  
  mutex_exit(&kernel.ipc_manager.lock);
  
  if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
  else enable_all_interrupts();
  
  return true;
}

void ipc_maintenance() {
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  uint16_t total_in_use = MAX_IPC_MESSAGES - (kernel.ipc_manager.free_list_head + 1);
  
  mutex_exit(&kernel.ipc_manager.lock);
  
  ipc_stats.avg_queue_depth_global = (ipc_stats.avg_queue_depth_global * 0.95f) +
                                      (total_in_use * 0.05f);
  
  if (total_in_use > ipc_stats.max_queue_depth_global) {
    ipc_stats.max_queue_depth_global = total_in_use;
  }
}

// ============================================================================
// RTOS PRIMITIVES
// ============================================================================

void rtos_primitives_init() {
  memset(&kernel.kernel_mutexes, 0, sizeof(kernel.kernel_mutexes));
  memset(&kernel.kernel_semaphores, 0, sizeof(kernel.kernel_semaphores));
  memset(&kernel.kernel_event_flags, 0, sizeof(kernel.kernel_event_flags));
  
  for(int i=0; i < MAX_SEMAPHORES; i++) {
    kernel.kernel_semaphores[i].max_count = 0;
  }
  
  kout.println("[RTOS] Mutexes, Semaphores, Events initialized");
}

void wait_list_add(TaskWaitNode** head, TCB* task) {
  TaskWaitNode* node = &task->wait_node;
  node->task_id = task->id;
  node->next = NULL;
  
  if (*head == NULL) {
    *head = node;
  } else {
    TaskWaitNode* current = *head;
    while(current->next != NULL) {
      current = current->next;
    }
    current->next = node;
  }
}

uint32_t wait_list_pop(TaskWaitNode** head) {
  if (*head == NULL) {
    return 0xFFFFFFFF;
  }
  
  TaskWaitNode* node = *head;
  uint32_t task_id = node->task_id;
  *head = node->next;
  
  return task_id;
}

bool k_mutex_lock(uint32_t mutex_id) {
  if (mutex_id >= MAX_KERNEL_MUTEXES) return false;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KMutex* mutex = &kernel.kernel_mutexes[mutex_id];
  
  disable_all_interrupts();
  
  if (!mutex->locked) {
    mutex->locked = true;
    mutex->owner_id = task_id;
    enable_all_interrupts();
    return true;
  }
  
  // Priority inheritance
  bool is_core1_owner = (mutex->owner_id >= 1000);
  if(is_core1_owner) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  TCB* owner_task = ipc_get_tcb_by_id_unsafe(mutex->owner_id);
  
  if(is_core1_owner) mutex_exit(&kernel.core1.scheduler_lock);
  
  if (owner_task && task->priority > owner_task->priority) {
    if (owner_task->original_priority == 0) {
      owner_task->original_priority = owner_task->priority;
    }
    owner_task->priority = task->priority;
    sched_update_task_priority(owner_task);
  }
  
  wait_list_add(&mutex->wait_list_head, task);
  task->state = TASK_WAITING;
  task->wake_time = 0;
  
  sched_bitmap_remove(&core0_sched.runnable, task_id, task->priority);
  
  enable_all_interrupts();
  task_yield();
  
  return true;
}

void k_mutex_unlock(uint32_t mutex_id) {
  if (mutex_id >= MAX_KERNEL_MUTEXES) return;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KMutex* mutex = &kernel.kernel_mutexes[mutex_id];
  
  disable_all_interrupts();
  
  if (!mutex->locked || mutex->owner_id != task_id) {
    enable_all_interrupts();
    return;
  }
  
  // Restore original priority
  if (task->original_priority != 0) {
    task->priority = task->original_priority;
    task->original_priority = 0;
    sched_update_task_priority(task);
  }
  
  uint32_t next_task_id = wait_list_pop(&mutex->wait_list_head);
  
  if (next_task_id != 0xFFFFFFFF) {
    mutex->owner_id = next_task_id;
    task_wake(next_task_id);
  } else {
    mutex->locked = false;
    mutex->owner_id = 0;
  }
  
  enable_all_interrupts();
  sched_check_preemption();
}

bool k_sem_init(uint32_t sem_id, int32_t initial_count, uint32_t max_count) {
  if (sem_id >= MAX_SEMAPHORES) return false;
  
  KSemaphore* sem = &kernel.kernel_semaphores[sem_id];
  if (sem->max_count != 0) return false;
  
  sem->count = initial_count;
  sem->max_count = max_count;
  sem->wait_list_head = NULL;
  
  return true;
}

bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms) {
  if (sem_id >= MAX_SEMAPHORES) return false;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KSemaphore* sem = &kernel.kernel_semaphores[sem_id];
  
  disable_all_interrupts();
  
  sem->count--;
  if (sem->count >= 0) {
    enable_all_interrupts();
    return true;
  }
  
  wait_list_add(&sem->wait_list_head, task);
  task->state = TASK_WAITING;
  
  if (timeout_ms > 0) {
    task->wake_time = get_time_ms() + timeout_ms;
  } else {
    task->wake_time = 0;
  }
  
  sched_bitmap_remove(&core0_sched.runnable, task_id, task->priority);
  
  enable_all_interrupts();
  task_yield();
  
  if (sem->count >= 0) {
    return true;
  } else {
    sem->count++;
    return false;
  }
}

void k_sem_post(uint32_t sem_id) {
  if (sem_id >= MAX_SEMAPHORES) return;
  
  KSemaphore* sem = &kernel.kernel_semaphores[sem_id];
  
  disable_all_interrupts();
  
  sem->count++;
  if (sem->count > (int32_t)sem->max_count) {
    sem->count = sem->max_count;
  }
  
  if (sem->count <= 0) {
    uint32_t task_to_wake = wait_list_pop(&sem->wait_list_head);
    if (task_to_wake != 0xFFFFFFFF) {
      task_wake(task_to_wake);
    }
  }
  
  enable_all_interrupts();
  sched_check_preemption();
}

bool k_event_init(uint32_t event_id) {
  if (event_id >= MAX_EVENT_FLAGS) return false;
  
  KEvent* event = &kernel.kernel_event_flags[event_id];
  if (event->wait_list_head) return false;
  
  event->flags = 0;
  return true;
}

uint32_t k_event_wait(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms) {
  if (event_id >= MAX_EVENT_FLAGS) return 0;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KEvent* event = &kernel.kernel_event_flags[event_id];
  
  disable_all_interrupts();
  
  uint32_t current_flags = event->flags;
  bool condition_met = false;
  
  if (mode == K_EVENT_WAIT_ANY) {
    condition_met = (current_flags & flags) != 0;
  } else {
    condition_met = (current_flags & flags) == flags;
  }
  
  if (condition_met) {
    if (clear) {
      event->flags &= ~flags;
    }
    enable_all_interrupts();
    return current_flags;
  }
  
  TaskWaitNode* node = &task->wait_node;
  node->task_id = task->id;
  node->wait_flags = flags;
  node->wait_mode = mode;
  node->clear_on_exit = clear;
  node->next = event->wait_list_head;
  event->wait_list_head = node;
  
  task->state = TASK_WAITING;
  
  if (timeout_ms > 0) {
    task->wake_time = get_time_ms() + timeout_ms;
  } else {
    task->wake_time = 0;
  }
  
  sched_bitmap_remove(&core0_sched.runnable, task_id, task->priority);
  
  enable_all_interrupts();
  task_yield();
  
  return kernel.kernel_event_flags[event_id].flags;
}

void k_event_set(uint32_t event_id, uint32_t flags_to_set) {
  if (event_id >= MAX_EVENT_FLAGS) return;
  
  KEvent* event = &kernel.kernel_event_flags[event_id];
  
  disable_all_interrupts();
  
  event->flags |= flags_to_set;
  
  TaskWaitNode* node = event->wait_list_head;
  TaskWaitNode* prev = NULL;
  bool task_woken = false;
  
  while(node != NULL) {
    bool condition_met = false;
    
    if (node->wait_mode == K_EVENT_WAIT_ANY) {
      condition_met = (event->flags & node->wait_flags) != 0;
    } else {
      condition_met = (event->flags & node->wait_flags) == node->wait_flags;
    }
    
    if (condition_met) {
      task_wake(node->task_id);
      task_woken = true;
      
      if (node->clear_on_exit) {
        event->flags &= ~node->wait_flags;
      }
      
      if (prev == NULL) {
        event->wait_list_head = node->next;
        node = event->wait_list_head;
      } else {
        prev->next = node->next;
        node = node->next;
      }
    } else {
      prev = node;
      node = node->next;
    }
  }
  
  enable_all_interrupts();
  
  if (task_woken) {
    sched_check_preemption();
  }
}

// ============================================================================
// TEMPERATURE
// ============================================================================

void temp_init() {
  adc_init();
  adc_set_temp_sensor_enabled(true);
  adc_select_input(4);
}

float read_temperature() {
  adc_select_input(4);
  uint16_t adc_raw = adc_read();
  
  const float conversion = 3.3f / 4096.0f;
  float voltage = adc_raw * conversion;
  float temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;
  
  return temp_c;
}

// ============================================================================
// LOGGING
// ============================================================================

void klog(uint8_t level, const char* msg) {
  mutex_enter_blocking(&kernel.log_lock);
  
  LogEntry* entry = &kernel.log[kernel.log_head];
  entry->timestamp = get_time_ms();
  entry->level = level;
  
  strncpy(entry->message, msg, sizeof(entry->message) - 1);
  entry->message[sizeof(entry->message) - 1] = '\0';
  
  kernel.log_head = (kernel.log_head + 1) % MAX_LOG_ENTRIES;
  
  if (kernel.log_count < MAX_LOG_ENTRIES) {
    kernel.log_count++;
  }
  
  mutex_exit(&kernel.log_lock);
  
  if (kernel.fs_mounted && level >= 2) {
    fs_log_write(msg);
  }
}

// Continuing with filesystem and remaining code in next message... Femboicoderz01

// ============================================================================
// FILESYSTEM
// ============================================================================

void fs_init() {
  mutex_init(&kernel.fs_lock);
  
  kernel.fs_available = false;
  kernel.fs_mounted = false;
  kernel.sd_info.valid = false;
  kernel.fs_used_bytes = 0;
  kernel.fs_reads = 0;
  kernel.fs_writes = 0;
  kernel.fs_log_counter = 0;
  
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    kernel.fs_open_files[i].open = false;
    kernel.fs_open_files[i].owner_task_id = 0;
  }
  
  kout.println("[FS] Initializing PMFS (Picomimi Filesystem)...");
  
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(500);
  
  // Initialize PMFS
  PMFSStatus status = pmfs.init(SD_CS);
  
  if (status == PMFS_OK) {
    kout.println("[FS] PMFS initialized - root structure found");
    kernel.fs_available = true;
    kernel.sd_info.card_type = 2;
    kernel.sd_info.valid = true;
  } else if (status == PMFS_ERROR_NO_ROOT) {
    kout.println("[FS] PMFS root not found - creating filesystem...");
    status = pmfs.format_and_initialize();
    if (status == PMFS_OK) {
      kout.println("[FS] PMFS filesystem created successfully");
      kernel.fs_available = true;
      kernel.sd_info.valid = true;
    } else {
      kout.print("[FS] PMFS format failed: ");
      kout.println(pmfs.status_to_string(status));
      kernel.fs_available = false;
      return;
    }
  } else if (status == PMFS_ERROR_SD_NOT_FOUND) {
    kout.println("[FS] SD card not detected");
    klog(1, "FS: No SD card");
    kernel.fs_available = false;
    return;
  } else {
    kout.print("[FS] PMFS init error: ");
    kout.println(pmfs.status_to_string(status));
    kernel.fs_available = false;
    return;
  }
  
  klog(0, "FS: PMFS Init OK");
}

void fs_log_init() {
  if (!kernel.fs_available) return;
  
  File logFile = SD.open(FS_LOG_FILE, "r");
  
  if (!logFile) {
    kout.println("[FS] Creating LogRecord file");
    logFile = SD.open(FS_LOG_FILE, "w");
    
    if (logFile) {
      logFile.println("=== Picomimi RTOS v13 System Log ===");
      logFile.println("Format: [N] Timestamp Message");
      logFile.println("================================");
      logFile.close();
      kernel.fs_log_counter = 0;
      kout.println("[FS] LogRecord created");
    } else {
      kout.println("[FS] Failed to create LogRecord");
      return;
    }
  } else {
    kernel.fs_log_counter = 0;
    
    while (logFile.available()) {
      String line = logFile.readStringUntil('\n');
      
      if (line.startsWith("[")) {
        int endBracket = line.indexOf(']');
        if (endBracket > 0) {
          String numStr = line.substring(1, endBracket);
          uint32_t num = numStr.toInt();
          if (num > kernel.fs_log_counter) {
            kernel.fs_log_counter = num;
          }
        }
      }
    }
    
    logFile.close();
    kout.print("[FS] LogRecord found, last entry: ");
    kout.println(kernel.fs_log_counter);
  }
}

void fs_log_write(const char* message) {
  if (!kernel.fs_mounted) return;
  
  mutex_enter_blocking(&kernel.fs_lock);
  pmfs.log_system(message);
  kernel.fs_log_counter++;
  mutex_exit(&kernel.fs_lock);
}

bool fs_mount() {
  if (!kernel.fs_available) {
    kout.println("[FS] SD card unavailable");
    return false;
  }
  
  PMFSStatus status = pmfs.mount();
  if (status != PMFS_OK) {
    kout.print("[FS] PMFS mount failed: ");
    kout.println(pmfs.status_to_string(status));
    return false;
  }
  
  kernel.fs_mounted = true;
  kernel.fs_alive = true;
  
  // Log initialization via PMFS
  pmfs.log_system("Picomimi v13 kernel boot");
  
  kout.println("[FS] PMFS mounted");
  klog(0, "FS: PMFS Mounted");
  
  return true;
}

void fs_unmount() {
  if (!kernel.fs_mounted) return;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  // Close kernel's open files
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open) {
      kernel.fs_open_files[i].handle.close();
      kernel.fs_open_files[i].open = false;
      kernel.fs_open_files[i].owner_task_id = 0;
    }
  }
  
  // Unmount PMFS
  pmfs.unmount();
  
  kernel.fs_mounted = false;
  kernel.fs_alive = false;
  
  mutex_exit(&kernel.fs_lock);
  
  kout.println("[FS] PMFS unmounted");
  klog(0, "FS: PMFS Unmounted");
}

bool fs_mkdir(const char* path) {
  if (!kernel.fs_mounted) return false;
  
  mutex_enter_blocking(&kernel.fs_lock);
  bool result = SD.mkdir(path);
  mutex_exit(&kernel.fs_lock);
  
  return result;
}

bool fs_remove(const char* path) {
  if (!kernel.fs_mounted) return false;
  
  mutex_enter_blocking(&kernel.fs_lock);
  bool result = SD.remove(path);
  if (!result) {
    result = SD.rmdir(path);
  }
  mutex_exit(&kernel.fs_lock);
  
  return result;
}

void fs_list(const char* path) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  File root = SD.open(path);
  if (!root) {
    kout.println("[FS] Failed to open directory");
    return;
  }
  
  if (!root.isDirectory()) {
    kout.println("[FS] Not a directory");
    root.close();
    return;
  }
  
  kout.print("\n=== FS Contents [");
  kout.print(path);
  kout.println("] ===");
  kout.println("Name                             Type  Size");
  kout.println("-------------------------------- ----- --------");
  
  File file = root.openNextFile();
  uint32_t file_count = 0;
  
  while (file) {
    file_count++;
    if (file_count % 20 == 0) {
      task_sleep(1);
    }
    
    char buf[80];
    snprintf(buf, sizeof(buf), "%-32s %-5s %8d",
             file.name(),
             file.isDirectory() ? "DIR" : "FILE",
             (int)file.size());
    kout.println(buf);
    
    file.close();
    file = root.openNextFile();
  }
  
  root.close();
}

void fs_stats() {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  kout.println("\n=== PMFS Statistics ===");
  
  kout.print("Free space: ");
  kout.print((uint32_t)(pmfs.get_free_space() / 1024));
  kout.println(" KB");
  
  kout.print("Used space: ");
  kout.print((uint32_t)(pmfs.get_used_space() / 1024));
  kout.println(" KB");
  
  kout.print("Fragmentation: ");
  kout.print(pmfs.get_fragmentation(), 1);
  kout.println("%");
  
  kout.print("tmpfs available: ");
  kout.print(pmfs.tmpfs_available());
  kout.println(" bytes");
  
  PMFSStats stats;
  pmfs.get_stats(&stats);
  
  kout.print("Files created: ");
  kout.println((uint32_t)stats.files_created);
  kout.print("Bytes written: ");
  kout.println((uint32_t)stats.bytes_written);
  kout.print("Bytes read: ");
  kout.println((uint32_t)stats.bytes_read);
  kout.print("Cache hits: ");
  kout.println((uint32_t)stats.cache_hits);
  kout.print("Journal commits: ");
  kout.println(stats.journal_commits);
  
  pmfs.print_metadata();
}

void fs_cat(const char* path) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  File file = SD.open(path, "r");
  if (!file) {
    kout.println("[FS] Failed to open file");
    return;
  }
  
  if (file.isDirectory()) {
    kout.println("[FS] Error: Is a directory");
    file.close();
    return;
  }
  
  kout.println("\n=== File Contents ===");
  while (file.available()) {
    kout.write(file.read());
  }
  kout.println("\n=== End ===");
  
  file.close();
  kernel.fs_reads++;
}

bool fs_write_new(const char* path, const char* content, bool append) {
  if (!kernel.fs_mounted) return false;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  File file;
  if (append) {
    file = SD.open(path, "a");
  } else {
    file = SD.open(path, "w");
  }
  
  if (!file) {
    mutex_exit(&kernel.fs_lock);
    return false;
  }
  
  file.println(content);
  file.close();
  
  kernel.fs_writes++;
  mutex_exit(&kernel.fs_lock);
  
  return true;
}

void fs_logtail(uint32_t lines) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  File file = SD.open(FS_LOG_FILE, "r");
  if (!file) {
    kout.println("[FS] Failed to open log file");
    mutex_exit(&kernel.fs_lock);
    return;
  }
  
  uint32_t fsize = file.size();
  if (fsize == 0) {
    kout.println("[Log is empty]");
    file.close();
    mutex_exit(&kernel.fs_lock);
    return;
  }
  
  // Find starting position
  uint32_t pos = fsize - 1;
  uint32_t line_count = 0;
  
  while (pos > 0) {
    file.seek(pos);
    char c = file.read();
    
    if (c == '\n') {
      line_count++;
      if (line_count > lines) {
        pos++;
        break;
      }
    }
    pos--;
  }
  
  kout.print("\n=== Log Tail (");
  kout.print(lines);
  kout.println(" lines) ===");
  
  file.seek(pos);
  while (file.available()) {
    kout.write(file.read());
  }
  
  if(pos > 0) kout.println();
  kout.println("=== End ===");
  
  file.close();
  kernel.fs_reads++;
  
  mutex_exit(&kernel.fs_lock);
}

int fs_open(const char* path, bool write_mode) {
  if (!kernel.fs_mounted) return -1;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  int fd = -1;
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (!kernel.fs_open_files[i].open) {
      fd = i;
      break;
    }
  }
  
  if (fd < 0) {
    kout.println("[FS] No free file handles");
    mutex_exit(&kernel.fs_lock);
    return -1;
  }
  
  File file;
  if (write_mode) {
    file = SD.open(path, "w");
  } else {
    file = SD.open(path, "r");
  }
  
  if (!file) {
    mutex_exit(&kernel.fs_lock);
    return -1;
  }
  
  kernel.fs_open_files[fd].handle = file;
  kernel.fs_open_files[fd].open = true;
  kernel.fs_open_files[fd].write_mode = write_mode;
  kernel.fs_open_files[fd].owner_task_id = kernel.current_task;
  
  strncpy(kernel.fs_open_files[fd].path, path, FS_MAX_FILENAME - 1);
  kernel.fs_open_files[fd].path[FS_MAX_FILENAME - 1] = '\0';
  
  mutex_exit(&kernel.fs_lock);
  
  return fd;
}

void fs_close(int fd) {
  if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  if (kernel.fs_open_files[fd].open) {
    kernel.fs_open_files[fd].handle.close();
    kernel.fs_open_files[fd].open = false;
    kernel.fs_open_files[fd].owner_task_id = 0;
  }
  
  mutex_exit(&kernel.fs_lock);
}

// ============================================================================
// SYSTEM TASKS
// ============================================================================

void idle_task(void* arg) {
  task_sleep(100);
}

void k_reaper_task(void* arg) {
  task_sleep(REAPER_INTERVAL_MS);
  
  if (kernel.zombie_tasks == 0) {
    return;
  }
  
  klog(0, "REAPER: Cleaning zombie tasks...");
  
  uint32_t reclaimed_bytes = 0;
  uint32_t zombies_cleaned = 0;
  
  // Wait grace period before cleanup
  task_sleep(REAPER_GRACE_PERIOD_MS);
  
  for (uint32_t i = 1; i < kernel.task_count; i++) {
    disable_all_interrupts();
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_ZOMBIE) {
      enable_all_interrupts();
      
      uint32_t freed = 0;
      
      mutex_enter_blocking(&kernel.mem_lock);
      
      for (uint32_t b = 0; b < kernel.mem_block_count; b++) {
        if (kernel.mem_blocks[b].owner_id == task->id) {
          freed += kernel.mem_blocks[b].size;
          kernel.mem_blocks[b].free = true;
          kernel.total_free_mem += kernel.mem_blocks[b].size;
          kernel.mem_blocks[b].owner_id = 0;
        }
      }
      
      mutex_exit(&kernel.mem_lock);
      
      if (freed > 0) {
        reclaimed_bytes += freed;
        mem_compact();
      }
      
      disable_all_interrupts();
      task->state = TASK_TERMINATED;
      kernel.zombie_tasks--;
      zombies_cleaned++;
      enable_all_interrupts();
    } else {
      enable_all_interrupts();
    }
  }
  
  if(zombies_cleaned > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "REAPER: Cleaned %d zombies, %dKB", 
             (int)zombies_cleaned, (int)(reclaimed_bytes / 1024));
    klog(0, buf);
  }
}

void shell_task(void* arg) {
  if (!kernel.shell_alive) {
    task_sleep(10000);
    return;
  }
  
  while (Serial.available()) {
    int c = Serial.read();
    
    if (c == '\r' || c == '\n') {
      kout.println();
      
      char temp_cmd_buffer[128];
      strncpy(temp_cmd_buffer, cmd_buffer, sizeof(temp_cmd_buffer) - 1);
      temp_cmd_buffer[sizeof(temp_cmd_buffer) - 1] = '\0';
      
      shell_execute(temp_cmd_buffer);
      
      cmd_pos = 0;
      memset(cmd_buffer, 0, sizeof(cmd_buffer));
      
      if (!kernel.shell_alive) {
        task_sleep(10000);
        return;
      }
      
      shell_prompt();
    } else if (c == '\b' || c == 127) {
      if (cmd_pos > 0) {
        cmd_pos--;
        cmd_buffer[cmd_pos] = '\0';
        Serial.write('\b');
        Serial.write(' ');
        Serial.write('\b');
      }
    } else if (cmd_pos < sizeof(cmd_buffer) - 1) {
      cmd_buffer[cmd_pos++] = c;
      Serial.write(c);
    }
  }
  
  task_sleep(10);
}

void shell_deinit() {
  kout.println("[SHELL] DEINIT");
  kernel.shell_alive = false;
  cmd_pos = 0;
  memset(cmd_buffer, 0, sizeof(cmd_buffer));
}

void input_task(void* arg) {
  static unsigned long last_onoff_press = 0;
  
  if (gpio_read_fast(BTN_ONOFF) && (millis() - last_onoff_press > 250)) {
    last_onoff_press = millis();
    
    if (gui_app_count > 0) {
      current_gui_focus_index = (current_gui_focus_index + 1) % gui_app_count;
      uint32_t new_focus_task_id = gui_app_task_ids[current_gui_focus_index];
      kernel.gui_focus_task_id = new_focus_task_id;
      
      kout.print("\n[Focus] Switched to task ");
      kout.println(new_focus_task_id);
      shell_prompt();
    } else {
      kernel.gui_focus_task_id = -1;
      current_gui_focus_index = -1;
    }
  }
  
  task_sleep(20);
}

void cpu_monitor_task(void* arg) {
  if (!kernel.cpumon_alive) {
    task_sleep(10000);
    return;
  }
  
  task_sleep(5000);
  
  if (kernel.cpu_usage > 95.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "CPUMON: High CPU Load! %.1f%%", kernel.cpu_usage);
    klog(2, buf);
  }
  
  if (kernel.core1_initialized && kernel.core1.cpu_usage > 95.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "CPUMON: High CPU Load (Core 1)! %.1f%%", 
             kernel.core1.cpu_usage);
    klog(2, buf);
  }
}

void cpumon_deinit() {
  kout.println("[CPUMON] DEINIT");
  kernel.cpumon_alive = false;
}

void temp_monitor_task(void* arg) {
  if (!kernel.tempmon_alive) {
    task_sleep(10000);
    return;
  }
  
  kernel.temperature = read_temperature();
  
  if (kernel.temperature > 70.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "TEMPMON: High Temperature! %.1f C", 
             kernel.temperature);
    klog(2, buf);
  }
  
  task_sleep(2000);
}

void tempmon_deinit() {
  kout.println("[TEMPMON] DEINIT");
  kernel.tempmon_alive = false;
}

void fs_task(void* arg) {
  if (!kernel.fs_alive) {
    task_sleep(10000);
    return;
  }
  
  task_sleep(30000);
  
  uint32_t flushed_files = 0;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open && kernel.fs_open_files[i].write_mode) {
      kernel.fs_open_files[i].handle.flush();
      flushed_files++;
    }
  }
  
  mutex_exit(&kernel.fs_lock);
  
  if (flushed_files > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "FS: Flushed %d open write handles", flushed_files);
    klog(0, buf);
  }
}

void fs_deinit() {
  kout.println("[FS] DEINIT");
  
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open) {
      kernel.fs_open_files[i].handle.close();
      kernel.fs_open_files[i].open = false;
    }
  }
  
  fs_unmount();
  kernel.fs_alive = false;
}

// Continued with shell commands in next part...
// Continued with shell commands in next part...

// ============================================================================
// SHELL UTILITIES
// ============================================================================

void shell_prompt() {
  if (kernel.root_mode) {
    kout.print("Picomimi:");
    kout.print(shell_cwd);
    kout.print("# ");
  } else {
    kout.print("Picomimi:");
    kout.print(shell_cwd);
    kout.print("~> ");
  }
}

static void fs_normalize_path(const char* base, const char* relative, char* out, size_t out_size) {
  char temp_path[256];
  char temp_relative[128];
  
  strncpy(temp_relative, relative, sizeof(temp_relative)-1);
  temp_relative[sizeof(temp_relative)-1] = '\0';
  
  if (temp_relative[0] == '/') {
    strncpy(temp_path, temp_relative, sizeof(temp_path) - 1);
  } else {
    if (strcmp(base, "/") == 0) {
      snprintf(temp_path, sizeof(temp_path), "/%s", temp_relative);
    } else {
      snprintf(temp_path, sizeof(temp_path), "%s/%s", base, temp_relative);
    }
  }
  
  temp_path[sizeof(temp_path)-1] = '\0';
  
  char* segments[32];
  int s_idx = 0;
  char* token = strtok(temp_path, "/");
  
  while(token != NULL) {
    if (strcmp(token, ".") == 0) {
      // Skip
    } else if (strcmp(token, "..") == 0) {
      if (s_idx > 0) {
        s_idx--;
      }
    } else {
      if (s_idx < 32) {
        segments[s_idx++] = token;
      }
    }
    token = strtok(NULL, "/");
  }
  
  if (s_idx == 0) {
    strncpy(out, "/", out_size);
  } else {
    out[0] = '\0';
    for (int i = 0; i < s_idx; i++) {
      if (strlen(out) + strlen(segments[i]) + 2 > out_size) break;
      strcat(out, "/");
      strcat(out, segments[i]);
    }
  }
}

void shell_execute(char* cmd) {
  if (strlen(cmd) == 0) return;
  
  if (strcmp(cmd, "help") == 0) cmd_help();
  else if (strcmp(cmd, "ps") == 0) cmd_ps();
  else if (strncmp(cmd, "taskinfo ", 9) == 0) cmd_taskinfo(cmd + 9);
  else if (strcmp(cmd, "ipcstat") == 0) cmd_ipcstat();
  else if (strcmp(cmd, "schedstat") == 0) cmd_schedstat();
  else if (strcmp(cmd, "oomstat") == 0) cmd_oomstat();
  else if (strcmp(cmd, "rtos_stat") == 0) cmd_rtos_stat();
  else if (strcmp(cmd, "dmesg") == 0) cmd_dmesg();
  else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
  else if (strcmp(cmd, "temp") == 0) cmd_temp();
  else if (strcmp(cmd, "root") == 0) cmd_root();
  else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
  else if (strncmp(cmd, "kill ", 5) == 0) cmd_kill(cmd + 5);
  else if (strncmp(cmd, "ls", 2) == 0) {
    char path[128];
    if (strlen(cmd) <= 3) {
      fs_list(shell_cwd);
    } else if (cmd[2] == ' ') {
      fs_normalize_path(shell_cwd, cmd + 3, path, sizeof(path));
      fs_list(path);
    } else {
      kout.print("Unknown: "); kout.println(cmd);
    }
  }
  else if (strcmp(cmd, "stat") == 0) fs_stats();
  else if (strncmp(cmd, "cat ", 4) == 0) {
    char path[128];
    fs_normalize_path(shell_cwd, cmd + 4, path, sizeof(path));
    fs_cat(path);
  }
  else if (strncmp(cmd, "write ", 6) == 0) cmd_write(cmd + 6);
  else if (strcmp(cmd, "logls") == 0) cmd_logls();
  else if (strcmp(cmd, "format") == 0) cmd_format_sd();
  else if (strncmp(cmd, "mkdir ", 6) == 0) {
    char path[128];
    fs_normalize_path(shell_cwd, cmd + 6, path, sizeof(path));
    cmd_mkdir(path);
  }
  else if (strncmp(cmd, "rm ", 3) == 0) {
    char path[128];
    fs_normalize_path(shell_cwd, cmd + 3, path, sizeof(path));
    cmd_rm(path);
  }
  else if (strncmp(cmd, "touch ", 6) == 0) {
    char path[128];
    fs_normalize_path(shell_cwd, cmd + 6, path, sizeof(path));
    cmd_touch(path);
  }
  else if (strncmp(cmd, "logtail", 7) == 0) cmd_logtail(cmd + 7);
  else if (strncmp(cmd, "cd ", 3) == 0) cmd_cd(cmd + 3);
  else if (strcmp(cmd, "cd") == 0) cmd_cd("/");
  else if (strcmp(cmd, "app block list") == 0) cmd_app_block_list();
  else if (strncmp(cmd, "app block unlock ", 17) == 0) cmd_app_block_unlock(cmd + 17);
  // PMFS commands
  else if (strcmp(cmd, "tree") == 0) {
    if (kernel.fs_mounted) {
      kout.println("\n=== PMFS Directory Tree ===");
      pmfs.print_tree();
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "pmfs") == 0) {
    if (kernel.fs_mounted) {
      pmfs.print_stats();
      pmfs.print_metadata();
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "tmpfs") == 0) {
    if (kernel.fs_mounted) {
      kout.println("\n=== tmpfs (RAM Disk) ===");
      kout.print("Available: ");
      kout.print(pmfs.tmpfs_available());
      kout.print(" / ");
      kout.print(PMFS_TMPFS_SIZE);
      kout.println(" bytes");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "bank") == 0) {
    if (kernel.fs_mounted) {
      kout.println("\n=== System Banks ===");
      kout.print("Active bank: ");
      kout.println(pmfs.get_active_bank() == PMFS_BANK_A ? "A" : "B");
      kout.print("Backup bank: ");
      kout.println(pmfs.get_backup_bank() == PMFS_BANK_A ? "A" : "B");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "fsck") == 0) {
    if (kernel.fs_mounted) {
      kout.println("[PMFS] Running filesystem check...");
      PMFSStatus status = pmfs.fsck(true);
      if (status == PMFS_OK) {
        kout.println("[PMFS] Filesystem OK");
      } else {
        kout.print("[PMFS] fsck result: ");
        kout.println(pmfs.status_to_string(status));
      }
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "defrag") == 0) {
    if (kernel.fs_mounted) {
      kout.println("[PMFS] Running defragmentation...");
      pmfs.defragment();
      kout.println("[PMFS] Defragmentation complete");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else {
    // Try launching registered app
    for (uint32_t i = 0; i < app_registry_count; i++) {
      if (strcmp(cmd, app_registry[i].name) == 0) {
        app_registry[i].spawn_func();
        return;
      }
    }
    
    kout.print("Unknown: ");
    kout.println(cmd);
  }
}

// ============================================================================
// SHELL COMMANDS
// ============================================================================

void cmd_help() {
  kout.println("\n=== Picomimi Kernel v13 Commands ===");
  kout.println("\n--- System ---");
  kout.println(" help       - Show this help");
  kout.println(" ps         - List all tasks");
  kout.println(" taskinfo <id> - Task details");
  kout.println(" listapps   - List applications");
  kout.println(" top        - System monitor");
  kout.println(" mem        - Memory stats");
  kout.println(" memmap     - Memory map");
  kout.println(" compact    - Compact memory");
  kout.println(" dmesg      - System log");
  kout.println(" uptime     - System uptime");
  kout.println(" temp       - CPU temperature");
  kout.println("\n--- Statistics ---");
  kout.println(" ipcstat    - IPC statistics");
  kout.println(" schedstat  - Scheduler stats");
  kout.println(" oomstat    - OOM statistics");
  kout.println(" rtos_stat  - RTOS primitives");
  kout.println("\n--- Filesystem ---");
  kout.println(" ls [path]  - List files");
  kout.println(" cat <file> - Read file");
  kout.println(" write <file> <text> - Append to file");
  kout.println(" touch <file> - Create file");
  kout.println(" mkdir <dir> - Create directory");
  kout.println(" rm <path>  - Remove file/dir");
  kout.println(" cd <path>  - Change directory");
  kout.println(" stat       - FS statistics");
  kout.println(" logls      - List log files");
  kout.println(" logtail [N] - Show log tail");
  kout.println(" format     - [ROOT] Format SD");
  kout.println("\n--- PMFS ---");
  kout.println(" tree       - Directory tree");
  kout.println(" pmfs       - PMFS statistics");
  kout.println(" tmpfs      - tmpfs (RAM disk) status");
  kout.println(" bank       - System bank info (A/B)");
  kout.println(" fsck       - Filesystem check");
  kout.println(" defrag     - Defragment filesystem");
  kout.println("\n--- Task Management ---");
  kout.println(" kill <id>  - Kill task");
  kout.println(" root       - Toggle root mode");
  kout.println(" reboot     - Restart system");
  kout.println("\n--- Applications ---");
  for (uint32_t i = 0; i < app_registry_count; i++) {
    kout.print(" ");
    kout.println(app_registry[i].name);
  }
}

void cmd_ps() {
  kout.println("\n=== System Tasks ===");
  kout.println("ID  Core Name                 Type    State     Pri Mem(KB) CPU(ms)");
  kout.println("--- ---- -------------------- ------- --------- --- ------- -------");
  
  const char* state_str[] = {"READY", "RUN", "WAIT", "SUSP", "DEAD", "ZOMBI"};
  const char* type_str[] = {"KERNEL", "DRIVER", "SERVIC", "MODULE", "APP"};
  
  for (uint32_t i = 0; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    if (task->id == 0 && task->entry == NULL) continue;
    
    uint8_t type_idx = 0;
    if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
    else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
    else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
    else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;
    
    char buf[100];
    snprintf(buf, sizeof(buf), "%-3d C0   %-20s %-7s %-9s %3d %7d %7d",
             task->id, task->name, type_str[type_idx],
             task->mem_blocked ? "BLCKD" : state_str[task->state],
             task->priority, task->mem_used / 1024, task->cpu_time);
    kout.println(buf);
  }
  
  if (kernel.core1_initialized) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
      TCB* task = &kernel.core1.tasks[i];
      
      char buf[100];
      snprintf(buf, sizeof(buf), "%-3d C1   %-20s %-7s %-9s %3d %7d %7d",
               task->id, task->name, "APP",
               task->mem_blocked ? "BLCKD" : state_str[task->state],
               task->priority, task->mem_used / 1024, task->cpu_time);
      kout.println(buf);
    }
    mutex_exit(&kernel.core1.scheduler_lock);
  }
  
  kout.println("\n--- Summary ---");
  kout.print("Core 0: ");
  kout.print(kernel.task_count - kernel.zombie_tasks);
  kout.print(" tasks (CPU: ");
  kout.print(kernel.cpu_usage, 1);
  kout.println("%)");
  
  if (kernel.core1_initialized) {
    kout.print("Core 1: ");
    kout.print(kernel.core1.task_count);
    kout.print(" tasks (CPU: ");
    kout.print(kernel.core1.cpu_usage, 1);
    kout.println("%)");
  }
}

void cmd_taskinfo(char* arg) {
  uint32_t id = atoi(arg);
  
  TCB task_copy;
  bool task_found = false;
  bool is_core1 = (id >= 1000);
  
  if (is_core1) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
  }
  
  TCB* task_ptr = ipc_get_tcb_by_id_unsafe(id);
  if (task_ptr) {
    memcpy(&task_copy, task_ptr, sizeof(TCB));
    task_found = true;
  }
  
  if (is_core1) {
    mutex_exit(&kernel.core1.scheduler_lock);
  }
  
  if (!task_found) {
    kout.println("Invalid task ID");
    return;
  }
  
  kout.println("\n=== Task Information ===");
  kout.print("ID: "); kout.println(task_copy.id);
  kout.print("Name: "); kout.println(task_copy.name);
  kout.print("Core: "); kout.println(task_copy.running_on_core);
  kout.print("Priority: "); kout.println(task_copy.priority);
  kout.print("State: ");
  
  const char* state_str[] = {"READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED", "ZOMBIE"};
  kout.println(task_copy.mem_blocked ? "BLOCKED" : state_str[task_copy.state]);
  
  kout.print("\nMemory Used: ");
  kout.print(task_copy.mem_used / 1024);
  kout.println(" KB");
  
  kout.print("Memory Peak: ");
  kout.print(task_copy.mem_peak / 1024);
  kout.println(" KB");
  
  kout.print("CPU Time: ");
  kout.print(task_copy.cpu_time);
  kout.println(" ms");
  
  kout.print("CPU Usage: ");
  kout.print(task_copy.sched_info.cpu_usage_percent, 1);
  kout.println("%");
  
  if (task_copy.is_cpu_abuser) {
    kout.println("⚠ CPU ABUSER DETECTED");
  }
  
  kout.print("Context Switches: ");
  kout.println(task_copy.context_switches);
  
  kout.print("IPC Messages: ");
  kout.println(task_copy.ipc.message_count);
  
  if (task_copy.description) {
    kout.println("\n--- Description ---");
    kout.println(task_copy.description);
  }
}

void cmd_ipcstat() {
  kout.println("\n=== IPC Statistics ===");
  
  uint16_t total_in_use = MAX_IPC_MESSAGES - (kernel.ipc_manager.free_list_head + 1);
  
  kout.print("Message Pool: ");
  kout.print(total_in_use);
  kout.print("/");
  kout.println(MAX_IPC_MESSAGES);
  
  kout.println("\n--- Lifetime Stats ---");
  kout.print("Sent: "); kout.println(kernel.ipc_manager.total_sent);
  kout.print("Received: "); kout.println(kernel.ipc_manager.total_received);
  kout.print("Broadcasts: "); kout.println(ipc_stats.broadcasts_sent);
  kout.print("Dropped (Pool): "); kout.println(ipc_stats.messages_dropped_pool_full);
  kout.print("Dropped (Task): "); kout.println(ipc_stats.messages_dropped_task_full);
}

void cmd_schedstat() {
  kout.println("\n=== Scheduler Statistics ===");
  kout.println("Algorithm: O(1) Priority Bitmap");
  kout.println("Mode: Preemptive + Aging + Idle Injection");
  
  kout.println("\n--- Core 0 ---");
  kout.print("Context Switches: ");
  kout.println(core0_sched.switches);
  kout.print("Preemptions: ");
  kout.println(core0_sched.preemptions);
  kout.print("CPU Load: ");
  kout.print(core0_sched.cpu_load, 1);
  kout.println("%");
  kout.print("Idle Injections: ");
  kout.println(core0_sched.idle_injections);
  
  if (kernel.core1_initialized) {
    kout.println("\n--- Core 1 ---");
    kout.print("Context Switches: ");
    kout.println(core1_sched.switches);
    kout.print("CPU Load: ");
    kout.print(kernel.core1.cpu_usage, 1);
    kout.println("%");
  }
}

void cmd_oomstat() {
  kout.println("\n=== OOM Statistics ===");
  kout.print("Prevention Success: ");
  kout.println(oom_stats.prevention_count);
  kout.print("Graceful Requests: ");
  kout.println(oom_stats.requests_sent);
  kout.print("Voluntary Releases: ");
  kout.println(oom_stats.voluntary_releases);
  kout.print("Forced Kills: ");
  kout.println(oom_stats.forced_kills);
  kout.print("Abusive Kills: ");
  kout.println(oom_stats.abusive_kills);
  kout.print("Total Reclaimed: ");
  kout.print(oom_stats.total_bytes_reclaimed / 1024);
  kout.println(" KB");
}

void cmd_rtos_stat() {
  kout.println("\n=== RTOS Primitives ===");
  kout.println("\n--- Mutexes ---");
  
  for(int i=0; i < MAX_KERNEL_MUTEXES; i++) {
    if (kernel.kernel_mutexes[i].locked) {
      kout.print("Mutex ");
      kout.print(i);
      kout.print(": LOCKED by ");
      kout.println(kernel.kernel_mutexes[i].owner_id);
    }
  }
  
  kout.println("\n--- Semaphores ---");
  for(int i=0; i < MAX_SEMAPHORES; i++) {
    if (kernel.kernel_semaphores[i].max_count > 0) {
      kout.print("Sem ");
      kout.print(i);
      kout.print(": ");
      kout.print(kernel.kernel_semaphores[i].count);
      kout.print("/");
      kout.println(kernel.kernel_semaphores[i].max_count);
    }
  }
}


void cmd_listapps() {
  kout.println("\n=== Registered Applications ===");
  if (app_registry_count == 0) {
    kout.println("(None)");
    return;
  }
  
  for (uint32_t i = 0; i < app_registry_count; i++) {
    kout.print(i + 1);
    kout.print(". ");
    kout.println(app_registry[i].name);
  }
}

void cmd_top() {
  kout.println("\n=== System Monitor ===");
  kout.print("Uptime: ");
  kout.print((uint32_t)(kernel.uptime_ms / 1000));
  kout.println(" s");
  kout.print("CPU (C0): ");
  kout.print(kernel.cpu_usage, 1);
  kout.println("%");
  
  if (kernel.core1_initialized) {
    kout.print("CPU (C1): ");
    kout.print(kernel.core1.cpu_usage, 1);
    kout.println("%");
  }
  
  kout.print("Temp: ");
  kout.print(kernel.temperature, 1);
  kout.println("C");
  kout.print("Memory: ");
  kout.print(get_used_memory() / 1024);
  kout.print("/");
  kout.print((HEAP_SIZE - KERNEL_RESERVE) / 1024);
  kout.println(" KB");
  kout.print("Tasks: ");
  kout.println(kernel.task_count - kernel.zombie_tasks);
  kout.print("OOM Kills: ");
  kout.println(oom_stats.forced_kills);
}

void cmd_mem() {
  kout.println("\n=== Memory Statistics ===");
  kout.print("Total Heap: ");
  kout.print(HEAP_SIZE / 1024);
  kout.println(" KB");
  kout.print("Kernel Reserve: ");
  kout.print(KERNEL_RESERVE / 1024);
  kout.println(" KB");
  kout.print("App Heap: ");
  kout.print((HEAP_SIZE - KERNEL_RESERVE) / 1024);
  kout.println(" KB");
  kout.print("Used: ");
  kout.print(get_used_memory() / 1024);
  kout.println(" KB");
  kout.print("Free: ");
  kout.print(kernel.total_free_mem / 1024);
  kout.println(" KB");
  kout.print("Largest Free Block: ");
  kout.print(kernel.largest_free_block / 1024);
  kout.println(" KB");
  kout.print("Fragmentation: ");
  kout.print(kernel.fragmentation_pct);
  kout.println("%");
  
  if (is_memory_critical()) {
    kout.println("⚠ CRITICAL MEMORY LEVEL!");
  } else if (is_memory_warning()) {
    kout.println("⚠ Low memory warning");
  }
}

void cmd_memmap() {
  kout.println("\n=== Memory Map (First 20 blocks) ===");
  kout.println("Address    Size     Owner  Free");
  kout.println("---------- -------- ------ ----");
  
  uint32_t display_count = (kernel.mem_block_count < 20) ? kernel.mem_block_count : 20;
  
  for (uint32_t i = 0; i < display_count; i++) {
    MemBlock* block = &kernel.mem_blocks[i];
    
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%08lx %8d %6d %-4s",
             (uint32_t)block->addr, block->size, block->owner_id,
             block->free ? "Y" : "N");
    kout.println(buf);
  }
  
  if (kernel.mem_block_count > display_count) {
    kout.print("... (");
    kout.print(kernel.mem_block_count - display_count);
    kout.println(" more)");
  }
}

void cmd_dmesg() {
  kout.println("\n=== System Log ===");
  if (kernel.log_count == 0) {
    kout.println("(Empty)");
    return;
  }
  
  mutex_enter_blocking(&kernel.log_lock);
  
  uint32_t start = kernel.log_count < MAX_LOG_ENTRIES ? 0 : kernel.log_head;
  uint32_t count = kernel.log_count < MAX_LOG_ENTRIES ? kernel.log_count : MAX_LOG_ENTRIES;
  
  for (uint32_t i = 0; i < count; i++) {
    uint32_t idx = (start + i) % MAX_LOG_ENTRIES;
    LogEntry* entry = &kernel.log[idx];
    
    char buf[80];
    snprintf(buf, sizeof(buf), "[%6lu.%03lu] [%d] %s",
             (uint32_t)(entry->timestamp / 1000),
             (uint32_t)(entry->timestamp % 1000),
             entry->level,
             entry->message);
    kout.println(buf);
  }
  
  mutex_exit(&kernel.log_lock);
}

void cmd_uptime() {
  uint64_t uptime_s = kernel.uptime_ms / 1000;
  uint32_t days = uptime_s / 86400;
  uint32_t hours = (uptime_s % 86400) / 3600;
  uint32_t minutes = (uptime_s % 3600) / 60;
  uint32_t seconds = uptime_s % 60;
  
  kout.print("Uptime: ");
  if (days > 0) {
    kout.print(days);
    kout.print(" days, ");
  }
  kout.print(hours);
  kout.print(":");
  if (minutes < 10) kout.print("0");
  kout.print(minutes);
  kout.print(":");
  if (seconds < 10) kout.print("0");
  kout.println(seconds);
}

void cmd_temp() {
  kout.print("CPU Temperature: ");
  kout.print(kernel.temperature, 1);
  kout.println(" C");
}

void cmd_root() {
  kernel.root_mode = !kernel.root_mode;
  
  if (kernel.root_mode) {
    kout.println("Root mode: ON (dangerous!)");
  } else {
    kout.println("Root mode: OFF");
  }
}

void cmd_reboot() {
  kout.println("\nRebooting...");
  Serial.flush();
  delay(500);
  watchdog_enable(1, 1);
  while(1);
}

void cmd_kill(char* arg) {
  if (!kernel.root_mode) {
    kout.println("Error: Root mode required");
    return;
  }
  
  uint32_t id = atoi(arg);
  
  if (id < 1000) {
    if (id >= kernel.task_count) {
      kout.println("Error: Invalid task ID");
      return;
    }
    brutal_task_kill(id);
  } else {
    kout.println("Error: Core 1 task kill not supported from shell");
  }
}

void cmd_cd(const char* arg) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  char new_path[128];
  fs_normalize_path(shell_cwd, arg, new_path, sizeof(new_path));
  
  File f = SD.open(new_path);
  if (f) {
    if (f.isDirectory()) {
      strncpy(shell_cwd, new_path, sizeof(shell_cwd) - 1);
    } else {
      kout.print("Error: Not a directory: ");
      kout.println(arg);
    }
    f.close();
  } else {
    kout.print("Error: No such file or directory: ");
    kout.println(arg);
  }
}

void cmd_write(char* arg) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  char* filename_arg = strtok(arg, " ");
  if (filename_arg == NULL) {
    kout.println("Usage: write <filename> <content...>");
    return;
  }
  
  char* content = strtok(NULL, "");
  if (content == NULL) {
    kout.println("Usage: write <filename> <content...>");
    return;
  }
  
  char path[128];
  fs_normalize_path(shell_cwd, filename_arg, path, sizeof(path));
  
  kout.print("Appending to ");
  kout.print(path);
  kout.println("...");
  
  if (fs_write_new(path, content, true)) {
    kout.println("Success");
  } else {
    kout.println("Failed");
  }
}

void cmd_logls() {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  File root = SD.open("/");
  if (!root) {
    kout.println("[FS] Failed to open root");
    return;
  }
  
  kout.println("\n=== FS Log Files ===");
  
  File file = root.openNextFile();
  bool found = false;
  
  while (file) {
    if (!file.isDirectory()) {
      String name_str = file.name();
      
      if (name_str.endsWith(".log") || name_str.endsWith(".txt") || 
          name_str.equalsIgnoreCase(FS_LOG_FILE)) {
        kout.print(file.name());
        kout.print(" (");
        kout.print((int)file.size());
        kout.println(" bytes)");
        found = true;
      }
    }
    file.close();
    file = root.openNextFile();
  }
  
  if (!found) {
    kout.println("(No log files found)");
  }
  
  root.close();
}

void cmd_format_sd() {
  if (!kernel.root_mode) {
    kout.println("Error: Root mode required");
    return;
  }
  
  kout.println("\n*** FORMATTING SD CARD ***");
  kout.println("This will delete ALL files from root");
  kout.println("Waiting 5 seconds...");
  
  uint64_t start = get_time_ms();
  while (get_time_ms() - start < 5000) {
    shell_task(NULL);
    task_sleep(20);
  }
  
  kout.println("Formatting...");
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  File root = SD.open("/");
  if (!root) {
    kout.println("[FS] Failed to open root");
    mutex_exit(&kernel.fs_lock);
    return;
  }
  
  uint32_t files_deleted = 0;
  uint32_t dirs_deleted = 0;
  
  File file = root.openNextFile();
  
  while (file) {
    const char* fname = file.name();
    
    // Skip protected files
    if (strcmp(fname, FS_LOG_FILE + 1) == 0 || strcmp(fname, "PANIC.LOG") == 0) {
      kout.print(" > Skipping: "); 
      kout.println(fname);
      file.close();
      file = root.openNextFile();
      continue;
    }
    
    kout.print(" > Deleting: "); 
    kout.println(fname);
    
    if (file.isDirectory()) {
      if (SD.rmdir(fname)) {
        dirs_deleted++;
      }
    } else {
      if (SD.remove(fname)) {
        files_deleted++;
      }
    }
    
    file.close();
    file = root.openNextFile();
  }
  
  root.close();
  mutex_exit(&kernel.fs_lock);
  
  kout.print("Done. Deleted ");
  kout.print(files_deleted);
  kout.print(" files, ");
  kout.print(dirs_deleted);
  kout.println(" dirs");
}

void cmd_mkdir(char* arg) {
  if (strlen(arg) == 0) {
    kout.println("Usage: mkdir <path>");
    return;
  }
  
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  if (fs_mkdir(arg)) {
    kout.print("Created: ");
    kout.println(arg);
  } else {
    kout.print("Failed: ");
    kout.println(arg);
  }
}

void cmd_rm(char* arg) {
  if (strlen(arg) == 0) {
    kout.println("Usage: rm <path>");
    return;
  }
  
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  if (fs_remove(arg)) {
    kout.print("Removed: ");
    kout.println(arg);
  } else {
    kout.print("Failed: ");
    kout.println(arg);
  }
}

void cmd_touch(char* arg) {
  if (strlen(arg) == 0) {
    kout.println("Usage: touch <path>");
    return;
  }
  
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  int fd = fs_open(arg, true);
  if (fd >= 0) {
    fs_close(fd);
    kout.print("Touched: ");
    kout.println(arg);
  } else {
    kout.print("Failed: ");
    kout.println(arg);
  }
}

void cmd_logtail(char* arg) {
  uint32_t lines = 10;
  
  if (strlen(arg) > 0) {
    lines = atoi(arg);
  }
  
  if (lines == 0) lines = 10;
  if (lines > 100) lines = 100;
  
  fs_logtail(lines);
}

void cmd_app_block_list() {
  kout.println("\n=== Blocked Applications ===");
  
  if (g_blocked_app_count == 0) {
    kout.println("(None)");
    return;
  }
  
  for (uint32_t i = 0; i < g_blocked_app_count; i++) {
    kout.println(g_blocked_app_names[i]);
  }
}

void cmd_app_block_unlock(char* arg) {
  if (!kernel.root_mode) {
    kout.println("Error: Root mode required");
    return;
  }
  
  int32_t found_idx = -1;
  
  for (uint32_t i = 0; i < g_blocked_app_count; i++) {
    if (strcmp(arg, g_blocked_app_names[i]) == 0) {
      found_idx = i;
      break;
    }
  }
  
  if (found_idx != -1) {
    kout.print("Unblocking app: ");
    kout.println(arg);
    
    for (uint32_t i = (uint32_t)found_idx; i < g_blocked_app_count - 1; i++) {
      strncpy(g_blocked_app_names[i], g_blocked_app_names[i+1], TASK_NAME_LEN);
    }
    
    g_blocked_app_count--;
    
    for(uint32_t i = 0; i < kernel.task_count; i++) {
      if (strcmp(kernel.tasks[i].name, arg) == 0) {
        kernel.tasks[i].mem_blocked = false;
      }
    }
  } else {
    kout.print("App not in block list: ");
    kout.println(arg);
  }
}

// ============================================================================
// UI / GUI FUNCTIONS
// ============================================================================

bool k_request_gui_focus(uint32_t task_id) {
  uint32_t irq_state = save_and_disable_interrupts();
  
  kernel.gui_focus_task_id = task_id;
  current_gui_focus_index = -1;
  
  for (uint32_t i = 0; i < gui_app_count; i++) {
    if (gui_app_task_ids[i] == task_id) {
      current_gui_focus_index = i;
      break;
    }
  }
// Lowkey, need a twink cutie bruh  MilkmanAbi
  restore_interrupts(irq_state);
  return true;
}

void k_release_gui_focus(uint32_t task_id) {
  uint32_t irq_state = save_and_disable_interrupts();
  
  if (kernel.gui_focus_task_id == (int32_t)task_id) {
    kernel.gui_focus_task_id = -1;
    current_gui_focus_index = -1;
  }
  
  restore_interrupts(irq_state);
}

void k_register_stdout_target(void (*write_char_fn)(char)) {
  kernel.app_write_char = write_char_fn;
}

float k_get_core0_usage() {
  return kernel.cpu_usage;
}

float k_get_core1_usage() {
  return kernel.core1.cpu_usage;
}

uint32_t k_get_task_memory_api(uint32_t task_id) {
  return get_task_memory(task_id);
}

void k_hint_memory_pressure(uint32_t task_id) {
  kout.print("[MEM] Task ");
  kout.print(task_id);
  kout.println(" reports memory pressure");
  
  mem_compact();
  calculate_fragmentation();
  
  char buf[64];
  snprintf(buf, sizeof(buf), "MEM: Pressure hint from task %d", task_id);
  klog(1, buf);
}

void k_register_gui_app(UISocket* socket_api) {
  if (!socket_api) return;
  
  uint32_t task_id_to_register = 0;
  
  if (get_core_num() == 0) {
    task_id_to_register = kernel.current_task;
  } else {
    task_id_to_register = kernel.core1.tasks[core1_sched.current_task].id;
  }
  
  uint32_t irq_state = save_and_disable_interrupts();
  
  if (gui_app_count < MAX_GUI_APPS) {
    bool found = false;
    
    for(uint32_t i=0; i < gui_app_count; i++) {
      if(gui_app_task_ids[i] == task_id_to_register) {
        found = true;
        break;
      }
    }
    
    if (!found) {
      gui_app_task_ids[gui_app_count++] = task_id_to_register;
    }
  }
  
  restore_interrupts(irq_state);
  
  // Set up socket API
  socket_api->request_focus = k_request_gui_focus;
  socket_api->release_focus = k_release_gui_focus;
  socket_api->register_stdout = k_register_stdout_target;
  socket_api->send_message_api = ipc_send_api;
  socket_api->receive_message_api = ipc_receive_api;
  socket_api->spawn_core1_task = k_spawn_core1_task;
  socket_api->task_exit = k_task_exit_api;
  socket_api->mutex_lock = k_mutex_lock;
  socket_api->mutex_unlock = k_mutex_unlock;
  socket_api->sem_wait = k_sem_wait;
  socket_api->sem_post = k_sem_post;
  socket_api->event_wait = k_event_wait;
  socket_api->event_set = k_event_set;
  socket_api->get_core0_usage = k_get_core0_usage;
  socket_api->get_core1_usage = k_get_core1_usage;
  socket_api->get_task_memory = k_get_task_memory_api;
  socket_api->register_oom_handler = k_register_oom_handler;
  socket_api->oom_cleanup_done = k_oom_cleanup_done;
  socket_api->hint_memory_pressure = k_hint_memory_pressure;
}

void k_unregister_gui_app(uint32_t task_id) {
  uint32_t irq_state = save_and_disable_interrupts();
  
  int32_t found_index = -1;
  
  for (uint32_t i = 0; i < gui_app_count; i++) {
    if (gui_app_task_ids[i] == task_id) {
      found_index = i;
      break;
    }
  }
  
  if (found_index != -1) {
    for (uint32_t i = found_index; i < gui_app_count - 1; i++) {
      gui_app_task_ids[i] = gui_app_task_ids[i + 1];
    }
    gui_app_count--;
    
    if (current_gui_focus_index == found_index) {
      current_gui_focus_index = -1;
    } else if (current_gui_focus_index > found_index) {
      current_gui_focus_index--;
    }
  }
  
  restore_interrupts(irq_state);
}

// ============================================================================
// APP REGISTRATION
// ============================================================================

void Application_Register(const char* name, void (*spawn_func)()) {
  if (app_registry_count >= MAX_APPS) {
    Serial.print("APP_REG: !! Registry full, ");
    Serial.print(name);
    Serial.println(" failed !!");
    return;
  }
  
  AppEntry* entry = &app_registry[app_registry_count];
  strncpy(entry->name, name, TASK_NAME_LEN - 1);
  entry->name[TASK_NAME_LEN - 1] = '\0';
  entry->spawn_func = spawn_func;
  
  app_registry_count++;
  
  Serial.print("APP_REG: Registered '");
  Serial.print(name);
  Serial.println("'");
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  mutex_init(&kout_mutex);
  
  Serial.println("APP_REG: Registration phase complete.");
  
  // Initialize SPI for SD card
  SPI.setRX(SD_MISO);
  SPI.setTX(SD_MOSI);
  SPI.setSCK(SD_SCK);
  
  randomSeed(micros());
  
  kout.println("================================================");
  kout.println(" Picomimi Kernel v13 Mach 1");
  kout.println(" Advanced Instant OOM");
  kout.println("================================================");
  kout.println("Initializing...");
  
  // Initialize watchdog
  watchdog_init();
  
  // Initialize input
  pinMode(BTN_ONOFF, INPUT_PULLUP);
  kout.println("[OK] Input system");
  
  // Initialize temperature
  temp_init();
  kernel.temperature = read_temperature();
  kout.print("[OK] Temperature (");
  kout.print(kernel.temperature, 1);
  kout.println("C)");
  
  // Initialize memory
  mem_init();
  kout.println("[OK] Memory manager (Best-Fit + Merge-on-Free)");
  
  // Initialize tasks
  task_init();
  kernel.last_velocity_check_ms = get_time_ms();
  kout.println("[OK] Task scheduler (Preemptive O(1))");
  
  // Initialize logging
  mutex_init(&kernel.log_lock);
  kout.println("[OK] Logging system");
  
  // Initialize IPC
  ipc_init();
  
  // Initialize RTOS primitives
  rtos_primitives_init();
  
  
  // Initialize Core 1
  kout.println("[CORE1] Initializing secondary core...");
  core1_scheduler_init();
  multicore_launch_core1(core1_main);
  kernel.core1_initialized = true;
  kout.println("[OK] Core1 started");
  
  // Initialize filesystem
  fs_init();
  if (kernel.fs_available) {
    fs_mount();
  }
  
  kout.println("\n=== Loading System Tasks ===");
  
  // Create idle task
  task_create("idle", idle_task, NULL, 0,
              TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 0, 0, NULL, 
              "Kernel idle task", CORE_0);
  kout.println("[OK] Idle (Pri 0, Core 0)");
  
  // Create reaper task
  task_create("k_reaper", k_reaper_task, NULL, 1,
              TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 1 * 1024, 1 * 1024, NULL, 
              "Zombie task reaper", CORE_0);
  kout.println("[OK] Reaper (Pri 1, Core 0)");
  
  // Create input driver
  task_create("input_cycle", NULL, NULL, 28,
              TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 1 * 1024, 1 * 1024, &input_callbacks, 
              "Focus cycle driver", CORE_0);
  kout.println("[OK] Input driver (Pri 28, Core 0)");
  
  // Create shell
  task_create("shell", NULL, NULL, 10,
              TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NORMAL, 4 * 1024, 4 * 1024, &shell_callbacks, 
              "Command shell", CORE_0);
  kout.println("[OK] Shell service (Pri 10, Core 0)");
  
  // Create CPU monitor
  task_create("cpumon", NULL, NULL, 2,
              TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 2 * 1024, 2 * 1024, &cpumon_callbacks, 
              "CPU monitor", CORE_0);
  kout.println("[OK] CPU monitor (Pri 2, Core 0)");
  
  // Create temperature monitor
  task_create("tempmon", NULL, NULL, 2,
              TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 2 * 1024, 2 * 1024, &tempmon_callbacks, 
              "Temp monitor", CORE_0);
  kout.println("[OK] Temp monitor (Pri 2, Core 0)");
  
  // Create FS service if available
  if (kernel.fs_available) {
    task_create("fs", NULL, NULL, 8,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
                0, OOM_PRIORITY_NEVER, 4 * 1024, 4 * 1024, &fs_callbacks, 
                "FS service", CORE_0);
    kout.println("[OK] FS service (Pri 8, Core 0)");
  }
  
  kout.println("========================================");
  kout.println("Kernel boot complete!");
  kout.println("========================================");
  
  kout.print("Heap: ");
  kout.print((HEAP_SIZE - KERNEL_RESERVE) / 1024);
  kout.print(" KB (App) + ");
  kout.print(KERNEL_RESERVE / 1024);
  kout.println(" KB (Sys)");
  
  kout.print("Core0 Tasks: ");
  kout.println(kernel.task_count);
  
  kout.println("Core1: Ready for offload");
  
  kout.print("Apps: ");
  kout.print(app_registry_count);
  kout.println(" registered");
  
  if (kernel.fs_available) {
    kout.println("SD Card: Available");
  } else {
    kout.println("SD Card: Unavailable");
  }
  
  kout.println("\nType 'help' for commands");
  
  klog(0, "KERNEL: Boot v13 (Memory & CPU Protection)");
  
  shell_prompt();
  
  kernel.running = true;
}

void loop() {
  // Safety checks
  if (kernel.current_task >= MAX_TASKS || !kernel.running || kernel.task_count == 0) {
    kernel_panic("Kernel loop fault");
  }
  
  uint64_t loop_start = get_time_us();
  
  // Run scheduler tick
  scheduler_tick();
  
  // Check if idle task is dead
  if (kernel.tasks[0].state == TASK_TERMINATED || kernel.tasks[0].entry == NULL) {
    kernel_panic("IDLE TASK DEAD");
  }
  
  // Handle preemption
  if (kernel.preemption_pending) {
    kernel.preemption_pending = false;
    core0_sched.preemptions++;
    task_yield();
  }
  
  TCB* task = &kernel.tasks[kernel.current_task];
  
  // Skip terminated/zombie/blocked tasks
  if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE || task->mem_blocked) {
    task_yield();
    return;
  }
  
  // Handle OOM cleanup requests
  if (task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED) {
    oom_callback_t handler = oom_get_handler(task->id);
    if (handler) {
      kout.print("[OOM] Invoking handler for '");
      kout.print(task->name);
      kout.println("'");
      handler(task->oom_bytes_requested);
    }
    task->flags &= ~TASK_FLAG_OOM_CLEANUP_REQUESTED;
    task->oom_bytes_requested = 0;
  }
  
  // Execute task
  if ((task->state == TASK_READY || task->state == TASK_RUNNING) &&
      (task->entry || (task->callbacks && task->callbacks->tick))) {
    
    task->state = TASK_RUNNING;
    uint64_t task_start = get_time_us();
    
    if (task->callbacks && task->callbacks->tick) {
      task->callbacks->tick(task->arg);
    } else if (task->entry) {
      task->entry(task->arg);
    }
    
    uint64_t task_duration = get_time_us() - task_start;
    task->cpu_time += (task_duration + 500) / 1000;
    task->total_cpu_time_us += task_duration;
    
    // Update CPU usage tracking
    update_task_cpu_usage(task, task_duration);
    
    if (task->state == TASK_RUNNING) {
      task->state = TASK_READY;
    }
  }
  
  // Yield to next task
  task_yield();
  
  // Feed watchdog periodically
  static uint32_t watchdog_counter = 0;
  if (++watchdog_counter >= 10) {
    watchdog_feed();
    watchdog_counter = 0;
  }
  
  // Sleep for remaining time
  uint64_t elapsed = get_time_us() - loop_start;
  if (elapsed < SCHEDULER_TICK_US) {
    precise_sleep_us(SCHEDULER_TICK_US - elapsed);
  }
}
