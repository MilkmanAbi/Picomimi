// Picomimi v10.0.0 - Grand Feature Release
// Unified Microkernel
//
// Features merged from v10_REFINE_MANIFEST:
// - O(1) Bitmap Scheduler (Core0 & Core1)
// - Priority-Aware IPC System with Flow Control
// - Graceful OOM Killer with App Callbacks
// - Hardware Watchdog Integration
// - Kernel Panic Handler
// - Unified 'ps' command
// - New 'taskinfo', 'schedstat', 'oomstat' commands
// - Upgraded UISocket API with OOM/Priority functions

// --- KERNEL INCLUDES ---
#include <SPI.h>
#include <SD.h>
#include <hardware/adc.h>
#include <hardware/watchdog.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <pico/platform.h>
#include <pico/multicore.h>
#include <pico/mutex.h>

// --- KERNEL MACROS ---
#define disable_all_interrupts() __asm__ volatile ("cpsid i" : : : "memory")
#define enable_all_interrupts() __asm__ volatile ("cpsie i" : : : "memory")

// --- CORE PIN DEFINITIONS ---
#define SD_CS       5
#define SD_MOSI     19
#define SD_MISO     16
#define SD_SCK      18
#define BTN_ONOFF   9

// --- Kernel & System Constants ---
#define MAX_TASKS 32
#define MAX_MEMORY_BLOCKS 256
#define HEAP_SIZE (180 * 1024)
#define TASK_NAME_LEN 24
#define MAX_LOG_ENTRIES 40
#define SCHEDULER_TICK_US 1000 // Base tick for main loop
#define WATCHDOG_TIMEOUT_MS 8000  // 8 second timeout

// --- DUAL CORE CONSTANTS ---
#define MAX_CORE1_TASKS 16
#define CORE1_STACK_SIZE (8 * 1024)
#define MAX_IPC_MESSAGES 32
#define IPC_MSG_SIZE 64

// --- VFS CONSTANTS ---
#define VFS_BLOCK_SIZE 256
#define VFS_MAX_FILES 16
#define VFS_FILENAME_LEN 16
#define VFS_MAX_FILE_SIZE (16 * 1024)
#define VFS_STORAGE_SIZE (128 * 1024)
#define VFS_FLASH_OFFSET (1024 * 1024)
#define VFS_MAX_BLOCKS_PER_FILE 64

// --- FS CONSTANTS ---
#define FS_MAX_FILENAME 32
#define FS_MAX_OPEN_FILES 8
#define FS_BUFFER_SIZE 512
#define FS_LOG_FILE "/LogRecord"

// --- APP REGISTRY CONSTANTS ---
#define MAX_APPS 16
#define MAX_GUI_APPS 8

// --- RESOURCE LOCKS ---
#define MAX_RESOURCES 16

// --- SCHEDULER CONSTANTS (O(1) BITMAP) ---
#define SCHED_NUM_PRIORITY_LEVELS 32  // 0-31, higher = more important
#define SCHED_RT_THRESHOLD 24         // Priorities >= 24 are real-time
#define SCHED_BASE_QUANTUM_US 5000    // 5ms base quantum
#define SCHED_MAX_QUANTUM_US 80000    // 80ms max quantum
#define SCHED_AGING_INTERVAL_MS 500   // Boost aging every 500ms
#define SCHED_IDLE_INJECTION_THRESHOLD 85  // Inject idle if CPU > 85%

// --- OOM KILLER CONSTANTS ---
#define OOM_REQUEST_TIMEOUT_MS 2000  // Give app 2 seconds to clean up
#define MAX_OOM_HANDLERS 16

// --- TASK DEFINITIONS ---
enum TaskState : uint8_t {
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING,
    TASK_SUSPENDED,
    TASK_TERMINATED,
    TASK_ZOMBIE
};

enum CoreAffinity : uint8_t {
    CORE_ANY = 0,
    CORE_0 = 1,
    CORE_1 = 2
};

#define TASK_TYPE_KERNEL      0x01
#define TASK_TYPE_DRIVER      0x02
#define TASK_TYPE_SERVICE     0x04
#define TASK_TYPE_MODULE      0x08
#define TASK_TYPE_APPLICATION 0x10

#define TASK_FLAG_PROTECTED   0x01
#define TASK_FLAG_CRITICAL    0x02
#define TASK_FLAG_RESPAWN     0x04
#define TASK_FLAG_ONESHOT     0x08
#define TASK_FLAG_PERSISTENT  0x10

#define OOM_PRIORITY_NEVER      0
#define OOM_PRIORITY_CRITICAL   1
#define OOM_PRIORITY_HIGH       2
#define OOM_PRIORITY_NORMAL     3
#define OOM_PRIORITY_LOW        4

// --- FILETYPE DEFINITIONS ---
#define FILE_TYPE_TEXT    0x01
#define FILE_TYPE_LOG     0x02
#define FILE_TYPE_DATA    0x03
#define FILE_TYPE_CONFIG  0x04

// --- IPC MESSAGE TYPES ---
enum IPCMessageType : uint8_t {
    IPC_NONE = 0,
    IPC_RENDER_FRAME,
    IPC_PROCESS_INPUT,
    IPC_COMPUTE_DATA,
    IPC_AUDIO_SAMPLE,
    IPC_USER_DEFINED
};

// --- IPC MESSAGE STRUCTURE (Enhanced) ---
struct IPCMessage {
    uint32_t sender_id;
    uint32_t target_id;
    IPCMessageType type;
    uint8_t priority;           // 0-255, higher = more urgent
    uint64_t timestamp;
    uint16_t sequence;          // For ordering within same priority
    uint8_t retry_count;
    uint8_t flags;              // For future extensions
    uint8_t data[IPC_MSG_SIZE];
    bool in_use;
} __attribute__((packed));

// --- IPC QUEUE (Priority-based) ---
#define IPC_QUEUE_HIGH_PRI    0
#define IPC_QUEUE_NORMAL_PRI  1
#define IPC_QUEUE_LOW_PRI     2
#define IPC_NUM_QUEUES        3

struct IPCQueue {
    IPCMessage messages[MAX_IPC_MESSAGES];
    uint16_t queue_heads[IPC_NUM_QUEUES];     // Head index for each priority queue
    uint16_t queue_tails[IPC_NUM_QUEUES];     // Tail index for each priority queue
    uint16_t queue_counts[IPC_NUM_QUEUES];    // Count for each priority queue
    uint16_t total_count;
    uint16_t sequence_counter;
    uint32_t dropped_messages;                 // Track overflows
    uint32_t total_sent;
    uint32_t total_received;
    mutex_t lock;
    
    // Flow control
    bool flow_control_active;
    uint32_t flow_control_threshold;          // Start throttling at this count
};

// --- IPC STATISTICS ---
struct IPCStats {
    uint32_t messages_sent[IPC_NUM_QUEUES];
    uint32_t messages_received[IPC_NUM_QUEUES];
    uint32_t messages_dropped[IPC_NUM_QUEUES];
    uint32_t queue_full_events;
    uint32_t flow_control_events;
    float avg_queue_depth;
    uint32_t max_queue_depth;
};

// --- RESOURCE LOCK STRUCTURE ---
struct ResourceLock {
    mutex_t mutex;
    uint32_t owner_id;
    bool in_use;
};

// --- Application Registration Struct ---
struct AppEntry {
    char name[TASK_NAME_LEN];
    void (*spawn_func)();
};

// --- MODULE CALLBACKS ---
struct ModuleCallbacks {
    void (*init)(uint32_t id);
    void (*tick)(void*);
    void (*deinit)();
};

// --- Task Scheduling Info (O(1) Scheduler) ---
struct TaskSchedInfo {
    uint8_t effective_priority;     // Current scheduling priority (changes with aging)
    uint8_t base_priority;          // Original priority
    uint32_t quantum_us;            // Current time quantum (FIXED: uint16_t to uint32_t)
    uint32_t cpu_time_slice;        // Time used in current slice
    uint64_t last_run;              // Last execution timestamp
    uint64_t total_runtime_us;      // Total CPU time
    uint32_t voluntary_yields;      // Count of early yields
    uint32_t preemptions;           // Count of forced preemptions
    uint8_t cpu_affinity;           // Preferred CPU (0, 1, or 0xFF for any)
    uint8_t age;                    // Starvation prevention counter
    bool is_realtime;               // Real-time task flag
} __attribute__((packed));

// --- TASK CONTROL BLOCK ---
struct TCB {
    uint32_t id;
    TaskState state;
    uint8_t task_type;
    uint8_t oom_priority;
    uint8_t priority;           // This is now managed by the scheduler
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
    uint32_t cpu_time;          // Total CPU time in ms (for stats)
    uint32_t last_run;          // Kept for oom_killer
    uint32_t page_faults;
    uint32_t context_switches;
    const char* description;
    
    // Core tracking
    uint8_t running_on_core;
    
    // Scheduler info
    TaskSchedInfo sched_info;
} __attribute__((aligned(64)));

// --- MEMORY BLOCK ---
struct MemBlock {
    void* addr;
    uint32_t size;
    uint32_t owner_id;
    uint32_t alloc_seq;
    uint32_t alloc_time;
    bool free;
    uint8_t _padding[3];
} __attribute__((packed));

// --- LOG ENTRY ---
struct LogEntry {
    uint64_t timestamp;
    char message[56];
    uint8_t level;
    uint8_t _padding[7];
} __attribute__((aligned(8)));

// --- VFS STRUCTURES ---
struct VFSBlockChain {
    uint16_t blocks[VFS_MAX_BLOCKS_PER_FILE];
    uint8_t block_count;
};

struct VFSFile {
    char name[VFS_FILENAME_LEN];
    uint8_t type;
    bool in_use;
    uint16_t _padding;
    uint32_t size;
    uint32_t created;
    uint32_t modified;
    uint32_t owner_id;
    VFSBlockChain chain;
} __attribute__((packed));

struct VFSSuperblock {
    uint32_t magic;
    uint32_t version;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t file_count;
    uint8_t block_bitmap[VFS_STORAGE_SIZE / VFS_BLOCK_SIZE / 8];
    VFSFile files[VFS_MAX_FILES];
} __attribute__((packed));

// --- FS FILE HANDLE ---
struct FSFile {
    File handle;
    char path[FS_MAX_FILENAME];
    bool open;
    bool write_mode;
    uint32_t owner_task_id;
};

// --- SD CARD INFO ---
struct SDCardInfo {
    uint8_t card_type;
    uint64_t card_size;
    uint32_t sector_count;
    uint16_t sector_size;
    bool valid;
};

// --- CORE1 SCHEDULER STATE ---
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

// --- OOM HANDLER STRUCTS ---
typedef void (*oom_callback_t)(uint32_t bytes_requested);

struct TaskOOMHandler {
    uint32_t task_id;
    oom_callback_t callback;
    bool active;
};

struct OOMRequest {
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
};

// --- MOVED OOMVictim STRUCT ---
// (Moved from line 2202 to here to fix compilation error)
struct OOMVictim {
    uint32_t task_id;
    uint32_t memory_used;
    uint8_t oom_priority;
    int32_t score;  // Higher = better victim
    bool has_handler;
};

// --- O(1) SCHEDULER STRUCTS ---
struct PriorityBitmap {
    uint32_t level_mask;            // Bit set = priority level has tasks
    uint32_t task_masks[SCHED_NUM_PRIORITY_LEVELS]; // Tasks per priority
};

struct CoreScheduler {
    PriorityBitmap runnable;        // Runnable tasks bitmap
    PriorityBitmap waiting;         // Waiting tasks bitmap
    
    uint32_t current_task;
    uint32_t idle_task;
    uint8_t current_priority;
    
    uint64_t last_switch;
    uint64_t total_runtime;
    uint64_t idle_time;
    
    uint32_t switches;
    uint32_t preemptions;
    uint32_t idle_injections;
    
    float cpu_load;                 // Exponentially weighted average
    float cpu_load_instant;         // Instant CPU load
    
    bool idle_injection_active;
    uint64_t last_aging;
    
    mutex_t lock;
} __attribute__((aligned(64)));

// --- KERNEL PANIC STRUCTS ---
struct PanicInfo {
    const char* reason;
    uint32_t task_id;
    uint32_t pc;           // Program counter
    uint32_t lr;           // Link register
    uint32_t sp;           // Stack pointer
    uint64_t timestamp;
    bool is_core1;
};

struct WatchdogState {
    bool enabled;
    uint64_t last_feed;
    uint32_t timeout_ms;
    uint32_t triggers;
    bool in_panic;
};

// --- ENHANCED UISocket API ---
struct UISocket {
    // Focus management
    bool (*request_focus)(uint32_t task_id);
    void (*release_focus)(uint32_t task_id);
    
    // Output redirection
    void (*register_stdout)(void (*write_char_fn)(char));
    
    // IPC (with priority control)
    bool (*send_to_core1)(uint32_t target_id, IPCMessageType type, void* data, size_t size);
    bool (*send_priority)(uint32_t target_id, IPCMessageType type, void* data, size_t size, uint8_t priority);
    bool (*receive_message)(IPCMessage* msg_out);
    
    // Core1 task management
    uint32_t (*spawn_core1_task)(const char* name, void (*entry)(void*), void* arg, uint8_t priority);
    
    // Resource locking
    bool (*lock_resource)(uint32_t resource_id);
    void (*unlock_resource)(uint32_t resource_id);
    
    // System monitoring
    float (*get_core0_usage)();
    float (*get_core1_usage)();
    uint32_t (*get_task_memory)(uint32_t task_id);
    
    // OOM handling (NEW)
    void (*register_oom_handler)(uint32_t task_id, void (*handler)(uint32_t bytes_requested));
    void (*oom_cleanup_done)(uint32_t task_id, uint32_t bytes_freed);
    
    // Memory hints (NEW)
    void (*hint_memory_pressure)(uint32_t task_id);  // App can signal it's under pressure
};

// --- KERNEL STATE STRUCT ---
struct KernelState {
    // Core 0 tasks
    TCB tasks[MAX_TASKS];
    uint32_t task_count;
    uint32_t current_task;
    
    // Memory management
    MemBlock mem_blocks[MAX_MEMORY_BLOCKS];
    uint32_t mem_block_count;
    mutex_t mem_lock;
    
    // System state
    uint64_t uptime_ms;
    bool running;
    bool panic_mode;
    
    // Memory stats
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t oom_kills;
    uint32_t alloc_sequence;
    uint32_t fragmentation_pct;
    uint32_t largest_free_block;
    
    // Task type counters
    uint8_t kernel_tasks;
    uint8_t driver_tasks;
    uint8_t service_tasks;
    uint8_t module_tasks;
    uint8_t application_tasks;
    
    // Service states
    bool shell_alive;
    bool cpumon_alive;
    bool tempmon_alive;
    bool vfs_alive;
    bool fs_alive;
    bool root_mode;
    
    // Performance
    float cpu_usage;
    float temperature;
    uint32_t total_context_switches;
    
    // Logging
    LogEntry log[MAX_LOG_ENTRIES];
    uint32_t log_head;
    uint32_t log_count;
    mutex_t log_lock;
    
    // VFS
    VFSSuperblock* vfs_sb;
    uint8_t* vfs_data;
    bool vfs_mounted;
    bool vfs_active;
    uint32_t vfs_writes;
    uint32_t vfs_reads;
    
    // FS
    bool fs_available;
    bool fs_mounted;
    SDCardInfo sd_info;
    uint64_t fs_used_bytes;
    uint32_t fs_reads;
    uint32_t fs_writes;
    uint32_t fs_log_counter;
    FSFile fs_open_files[FS_MAX_OPEN_FILES];
    mutex_t fs_lock;
    
    // GUI & App state
    int32_t gui_focus_task_id;
    void (*app_write_char)(char);
    
    // Dual-core state
    Core1State core1;
    IPCQueue ipc_queue;
    ResourceLock resources[MAX_RESOURCES];
    bool core1_initialized;
    
    uint8_t heap[HEAP_SIZE];
} __attribute__((aligned(64)));

// --- GLOBAL KERNEL STATE ---
static KernelState kernel __attribute__((aligned(64)));

// --- GLOBAL SHELL STATE ---
static char cmd_buffer[128];
static uint32_t cmd_pos = 0;

// --- GLOBAL APP/GUI STATE ---
static AppEntry app_registry[MAX_APPS];
static uint32_t app_registry_count = 0;
static uint32_t gui_app_task_ids[MAX_GUI_APPS];
static uint32_t gui_app_count = 0;
static int32_t current_gui_focus_index = -1;

// --- GLOBAL IPC STATS ---
static IPCStats ipc_stats;

// --- GLOBAL OOM STATE ---
static TaskOOMHandler oom_handlers[MAX_OOM_HANDLERS];
static OOMRequest oom_current_request = {0};
static OOMStats oom_stats = {0};

// --- GLOBAL O(1) SCHEDULER STATE ---
static CoreScheduler core0_sched;
static CoreScheduler core1_sched;

// --- GLOBAL KERNEL PANIC/WATCHDOG STATE ---
static PanicInfo last_panic;
static bool in_panic = false;
static WatchdogState watchdog_state = {false, 0, WATCHDOG_TIMEOUT_MS, 0, false};


// --- TIME & PIN UTILITIES ---
static inline uint64_t get_time_us() {
    return micros();
}

static inline uint64_t get_time_ms() {
    return millis();
}

static inline void precise_sleep_us(uint32_t us) {
    if (us == 0) return;
    delayMicroseconds(us);
}

static inline bool gpio_read_fast(uint8_t pin) {
    return digitalRead(pin) == LOW;
}

// --- MultiPrint Class ---
class MultiPrint : public Print {
public:
    virtual size_t write(uint8_t c) {
        Serial.write(c);
        if (kernel.app_write_char) {
            kernel.app_write_char(c);
        }
        return 1;
    }

    virtual size_t write(const uint8_t *buffer, size_t size) {
        for(size_t i = 0; i < size; i++) {
            write(buffer[i]);
        }
        return size;
    }
};
MultiPrint kout; // Define kout HERE

// --- FORWARD DECLARATIONS ---

// Panic & Watchdog
__attribute__((noreturn))
void kernel_panic(const char* reason);
void watchdog_init();
void watchdog_feed();
void watchdog_check();

// Time & Mem
// get_time_us() and get_time_ms() are inline
size_t get_free_memory();
size_t get_used_memory();
size_t get_task_memory(uint32_t task_id);
void mem_compact();

// Tasks & Core1
void core1_main();
void scheduler_tick();
void task_yield();
uint32_t task_create(const char* name, void (*entry)(void*), void* arg, 
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, ModuleCallbacks* callbacks,
                     const char* description);
void brutal_task_kill(uint32_t id);
void task_sleep(uint32_t ms);

// Kernel Tasks
void idle_task(void* arg);
void shell_task(void* arg);
void shell_deinit();
void input_task(void* arg);
void cpu_monitor_task(void* arg);
void cpumon_deinit();
void temp_monitor_task(void* arg);
void tempmon_deinit();
void vfs_task(void* arg);
void fs_task(void* arg);
void fs_deinit();

// VFS
bool vfs_mount();
void vfs_unmount();
void vfs_list();
void vfs_stats();

// FS
void fs_unmount();
void fs_list(const char* path);
void fs_stats();
void fs_cat(const char* path);

// Shell Commands
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
void cmd_reboot();
void cmd_kill(char* arg);
void cmd_root();

// Init
void temp_init();
float read_temperature();
void mem_init();
void task_init();
void ipc_init();
void resource_locks_init();
void core1_scheduler_init();
void vfs_init();
void fs_init();
bool fs_mount();
void klog(uint8_t level, const char* msg);
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);

// --- END FORWARD DECLARATIONS ---


void vfs_deinit() {
    kout.println("[VFS] DEINIT");
    vfs_unmount();
    if (kernel.vfs_data) {
        kfree(kernel.vfs_data);
        kernel.vfs_data = NULL;
    }
    if (kernel.vfs_sb) {
        kfree(kernel.vfs_sb);
        kernel.vfs_sb = NULL;
    }
    kernel.vfs_active = false;
    kernel.vfs_alive = false;
}

void fs_task(void* arg) {
    if (!kernel.fs_alive) {
        task_sleep(10000);
        return;
    }
    task_sleep(10000);
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

// Module callbacks
ModuleCallbacks shell_callbacks = { NULL, shell_task, shell_deinit };
ModuleCallbacks input_callbacks = { NULL, input_task, NULL };
ModuleCallbacks cpumon_callbacks = { NULL, cpu_monitor_task, cpumon_deinit };
ModuleCallbacks tempmon_callbacks = { NULL, temp_monitor_task, tempmon_deinit };
ModuleCallbacks vfs_callbacks = { NULL, vfs_task, vfs_deinit };
ModuleCallbacks fs_callbacks = { NULL, fs_task, fs_deinit };

// ============================================================================
// KERNEL PANIC & WATCHDOG
// ============================================================================

__attribute__((noreturn))
void kernel_panic(const char* reason) {
    // Prevent recursive panics
    if (in_panic) {
        while(1) { watchdog_update(); __asm__ volatile ("wfi"); }
    }
    in_panic = true;
    watchdog_state.in_panic = true;
    
    // Disable interrupts
    disable_all_interrupts();
    
    // Record panic info
    last_panic.reason = reason;
    last_panic.task_id = kernel.current_task;
    last_panic.timestamp = get_time_ms();
    last_panic.is_core1 = (get_core_num() == 1);
    
    // Try to get registers (architecture specific)
    // last_panic.sp = __get_MSP();
    
    // Print panic info
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║     *** KERNEL PANIC *** ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    
    Serial.print("Reason:    ");
    Serial.println(reason);
    
    Serial.print("Core:      ");
    Serial.println(last_panic.is_core1 ? "1" : "0");
    
    Serial.print("Task:      ");
    if (last_panic.task_id < kernel.task_count) {
        Serial.print(kernel.tasks[last_panic.task_id].name);
        Serial.print(" (ID=");
        Serial.print(last_panic.task_id);
        Serial.println(")");
    } else {
        Serial.println("UNKNOWN");
    }
    
    Serial.print("Uptime:    ");
    Serial.print((uint32_t)(last_panic.timestamp / 1000));
    Serial.println(" s");
    
    Serial.println();
    Serial.println("--- System State ---");
    Serial.print("Tasks:     ");
    Serial.println(kernel.task_count);
    Serial.print("Memory:    ");
    Serial.print(get_used_memory() / 1024);
    Serial.print("/");
    Serial.print(HEAP_SIZE / 1024);
    Serial.println(" KB");
    Serial.print("CPU Usage: ");
    Serial.print(kernel.cpu_usage, 1);
    Serial.println("%");
    
    Serial.println();
    Serial.println("System halted. Watchdog will reset in 8s...");
    Serial.flush();
    
    // Write panic to FS if available
    if (kernel.fs_mounted) {
        File panic_file = SD.open("/PANIC.LOG", FILE_WRITE);
        if (panic_file) {
            panic_file.print("[");
            panic_file.print((uint32_t)last_panic.timestamp);
            panic_file.print("] ");
            panic_file.println(reason);
            panic_file.close();
        }
    }
    
    // Halt and wait for watchdog reset
    while(1) {
        watchdog_update();  // Keep feeding so it doesn't reset too quickly
        delay(100);
        if (get_time_ms() - last_panic.timestamp > 8000) {
            // Stop feeding, let watchdog reset us
            while(1) { __asm__ volatile ("wfi"); }
        }
    }
}

void watchdog_init() {
    if (watchdog_caused_reboot()) {
        kout.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!");
        kout.println("!!! REBOOT BY WATCHDOG !!!");
        kout.println("!!!!!!!!!!!!!!!!!!!!!!!!!!");
        watchdog_state.triggers++; // This will reset, but good to know
    }

    watchdog_state.enabled = true;
    watchdog_state.last_feed = get_time_ms();
    watchdog_state.in_panic = false;
    
    // Enable hardware watchdog
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
        // 75% of timeout reached - warn
        kout.println("\n*** WATCHDOG WARNING: No feed in ");
        kout.print((uint32_t)elapsed);
        kout.println(" ms ***");
        
        klog(2, "WDT: Feed timeout approaching!");
    }
}

// ============================================================================
// SHELL COMMANDS
// ============================================================================

void shell_prompt() {
    if (kernel.root_mode) {
        kout.print("Picomimi# ");
    } else {
        kout.print("Picomimi~> ");
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
    else if (strcmp(cmd, "listapps") == 0) cmd_listapps();
    else if (strcmp(cmd, "top") == 0) cmd_top();
    else if (strcmp(cmd, "mem") == 0) cmd_mem();
    else if (strcmp(cmd, "memmap") == 0) cmd_memmap();
    else if (strcmp(cmd, "compact") == 0) {
        kout.println("Compacting memory...");
        mem_compact();
        kout.println("Done");
    }
    else if (strcmp(cmd, "dmesg") == 0) cmd_dmesg();
    else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
    else if (strcmp(cmd, "temp") == 0) cmd_temp();
    else if (strcmp(cmd, "root") == 0) cmd_root();
    else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
    else if (strncmp(cmd, "kill ", 5) == 0) cmd_kill(cmd + 5);
    else if (strcmp(cmd, "vfscreate") == 0) {
        if (kernel.vfs_active) {
            kout.println("VFS already active");
            return;
        }
        kernel.vfs_sb = (VFSSuperblock*)kmalloc(sizeof(VFSSuperblock), 0);
        if (!kernel.vfs_sb) {
            kout.println("[VFS] Failed to allocate superblock");
            return;
        }
        kernel.vfs_data = (uint8_t*)kmalloc(VFS_STORAGE_SIZE, 0);
        if (!kernel.vfs_data) {
            kfree(kernel.vfs_sb);
            kernel.vfs_sb = NULL;
            kout.println("[VFS] Failed to allocate data");
            return;
        }
        kernel.vfs_active = true;
        vfs_mount();
    }
    else if (strcmp(cmd, "vfsls") == 0) vfs_list();
    else if (strcmp(cmd, "vfsstat") == 0) vfs_stats();
    else if (strcmp(cmd, "ls") == 0) fs_list("/");
    else if (strcmp(cmd, "stat") == 0) fs_stats();
    else if (strncmp(cmd, "cat ", 4) == 0) {
        fs_cat(cmd + 4);
    }
    else {
        // Try to launch app
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

void cmd_help() {
    kout.println("\n=== System Commands ===");
    kout.println("  help       - Show this help");
    kout.println("  ps         - List all tasks (both cores)");
    kout.println("  taskinfo <id> - Detailed task info");
    kout.println("  listapps   - List applications");
    kout.println("  top        - System monitor");
    kout.println("  mem        - Memory statistics");
    kout.println("  memmap     - Memory map");
    kout.println("  compact    - Memory compaction");
    kout.println("  dmesg      - System log");
    kout.println("  uptime     - System uptime");
    kout.println("  temp       - CPU temperature");
    kout.println("  ipcstat    - IPC statistics");
    kout.println("  schedstat  - Scheduler statistics");
    kout.println("  oomstat    - OOM statistics");
    kout.println("  reboot     - Restart system");
    
    kout.println("\n=== VFS Commands ===");
    kout.println("  vfscreate  - Create VFS");
    kout.println("  vfsls      - List VFS files");
    kout.println("  vfsstat    - VFS statistics");
    
    kout.println("\n=== FS Commands ===");
    kout.println("  ls [path]  - List SD files");
    kout.println("  stat       - FS statistics");
    kout.println("  cat <path> - Read file");
    
    kout.println("\n=== Task Management ===");
    kout.println("  kill <id>  - Kill task");
    kout.println("  root       - Toggle root mode");
    
    kout.println("\n=== Applications ===");
    if (app_registry_count == 0) {
        kout.println("  (None registered)");
    }
    for (uint32_t i = 0; i < app_registry_count; i++) {
        kout.print("  ");
        kout.println(app_registry[i].name);
    }
}

void cmd_ps() {
    kout.println("\n=== System Tasks ===");
    kout.println("ID   Core Name                 Type      State     Pri  Mem(KB) CPU(ms)");
    kout.println("---- ---- -------------------- --------- --------- ---  ------- -------");
    
    const char* state_str[] = {"READY", "RUN", "WAIT", "SUSP", "DEAD", "ZOMBI"};
    const char* type_str[] = {"KERNEL", "DRIVER", "SERVIC", "MODULE", "APP"};
    
    // Show Core 0 tasks
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        
        uint8_t type_idx = 0;
        if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
        else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
        else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
        else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;
        
        char buf[96];
        snprintf(buf, sizeof(buf), "%4d C0   %-20s %-9s %-9s %3d  %7d %7d",
                 task->id, 
                 task->name, 
                 type_str[type_idx], 
                 state_str[task->state],
                 task->priority, 
                 task->mem_used / 1024,
                 task->cpu_time);
        kout.println(buf);
    }
    
    // Show Core 1 tasks
    if (kernel.core1_initialized) {
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        
        for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
            TCB* task = &kernel.core1.tasks[i];
            
            char buf[96];
            snprintf(buf, sizeof(buf), "%4d C1   %-20s %-9s %-9s %3d  %7d %7d",
                     task->id, 
                     task->name, 
                     "APP",  // Core1 tasks are always apps
                     state_str[task->state],
                     task->priority,
                     task->mem_used / 1024,
                     task->cpu_time);
            kout.println(buf);
        }
        
        mutex_exit(&kernel.core1.scheduler_lock);
    }
    
    // Summary
    kout.println("\n--- Summary ---");
    kout.print("Core 0 Tasks: ");
    kout.print(kernel.task_count);
    kout.print(" (CPU: ");
    kout.print(kernel.cpu_usage, 1);
    kout.println("%)");
    
    if (kernel.core1_initialized) {
        kout.print("Core 1 Tasks: ");
        kout.print(kernel.core1.task_count);
        kout.print(" (CPU: ");
        kout.print(kernel.core1.cpu_usage, 1);
        kout.println("%)");
    }
    
    kout.print("Total:        ");
    kout.println(kernel.task_count + (kernel.core1_initialized ? kernel.core1.task_count : 0));
}

void cmd_taskinfo(char* arg) {
    uint32_t id = atoi(arg);
    
    // Check Core 0
    if (id < 1000) {
        if (id >= kernel.task_count) {
            kout.println("Invalid task ID");
            return;
        }
        
        TCB* task = &kernel.tasks[id];
        
        kout.println("\n=== Task Information ===");
        kout.print("ID:            "); kout.println(task->id);
        kout.print("Name:          "); kout.println(task->name);
        kout.print("Core:          0");
        
        const char* state_str[] = {"READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED", "ZOMBIE"};
        kout.print("\nState:         "); kout.println(state_str[task->state]);
        
        const char* type_str[] = {"KERNEL", "DRIVER", "SERVICE", "MODULE", "APPLICATION"};
        uint8_t type_idx = 0;
        if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
        else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
        else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
        else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;
        kout.print("Type:          "); kout.println(type_str[type_idx]);
        
        kout.print("Base Priority: "); kout.println(task->sched_info.base_priority);
        kout.print("Eff. Priority: "); kout.println(task->priority);
        kout.print("OOM Priority:  "); kout.println(task->oom_priority);
        
        kout.print("\nMemory Used:   "); 
        kout.print(task->mem_used / 1024); 
        kout.println(" KB");
        
        kout.print("Memory Peak:   "); 
        kout.print(task->mem_peak / 1024); 
        kout.println(" KB");
        
        if (task->mem_limit > 0) {
            kout.print("Memory Limit:  "); 
            kout.print(task->mem_limit / 1024); 
            kout.println(" KB");
        } else {
            kout.println("Memory Limit:  Unlimited");
        }
        
        kout.print("\nCPU Time:      "); 
        kout.print(task->cpu_time); 
        kout.println(" ms");
        
        kout.print("Context Switches: "); 
        kout.println(task->context_switches);
        
        kout.print("Page Faults:   "); 
        kout.println(task->page_faults);
        
        uint64_t uptime = (get_time_ms() - task->start_time) / 1000;
        kout.print("\nUptime:        "); 
        kout.print((uint32_t)uptime); 
        kout.println(" s");
        
        if (task->max_runtime > 0) {
            kout.print("Max Runtime:   "); 
            kout.print((uint32_t)(task->max_runtime / 1000)); 
            kout.println(" s");
        }
        
        if (task->respawn_count > 0) {
            kout.print("Respawn Count: "); 
            kout.println(task->respawn_count);
        }
        
        kout.println("\n--- Flags ---");
        if (task->flags & TASK_FLAG_PROTECTED) kout.println("  PROTECTED");
        if (task->flags & TASK_FLAG_CRITICAL) kout.println("  CRITICAL");
        if (task->flags & TASK_FLAG_RESPAWN) kout.println("  RESPAWN");
        if (task->flags & TASK_FLAG_ONESHOT) kout.println("  ONESHOT");
        if (task->flags & TASK_FLAG_PERSISTENT) kout.println("  PERSISTENT");
        
        if (task->description) {
            kout.println("\n--- Description ---");
            kout.println(task->description);
        }
        
    } 
    // Check Core 1
    else if (kernel.core1_initialized) {
        uint32_t local_id = id - 1000;
        
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        
        if (local_id >= kernel.core1.task_count) {
            mutex_exit(&kernel.core1.scheduler_lock);
            kout.println("Invalid task ID");
            return;
        }
        
        TCB* task = &kernel.core1.tasks[local_id];
        
        kout.println("\n=== Task Information ===");
        kout.print("ID:            "); kout.println(task->id);
        kout.print("Name:          "); kout.println(task->name);
        kout.println("Core:          1");
        
        const char* state_str[] = {"READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED", "ZOMBIE"};
        kout.print("\nState:         "); kout.println(state_str[task->state]);
        
        kout.println("Type:          APPLICATION");
        
        kout.print("Base Priority: "); kout.println(task->sched_info.base_priority);
        kout.print("Eff. Priority: "); kout.println(task->priority);
        
        kout.print("\nMemory Used:   "); 
        kout.print(task->mem_used / 1024); 
        kout.println(" KB");
        
        kout.print("CPU Time:      "); 
        kout.print(task->cpu_time); 
        kout.println(" ms");
        
        uint64_t uptime = (get_time_ms() - task->start_time) / 1000;
        kout.print("\nUptime:        "); 
        kout.print((uint32_t)uptime); 
        kout.println(" s");
        
        mutex_exit(&kernel.core1.scheduler_lock);
    }
}

void cmd_ipcstat() {
    kout.println("\n=== IPC Statistics ===");
    
    kout.print("Total Messages: ");
    kout.print(kernel.ipc_queue.total_count);
    kout.print("/");
    kout.println(MAX_IPC_MESSAGES);
    
    kout.print("  High Priority:   ");
    kout.println(kernel.ipc_queue.queue_counts[IPC_QUEUE_HIGH_PRI]);
    
    kout.print("  Normal Priority: ");
    kout.println(kernel.ipc_queue.queue_counts[IPC_QUEUE_NORMAL_PRI]);
    
    kout.print("  Low Priority:    ");
    kout.println(kernel.ipc_queue.queue_counts[IPC_QUEUE_LOW_PRI]);
    
    kout.print("\nFlow Control: ");
    kout.println(kernel.ipc_queue.flow_control_active ? "ACTIVE" : "Inactive");
    
    kout.print("Threshold: ");
    kout.println(kernel.ipc_queue.flow_control_threshold);
    
    kout.println("\n--- Lifetime Stats ---");
    kout.print("Sent:     "); kout.println(kernel.ipc_queue.total_sent);
    kout.print("Received: "); kout.println(kernel.ipc_queue.total_received);
    kout.print("Dropped:  "); kout.println(kernel.ipc_queue.dropped_messages);
    kout.print("Queue Full Events: "); kout.println(ipc_stats.queue_full_events);
    kout.print("Flow Control Events: "); kout.println(ipc_stats.flow_control_events);
    
    kout.print("\nAvg Queue Depth: ");
    kout.println(ipc_stats.avg_queue_depth, 1);
    
    kout.print("Max Queue Depth: ");
    kout.println(ipc_stats.max_queue_depth);
    
    kout.println("\n--- Resource Locks ---");
    uint32_t locked = 0;
    for (uint32_t i = 0; i < MAX_RESOURCES; i++) {
        if (kernel.resources[i].in_use) {
            kout.print("Lock ");
            kout.print(i);
            kout.print(": Task ");
            kout.println(kernel.resources[i].owner_id);
            locked++;
        }
    }
    if (locked == 0) {
        kout.println("All unlocked");
    }
}

void cmd_schedstat() {
    kout.println("\n=== Scheduler Statistics ===");
    kout.println("Algorithm: O(1) Priority Bitmap");
    kout.print("Priority Levels: ");
    kout.println(SCHED_NUM_PRIORITY_LEVELS);
    
    kout.println("\n--- Core 0 ---");
    kout.print("Context Switches: ");
    kout.println(core0_sched.switches);
    
    kout.print("CPU Load (avg):   ");
    kout.print(core0_sched.cpu_load, 1);
    kout.println("%");
    
    kout.print("CPU Load (inst):  ");
    kout.print(core0_sched.cpu_load_instant, 1);
    kout.println("%");
    
    kout.print("Idle Injections:  ");
    kout.println(core0_sched.idle_injections);
    
    kout.print("Current Task:     ");
    if (core0_sched.current_task < kernel.task_count) {
        kout.print(kernel.tasks[core0_sched.current_task].name);
        kout.print(" (pri=");
        kout.print(core0_sched.current_priority);
        kout.println(")");
    }
    
    if (kernel.core1_initialized) {
        kout.println("\n--- Core 1 ---");
        kout.print("Context Switches: ");
        kout.println(core1_sched.switches);
        
        kout.print("CPU Load:         ");
        kout.print(kernel.core1.cpu_usage, 1);
        kout.println("%");
        
        kout.print("Current Task:     ");
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        if (core1_sched.current_task < kernel.core1.task_count) {
            kout.print(kernel.core1.tasks[core1_sched.current_task].name);
            kout.print(" (pri=");
            kout.print(core1_sched.current_priority);
            kout.println(")");
        }
        mutex_exit(&kernel.core1.scheduler_lock);
    }
    
    kout.println("\n--- Priority Distribution (Core 0) ---");
    uint32_t pri_counts[SCHED_NUM_PRIORITY_LEVELS] = {0};
    
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        if (kernel.tasks[i].state != TASK_TERMINATED) {
            uint8_t pri = kernel.tasks[i].priority;
            if (pri < SCHED_NUM_PRIORITY_LEVELS) {
                pri_counts[pri]++;
            }
        }
    }
    
    for (int i = SCHED_NUM_PRIORITY_LEVELS - 1; i >= 0; i--) {
        if (pri_counts[i] > 0) {
            kout.print("Pri ");
            if (i < 10) kout.print(" ");
            kout.print(i);
            kout.print(": ");
            kout.print(pri_counts[i]);
            kout.print(" task");
            if (pri_counts[i] > 1) kout.print("s");
            if (i >= SCHED_RT_THRESHOLD) kout.print(" [RT]");
            kout.println();
        }
    }
}

void cmd_oomstat() {
    kout.println("\n=== OOM Statistics ===");
    kout.print("Prevention Success:  ");
    kout.println(oom_stats.prevention_count);
    
    kout.print("Graceful Requests:   ");
    kout.println(oom_stats.requests_sent);
    
    kout.print("Voluntary Releases:  ");
    kout.println(oom_stats.voluntary_releases);
    
    kout.print("Forced Kills:        ");
    kout.println(oom_stats.forced_kills);
    
    kout.print("Total Reclaimed:     ");
    kout.print(oom_stats.total_bytes_reclaimed / 1024);
    kout.println(" KB");
    
    kout.print("\nRegistered Handlers: ");
    uint32_t handler_count = 0;
    for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
        if (oom_handlers[i].active) handler_count++;
    }
    kout.println(handler_count);
    
    if (handler_count > 0) {
        kout.println("\nTasks with handlers:");
        for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
            if (oom_handlers[i].active) {
                uint32_t tid = oom_handlers[i].task_id;
                if (tid < kernel.task_count) {
                    kout.print("  - ");
                    kout.println(kernel.tasks[tid].name);
                }
            }
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
    kout.print("Uptime:    "); 
    kout.print((uint32_t)(kernel.uptime_ms / 1000)); 
    kout.println(" s");
    
    kout.print("CPU (C0):  "); 
    kout.print(kernel.cpu_usage, 1); 
    kout.println("%");
    
    if (kernel.core1_initialized) {
        kout.print("CPU (C1):  "); 
        kout.print(kernel.core1.cpu_usage, 1); 
        kout.println("%");
    }
    
    kout.print("Temp:      "); 
    kout.print(kernel.temperature, 1); 
    kout.println("C");
    
    kout.print("Memory:    "); 
    kout.print(get_used_memory() / 1024); 
    kout.print("/"); 
    kout.print(HEAP_SIZE / 1024); 
    kout.println(" KB");
    
    kout.print("Tasks:     "); 
    kout.println(kernel.task_count + (kernel.core1_initialized ? kernel.core1.task_count : 0));
    
    kout.print("OOM Kills: "); 
    kout.println(oom_stats.forced_kills);
}

void cmd_mem() {
    kout.println("\n=== Memory Statistics ===");
    kout.print("Total:         "); 
    kout.print(HEAP_SIZE / 1024); 
    kout.println(" KB");
    
    kout.print("Used:          "); 
    kout.print(get_used_memory() / 1024); 
    kout.println(" KB");
    
    kout.print("Free:          "); 
    kout.print(get_free_memory() / 1024); 
    kout.println(" KB");
    
    kout.print("Largest free:  "); 
    kout.print(kernel.largest_free_block / 1024); 
    kout.println(" KB");
    
    kout.print("Fragmentation: "); 
    kout.print(kernel.fragmentation_pct); 
    kout.println("%");
    
    kout.print("Allocations:   "); 
    kout.println(kernel.total_allocations);
    
    kout.print("Frees:         "); 
    kout.println(kernel.total_frees);
    
    kout.print("Blocks:        "); 
    kout.println(kernel.mem_block_count);
    
    kout.print("OOM kills:     "); 
    kout.println(oom_stats.forced_kills);
}

void cmd_memmap() {
    kout.println("\n=== Memory Map ===");
    kout.println("Addr       Size     Owner  Free  Seq");
    kout.println("---------- -------- ------ ----- -----");
    
    for (uint32_t i = 0; i < kernel.mem_block_count && i < 20; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        char buf[64];
        snprintf(buf, sizeof(buf), "0x%08lx %8d %6d %-5s %5d",
                 (uint32_t)block->addr, block->size, block->owner_id,
                 block->free ? "Y" : "N", block->alloc_seq);
        kout.println(buf);
    }
    
    if (kernel.mem_block_count > 20) {
        kout.print("... (");
        kout.print(kernel.mem_block_count - 20);
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
    if (kernel.root_mode) {
        kernel.root_mode = false;
        kout.println("Root mode: OFF");
    } else {
        kernel.root_mode = true;
        kout.println("Root mode: ON (dangerous!)");
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
    if (id >= kernel.task_count) {
        kout.println("Error: Invalid task ID");
        return;
    }
    
    brutal_task_kill(id);
}

// ============================================================================
// KERNEL BOOTSTRAP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("APP_REG: Registration phase complete.");
    
    SPI.setRX(SD_MISO);
    SPI.setTX(SD_MOSI);
    SPI.setSCK(SD_SCK);
    randomSeed(micros());
    
    kout.println("========================================");
    kout.println("  RP2040 Kernel v10.0.0");
    kout.println("  Grand Feature Release");
    kout.println("========================================");
    kout.println("Initializing...");
    
    watchdog_init(); // Init watchdog early
    
    pinMode(BTN_ONOFF, INPUT_PULLUP);
    kout.println("[OK] Input system");
    
    temp_init();
    kernel.temperature = read_temperature();
    kout.print("[OK] Temperature (");
    kout.print(kernel.temperature, 1);
    kout.println("C)");
    
    mem_init();
    kout.println("[OK] Memory manager");
    
    task_init();
    kout.println("[OK] Task scheduler (O(1) Bitmap)");
    
    mutex_init(&kernel.log_lock);
    kout.println("[OK] Logging system");
    
    ipc_init();
    resource_locks_init();
    
    kout.println("[CORE1] Initializing secondary core...");
    core1_scheduler_init();
    multicore_launch_core1(core1_main);
    kernel.core1_initialized = true;
    kout.println("[OK] Core1 started");
    
    vfs_init();
    fs_init();
    if (kernel.fs_available) {
        fs_mount();
    }
    
    kout.println("\n=== Loading System Tasks ===");
    
    task_create("idle", idle_task, NULL, 0,
                TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 0, NULL, "Kernel idle task");
    kout.println("[OK] Idle (Pri 0)");
    
    task_create("input_cycle", NULL, NULL, 28, // High priority driver
                TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 1 * 1024, &input_callbacks, "Focus cycle driver");
    kout.println("[OK] Input driver (Pri 28)");
    
    task_create("shell", NULL, NULL, 10,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NORMAL, 4 * 1024, &shell_callbacks, "Command shell");
    kout.println("[OK] Shell service (Pri 10)");
    
    task_create("cpumon", NULL, NULL, 2,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &cpumon_callbacks, "CPU monitor");
    kout.println("[OK] CPU monitor (Pri 2)");
    
    task_create("tempmon", NULL, NULL, 2,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &tempmon_callbacks, "Temp monitor");
    kout.println("[OK] Temp monitor (Pri 2)");
    
    if (kernel.fs_available) {
        task_create("fs", NULL, NULL, 8,
                    TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                    OOM_PRIORITY_NEVER, 4 * 1024, &fs_callbacks, "FS service");
        kout.println("[OK] FS service (Pri 8)");
    }
    
    kout.println("========================================");
    kout.println("Kernel boot complete!");
    kout.println("========================================");
    kout.print("Heap:      "); kout.print(HEAP_SIZE / 1024); kout.println(" KB");
    kout.print("Core0 Tasks: "); kout.println(kernel.task_count);
    kout.println("Core1:     Ready for offload");
    kout.print("Apps:      "); kout.print(app_registry_count); kout.println(" registered");
    
    if (kernel.fs_available && kernel.sd_info.valid) {
        kout.print("SD Card:   ");
        if (kernel.sd_info.card_size >= 1024ULL * 1024 * 1024) {
            kout.print((uint32_t)(kernel.sd_info.card_size / (1024ULL * 1024 * 1024)));
            kout.println(" GB");
        } else {
            kout.print((uint32_t)(kernel.sd_info.card_size / (1024 * 1024)));
            kout.println(" MB");
        }
    } else {
        kout.println("SD Card:   Unavailable");
    }
    
    kout.println("\nType 'help' for commands");
    klog(0, "KERNEL: Boot v10.0.0 Grand Release");
    
    shell_prompt();
    kernel.running = true;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    if (kernel.current_task >= MAX_TASKS || !kernel.running || kernel.task_count == 0) {
        kernel_panic("Kernel loop fault");
    }
    
    uint64_t loop_start = get_time_us();
    
    scheduler_tick();
    
    // Check idle task health
    if (kernel.tasks[0].state == TASK_TERMINATED || kernel.tasks[0].entry == NULL) {
        kernel_panic("IDLE TASK DEAD");
    }
    
    TCB* task = &kernel.tasks[kernel.current_task];
    if (task->state == TASK_TERMINATED) {
        task_yield();
        return;
    }
    
    // Execute current task
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
        task->cpu_time += task_duration / 1000;
        
        if (task->state == TASK_RUNNING) {
            task->state = TASK_READY;
        }
    }
    
    task_yield();
    
    // Feed watchdog
    static uint32_t watchdog_counter = 0;
    if (++watchdog_counter >= 10) { // Feed more often
        watchdog_feed();
        watchdog_counter = 0;
    }
    
    // Maintain scheduler timing
    uint64_t elapsed = get_time_us() - loop_start;
    if (elapsed < SCHEDULER_TICK_US) {
        precise_sleep_us(SCHEDULER_TICK_US - elapsed);
    }
}

// ============================================================================
// IPC SYSTEM (Priority-Aware)
// ============================================================================

void ipc_init() {
    mutex_init(&kernel.ipc_queue.lock);
    
    for (int i = 0; i < IPC_NUM_QUEUES; i++) {
        kernel.ipc_queue.queue_heads[i] = 0;
        kernel.ipc_queue.queue_tails[i] = 0;
        kernel.ipc_queue.queue_counts[i] = 0;
    }
    
    kernel.ipc_queue.total_count = 0;
    kernel.ipc_queue.sequence_counter = 0;
    kernel.ipc_queue.dropped_messages = 0;
    kernel.ipc_queue.total_sent = 0;
    kernel.ipc_queue.total_received = 0;
    kernel.ipc_queue.flow_control_active = false;
    kernel.ipc_queue.flow_control_threshold = MAX_IPC_MESSAGES * 3 / 4; // 75% threshold
    
    for (uint32_t i = 0; i < MAX_IPC_MESSAGES; i++) {
        kernel.ipc_queue.messages[i].in_use = false;
    }
    
    memset(&ipc_stats, 0, sizeof(ipc_stats));
    
    kout.println("[IPC] Priority-aware queue initialized");
}

static inline uint8_t ipc_get_queue_index(uint8_t priority) {
    if (priority >= 200) return IPC_QUEUE_HIGH_PRI;
    if (priority >= 100) return IPC_QUEUE_NORMAL_PRI;
    return IPC_QUEUE_LOW_PRI;
}

bool ipc_send(uint32_t sender_id, uint32_t target_id, IPCMessageType type, 
              void* data, size_t size, uint8_t priority) {
    
    if (size > IPC_MSG_SIZE) {
        klog(2, "IPC: Message too large");
        return false;
    }
    
    mutex_enter_blocking(&kernel.ipc_queue.lock);
    
    // Flow control check
    if (kernel.ipc_queue.total_count >= kernel.ipc_queue.flow_control_threshold) {
        kernel.ipc_queue.flow_control_active = true;
        ipc_stats.flow_control_events++;
        
        // Only allow high-priority messages when under flow control
        if (priority < 200) {
            mutex_exit(&kernel.ipc_queue.lock);
            return false;
        }
    }
    
    // Check for queue full
    if (kernel.ipc_queue.total_count >= MAX_IPC_MESSAGES) {
        kernel.ipc_queue.dropped_messages++;
        uint8_t queue_idx = ipc_get_queue_index(priority);
        ipc_stats.messages_dropped[queue_idx]++;
        ipc_stats.queue_full_events++;
        
        mutex_exit(&kernel.ipc_queue.lock);
        klog(2, "IPC: Queue full, message dropped");
        return false;
    }
    
    // Find free message slot
    int free_slot = -1;
    for (uint32_t i = 0; i < MAX_IPC_MESSAGES; i++) {
        if (!kernel.ipc_queue.messages[i].in_use) {
            free_slot = i;
            break;
        }
    }
    
    if (free_slot < 0) {
        // This should not happen if total_count is correct
        mutex_exit(&kernel.ipc_queue.lock);
        kernel_panic("IPC queue desync");
        return false;
    }
    
    // Prepare message
    IPCMessage* msg = &kernel.ipc_queue.messages[free_slot];
    msg->sender_id = sender_id;
    msg->target_id = target_id;
    msg->type = type;
    msg->priority = priority;
    msg->timestamp = get_time_us();
    msg->sequence = kernel.ipc_queue.sequence_counter++;
    msg->retry_count = 0;
    msg->flags = 0;
    msg->in_use = true;
    
    if (data && size > 0) {
        memcpy(msg->data, data, size);
    }
    
    // Add to appropriate priority queue
    uint8_t queue_idx = ipc_get_queue_index(priority);
    kernel.ipc_queue.queue_counts[queue_idx]++;
    kernel.ipc_queue.total_count++;
    
    // Update statistics
    ipc_stats.messages_sent[queue_idx]++;
    kernel.ipc_queue.total_sent++;
    
    if (kernel.ipc_queue.total_count > ipc_stats.max_queue_depth) {
        ipc_stats.max_queue_depth = kernel.ipc_queue.total_count;
    }
    
    // Clear flow control if queue drops below threshold
    if (kernel.ipc_queue.total_count < kernel.ipc_queue.flow_control_threshold / 2) {
        kernel.ipc_queue.flow_control_active = false;
    }
    
    mutex_exit(&kernel.ipc_queue.lock);
    return true;
}

bool ipc_receive(uint32_t task_id, IPCMessage* msg_out) {
    if (!msg_out) return false;
    
    mutex_enter_blocking(&kernel.ipc_queue.lock);
    
    if (kernel.ipc_queue.total_count == 0) {
        mutex_exit(&kernel.ipc_queue.lock);
        return false;
    }
    
    // Check queues in priority order: HIGH -> NORMAL -> LOW
    for (int queue_idx = 0; queue_idx < IPC_NUM_QUEUES; queue_idx++) {
        if (kernel.ipc_queue.queue_counts[queue_idx] == 0) continue;
        
        // Scan for matching message in this priority level
        uint32_t best_match = 0xFFFFFFFF;
        uint16_t best_sequence = 0xFFFF;
        
        for (uint32_t i = 0; i < MAX_IPC_MESSAGES; i++) {
            IPCMessage* msg = &kernel.ipc_queue.messages[i];
            
            if (!msg->in_use) continue;
            
            // Check priority matches current queue
            uint8_t msg_queue_idx = ipc_get_queue_index(msg->priority);
            if (msg_queue_idx != queue_idx) continue;
            
            // Check if message is for this task (or broadcast)
            if (msg->target_id != task_id && msg->target_id != 0xFFFFFFFF) continue;
            
            // Find oldest message (lowest sequence) in this priority level
            if (best_match == 0xFFFFFFFF || msg->sequence < best_sequence) {
                best_match = i;
                best_sequence = msg->sequence;
            }
        }
        
        // Found a message
        if (best_match != 0xFFFFFFFF) {
            IPCMessage* msg = &kernel.ipc_queue.messages[best_match];
            memcpy(msg_out, msg, sizeof(IPCMessage));
            
            // Mark as free
            msg->in_use = false;
            
            // Update queue counts
            kernel.ipc_queue.queue_counts[queue_idx]--;
            kernel.ipc_queue.total_count--;
            
            // Update statistics
            ipc_stats.messages_received[queue_idx]++;
            kernel.ipc_queue.total_received++;
            
            mutex_exit(&kernel.ipc_queue.lock);
            return true;
        }
    }
    
    mutex_exit(&kernel.ipc_queue.lock);
    return false;
}

void ipc_maintenance() {
    mutex_enter_blocking(&kernel.ipc_queue.lock);
    
    uint64_t now = get_time_us();
    uint32_t expired = 0;
    
    // Clean up old messages (older than 5 seconds)
    for (uint32_t i = 0; i < MAX_IPC_MESSAGES; i++) {
        if (kernel.ipc_queue.messages[i].in_use) {
            if ((now - kernel.ipc_queue.messages[i].timestamp) > 5000000) { // 5 sec
                uint8_t queue_idx = ipc_get_queue_index(kernel.ipc_queue.messages[i].priority);
                kernel.ipc_queue.messages[i].in_use = false;
                kernel.ipc_queue.queue_counts[queue_idx]--;
                kernel.ipc_queue.total_count--;
                expired++;
            }
        }
    }
    
    // Update average queue depth
    ipc_stats.avg_queue_depth = (ipc_stats.avg_queue_depth * 0.95f) + 
                                 (kernel.ipc_queue.total_count * 0.05f);
    
    mutex_exit(&kernel.ipc_queue.lock);
    
    if (expired > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "IPC: Expired %d old messages", expired);
        klog(1, buf);
    }
}


// ============================================================================
// RESOURCE LOCKING
// ============================================================================

void resource_locks_init() {
    for (uint32_t i = 0; i < MAX_RESOURCES; i++) {
        mutex_init(&kernel.resources[i].mutex);
        kernel.resources[i].owner_id = 0;
        kernel.resources[i].in_use = false;
    }
    kout.println("[LOCKS] Resource locks initialized");
}

bool k_lock_resource(uint32_t resource_id) {
    if (resource_id >= MAX_RESOURCES) return false;
    
    ResourceLock* lock = &kernel.resources[resource_id];
    mutex_enter_blocking(&lock->mutex);
    lock->owner_id = kernel.current_task;
    lock->in_use = true;
    return true;
}

void k_unlock_resource(uint32_t resource_id) {
    if (resource_id >= MAX_RESOURCES) return;
    
    ResourceLock* lock = &kernel.resources[resource_id];
    lock->owner_id = 0;
    lock->in_use = false;
    mutex_exit(&lock->mutex);
}

// ============================================================================
// TEMPERATURE SENSOR
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
// MEMORY MANAGER (with Graceful OOM Killer)
// ============================================================================

void mem_init() {
    mutex_init(&kernel.mem_lock);
    memset(&kernel.mem_blocks, 0, sizeof(kernel.mem_blocks));
    
    kernel.mem_block_count = 1;
    kernel.mem_blocks[0].addr = kernel.heap;
    kernel.mem_blocks[0].size = HEAP_SIZE;
    kernel.mem_blocks[0].owner_id = 0;
    kernel.mem_blocks[0].free = true;
    kernel.mem_blocks[0].alloc_time = 0;
    kernel.mem_blocks[0].alloc_seq = 0;
    
    kernel.total_allocations = 0;
    kernel.total_frees = 0;
    kernel.oom_kills = 0;
    kernel.alloc_sequence = 0;
    kernel.fragmentation_pct = 0;
    kernel.largest_free_block = HEAP_SIZE;
    
    memset(&oom_handlers, 0, sizeof(oom_handlers));
    memset(&oom_stats, 0, sizeof(oom_stats));
}

size_t get_free_memory() {
    size_t free_mem = 0;
    mutex_enter_blocking(&kernel.mem_lock);
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].free) {
            free_mem += kernel.mem_blocks[i].size;
        }
    }
    mutex_exit(&kernel.mem_lock);
    return free_mem;
}

size_t get_used_memory() {
    return HEAP_SIZE - get_free_memory();
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

void mem_compact() {
    mutex_enter_blocking(&kernel.mem_lock);
    
    bool merged;
    uint32_t merges = 0;
    do {
        merged = false;
        
        // Sort blocks by address
        for (uint32_t i = 0; i < kernel.mem_block_count - 1; i++) {
            for (uint32_t j = i + 1; j < kernel.mem_block_count; j++) {
                if (kernel.mem_blocks[j].addr < kernel.mem_blocks[i].addr) {
                    MemBlock temp = kernel.mem_blocks[i];
                    kernel.mem_blocks[i] = kernel.mem_blocks[j];
                    kernel.mem_blocks[j] = temp;
                }
            }
        }
        
        // Merge adjacent free blocks
        for (uint32_t i = 0; i < kernel.mem_block_count - 1; i++) {
            if (!kernel.mem_blocks[i].free) continue;
            if (kernel.mem_blocks[i + 1].free &&
                (uint8_t*)kernel.mem_blocks[i].addr + kernel.mem_blocks[i].size == 
                (uint8_t*)kernel.mem_blocks[i + 1].addr) {
                
                kernel.mem_blocks[i].size += kernel.mem_blocks[i + 1].size;
                
                for (uint32_t k = i + 1; k < kernel.mem_block_count - 1; k++) {
                    kernel.mem_blocks[k] = kernel.mem_blocks[k + 1];
                }
                kernel.mem_block_count--;
                merged = true;
                merges++;
                break;
            }
        }
    } while (merged && merges < 50);
    
    if (merges > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "MEM: Compacted %d blocks", merges);
        klog(0, buf);
    }
    
    mutex_exit(&kernel.mem_lock);
}

// --- OOM Killer Implementation ---

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
    
    // Step 1: Compact memory
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
    
    // Step 2: Check if we have enough total free memory (just fragmented)
    size_t total_free = get_free_memory();
    if (total_free >= bytes_needed) {
        kout.println("[OOM] Step 2: Aggressive compaction");
        
        // Try compaction multiple times
        for (int pass = 0; pass < 3; pass++) {
            mem_compact();
            calculate_fragmentation();
            
            if (kernel.largest_free_block >= bytes_needed) {
                kout.print("[OOM] Success after ");
                kout.print(pass + 2);
                kout.println(" passes");
                oom_stats.prevention_count++;
                return true;
            }
        }
    }
    
    kout.println("[OOM] Prevention failed - proceeding to victim selection");
    return false;
}

// *** OOMVictim struct was moved up to ~line 424 ***

static int32_t oom_calculate_victim_score(TCB* task, uint32_t mem_used) {
    int32_t score = 0;
    
    // Base score on memory usage (more memory = better victim)
    score += (mem_used / 1024);  // KB of memory
    
    // OOM priority (higher priority = better victim)
    score += (task->oom_priority * 100);
    
    // Recent CPU usage (less active = better victim)
    uint64_t idle_time = get_time_ms() - task->last_run;
    if (idle_time > 5000) score += 200;      // Idle > 5s
    else if (idle_time > 1000) score += 50;  // Idle > 1s
    
    // Penalty for having cleanup handler (give them a chance)
    oom_callback_t handler = oom_get_handler(task->id);
    if (handler) score -= 50;
    
    // Penalty for system importance
    if (task->flags & TASK_FLAG_CRITICAL) score = -10000;  // Never kill
    if (task->task_type != TASK_TYPE_APPLICATION) score = -10000;
    
    return score;
}

OOMVictim oom_select_victim(size_t bytes_needed) {
    OOMVictim victim = {0};
    victim.task_id = 0xFFFFFFFF;
    victim.score = -10000;
    
    kout.println("[OOM] Selecting victim...");
    
    // Evaluate all application tasks
    for (uint32_t i = 1; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        
        if (task->state == TASK_TERMINATED) continue;
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
    
    return victim;
}

bool oom_request_cleanup(OOMVictim* victim, size_t bytes_needed) {
    if (victim->task_id == 0xFFFFFFFF || victim->task_id >= kernel.task_count) {
        return false;
    }
    
    TCB* task = &kernel.tasks[victim->task_id];
    oom_callback_t handler = oom_get_handler(victim->task_id);
    
    if (!handler) {
        kout.print("[OOM] Task '");
        kout.print(task->name);
        kout.println("' has no cleanup handler");
        return false;
    }
    
    kout.print("[OOM] Requesting cleanup from '");
    kout.print(task->name);
    kout.print("' (");
    kout.print(victim->memory_used / 1024);
    kout.println(" KB)");
    
    // Set up request tracking
    oom_current_request.target_task_id = victim->task_id;
    oom_current_request.request_time = get_time_ms();
    oom_current_request.bytes_requested = bytes_needed;
    oom_current_request.request_sent = true;
    oom_current_request.task_complied = false;
    
    // Invoke the handler
    // This is tricky. We can't just call it, as it's in the task's context.
    // A real implementation would send a high-priority system event/message.
    // For this kernel, we'll log that it's not supported.
    // A true implementation is too complex for this merge.
    kout.println("[OOM] Graceful handler invocation not yet supported.");
    kout.println("[OOM] Proceeding to forced termination.");
    oom_current_request.request_sent = false;
    
    // handler(bytes_needed); // This would be the ideal
    oom_stats.requests_sent++;
    
    return false; // Return false as we can't *actually* do the cleanup
}

void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed) {
    if (!oom_current_request.request_sent) return;
    if (oom_current_request.target_task_id != task_id) return;
    
    TCB* task = &kernel.tasks[task_id];
    
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
}

void oom_killer(size_t bytes_needed) {
    kout.println("\n!!! OUT OF MEMORY !!!");
    kout.print("Need: ");
    kout.print(bytes_needed / 1024);
    kout.println(" KB");
    
    char buf[64];
    snprintf(buf, sizeof(buf), "OOM: Need %d KB", (int)(bytes_needed / 1024));
    klog(3, buf);
    
    // Step 1: Prevention
    if (oom_prevent(bytes_needed)) {
        return;  // Crisis averted!
    }
    
    // Step 2: Select victim
    OOMVictim victim = oom_select_victim(bytes_needed);
    
    if (victim.task_id == 0xFFFFFFFF) {
        kout.println("OOM: NO KILLABLE APPLICATIONS!");
        klog(3, "OOM: No victims, PANIC!");
        kernel_panic("OOM: No killable victims");
        return;
    }
    
    TCB* victim_task = &kernel.tasks[victim.task_id];
    
    kout.print("[OOM] Selected victim: '");
    kout.print(victim_task->name);
    kout.print("' (");
    kout.print(victim.memory_used / 1024);
    kout.print(" KB, score=");
    kout.print(victim.score);
    kout.println(")");
    
    // Step 3: Try graceful cleanup first
    // bool cleanup_requested = false;
    // if (victim.has_handler) {
    //     cleanup_requested = oom_request_cleanup(&victim, bytes_needed);
    // }
    
    // For now, we skip the wait and go straight to kill
    // as graceful cleanup is not fully implemented.
    
    // Step 4: Force kill
    kout.print("[OOM] Killing '");
    kout.print(victim_task->name);
    kout.print("' (");
    kout.print(victim.memory_used / 1024);
    kout.println(" KB)");
    
    snprintf(buf, sizeof(buf), "OOM: Killed %s (%dKB)", 
             victim_task->name, victim.memory_used / 1024);
    klog(2, buf);
    
    brutal_task_kill(victim.task_id);
    kernel.oom_kills++;
    oom_stats.forced_kills++;
    oom_stats.total_bytes_reclaimed += victim.memory_used;
    
    oom_current_request.request_sent = false;
}

void* kmalloc(size_t size, uint32_t task_id) {
    if (size == 0) return NULL;
    size = (size + 3) & ~3;  // 4-byte alignment
    
    mutex_enter_blocking(&kernel.mem_lock);
    
    // Check task memory limit
    if (task_id < MAX_TASKS) {
        TCB* task = &kernel.tasks[task_id];
        task->page_faults++;
        
        if (task->task_type == TASK_TYPE_APPLICATION && task->mem_limit > 0) {
            // Get task memory usage *within* the lock
            uint32_t current_usage = 0;
            for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
                if (!kernel.mem_blocks[i].free && kernel.mem_blocks[i].owner_id == task_id) {
                    current_usage += kernel.mem_blocks[i].size;
                }
            }
            
            if (current_usage + size > task->mem_limit) {
                mutex_exit(&kernel.mem_lock);
                klog(1, "MEM: Task exceeded memory limit");
                return NULL;
            }
        }
    }
    
    // First-fit allocation
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        if (block->free && block->size >= size) {
            // Split block if there's enough space left
            if (block->size > size + 32 && kernel.mem_block_count < MAX_MEMORY_BLOCKS) {
                MemBlock* new_block = &kernel.mem_blocks[kernel.mem_block_count++];
                new_block->addr = (uint8_t*)block->addr + size;
                new_block->size = block->size - size;
                new_block->owner_id = 0;
                new_block->free = true;
                new_block->alloc_time = 0;
                new_block->alloc_seq = 0;
                
                block->size = size;
            }
            
            block->free = false;
            block->owner_id = task_id;
            block->alloc_time = get_time_ms();
            block->alloc_seq = kernel.alloc_sequence++;
            kernel.total_allocations++;
            
            // Update task memory stats
            if (task_id < MAX_TASKS) {
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
            
            calculate_fragmentation();
            mutex_exit(&kernel.mem_lock);
            return block->addr;
        }
    }
    
    mutex_exit(&kernel.mem_lock);
    
    // Failed - invoke OOM killer
    oom_killer(size);
    
    // Try one more time
    mutex_enter_blocking(&kernel.mem_lock);
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        if (block->free && block->size >= size) {
            // (Split logic again)
            if (block->size > size + 32 && kernel.mem_block_count < MAX_MEMORY_BLOCKS) {
                MemBlock* new_block = &kernel.mem_blocks[kernel.mem_block_count++];
                new_block->addr = (uint8_t*)block->addr + size;
                new_block->size = block->size - size;
                new_block->owner_id = 0;
                new_block->free = true;
                block->size = size;
            }
            
            block->free = false;
            block->owner_id = task_id;
            block->alloc_time = get_time_ms();
            block->alloc_seq = kernel.alloc_sequence++;
            kernel.total_allocations++;
            calculate_fragmentation();
            mutex_exit(&kernel.mem_lock);
            return block->addr;
        }
    }
    mutex_exit(&kernel.mem_lock);
    
    return NULL;
}


void kfree(void* ptr) {
    if (!ptr) return;
    
    mutex_enter_blocking(&kernel.mem_lock);
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].addr == ptr) {
            uint32_t owner = kernel.mem_blocks[i].owner_id;
            kernel.mem_blocks[i].free = true;
            kernel.mem_blocks[i].owner_id = 0;
            kernel.total_frees++;
            
            // Update task memory stats
            if (owner < MAX_TASKS) {
                uint32_t task_mem = 0;
                for (uint32_t j = 0; j < kernel.mem_block_count; j++) {
                    if (!kernel.mem_blocks[j].free && kernel.mem_blocks[j].owner_id == owner) {
                        task_mem += kernel.mem_blocks[j].size;
                    }
                }
                kernel.tasks[owner].mem_used = task_mem;
            }
            
            calculate_fragmentation();
            mutex_exit(&kernel.mem_lock);
            return;
        }
    }
    mutex_exit(&kernel.mem_lock);
}

// ============================================================================
// KERNEL LOGGING
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
    
    // Write to FS log if mounted and important
    if (kernel.fs_mounted && level >= 2) {
        extern void fs_log_write(const char* msg);
        fs_log_write(msg);
    }
}

// ============================================================================
// VFS IMPLEMENTATION (Unchanged)
// ============================================================================

void vfs_init() {
    kernel.vfs_sb = NULL;
    kernel.vfs_data = NULL;
    kernel.vfs_mounted = false;
    kernel.vfs_active = false;
    kernel.vfs_writes = 0;
    kernel.vfs_reads = 0;
    
    kout.println("[VFS] Initialized (inactive)");
    klog(0, "VFS: Init OK");
}

void vfs_format() {
    if (!kernel.vfs_sb || !kernel.vfs_data) {
        kout.println("[VFS] Not allocated");
        return;
    }
    
    kout.println("[VFS] Formatting filesystem...");
    
    memset(kernel.vfs_sb, 0, sizeof(VFSSuperblock));
    memset(kernel.vfs_data, 0xFF, VFS_STORAGE_SIZE);
    
    kernel.vfs_sb->magic = 0x52503230;
    kernel.vfs_sb->version = 2;
    kernel.vfs_sb->total_blocks = VFS_STORAGE_SIZE / VFS_BLOCK_SIZE;
    kernel.vfs_sb->free_blocks = kernel.vfs_sb->total_blocks;
    kernel.vfs_sb->file_count = 0;
    
    memset(kernel.vfs_sb->block_bitmap, 0, sizeof(kernel.vfs_sb->block_bitmap));
    
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        kernel.vfs_sb->files[i].in_use = false;
        kernel.vfs_sb->files[i].name[0] = '\0';
        kernel.vfs_sb->files[i].chain.block_count = 0;
    }
    
    kout.println("[VFS] Format complete");
    klog(0, "VFS: Formatted v2");
}

bool vfs_mount() {
    if (!kernel.vfs_active) {
        kout.println("[VFS] Not active. Use 'vfscreate' first");
        return false;
    }
    
    if (!kernel.vfs_sb || !kernel.vfs_data) {
        kout.println("[VFS] Not allocated");
        return false;
    }
    
    vfs_format();
    
    kernel.vfs_mounted = true;
    kernel.vfs_alive = true;
    
    kout.println("[VFS] Mounted");
    klog(0, "VFS: Mounted");
    
    return true;
}

void vfs_unmount() {
    if (!kernel.vfs_mounted) return;
    
    kernel.vfs_mounted = false;
    kernel.vfs_alive = false;
    
    kout.println("[VFS] Unmounted");
    klog(0, "VFS: Unmounted");
}

int vfs_create(const char* name, uint8_t type, uint32_t owner_id) {
    if (!kernel.vfs_mounted) {
        kout.println("[VFS] Not mounted");
        return -1;
    }
    
    if (strlen(name) >= VFS_FILENAME_LEN) {
        kout.println("[VFS] Filename too long");
        return -1;
    }
    
    // Check if file exists
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (kernel.vfs_sb->files[i].in_use && 
            strcmp(kernel.vfs_sb->files[i].name, name) == 0) {
            kout.println("[VFS] File exists");
            return -1;
        }
    }
    
    // Find free file entry
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!kernel.vfs_sb->files[i].in_use) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        kout.println("[VFS] No free file entries");
        return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    strncpy(file->name, name, VFS_FILENAME_LEN - 1);
    file->name[VFS_FILENAME_LEN - 1] = '\0';
    file->type = type;
    file->in_use = true;
    file->chain.block_count = 0;
    file->size = 0;
    file->created = get_time_ms();
    file->modified = file->created;
    file->owner_id = owner_id;
    
    kernel.vfs_sb->file_count++;
    
    kout.print("[VFS] Created: ");
    kout.println(name);
    
    return fd;
}

int vfs_write(int fd, const void* data, uint32_t size) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use) return -1;
    
    if (size > VFS_MAX_FILE_SIZE) {
        size = VFS_MAX_FILE_SIZE;
    }
    
    uint32_t blocks_needed = (size + VFS_BLOCK_SIZE - 1) / VFS_BLOCK_SIZE;
    
    if (blocks_needed > kernel.vfs_sb->free_blocks) {
        kout.println("[VFS] Insufficient space");
        return -1;
    }
    
    if (blocks_needed > VFS_MAX_BLOCKS_PER_FILE) {
        kout.println("[VFS] File too large for block chain");
        return -1;
    }
    
    // Allocate blocks
    uint16_t allocated_blocks[VFS_MAX_BLOCKS_PER_FILE];
    uint32_t allocated_count = 0;
    
    for (uint32_t i = 0; i < kernel.vfs_sb->total_blocks && allocated_count < blocks_needed; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(kernel.vfs_sb->block_bitmap[byte_idx] & (1 << bit_idx))) {
            allocated_blocks[allocated_count++] = i;
        }
    }
    
    if (allocated_count < blocks_needed) {
        kout.println("[VFS] Block allocation failed");
        return -1;
    }
    
    // Write data to blocks
    file->chain.block_count = 0;
    uint32_t bytes_written = 0;
    
    for (uint32_t i = 0; i < allocated_count; i++) {
        uint16_t block_num = allocated_blocks[i];
        
        // Mark block as used
        uint32_t byte_idx = block_num / 8;
        uint32_t bit_idx = block_num % 8;
        kernel.vfs_sb->block_bitmap[byte_idx] |= (1 << bit_idx);
        kernel.vfs_sb->free_blocks--;
        
        file->chain.blocks[file->chain.block_count++] = block_num;
        
        uint32_t bytes_to_write = min((uint32_t)VFS_BLOCK_SIZE, size - bytes_written);
        uint32_t offset = block_num * VFS_BLOCK_SIZE;
        
        memcpy(kernel.vfs_data + offset, (uint8_t*)data + bytes_written, bytes_to_write);
        bytes_written += bytes_to_write;
    }
    
    file->size = size;
    file->modified = get_time_ms();
    kernel.vfs_writes++;
    
    return size;
}

int vfs_read(int fd, void* buffer, uint32_t size) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use || file->chain.block_count == 0) {
        return -1;
    }
    
    uint32_t read_size = size < file->size ? size : file->size;
    uint32_t bytes_read = 0;
    
    for (uint8_t i = 0; i < file->chain.block_count && bytes_read < read_size; i++) {
        uint16_t block_num = file->chain.blocks[i];
        uint32_t offset = block_num * VFS_BLOCK_SIZE;
        uint32_t bytes_to_read = min((uint32_t)VFS_BLOCK_SIZE, read_size - bytes_read);
        
        memcpy((uint8_t*)buffer + bytes_read, kernel.vfs_data + offset, bytes_to_read);
        bytes_read += bytes_to_read;
    }
    
    kernel.vfs_reads++;
    return bytes_read;
}

void vfs_delete(int fd) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use) return;
    
    // Free all blocks
    for (uint8_t i = 0; i < file->chain.block_count; i++) {
        uint16_t block_num = file->chain.blocks[i];
        uint32_t byte_idx = block_num / 8;
        uint32_t bit_idx = block_num % 8;
        kernel.vfs_sb->block_bitmap[byte_idx] &= ~(1 << bit_idx);
        kernel.vfs_sb->free_blocks++;
    }
    
    file->in_use = false;
    file->chain.block_count = 0;
    kernel.vfs_sb->file_count--;
    
    kout.print("[VFS] Deleted: ");
    kout.println(file->name);
}

void vfs_list() {
    if (!kernel.vfs_mounted) {
        kout.println("[VFS] Not mounted");
        return;
    }
    
    kout.println("\n=== VFS Contents ===");
    kout.println("ID  Name             Type   Size    Blks  Owner");
    kout.println("--  ---------------  -----  ------  ----  -----");
    
    const char* type_str[] = {"", "TEXT", "LOG", "DATA", "CONF"};
    
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        VFSFile* file = &kernel.vfs_sb->files[i];
        if (file->in_use) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%2d  %-15s  %-5s  %6d  %4d  %5d",
                     i, file->name, 
                     file->type < 5 ? type_str[file->type] : "?",
                     file->size, file->chain.block_count, file->owner_id);
            kout.println(buf);
        }
    }
    
    kout.print("\nFiles: ");
    kout.print(kernel.vfs_sb->file_count);
    kout.print("/");
    kout.println(VFS_MAX_FILES);
}

void vfs_stats() {
    if (!kernel.vfs_mounted) {
        kout.println("[VFS] Not mounted");
        return;
    }
    
    kout.println("\n=== VFS Statistics ===");
    kout.print("Total blocks:  "); kout.println(kernel.vfs_sb->total_blocks);
    kout.print("Free blocks:   "); kout.println(kernel.vfs_sb->free_blocks);
    kout.print("Files:         "); 
    kout.print(kernel.vfs_sb->file_count);
    kout.print("/"); 
    kout.println(VFS_MAX_FILES);
    kout.print("Total writes:  "); kout.println(kernel.vfs_writes);
    kout.print("Total reads:   "); kout.println(kernel.vfs_reads);
}

// ============================================================================
// FS (SD CARD) IMPLEMENTATION (Unchanged)
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
    
    kout.println("[FS] Initializing SD card...");
    kout.println("[FS] Waiting for card to stabilize...");
    
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    delay(1000);
    
    kout.println("[FS] Attempting connection at 400kHz...");
    if (SD.begin(SD_CS, 400000)) {
        kout.println("[FS] SD card detected!");
        kernel.fs_available = true;
        
        // Detect actual card size
        extern bool fs_detect_card_info();
        if (fs_detect_card_info()) {
            kout.print("[FS] Card Type: ");
            if (kernel.sd_info.card_type == 1) kout.println("SD1");
            else if (kernel.sd_info.card_type == 2) kout.println("SD2/SDHC");
            else kout.println("Unknown");
        }
    } else {
        kout.println("[FS] SD card not detected");
        klog(1, "FS: No SD card");
        return;
    }
    
    klog(0, "FS: Init OK");
}

bool fs_detect_card_info() {
    kernel.sd_info.valid = false;
    kernel.sd_info.card_type = 0;
    kernel.sd_info.card_size = 0;
    kernel.sd_info.sector_count = 0;
    kernel.sd_info.sector_size = 512;
    
    File root = SD.open("/");
    if (!root) {
        kout.println("[FS] Cannot open root for detection");
        return false;
    }
    
    // Estimate by traversing filesystem
    uint64_t total_size = 0;
    uint32_t file_count = 0;
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            total_size += file.size();
            file_count++;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    // Try common card sizes and pick closest match
    const uint64_t common_sizes[] = {
        512ULL * 1024 * 1024,        // 512MB
        1ULL * 1024 * 1024 * 1024,   // 1GB
        2ULL * 1024 * 1024 * 1024,   // 2GB
        4ULL * 1024 * 1024 * 1024,   // 4GB
        8ULL * 1024 * 1024 * 1024,   // 8GB
        16ULL * 1024 * 1024 * 1024,  // 16GB
        32ULL * 1024 * 1024 * 1024,  // 32GB
        64ULL * 1024 * 1024 * 1024,  // 64GB
        128ULL * 1024 * 1024 * 1024  // 128GB
    };
    
    uint64_t estimated_size = 4ULL * 1024 * 1024 * 1024; // Default 4GB
    for (uint32_t i = 0; i < 9; i++) {
        if (total_size < (common_sizes[i] * 85) / 100) {
            estimated_size = common_sizes[i];
            break;
        }
    }
    
    kernel.sd_info.card_size = estimated_size;
    kernel.sd_info.sector_count = estimated_size / 512;
    kernel.sd_info.card_type = 2; // Assume SDHC
    kernel.sd_info.valid = true;
    
    kout.print("[FS] Detected card size: ");
    if (estimated_size >= 1024ULL * 1024 * 1024) {
        kout.print((uint32_t)(estimated_size / (1024ULL * 1024 * 1024)));
        kout.println(" GB");
    } else {
        kout.print((uint32_t)(estimated_size / (1024 * 1024)));
        kout.println(" MB");
    }
    
    return true;
}

void fs_log_init() {
    if (!kernel.fs_available) return;
    
    File logFile = SD.open(FS_LOG_FILE, FILE_READ);
    if (!logFile) {
        kout.println("[FS] Creating LogRecord file");
        logFile = SD.open(FS_LOG_FILE, FILE_WRITE);
        if (logFile) {
            logFile.println("=== RP2040 Kernel Error Log ===");
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
    
    File logFile = SD.open(FS_LOG_FILE, FILE_WRITE);
    if (!logFile) {
        mutex_exit(&kernel.fs_lock);
        return;
    }
    
    kernel.fs_log_counter++;
    
    char timestamp[32];
    uint64_t ms = get_time_ms();
    snprintf(timestamp, sizeof(timestamp), "%lu.%03lu", 
             (uint32_t)(ms / 1000), (uint32_t)(ms % 1000));
    
    logFile.print("[");
    logFile.print(kernel.fs_log_counter);
    logFile.print("] ");
    logFile.print(timestamp);
    logFile.print(" ");
    logFile.println(message);
    
    logFile.close();
    mutex_exit(&kernel.fs_lock);
}

bool fs_mount() {
    if (!kernel.fs_available) {
        kout.println("[FS] SD card unavailable");
        return false;
    }
    
    kernel.fs_mounted = true;
    kernel.fs_alive = true;
    
    fs_log_init();
    
    kout.println("[FS] Mounted");
    klog(0, "FS: Mounted");
    
    return true;
}

void fs_unmount() {
    if (!kernel.fs_mounted) return;
    
    mutex_enter_blocking(&kernel.fs_lock);
    
    for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (kernel.fs_open_files[i].open) {
            kernel.fs_open_files[i].handle.close();
            kernel.fs_open_files[i].open = false;
            kernel.fs_open_files[i].owner_task_id = 0;
        }
    }
    
    kernel.fs_mounted = false;
    kernel.fs_alive = false;
    
    mutex_exit(&kernel.fs_lock);
    
    kout.println("[FS] Unmounted");
    klog(0, "FS: Unmounted");
}

bool fs_exists(const char* path) {
    if (!kernel.fs_mounted) return false;
    return SD.exists(path);
}

bool fs_mkdir(const char* path) {
    if (!kernel.fs_mounted) return false;
    return SD.mkdir(path);
}

bool fs_remove(const char* path) {
    if (!kernel.fs_mounted) return false;
    return SD.remove(path);
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
    
    kout.println("\n=== FS Contents ===");
    kout.println("Name                             Type   Size");
    kout.println("-------------------------------  -----  --------");
    
    File file = root.openNextFile();
    while (file) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%-31s  %-5s  %8d",
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
    
    uint64_t used = 0;
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                used += file.size();
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
    }
    
    kout.println("\n=== FS Statistics ===");
    
    if (kernel.sd_info.valid && kernel.sd_info.card_size > 0) {
        uint64_t total_mb = kernel.sd_info.card_size / (1024 * 1024);
        uint64_t used_mb = used / (1024 * 1024);
        uint64_t free_mb = (kernel.sd_info.card_size - used) / (1024 * 1024);
        
        kout.print("Total space:   ");
        kout.print((uint32_t)total_mb);
        kout.println(" MB");
        
        kout.print("Used space:    ");
        kout.print((uint32_t)used_mb);
        kout.println(" MB");
        
        kout.print("Free space:    ");
        kout.print((uint32_t)free_mb);
        kout.println(" MB");
        
        uint32_t usage_pct = (used * 100) / kernel.sd_info.card_size;
        kout.print("Usage:         ");
        kout.print(usage_pct);
        kout.println("%");
    } else {
        kout.println("Card size: Unknown");
        kout.print("Used: ");
        kout.print((uint32_t)(used / (1024 * 1024)));
        kout.println(" MB");
    }
    
    kout.print("Total reads:   "); kout.println(kernel.fs_reads);
    kout.print("Total writes:  "); kout.println(kernel.fs_writes);
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
        file = SD.open(path, FILE_WRITE);
    } else {
        file = SD.open(path, FILE_READ);
    }
    
    if (!file) {
        kout.println("[FS] Failed to open file");
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

int fs_write_str(int fd, const char* data) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return -1;
    if (!kernel.fs_open_files[fd].open) return -1;
    if (!kernel.fs_open_files[fd].write_mode) return -1;
    
    int written = kernel.fs_open_files[fd].handle.print(data);
    kernel.fs_writes++;
    return written;
}

int fs_read_str(int fd, char* buffer, size_t size) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return -1;
    if (!kernel.fs_open_files[fd].open) return -1;
    
    int bytes = kernel.fs_open_files[fd].handle.readBytes(buffer, size);
    kernel.fs_reads++;
    return bytes;
}

void fs_cat(const char* path) {
    if (!kernel.fs_mounted) {
        kout.println("[FS] Not mounted");
        return;
    }
    
    File file = SD.open(path, FILE_READ);
    if (!file) {
        kout.println("[FS] Failed to open file");
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

// ============================================================================
// O(1) BITMAP SCHEDULER
// ============================================================================

// --- Bitmap Operations ---
static inline int bitmap_ffs(uint32_t mask) {
    if (mask == 0) return -1;
    return __builtin_ctz(mask);  // Count trailing zeros
}

static inline void bitmap_set(uint32_t* mask, uint8_t bit) {
    *mask |= (1U << bit);
}

static inline void bitmap_clear(uint32_t* mask, uint8_t bit) {
    *mask &= ~(1U << bit);
}

// --- Priority Bitmap Operations ---
static void sched_bitmap_add(PriorityBitmap* bm, uint32_t task_id, uint8_t priority) {
    if (priority >= SCHED_NUM_PRIORITY_LEVELS) priority = SCHED_NUM_PRIORITY_LEVELS - 1;
    if (task_id >= 32) return;  // Only track first 32 tasks in bitmap
    
    bitmap_set(&bm->task_masks[priority], task_id);
    bitmap_set(&bm->level_mask, priority);
}

static void sched_bitmap_remove(PriorityBitmap* bm, uint32_t task_id, uint8_t priority) {
    if (priority >= SCHED_NUM_PRIORITY_LEVELS) priority = SCHED_NUM_PRIORITY_LEVELS - 1;
    if (task_id >= 32) return;
    
    bitmap_clear(&bm->task_masks[priority], task_id);
    
    // Clear level bit if no more tasks at this priority
    if (bm->task_masks[priority] == 0) {
        bitmap_clear(&bm->level_mask, priority);
    }
}

static int sched_bitmap_find_highest(PriorityBitmap* bm, uint32_t* task_id_out) {
    if (bm->level_mask == 0) return -1;
    
    // Find highest priority level with tasks
    int level = 31 - __builtin_clz(bm->level_mask);  // Count leading zeros
    
    // Find first task at this level
    int task_id = bitmap_ffs(bm->task_masks[level]);
    if (task_id >= 0) {
        *task_id_out = task_id;
        return level;
    }
    
    return -1;
}

// --- Scheduler Initialization ---
void scheduler_init_core0() {
    memset(&core0_sched, 0, sizeof(core0_sched));
    mutex_init(&core0_sched.lock);
    
    core0_sched.idle_task = 0;  // Task 0 is idle
    core0_sched.current_task = 0;
    core0_sched.last_aging = get_time_ms();
    
    kout.println("[SCHED] Core0 initialized (O(1) bitmap scheduler)");
}

void scheduler_init_core1() {
    memset(&core1_sched, 0, sizeof(core1_sched));
    mutex_init(&core1_sched.lock);
    
    core1_sched.last_aging = get_time_ms();
    
    kout.println("[SCHED] Core1 initialized (O(1) bitmap scheduler)");
}

// --- Quantum/Aging/Load ---
static void sched_adjust_quantum(TCB* task, bool yielded_early) {
    if (!task) return;
    
    uint8_t prio = task->priority;
    
    if (yielded_early && prio < SCHED_NUM_PRIORITY_LEVELS - 1) {
        // I/O bound - increase priority
        if (task->priority < 30) task->priority++;
    } else if (!yielded_early && prio > 0) {
        // CPU bound - decrease priority
        if (task->priority > 5) task->priority--;
    }
}

static void sched_age_tasks(CoreScheduler* sched, TCB* tasks, uint32_t task_count) {
    uint64_t now = get_time_ms();
    
    if (now - sched->last_aging < SCHED_AGING_INTERVAL_MS) return;
    
    sched->last_aging = now;
    
    for (uint32_t i = 0; i < task_count; i++) {
        TCB* task = &tasks[i];
        
        if (task->state != TASK_READY) continue;
        if (task->priority >= SCHED_RT_THRESHOLD) continue;  // Don't age RT tasks
        
        // Increase priority of starving tasks
        uint64_t wait_time = now - task->sched_info.last_run;
        if (wait_time > 1000 && task->priority < SCHED_NUM_PRIORITY_LEVELS - 1) {
            
            uint8_t old_prio = task->priority;
            task->priority++;  // Boost priority
            
            // Update bitmap
            mutex_enter_blocking(&sched->lock);
            sched_bitmap_remove(&sched->runnable, i, old_prio);
            sched_bitmap_add(&sched->runnable, i, task->priority);
            mutex_exit(&sched->lock);
        }
    }
}

static bool sched_should_inject_idle(CoreScheduler* sched) {
    if (sched->cpu_load < SCHED_IDLE_INJECTION_THRESHOLD) {
        sched->idle_injection_active = false;
        return false;
    }
    
    // Inject 10% idle time when CPU is hot
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
    
    // Exponential weighted moving average (alpha = 0.1)
    sched->cpu_load_instant = instant_load;
    sched->cpu_load = (sched->cpu_load * 0.9f) + (instant_load * 0.1f);
}

// --- Core0 Task Selection ---
uint32_t sched_select_next_core0() {
    mutex_enter_blocking(&core0_sched.lock);
    
    // Rebuild runnable bitmap from task states (simple version)
    memset(&core0_sched.runnable, 0, sizeof(PriorityBitmap));
    for (uint32_t i = 0; i < kernel.task_count && i < 32; i++) {
        TCB* task = &kernel.tasks[i];
        if (task->state == TASK_READY || task->state == TASK_RUNNING) {
            if (task->affinity != CORE_1) {
                sched_bitmap_add(&core0_sched.runnable, i, task->priority);
            }
        }
    }
    
    // Idle injection?
    if (sched_should_inject_idle(&core0_sched)) {
        mutex_exit(&core0_sched.lock);
        return core0_sched.idle_task;
    }
    
    // Find highest priority runnable task
    uint32_t task_id;
    int priority = sched_bitmap_find_highest(&core0_sched.runnable, &task_id);
    
    if (priority < 0) {
        // No runnable tasks - use idle
        mutex_exit(&core0_sched.lock);
        return core0_sched.idle_task;
    }
    
    // Validate task
    if (task_id >= kernel.task_count) {
        mutex_exit(&core0_sched.lock);
        return core0_sched.idle_task;
    }
    
    TCB* task = &kernel.tasks[task_id];
    if (task->state != TASK_READY && task->state != TASK_RUNNING) {
        // Stale bitmap entry
        sched_bitmap_remove(&core0_sched.runnable, task_id, priority);
        mutex_exit(&core0_sched.lock);
        return sched_select_next_core0();  // Retry
    }
    
    core0_sched.current_task = task_id;
    core0_sched.current_priority = priority;
    
    mutex_exit(&core0_sched.lock);
    return task_id;
}

// --- Core1 Task Selection ---
uint32_t sched_select_next_core1() {
    mutex_enter_blocking(&core1_sched.lock);
    
    // Rebuild runnable bitmap
    memset(&core1_sched.runnable, 0, sizeof(PriorityBitmap));
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count && i < 32; i++) {
        TCB* task = &kernel.core1.tasks[i];
        if (task->state == TASK_READY || task->state == TASK_RUNNING) {
            sched_bitmap_add(&core1_sched.runnable, i, task->priority);
        }
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    
    // Find highest priority runnable task
    uint32_t task_id;
    int priority = sched_bitmap_find_highest(&core1_sched.runnable, &task_id);
    
    if (priority < 0) {
        // No runnable tasks - spin
        mutex_exit(&core1_sched.lock);
        return 0xFFFFFFFF;
    }
    
    // Validate task
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


// --- Task Init & Main Tick ---
void task_init() {
    memset(&kernel.tasks, 0, sizeof(kernel.tasks));
    
    kernel.task_count = 0;
    kernel.current_task = 0;
    kernel.cpu_usage = 0.0f;
    kernel.root_mode = false;
    kernel.panic_mode = false;
    kernel.kernel_tasks = 0;
    kernel.driver_tasks = 0;
    kernel.service_tasks = 0;
    kernel.module_tasks = 0;
    kernel.application_tasks = 0;
    kernel.shell_alive = true;
    kernel.cpumon_alive = true;
    kernel.tempmon_alive = true;
    kernel.vfs_alive = false;
    kernel.fs_alive = false;
    kernel.log_head = 0;
    kernel.log_count = 0;
    kernel.total_context_switches = 0;
    kernel.gui_focus_task_id = -1;
    kernel.app_write_char = NULL;
    
    scheduler_init_core0();
}

uint32_t task_create(const char* name, void (*entry)(void*), void* arg, 
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, ModuleCallbacks* callbacks,
                     const char* description) {
    
    if (kernel.task_count >= MAX_TASKS) {
        kout.println("ERROR: Maximum tasks reached!");
        return 0;
    }
    
    // Non-applications cannot be OOM killed
    if (task_type != TASK_TYPE_APPLICATION) {
        oom_priority = OOM_PRIORITY_NEVER;
    }
    
    // Check for ONESHOT conflicts
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
    
    uint32_t id = kernel.task_count++;
    TCB* task = &kernel.tasks[id];
    
    memset(task, 0, sizeof(TCB));
    
    task->id = id;
    strncpy(task->name, name, TASK_NAME_LEN - 1);
    task->name[TASK_NAME_LEN - 1] = '\0';
    task->state = TASK_READY;
    task->task_type = task_type;
    task->priority = priority; // This will be used as effective_priority
    task->flags = flags;
    task->oom_priority = oom_priority;
    task->affinity = CORE_0;
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
    task->cpu_time = 0;
    task->last_run = get_time_ms();
    task->page_faults = 0;
    task->context_switches = 0;
    task->callbacks = callbacks;
    task->description = description;
    
    // Init scheduler info
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
    si->cpu_affinity = 0xFF;
    si->last_run = get_time_us();
    
    // Update task type counters
    if (task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks++;
    else if (task_type == TASK_TYPE_DRIVER) kernel.driver_tasks++;
    else if (task_type == TASK_TYPE_SERVICE) kernel.service_tasks++;
    else if (task_type == TASK_TYPE_MODULE) kernel.module_tasks++;
    else if (task_type == TASK_TYPE_APPLICATION) kernel.application_tasks++;
    
    // Add to scheduler bitmap
    mutex_enter_blocking(&core0_sched.lock);
    sched_bitmap_add(&core0_sched.runnable, id, priority);
    mutex_exit(&core0_sched.lock);
    
    // Call init callback if present
    if (callbacks && callbacks->init) {
        callbacks->init(id);
    }
    
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
    
    // Remove from runnable bitmap
    mutex_enter_blocking(&core0_sched.lock);
    sched_bitmap_remove(&core0_sched.runnable, task->id, task->priority);
    mutex_exit(&core0_sched.lock);
}

void scheduler_tick() {
    uint64_t now = get_time_ms();
    kernel.uptime_ms = now;
    
    if (kernel.task_count == 0 || kernel.task_count > MAX_TASKS) return;
    
    // Wake sleeping tasks
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        
        if (task->state == TASK_WAITING && now >= task->wake_time) {
            task->state = TASK_READY;
            
            // Add to runnable bitmap
            mutex_enter_blocking(&core0_sched.lock);
            sched_bitmap_add(&core0_sched.runnable, i, task->priority);
            mutex_exit(&core0_sched.lock);
        }
        
        // Runtime limits
        if (task->max_runtime > 0 && task->state != TASK_TERMINATED) {
            if ((now - task->start_time) > task->max_runtime) {
                char buf[64];
                snprintf(buf, sizeof(buf), "TIMEOUT: %s", task->name);
                klog(1, buf);
                brutal_task_kill(task->id);
            }
        }
        
        // Respawn
        if (task->state == TASK_TERMINATED && (task->flags & TASK_FLAG_RESPAWN)) {
            if (task->task_type == TASK_TYPE_KERNEL) continue;
            
            if (now - task->last_respawn > 5000) {
                task->state = TASK_READY;
                task->start_time = now;
                task->last_respawn = now;
                task->respawn_count++;
                task->mem_used = 0;
                task->cpu_time = 0;
                
                if (task->callbacks && task->callbacks->init) {
                    task->callbacks->init(task->id);
                }
                
                mutex_enter_blocking(&core0_sched.lock);
                sched_bitmap_add(&core0_sched.runnable, i, task->priority);
                mutex_exit(&core0_sched.lock);
            }
        }
    }
    
    // Age tasks
    sched_age_tasks(&core0_sched, kernel.tasks, kernel.task_count);
    
    // Update CPU load
    static uint64_t last_load_update = 0;
    static uint32_t last_idle_time = 0;
    static uint32_t last_total_time = 0;
    
    if (now - last_load_update >= 1000) {
        uint32_t idle_cpu_time = kernel.tasks[0].cpu_time;
        uint32_t total_cpu_time = 0;
        for (uint32_t i = 0; i < kernel.task_count; i++) {
            total_cpu_time += kernel.tasks[i].cpu_time;
        }
        
        uint32_t idle_delta = idle_cpu_time - last_idle_time;
        uint32_t total_delta = total_cpu_time - last_total_time;
        
        sched_update_load(&core0_sched, idle_delta, total_delta);
        kernel.cpu_usage = core0_sched.cpu_load;
        
        last_load_update = now;
        last_idle_time = idle_cpu_time;
        last_total_time = total_cpu_time;
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
        prev_task->last_run = now; // Also update legacy
        
        // Adjust quantum based on usage
        bool yielded_early = (prev_task->state == TASK_WAITING);
        sched_adjust_quantum(prev_task, yielded_early);
    }
    
    // Select next task
    uint32_t next_task = sched_select_next_core0();
    
    if (next_task < kernel.task_count) {
        kernel.current_task = next_task;
        kernel.total_context_switches++;
        kernel.tasks[next_task].context_switches++;
        kernel.tasks[next_task].sched_info.last_run = now;
        
        core0_sched.switches++;
    }
}

void brutal_task_kill(uint32_t id) {
    if (id >= kernel.task_count) return;
    TCB* task = &kernel.tasks[id];
    if (task->state == TASK_TERMINATED) return;
    
    kout.print("[KILL] '");
    kout.print(task->name);
    kout.println("'");
    
    // KERNEL TASK KILL = SYSTEM PANIC
    if (task->task_type == TASK_TYPE_KERNEL) {
        kernel_panic("KERNEL TASK KILLED");
    }
    
    // Release GUI focus if held
    if (kernel.gui_focus_task_id == (int32_t)id) {
        extern void k_release_gui_focus(uint32_t task_id);
        k_release_gui_focus(id);
    }
    
    // Unregister from GUI apps
    extern void k_unregister_gui_app(uint32_t task_id);
    k_unregister_gui_app(id);
    
    // Unregister OOM handler
    k_unregister_oom_handler(id);
    
    // Close all open files
    uint32_t files_closed = 0;
    for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (kernel.fs_open_files[i].open && 
            kernel.fs_open_files[i].owner_task_id == id) {
            
            kout.print("  > Closing file: ");
            kout.println(kernel.fs_open_files[i].path);
            
            kernel.fs_open_files[i].handle.close();
            kernel.fs_open_files[i].open = false;
            kernel.fs_open_files[i].owner_task_id = 0;
            files_closed++;
        }
    }
    
    if (files_closed > 0) {
        kout.print("  > Closed ");
        kout.print(files_closed);
        kout.println(" file(s)");
    }
    
    // Call deinit callback
    if (task->callbacks && task->callbacks->deinit) {
        task->callbacks->deinit();
    }
    
    // Free all memory
    uint32_t freed = 0;
    mutex_enter_blocking(&kernel.mem_lock);
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].owner_id == id) {
            freed += kernel.mem_blocks[i].size;
            kernel.mem_blocks[i].free = true;
            kernel.mem_blocks[i].owner_id = 0;
        }
    }
    mutex_exit(&kernel.mem_lock);
    
    if (freed > 0) {
        kout.print("  > Freed ");
        kout.print(freed);
        kout.println(" bytes");
    }
    
    // Update service states
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
    else if (strcmp(task->name, "vfs") == 0) {
        kernel.vfs_alive = false;
        kout.println("\n*** VFS DEAD ***");
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

    task->state = TASK_TERMINATED;
    task->mem_used = 0;
    task->last_run = get_time_ms();
    
    task->entry = NULL;
    task->callbacks = NULL;
    task->arg = NULL;
    
    // Remove from scheduler
    mutex_enter_blocking(&core0_sched.lock);
    sched_bitmap_remove(&core0_sched.runnable, id, task->priority);
    mutex_exit(&core0_sched.lock);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "KILL: %s terminated", task->name);
    klog(2, buf);
}

// ============================================================================
// CORE1 SCHEDULER
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
    
    while (kernel.core1.running) {
        if (kernel.core1.task_count == 0) {
            tight_loop_contents();
            sleep_us(1000);
            continue;
        }
        
        uint64_t loop_start = get_time_us();
        
        // Wake sleeping tasks
        uint64_t now_ms = get_time_ms();
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
            TCB* task = &kernel.core1.tasks[i];
            if (task->state == TASK_WAITING && now_ms >= task->wake_time) {
                task->state = TASK_READY;
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
        
        // Execute task
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        
        if (task_id >= kernel.core1.task_count) {
            mutex_exit(&kernel.core1.scheduler_lock);
            continue;
        }
        
        TCB* task = &kernel.core1.tasks[task_id];
        
        if (task->state == TASK_READY || task->state == TASK_RUNNING) {
            task->state = TASK_RUNNING;
            task->running_on_core = 1;
            
            uint64_t task_start = get_time_us();
            
            mutex_exit(&kernel.core1.scheduler_lock);
            
            // Run task
            if (task->callbacks && task->callbacks->tick) {
                task->callbacks->tick(task->arg);
            } else if (task->entry) {
                task->entry(task->arg);
            }
            
            mutex_enter_blocking(&kernel.core1.scheduler_lock);
            
            uint64_t task_duration = get_time_us() - task_start;
            task->cpu_time += task_duration / 1000;
            task->sched_info.last_run = get_time_ms();
            
            if (task->state == TASK_RUNNING) {
                task->state = TASK_READY;
            }
        }
        
        mutex_exit(&kernel.core1.scheduler_lock);
        
        core1_sched.switches++;
        
        // Update stats
        static uint32_t last_total_time = 0;
        if (now_ms - last_stats_update >= 1000) {
            mutex_enter_blocking(&kernel.core1.scheduler_lock);
            uint64_t total_cpu = 0;
            for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
                total_cpu += kernel.core1.tasks[i].cpu_time;
            }
            uint32_t total_delta = total_cpu - last_total_time;
            last_total_time = total_cpu;
            
            kernel.core1.cpu_usage = (total_delta / 10.0f); // ms per 1000ms = %
            if (kernel.core1.cpu_usage > 100) kernel.core1.cpu_usage = 100;
            mutex_exit(&kernel.core1.scheduler_lock);
            
            last_stats_update = now_ms;
        }
        
        // Maintain timing
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
    
    task->id = id + 1000; // Offset Core1 IDs
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
    
    // Init scheduler info
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
    
    mutex_exit(&kernel.core1.scheduler_lock);
    
    kout.print("[CORE1] Spawned task: ");
    kout.print(name);
    kout.print(" (ID=");
    kout.print(task->id);
    kout.println(")");
    
    return task->id;
}

// ============================================================================
// UISocket API IMPLEMENTATION
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

bool k_send_to_core1(uint32_t target_id, IPCMessageType type, void* data, size_t size) {
    // Default to normal priority
    return ipc_send(kernel.current_task, target_id, type, data, size, 150);
}

bool k_send_to_core1_priority(uint32_t target_id, IPCMessageType type, 
                               void* data, size_t size, uint8_t priority) {
    return ipc_send(kernel.current_task, target_id, type, data, size, priority);
}


bool k_receive_message(IPCMessage* msg_out) {
    // This is tricky. Core0 tasks receive from Core0 queue. Core1 tasks receive from Core0 queue.
    // We need to know which core is calling this.
    
    uint32_t task_id = 0;
    if (get_core_num() == 0) {
        task_id = kernel.current_task;
    } else {
        // Find task ID for Core 1
        // This is a simplification; a real UISocket would be per-task
        task_id = kernel.core1.tasks[kernel.core1.current_task].id;
    }
    
    return ipc_receive(task_id, msg_out);
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
        task_id_to_register = kernel.core1.tasks[kernel.core1.current_task].id;
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

    // Populate UISocket API
    socket_api->request_focus = k_request_gui_focus;
    socket_api->release_focus = k_release_gui_focus;
    socket_api->register_stdout = k_register_stdout_target;
    socket_api->send_to_core1 = k_send_to_core1;
    socket_api->send_priority = k_send_to_core1_priority;
    socket_api->receive_message = k_receive_message;
    socket_api->spawn_core1_task = k_spawn_core1_task;
    socket_api->lock_resource = k_lock_resource;
    socket_api->unlock_resource = k_unlock_resource;
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
// APPLICATION REGISTRATION
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
// CORE KERNEL TASKS
// ============================================================================

void idle_task(void* arg) {
    task_sleep(100);
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
            shell_execute(cmd_buffer);
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
    
    // This is now handled by scheduler_tick()
    
    task_sleep(1000);
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
    task_sleep(2000);
}

void tempmon_deinit() {
    kout.println("[TEMPMON] DEINIT");
    kernel.tempmon_alive = false;
}

void vfs_task(void* arg) {
    if (!kernel.vfs_alive) {
        task_sleep(10000);
        return;
    }
    task_sleep(5000);
}

/* * ============================================================================
 * V10 DEVELOPER GUIDE
 * ============================================================================
 * * 1. CREATE YOUR APP FILE (e.g., MyApp.ino):
 * * #include "v10.ino" // Or include the main kernel file
 * * static uint32_t my_task_id;
 * static UISocket ui;
 * * // Graceful OOM Handler (Optional but Recommended)
 * void my_oom_handler(uint32_t bytes_requested) {
 * kout.println("[MyApp] Received OOM request, cleaning up...");
 * uint32_t freed = 0;
 * * // Free your caches, buffers, etc.
 * // if (my_cache) {
 * //     freed += cache_size;
 * //     kfree(my_cache);
 * //     my_cache = NULL;
 * // }
 * * // Notify kernel
 * ui.oom_cleanup_done(my_task_id, freed);
 * }
 * * // Core1 Renderer (Example)
 * void render_loop(void* arg) {
 * k_register_gui_app(&ui);
 * * while(1) {
 * IPCMessage msg;
 * if (ui.receive_message(&msg)) {
 * if (msg.type == IPC_RENDER_FRAME) {
 * // GameState* frame = (GameState*)msg.data;
 * ui.lock_resource(0); // Lock display
 * // draw_frame(frame);
 * ui.unlock_resource(0); // Unlock display
 * }
 * }
 * task_sleep(1); // Check messages frequently
 * }
 * }
 * * // Core0 Main Logic
 * void spawn_myapp() {
 * k_register_gui_app(&ui);
 * ui.request_focus(kernel.current_task);
 * my_task_id = kernel.current_task;
 *
 * // Register OOM handler
 * ui.register_oom_handler(my_task_id, my_oom_handler);
 * * // Spawn high-priority renderer on Core1
 * uint32_t renderer_id = ui.spawn_core1_task("myapp_render", render_loop, NULL, 28); // 28 = high pri
 * * while(1) {
 * // Game logic...
 * * // Send frame data to Core1 with high priority (200)
 * // GameState state = get_game_state();
 * // ui.send_priority(renderer_id, IPC_RENDER_FRAME, &state, sizeof(state), 200);
 * * // Hint if we're low on memory
 * if (get_free_memory() < 10240) { // Less than 10KB free
 * ui.hint_memory_pressure(my_task_id);
 * }
 * * task_sleep(16); // ~60 FPS
 * }
 * }
 * * // Register the app with the kernel
 * struct MyAppReg {
 * MyAppReg() {
 * Application_Register("myapp", spawn_myapp);
 * }
 * } _myapp_reg;
 * * ============================================================================
 * DUAL CORE BEST PRACTICES:
 * ============================================================================
 * * Core0 (Main):
 * - Game logic, physics, input, File I/O
 * * Core1 (Offload):
 * - Rendering, audio processing, heavy computations
 * * Communication:
 * - Use IPC messages for data transfer.
 * - Use ui.send_priority() to send time-sensitive data (like render frames).
 * - Priority Levels: 200+ (High), 100-199 (Normal), 0-99 (Low)
 * - Lock resources (like display buffer) with ui.lock_resource(0);
 * * ============================================================================
 * KERNEL API REFERENCE (via UISocket):
 * ============================================================================
 * * // IPC
 * .send_to_core1(target, type, data, size)     - Send message with normal priority
 * .send_priority(target, type, data, size, pri) - Send with specific priority
 * .receive_message(msg_out)                    - Receive highest-priority message
 * * // Tasking
 * .spawn_core1_task(name, fn, arg, pri)        - Create Core1 task (0-31 priority)
 * * // OOM
 * .register_oom_handler(id, handler_fn)        - Register your cleanup function
 * .oom_cleanup_done(id, bytes_freed)           - Call after cleanup
 * .hint_memory_pressure(id)                    - Tell kernel to run compaction
 * * // Other
 * .lock_resource(id)                           - Lock shared resource
 * .unlock_resource(id)                         - Unlock shared resource
 * .get_core0_usage()                           - Get Core0 CPU %
 * .get_core1_usage()                           - Get Core1 CPU %
 * .get_task_memory(task_id)                    - Get task memory usage
 * * ============================================================================
 * KERNEL FUNCTIONS (GLOBAL):
 * ============================================================================
 * * - kmalloc(size, task_id)                     - Allocate memory
 * - kfree(ptr)                                 - Free memory
 * - task_sleep(ms)                             - Sleep task (on current core)
 * - klog(level, msg)                           - Write to kernel log
 * - Application_Register(name, fn)             - Register app
 * * ============================================================================
 */
