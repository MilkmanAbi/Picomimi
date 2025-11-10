// Libraries
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

// HARDWARE PINOUTS CONFIG!!
#define SD_CS 5
#define SD_MOSI 19
#define SD_MISO 16
#define SD_SCK 18
#define BTN_ONOFF 9

#define disable_all_interrupts() __asm__ volatile ("cpsid i" : : : "memory")
#define enable_all_interrupts() __asm__ volatile ("cpsie i" : : : "memory")

#define MAX_TASKS 32
#define MAX_MEMORY_BLOCKS 256
#define HEAP_SIZE (180 * 1024)
#define TASK_NAME_LEN 24
#define MAX_LOG_ENTRIES 40
#define MAX_APPS 16
#define MAX_GUI_APPS 8
#define MAX_KERNEL_MUTEXES 16
#define MAX_SEMAPHORES 16
#define MAX_EVENT_FLAGS 16

#define SCHEDULER_TICK_US 1000
#define SCHED_NUM_PRIORITY_LEVELS 32
#define SCHED_RT_THRESHOLD 24
#define SCHED_BASE_QUANTUM_US 5000
#define SCHED_MAX_QUANTUM_US 80000
#define SCHED_AGING_INTERVAL_MS 500
#define SCHED_IDLE_INJECTION_THRESHOLD 85

#define WATCHDOG_TIMEOUT_MS 8000
#define REAPER_INTERVAL_MS 5000

#define MAX_CORE1_TASKS 16
#define CORE1_STACK_SIZE (8 * 1024)

#define MAX_IPC_MESSAGES 64
#define IPC_MSG_SIZE 64
#define IPC_NULL_MSG 0xFFFF
#define IPC_TARGET_BROADCAST 0xFFFFFFFF

#define VFS_BLOCK_SIZE 256
#define VFS_MAX_FILES 16
#define VFS_FILENAME_LEN 16
#define VFS_MAX_FILE_SIZE (16 * 1024)
#define VFS_STORAGE_SIZE (128 * 1024)
#define VFS_FLASH_OFFSET (1024 * 1024)
#define VFS_MAX_BLOCKS_PER_FILE 64

#define FS_MAX_FILENAME 32
#define FS_MAX_OPEN_FILES 8
#define FS_BUFFER_SIZE 512
#define FS_LOG_FILE "/LogRecord"

#define OOM_REQUEST_TIMEOUT_MS 2000
#define MAX_OOM_HANDLERS 16

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

#define TASK_TYPE_KERNEL 0x01
#define TASK_TYPE_DRIVER 0x02
#define TASK_TYPE_SERVICE 0x04
#define TASK_TYPE_MODULE 0x08
#define TASK_TYPE_APPLICATION 0x10

#define TASK_FLAG_PROTECTED 0x01
#define TASK_FLAG_CRITICAL 0x02
#define TASK_FLAG_RESPAWN 0x04
#define TASK_FLAG_ONESHOT 0x08
#define TASK_FLAG_PERSISTENT 0x10
#define TASK_FLAG_OOM_CLEANUP_REQUESTED 0x20

#define OOM_PRIORITY_NEVER 0
#define OOM_PRIORITY_CRITICAL 1
#define OOM_PRIORITY_HIGH 2
#define OOM_PRIORITY_NORMAL 3
#define OOM_PRIORITY_LOW 4

#define FILE_TYPE_TEXT 0x01
#define FILE_TYPE_LOG 0x02
#define FILE_TYPE_DATA 0x03
#define FILE_TYPE_CONFIG 0x04

enum IPCMessageType : uint8_t {
    IPC_NONE = 0,
    IPC_RENDER_FRAME,
    IPC_PROCESS_INPUT,
    IPC_COMPUTE_DATA,
    IPC_AUDIO_SAMPLE,
    IPC_USER_DEFINED
};

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

struct AppEntry {
    char name[TASK_NAME_LEN];
    void (*spawn_func)();
};

struct ModuleCallbacks {
    void (*init)(uint32_t id);
    void (*tick)(void*);
    void (*deinit)();
};

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
} __attribute__((packed));

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
} __attribute__((aligned(64)));

struct MemBlock {
    void* addr;
    uint32_t size;
    uint32_t owner_id;
    uint32_t alloc_seq;
    uint32_t alloc_time;
    bool free;
    uint8_t _padding[3];
} __attribute__((packed));

struct LogEntry {
    uint64_t timestamp;
    char message[56];
    uint8_t level;
    uint8_t _padding[7];
} __attribute__((aligned(8)));

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
};

struct OOMVictim {
    uint32_t task_id;
    uint32_t memory_used;
    uint8_t oom_priority;
    int32_t score;
    bool has_handler;
};

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

struct PanicInfo {
    const char* reason;
    uint32_t task_id;
    uint32_t pc;
    uint32_t lr;
    uint32_t sp;
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
    uint8_t kernel_tasks;
    uint8_t driver_tasks;
    uint8_t service_tasks;
    uint8_t module_tasks;
    uint8_t application_tasks;
    uint32_t zombie_tasks;
    bool shell_alive;
    bool cpumon_alive;
    bool tempmon_alive;
    bool vfs_alive;
    bool fs_alive;
    bool root_mode;
    float cpu_usage;
    float temperature;
    uint32_t total_context_switches;
    LogEntry log[MAX_LOG_ENTRIES];
    uint32_t log_head;
    uint32_t log_count;
    mutex_t log_lock;
    VFSSuperblock* vfs_sb;
    uint8_t* vfs_data;
    bool vfs_mounted;
    bool vfs_active;
    uint32_t vfs_writes;
    uint32_t vfs_reads;
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
    uint8_t heap[HEAP_SIZE];
} __attribute__((aligned(64)));

static KernelState kernel __attribute__((aligned(64)));
static mutex_t kout_mutex;
static char cmd_buffer[128];
static uint32_t cmd_pos = 0;
static char shell_cwd[128] = "/";
static AppEntry app_registry[MAX_APPS];
static uint32_t app_registry_count = 0;
static uint32_t gui_app_task_ids[MAX_GUI_APPS];
static uint32_t gui_app_count = 0;
static int32_t current_gui_focus_index = -1;
static IPCStats ipc_stats;
static TaskOOMHandler oom_handlers[MAX_OOM_HANDLERS];
static OOMRequest oom_current_request = {0};
static OOMStats oom_stats = {0};
static CoreScheduler core0_sched;
static CoreScheduler core1_sched;
static PanicInfo last_panic;
static bool in_panic = false;
static WatchdogState watchdog_state = {false, 0, WATCHDOG_TIMEOUT_MS, 0, false};

static inline uint64_t get_time_us() { return micros(); }
static inline uint64_t get_time_ms() { return millis(); }
static inline void precise_sleep_us(uint32_t us) { if (us == 0) return; delayMicroseconds(us); }
static inline bool gpio_read_fast(uint8_t pin) { return digitalRead(pin) == LOW; }

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

__attribute__((noreturn))
void kernel_panic(const char* reason);

void watchdog_init();
void watchdog_feed();
void watchdog_check();

void klib_sort(void* base, size_t num, size_t size, int (*compare)(const void*, const void*));

size_t get_free_memory();
size_t get_used_memory();
size_t get_task_memory(uint32_t task_id);
void mem_compact();
void mem_init();
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);

bool oom_request_cleanup(OOMVictim* victim, size_t bytes_needed);
void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed);
static oom_callback_t oom_get_handler(uint32_t task_id);

void core1_main();

void scheduler_tick();
void sched_check_preemption();
void sched_update_task_priority(TCB* task);
static void sched_bitmap_add(PriorityBitmap* bm, uint32_t task_id, uint8_t priority);
static void sched_bitmap_remove(PriorityBitmap* bm, uint32_t task_id, uint8_t priority);

void task_yield();
uint32_t task_create(const char* name, void (*entry)(void*), void* arg,
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, ModuleCallbacks* callbacks,
                     const char* description, CoreAffinity affinity);
void brutal_task_kill(uint32_t id);
void task_sleep(uint32_t ms);
void task_wake(uint32_t task_id);
void task_init();
void core1_scheduler_init();

void ipc_init();
bool ipc_send_api(uint32_t target_id, IPCMessageType type, void* data, size_t size, uint8_t priority);
bool ipc_receive_api(IPCMessage* msg_out);
TCB* ipc_get_tcb_by_id(uint32_t task_id);

void rtos_primitives_init();
bool k_mutex_lock(uint32_t mutex_id);
void k_mutex_unlock(uint32_t mutex_id);
bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms);
void k_sem_post(uint32_t sem_id);
uint32_t k_event_wait(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms);
void k_event_set(uint32_t event_id, uint32_t flags);
void wait_list_add(TaskWaitNode** head, TCB* task);
uint32_t wait_list_pop(TaskWaitNode** head);

void vfs_init();
bool vfs_mount();
void vfs_unmount();
void vfs_list();
void vfs_stats();

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
void fs_log_write(const char* message); // Forward declaration
void fs_log_init(); // Forward declaration
int fs_open(const char* path, bool write_mode); // Forward declaration
void fs_close(int fd); // Forward declaration

void temp_init();
float read_temperature();

void idle_task(void* arg);
void k_reaper_task(void* arg);
void shell_task(void* arg);
void shell_deinit();
void input_task(void* arg);
void cpu_monitor_task(void* arg);
void cpumon_deinit();
void temp_monitor_task(void* arg);
void tempmon_deinit();
void vfs_task(void* arg);
void vfs_deinit();
void fs_task(void* arg);
void fs_deinit();

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

// VFS Commands
void cmd_vfscreate();
void cmd_vfsls();
void cmd_vfsstat();
void cmd_vfswrite(char* arg);
void cmd_vfsread(char* arg);
void cmd_vfsdel(char* arg);

void cmd_write(char* arg);
void cmd_logls();
void cmd_format_sd();
void cmd_mkdir(char* arg);
void cmd_rm(char* arg);
void cmd_touch(char* arg);
void cmd_logtail(char* arg);
void cmd_cd(const char* arg);

void klog(uint8_t level, const char* msg);

void k_task_exit_api();

ModuleCallbacks shell_callbacks = { NULL, shell_task, shell_deinit };
ModuleCallbacks input_callbacks = { NULL, input_task, NULL };
ModuleCallbacks cpumon_callbacks = { NULL, cpu_monitor_task, cpumon_deinit };
ModuleCallbacks tempmon_callbacks = { NULL, temp_monitor_task, tempmon_deinit };
ModuleCallbacks vfs_callbacks = { NULL, vfs_task, vfs_deinit };
ModuleCallbacks fs_callbacks = { NULL, fs_task, fs_deinit };

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
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║          *** KERNEL PANIC *** ║");
    Serial.println("╚════════════════════════════════════════╝");
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
        File panic_file = SD.open("/PANIC.LOG", "a");
        if (panic_file) {
            panic_file.print("["); panic_file.print((uint32_t)last_panic.timestamp);
            panic_file.print("] "); panic_file.println(reason);
            panic_file.close();
        }
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
        kout.println("\n*** WATCHDOG WARNING: No feed in ");
        kout.print((uint32_t)elapsed);
        kout.println(" ms ***");
        klog(2, "WDT: Feed timeout approaching!");
    }
}

void klib_sort(void* base, size_t num, size_t size, int (*compare)(const void*, const void*)) {
    if (num <= 1) return;
    char* arr = (char*)base;
    int stack[64];
    int top = -1;

    stack[++top] = 0;
    stack[++top] = num - 1;

    while (top >= 0) {
        int h = stack[top--];
        int l = stack[top--];
        char* pivot = arr + (l + (h - l) / 2) * size;
        int i = l, j = h;
        char temp[size];

        while (i <= j) {
            while (compare(arr + i * size, pivot) < 0) i++;
            while (compare(arr + j * size, pivot) > 0) j--;
            if (i <= j) {
                memcpy(temp, arr + i * size, size);
                memcpy(arr + i * size, arr + j * size, size);
                memcpy(arr + j * size, temp, size);
                i++;
                j--;
            }
        }
        if (l < j) {
            stack[++top] = l;
            stack[++top] = j;
        }
        if (i < h) {
            stack[++top] = i;
            stack[++top] = h;
        }
    }
}

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
    // VFS Commands
    else if (strcmp(cmd, "vfscreate") == 0) cmd_vfscreate();
    else if (strcmp(cmd, "vfsls") == 0) cmd_vfsls();
    else if (strcmp(cmd, "vfsstat") == 0) cmd_vfsstat();
    else if (strncmp(cmd, "vfswrite ", 9) == 0) cmd_vfswrite(cmd + 9);
    else if (strncmp(cmd, "vfsread ", 8) == 0) cmd_vfsread(cmd + 8);
    else if (strncmp(cmd, "vfsdel ", 7) == 0) cmd_vfsdel(cmd + 7);
    // FS Commands
    else if (strncmp(cmd, "ls", 2) == 0) {
        char path[128];
        if (strlen(cmd) <= 3) { // Handles "ls"
            fs_list(shell_cwd);
        } else if (cmd[2] == ' ') { // Handles "ls /path"
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
    else {
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
    kout.println(" help - Show this help");
    kout.println(" ps - List all tasks (both cores)");
    kout.println(" taskinfo <id> - Detailed task info");
    kout.println(" listapps - List applications");
    kout.println(" top - System monitor");
    kout.println(" mem - Memory statistics");
    kout.println(" memmap - Memory map");
    kout.println(" compact - Memory compaction");
    kout.println(" dmesg - System log");
    kout.println(" uptime - System uptime");
    kout.println(" temp - CPU temperature");
    kout.println(" ipcstat - IPC statistics");
    kout.println(" schedstat - Scheduler statistics");
    kout.println(" oomstat - OOM statistics");
    kout.println(" rtos_stat - RTOS primitive statistics");
    kout.println(" reboot - Restart system");
    kout.println("\n=== FS (SD Card) Commands ===");
    kout.println(" ls [path] - List SD files (e.g. 'ls' or 'ls /mydir')");
    kout.println(" stat - FS (SD card) statistics");
    kout.println(" cat <path> - Read file content");
    kout.println(" write <filename> <content> - Append content to file in cwd");
    kout.println("   (e.g. 'write hi.txt Hello World')");
    kout.println(" touch <path> - Create an empty file");
    kout.println(" mkdir <path> - Create a directory");
    kout.println(" rm <path> - Remove a file or empty directory");
    kout.println(" cd <path> - Change directory");
    kout.println(" logls - List log files on SD");
    kout.println(" logtail [N] - Show last N lines of kernel log (default 10)");
    kout.println(" format - [ROOT] Delete all files/dirs from SD root");
    kout.println("\n=== Task Management ===");
    kout.println(" kill <id> - Kill task");
    kout.println(" root - Toggle root mode");
    kout.println("\n=== VFS (RAM Disk) Commands ===");
    kout.println(" vfscreate - [ROOT] Create/Format VFS in RAM");
    kout.println(" vfsls - List VFS files");
    kout.println(" vfsstat - VFS statistics");
    kout.println(" vfswrite <file> <data> - Write data to VFS file");
    kout.println(" vfsread <id> - Read data from VFS file ID");
    kout.println(" vfsdel <id> - Delete VFS file ID");
    kout.println("\n=== Applications ===");
    if (app_registry_count == 0) {
        kout.println(" (None registered)");
    }
    for (uint32_t i = 0; i < app_registry_count; i++) {
        kout.print(" ");
        kout.println(app_registry[i].name);
    }
}

void cmd_ps() {
    kout.println("\n=== System Tasks ===");
    kout.println("ID Core Name Type State Pri Aff Mem(KB) CPU(ms) IPC");
    kout.println("---- ---- -------------------- --------- --------- --- --- ------- ------- ---");

    const char* state_str[] = {"READY", "RUN", "WAIT", "SUSP", "DEAD", "ZOMBI"};
    const char* type_str[] = {"KERNEL", "DRIVER", "SERVIC", "MODULE", "APP"};
    const char* aff_str[] = {"ANY", "C0", "C1"};

    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        if (task->id == 0 && task->entry == NULL) continue;

        uint8_t type_idx = 0;
        if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
        else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
        else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
        else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;

        char buf[100];
        snprintf(buf, sizeof(buf), "%4d C0 %-20s %-9s %-9s %3d %-3s %7d %7d %3d",
                 task->id,
                 task->name,
                 type_str[type_idx],
                 state_str[task->state],
                 task->priority,
                 aff_str[task->affinity],
                 task->mem_used / 1024,
                 task->cpu_time,
                 task->ipc.message_count);
        kout.println(buf);
    }

    if (kernel.core1_initialized) {
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
            TCB* task = &kernel.core1.tasks[i];
            char buf[100];
            snprintf(buf, sizeof(buf), "%4d C1 %-20s %-9s %-9s %3d %-3s %7d %7d %3d",
                     task->id,
                     task->name,
                     "APP",
                     state_str[task->state],
                     task->priority,
                     aff_str[task->affinity],
                     task->mem_used / 1024,
                     task->cpu_time,
                     task->ipc.message_count);
            kout.println(buf);
        }
        mutex_exit(&kernel.core1.scheduler_lock);
    }

    kout.println("\n--- Summary ---");
    kout.print("Core 0 Tasks: ");
    kout.print(kernel.task_count - kernel.zombie_tasks);
    if(kernel.zombie_tasks > 0) {
        kout.print(" ("); kout.print(kernel.zombie_tasks); kout.print(" zombies)");
    }
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

    kout.print("Total: ");
    kout.println(kernel.task_count - kernel.zombie_tasks + (kernel.core1_initialized ? kernel.core1.task_count : 0));
}

void cmd_taskinfo(char* arg) {
    uint32_t id = atoi(arg);
    TCB* task = ipc_get_tcb_by_id(id);

    if (task == NULL) {
        kout.println("Invalid task ID");
        return;
    }

    kout.println("\n=== Task Information ===");
    kout.print("ID: "); kout.println(task->id);
    kout.print("Name: "); kout.println(task->name);
    kout.print("Core: "); kout.println(task->running_on_core);

    const char* aff_str[] = {"ANY", "CORE_0", "CORE_1"};
    kout.print("\nAffinity: "); kout.println(aff_str[task->affinity]);

    const char* state_str[] = {"READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED", "ZOMBIE"};
    kout.print("\nState: "); kout.println(state_str[task->state]);

    const char* type_str[] = {"KERNEL", "DRIVER", "SERVICE", "MODULE", "APPLICATION"};
    uint8_t type_idx = 0;
    if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
    else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
    else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
    else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;
    kout.print("Type: "); kout.println(type_str[type_idx]);

    kout.print("Base Priority: "); kout.println(task->sched_info.base_priority);
    kout.print("Eff. Priority: "); kout.println(task->priority);
    if (task->original_priority != 0 && task->original_priority != task->priority) {
        kout.print("Orig. Priority: "); kout.println(task->original_priority);
        kout.println(" (Boosted by Priority Inheritance)");
    }
    kout.print("OOM Priority: "); kout.println(task->oom_priority);

    kout.print("\nMemory Used: ");
    kout.print(task->mem_used / 1024);
    kout.println(" KB");
    kout.print("Memory Peak: ");
    kout.print(task->mem_peak / 1024);
    kout.println(" KB");
    if (task->mem_limit > 0) {
        kout.print("Memory Limit: ");
        kout.print(task->mem_limit / 1024);
        kout.println(" KB");
    } else {
        kout.println("Memory Limit: Unlimited");
    }

    kout.print("\nCPU Time: ");
    kout.print(task->cpu_time);
    kout.println(" ms");
    kout.print("Context Switches: ");
    kout.println(task->context_switches);
    kout.print("Page Faults: ");
    kout.println(task->page_faults);

    uint64_t uptime = (get_time_ms() - task->start_time) / 1000;
    kout.print("\nUptime: ");
    kout.print((uint32_t)uptime);
    kout.println(" s");

    if (task->max_runtime > 0) {
        kout.print("Max Runtime: ");
        kout.print((uint32_t)(task->max_runtime / 1000));
        kout.println(" s");
    }
    if (task->respawn_count > 0) {
        kout.print("Respawn Count: ");
        kout.println(task->respawn_count);
    }

    kout.println("\n--- Flags ---");
    if (task->flags & TASK_FLAG_PROTECTED) kout.println(" PROTECTED");
    if (task->flags & TASK_FLAG_CRITICAL) kout.println(" CRITICAL");
    if (task->flags & TASK_FLAG_RESPAWN) kout.println(" RESPAWN");
    if (task->flags & TASK_FLAG_ONESHOT) kout.println(" ONESHOT");
    if (task->flags & TASK_FLAG_PERSISTENT) kout.println(" PERSISTENT");
    if (task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED) kout.println(" OOM_CLEANUP_REQUESTED");

    kout.println("\n--- IPC Queue ---");
    kout.print(" Messages pending: "); kout.println(task->ipc.message_count);
    kout.print(" Priority Bitmap: 0x"); kout.println(task->ipc.priority_bitmap, HEX);

    if (task->description) {
        kout.println("\n--- Description ---");
        kout.println(task->description);
    }
}

void cmd_ipcstat() {
    kout.println("\n=== IPC Statistics (v13 O(1) Pool) ===");
    uint16_t total_in_use = MAX_IPC_MESSAGES - (kernel.ipc_manager.free_list_head + 1);
    kout.print("Message Pool: ");
    kout.print(total_in_use);
    kout.print("/");
    kout.println(MAX_IPC_MESSAGES);

    uint16_t total_queued = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        total_queued += kernel.tasks[i].ipc.message_count;
    }
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
        total_queued += kernel.core1.tasks[i].ipc.message_count;
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    kout.print("Messages in Task Queues: ");
    kout.println(total_queued);

    kout.println("\n--- Lifetime Stats ---");
    kout.print("Sent: "); kout.println(kernel.ipc_manager.total_sent);
    kout.print("Received: "); kout.println(kernel.ipc_manager.total_received);
    kout.print("Broadcasts: "); kout.println(ipc_stats.broadcasts_sent);
    kout.print("Dropped (Pool Full): "); kout.println(ipc_stats.messages_dropped_pool_full);
    kout.print("Dropped (Task Full): "); kout.println(ipc_stats.messages_dropped_task_full);
    kout.print("\nAvg Pool Depth: ");
    kout.println(ipc_stats.avg_queue_depth_global, 1);
    kout.print("Max Pool Depth: ");
    kout.println(ipc_stats.max_queue_depth_global);
}

void cmd_schedstat() {
    kout.println("\n=== Scheduler Statistics ===");
    kout.println("Algorithm: O(1) Priority Bitmap");
    kout.println("Core 0 Mode: [PREEMPTIVE] (Priority-based, Tick-driven)");
    kout.print("Priority Levels: ");
    kout.println(SCHED_NUM_PRIORITY_LEVELS);

    kout.println("\n--- Core 0 ---");
    kout.print("Context Switches: ");
    kout.println(core0_sched.switches);
    kout.print("Preemptions: ");
    kout.println(core0_sched.preemptions);
    kout.print("CPU Load (avg): ");
    kout.print(core0_sched.cpu_load, 1);
    kout.println("%");
    kout.print("CPU Load (inst): ");
    kout.print(core0_sched.cpu_load_instant, 1);
    kout.println("%");
    kout.print("Idle Injections: ");
    kout.println(core0_sched.idle_injections);
    kout.print("Current Task: ");
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
        kout.print("CPU Load: ");
        kout.print(kernel.core1.cpu_usage, 1);
        kout.println("%");
        kout.print("Current Task: ");
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
        if (kernel.tasks[i].state != TASK_TERMINATED && kernel.tasks[i].state != TASK_ZOMBIE) {
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
    kout.print("Prevention Success: ");
    kout.println(oom_stats.prevention_count);
    kout.print("Graceful Requests: ");
    kout.println(oom_stats.requests_sent);
    kout.print("Voluntary Releases: ");
    kout.println(oom_stats.voluntary_releases);
    kout.print("Forced Kills: ");
    kout.println(oom_stats.forced_kills);
    kout.print("Total Reclaimed: ");
    kout.print(oom_stats.total_bytes_reclaimed / 1024);
    kout.println(" KB");

    if (oom_current_request.request_sent) {
        kout.println("\n--- Active OOM Request ---");
        kout.print(" Allocating Task: ");
        kout.println(oom_current_request.allocating_task_id);
        kout.print(" Victim Task: ");
        kout.println(oom_current_request.target_task_id);
        kout.print(" Time: ");
        kout.print((uint32_t)(get_time_ms() - oom_current_request.request_time));
        kout.println(" ms ago");
    }

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
                TCB* task = ipc_get_tcb_by_id(oom_handlers[i].task_id);
                if (task) {
                    kout.print(" - ");
                    kout.println(task->name);
                }
            }
        }
    }
}

void cmd_rtos_stat() {
    kout.println("\n=== RTOS Primitive Statistics ===");

    kout.println("\n--- Kernel Mutexes ---");
    kout.println("ID State Owner Waiting");
    kout.println("--- --------- --------- -------");
    for(int i=0; i < MAX_KERNEL_MUTEXES; i++) {
        KMutex* m = &kernel.kernel_mutexes[i];
        if (m->locked || m->wait_list_head) {
            uint32_t wait_count = 0;
            TaskWaitNode* node = m->wait_list_head;
            while(node) {
                wait_count++;
                node = node->next;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%3d %-9s %-9d %7d",
                     i, "LOCKED", m->owner_id, wait_count);
            kout.println(buf);
        }
    }

    kout.println("\n--- Kernel Semaphores ---");
    kout.println("ID Count Max Waiting");
    kout.println("--- --------- --------- -------");
    for(int i=0; i < MAX_SEMAPHORES; i++) {
        KSemaphore* s = &kernel.kernel_semaphores[i];
        if (s->max_count > 0) {
            uint32_t wait_count = 0;
            TaskWaitNode* node = s->wait_list_head;
            while(node) {
                wait_count++;
                node = node->next;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%3d %-9d %-9d %7d",
                     i, s->count, s->max_count, wait_count);
            kout.println(buf);
        }
    }

    kout.println("\n--- Kernel Event Flags ---");
    kout.println("ID Flags Waiting");
    kout.println("--- ---------- -------");
    for(int i=0; i < MAX_EVENT_FLAGS; i++) {
        KEvent* e = &kernel.kernel_event_flags[i];
        if (e->flags != 0 || e->wait_list_head) {
            uint32_t wait_count = 0;
            TaskWaitNode* node = e->wait_list_head;
            while(node) {
                wait_count++;
                node = node->next;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%3d 0x%08lX %7d",
                     i, e->flags, wait_count);
            kout.println(buf);
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
    kout.print(HEAP_SIZE / 1024);
    kout.println(" KB");
    kout.print("Tasks: ");
    kout.println(kernel.task_count - kernel.zombie_tasks);
    kout.print("OOM Kills: ");
    kout.println(oom_stats.forced_kills);
}

void cmd_mem() {
    kout.println("\n=== Memory Statistics ===");
    kout.print("Total: ");
    kout.print(HEAP_SIZE / 1024);
    kout.println(" KB");
    kout.print("Used: ");
    kout.print(get_used_memory() / 1024);
    kout.println(" KB");
    kout.print("Free: ");
    kout.print(get_free_memory() / 1024);
    kout.println(" KB");
    kout.print("Largest free: ");
    kout.print(kernel.largest_free_block / 1024);
    kout.println(" KB");
    kout.print("Fragmentation: ");
    kout.print(kernel.fragmentation_pct);
    kout.println("%");
    kout.print("Allocations: ");
    kout.println(kernel.total_allocations);
    kout.print("Frees: ");
    kout.println(kernel.total_frees);
    kout.print("Blocks: ");
    kout.println(kernel.mem_block_count);
    kout.print("OOM kills: ");
    kout.println(oom_stats.forced_kills);
}

void cmd_memmap() {
    kout.println("\n=== Memory Map ===");
    kout.println("Addr Size Owner Free Seq");
    kout.println("---------- -------- ------ ----- -----");

    MemBlock display_blocks[MAX_MEMORY_BLOCKS];
    uint32_t count = kernel.mem_block_count;
    if (count > MAX_MEMORY_BLOCKS) count = MAX_MEMORY_BLOCKS;

    memcpy(display_blocks, kernel.mem_blocks, count * sizeof(MemBlock));

    for (uint32_t i = 1; i < count; i++) {
        MemBlock key = display_blocks[i];
        int32_t j = i - 1;
        while (j >= 0 && display_blocks[j].addr > key.addr) {
            display_blocks[j + 1] = display_blocks[j];
            j = j - 1;
        }
        display_blocks[j + 1] = key;
    }

    uint32_t display_count = (count < 20) ? count : 20;
    for (uint32_t i = 0; i < display_count; i++) {
        MemBlock* block = &display_blocks[i];
        char buf[64];
        snprintf(buf, sizeof(buf), "0x%08lx %8d %6d %-5s %5d",
                 (uint32_t)block->addr, block->size, block->owner_id,
                 block->free ? "Y" : "N", block->alloc_seq);
        kout.println(buf);
    }
    if (count > display_count) {
        kout.print("... (");
        kout.print(count - display_count);
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
    if (id < 1000) {
        if (id >= kernel.task_count) {
            kout.println("Error: Invalid Core 0 task ID");
            return;
        }
        brutal_task_kill(id);
    } else {
        kout.println("Error: Core 1 task kill not yet supported from shell.");
    }
}

// --- VFS Command Implementations ---

void cmd_vfscreate() {
    if (!kernel.root_mode) {
        kout.println("Error: Root mode required");
        return;
    }
    if (kernel.vfs_active) {
        kout.println("[VFS] Already active. Unmount/Deinit first.");
        return;
    }
    
    kout.println("[VFS] Allocating VFS memory...");
    kernel.vfs_sb = (VFSSuperblock*)kmalloc(sizeof(VFSSuperblock), kernel.current_task);
    if (!kernel.vfs_sb) {
        kout.println("[VFS] Superblock allocation failed!");
        return;
    }
    
    kernel.vfs_data = (uint8_t*)kmalloc(VFS_STORAGE_SIZE, kernel.current_task);
    if (!kernel.vfs_data) {
        kout.println("[VFS] Data storage allocation failed!");
        kfree(kernel.vfs_sb);
        kernel.vfs_sb = NULL;
        return;
    }
    
    kernel.vfs_active = true;
    kout.print("[VFS] Allocated ");
    kout.print((VFS_STORAGE_SIZE + sizeof(VFSSuperblock)) / 1024);
    kout.println(" KB RAM");
    vfs_mount();
}

void cmd_vfsls() {
    vfs_list();
}

void cmd_vfsstat() {
    vfs_stats();
}

void cmd_vfswrite(char* arg) {
    char* filename = strtok(arg, " ");
    if (filename == NULL) {
        kout.println("Usage: vfswrite <filename> <data...>");
        return;
    }
    char* data = strtok(NULL, "");
    if (data == NULL) {
        kout.println("Usage: vfswrite <filename> <data...>");
        return;
    }

    int fd = vfs_create(filename, FILE_TYPE_DATA, kernel.current_task);
    if (fd < 0) {
        kout.println("[VFS] Failed to create file");
        return;
    }

    int written = vfs_write(fd, data, strlen(data));
    if (written < 0) {
        kout.println("[VFS] Write failed");
        vfs_delete(fd);
    } else {
        kout.print("[VFS] Wrote ");
        kout.print(written);
        kout.print(" bytes to ");
        kout.println(filename);
    }
}

void cmd_vfsread(char* arg) {
    int fd = atoi(arg);
    if (fd < 0 || fd >= VFS_MAX_FILES) {
        kout.println("Invalid VFS file ID");
        return;
    }

    if (!kernel.vfs_sb || !kernel.vfs_sb->files[fd].in_use) {
        kout.println("File ID not in use");
        return;
    }
    
    uint32_t fsize = kernel.vfs_sb->files[fd].size;
    if (fsize == 0) {
        kout.println("[VFS] File is empty");
        return;
    }
    
    char* buf = (char*)kmalloc(fsize + 1, kernel.current_task);
    if (!buf) {
        kout.println("[VFS] Failed to alloc read buffer");
        return;
    }

    int bytes_read = vfs_read(fd, buf, fsize);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0';
        kout.println("\n=== VFS File Content ===");
        kout.println(buf);
        kout.println("=== End ===");
    } else {
        kout.println("[VFS] Read failed");
    }
    kfree(buf);
}

void cmd_vfsdel(char* arg) {
    int fd = atoi(arg);
    if (fd < 0 || fd >= VFS_MAX_FILES) {
        kout.println("Invalid VFS file ID");
        return;
    }
    vfs_delete(fd);
}

// --- End VFS Commands ---

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
        kout.println("Append successful.");
    } else {
        kout.println("Append failed.");
    }
}

void cmd_logls() {
    if (!kernel.fs_mounted) {
        kout.println("[FS] Not mounted");
        return;
    }
    File root = SD.open("/");
    if (!root) {
        kout.println("[FS] Failed to open root directory");
        return;
    }

    kout.println("\n=== FS Log Files ===");
    kout.println("Name Type Size");
    kout.println("------------------------------- ----- --------");
    
    File file = root.openNextFile();
    bool found = false;
    while (file) {
        if (!file.isDirectory()) {
            String name_str = file.name();
            if (name_str.endsWith(".log") || name_str.endsWith(".txt") || name_str.equalsIgnoreCase(FS_LOG_FILE)) {
                char buf[80];
                snprintf(buf, sizeof(buf), "%-31s %-5s %8d",
                         file.name(), "FILE", (int)file.size());
                kout.println(buf);
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
    
    kout.println("\n*** FORMATTING SD CARD (ROOT) ***");
    kout.println("This will delete ALL files and directories from /");
    kout.println("This command is IRREVERSIBLE.");
    kout.println("... Waiting 5 seconds (Ctrl+C to abort)...");
    
    uint64_t start = get_time_ms();
    while (get_time_ms() - start < 5000) {
        shell_task(NULL); 
        task_sleep(20);
    }
    
    kout.println("Formatting...");
    
    mutex_enter_blocking(&kernel.fs_lock);
    File root = SD.open("/");
    if (!root) {
        kout.println("[FS] Failed to open root. Aborting.");
        mutex_exit(&kernel.fs_lock);
        return;
    }

    uint32_t files_deleted = 0;
    uint32_t dirs_deleted = 0;

    File file = root.openNextFile();
    while (file) {
        const char* fname = file.name();
        if (strcmp(fname, FS_LOG_FILE + 1) == 0 || strcmp(fname, "PANIC.LOG") == 0) {
            kout.print(" > Skipping protected file: "); kout.println(fname);
            file.close();
            file = root.openNextFile();
            continue;
        }

        kout.print(" > Deleting: "); kout.println(fname);
        if (file.isDirectory()) {
            if (SD.rmdir(fname)) {
                dirs_deleted++;
            } else {
                kout.print(" > Failed to delete dir (may not be empty): "); kout.println(fname);
            }
        } else {
            if (SD.remove(fname)) {
                files_deleted++;
            } else {
                kout.print(" > Failed to delete file: "); kout.println(fname);
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    mutex_exit(&kernel.fs_lock);

    kout.print("Format complete. Deleted ");
    kout.print(files_deleted);
    kout.print(" files and ");
    kout.print(dirs_deleted);
    kout.println(" directories from root.");

    fs_log_init();
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
        kout.print("Directory created: ");
        kout.println(arg);
    } else {
        kout.print("Failed to create directory: ");
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
        kout.print("Failed to remove (is dir empty?): ");
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
        kout.print("File touched: ");
        kout.println(arg);
    } else {
        kout.print("Failed to touch file: ");
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


void setup() {
    Serial.begin(115200);
    delay(2000);
    mutex_init(&kout_mutex);

    Application_Register("stress", spawn_stress_app);
    Serial.println("APP_REG: Registration phase complete.");

    SPI.setRX(SD_MISO);
    SPI.setTX(SD_MOSI);
    SPI.setSCK(SD_SCK);
    randomSeed(micros());

    kout.println("========================================");
    kout.println(" Picomimi Kernel v11 MK4");
    kout.println(" (RTOS Feature Release)");
    kout.println("========================================");
    kout.println("Initializing...");

    watchdog_init();
    pinMode(BTN_ONOFF, INPUT_PULLUP);
    kout.println("[OK] Input system");

    temp_init();
    kernel.temperature = read_temperature();
    kout.print("[OK] Temperature (");
    kout.print(kernel.temperature, 1);
    kout.println("C)");

    mem_init();
    kout.println("[OK] Memory manager (Best-Fit, Merge-on-Free)");

    task_init();
    kout.println("[OK] Task scheduler (Preemptive O(1) Bitmap)");

    mutex_init(&kernel.log_lock);
    kout.println("[OK] Logging system");

    ipc_init();
    rtos_primitives_init();

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
                OOM_PRIORITY_NEVER, 0, NULL, "Kernel idle task", CORE_0);
    kout.println("[OK] Idle (Pri 0, Core 0)");

    task_create("k_reaper", k_reaper_task, NULL, 1,
                TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 1 * 1024, NULL, "Zombie task reaper", CORE_0);
    kout.println("[OK] Reaper (Pri 1, Core 0)");

    task_create("input_cycle", NULL, NULL, 28,
                TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 1 * 1024, &input_callbacks, "Focus cycle driver", CORE_0);
    kout.println("[OK] Input driver (Pri 28, Core 0)");

    task_create("shell", NULL, NULL, 10,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NORMAL, 4 * 1024, &shell_callbacks, "Command shell", CORE_0);
    kout.println("[OK] Shell service (Pri 10, Core 0)");

    task_create("cpumon", NULL, NULL, 2,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &cpumon_callbacks, "CPU monitor", CORE_0);
    kout.println("[OK] CPU monitor (Pri 2, Core 0)");

    task_create("tempmon", NULL, NULL, 2,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &tempmon_callbacks, "Temp monitor", CORE_0);
    kout.println("[OK] Temp monitor (Pri 2, Core 0)");

    // Re-enable VFS task
    task_create("vfs", NULL, NULL, 7,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &vfs_callbacks, "VFS service", CORE_0);
    kout.println("[OK] VFS service (Pri 7, Core 0)");

    if (kernel.fs_available) {
        task_create("fs", NULL, NULL, 8,
                    TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                    OOM_PRIORITY_NEVER, 4 * 1024, &fs_callbacks, "FS service", CORE_0);
        kout.println("[OK] FS service (Pri 8, Core 0)");
    }

    kout.println("========================================");
    kout.println("Kernel boot complete!");
    kout.println("========================================");
    kout.print("Heap: "); kout.print(HEAP_SIZE / 1024); kout.println(" KB");
    kout.print("Core0 Tasks: "); kout.println(kernel.task_count);
    kout.println("Core1: Ready for offload");
    kout.print("Apps: "); kout.print(app_registry_count); kout.println(" registered");

    if (kernel.fs_available && kernel.sd_info.valid) {
        kout.print("SD Card: ");
        if (kernel.sd_info.card_size > 0) {
            if (kernel.sd_info.card_size >= 1024ULL * 1024 * 1024) {
                kout.print((float)kernel.sd_info.card_size / (1024.0f * 1024.0f * 1024.0f), 2);
                kout.println(" GB");
            } else {
                kout.print((float)kernel.sd_info.card_size / (1024.0f * 1024.0f), 2);
                kout.println(" MB");
            }
        } else {
            kout.println("Detected (Size N/A on RP2040)");
        }
    } else {
        kout.println("SD Card: Unavailable");
    }

    kout.println("\nType 'help' for commands");
    klog(0, "KERNEL: Boot v11 MK2 (RTOS)");
    shell_prompt();
    kernel.running = true;
}

void loop() {
    if (kernel.current_task >= MAX_TASKS || !kernel.running || kernel.task_count == 0) {
        kernel_panic("Kernel loop fault");
    }

    uint64_t loop_start = get_time_us();

    scheduler_tick();

    if (kernel.tasks[0].state == TASK_TERMINATED || kernel.tasks[0].entry == NULL) {
        kernel_panic("IDLE TASK DEAD");
    }

    if (kernel.preemption_pending) {
        kernel.preemption_pending = false;
        core0_sched.preemptions++;
        task_yield();
    }

    TCB* task = &kernel.tasks[kernel.current_task];

    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) {
        task_yield();
        return;
    }

    if (task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED) {
        oom_callback_t handler = oom_get_handler(task->id);
        if (handler) {
            kout.print("[OOM] Invoking graceful handler for '");
            kout.print(task->name);
            kout.println("'");
            handler(task->oom_bytes_requested);
        }
        task->flags &= ~TASK_FLAG_OOM_CLEANUP_REQUESTED;
        task->oom_bytes_requested = 0;
    }

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

        if (task->state == TASK_RUNNING) {
            task->state = TASK_READY;
        }
    }

    task_yield();

    static uint32_t watchdog_counter = 0;
    if (++watchdog_counter >= 10) {
        watchdog_feed();
        watchdog_counter = 0;
    }

    uint64_t elapsed = get_time_us() - loop_start;
    if (elapsed < SCHEDULER_TICK_US) {
        precise_sleep_us(SCHEDULER_TICK_US - elapsed);
    }
}

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

TCB* ipc_get_tcb_by_id(uint32_t task_id) {
    if (task_id < 1000) {
        if (task_id < kernel.task_count) {
            return &kernel.tasks[task_id];
        }
    } else {
        uint32_t local_id = task_id - 1000;
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        if (local_id < kernel.core1.task_count) {
            TCB* task = &kernel.core1.tasks[local_id];
            mutex_exit(&kernel.core1.scheduler_lock);
            return task;
        }
        mutex_exit(&kernel.core1.scheduler_lock);
    }
    return NULL;
}

static inline void ipc_lock_task_queue(TCB* task) {
    if (task->running_on_core == 0) {
        disable_all_interrupts();
    } else {
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
    }
}

static inline void ipc_unlock_task_queue(TCB* task) {
    if (task->running_on_core == 0) {
        enable_all_interrupts();
    } else {
        mutex_exit(&kernel.core1.scheduler_lock);
    }
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

    TCB* target_task = ipc_get_tcb_by_id(target_id);
    if (target_task == NULL || target_task->state == TASK_ZOMBIE) {
        mutex_enter_blocking(&kernel.ipc_manager.lock);
        msg->in_use = false;
        kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
        mutex_exit(&kernel.ipc_manager.lock);
        return false;
    }

    ipc_lock_task_queue(target_task);
    if (target_task->ipc.message_count >= MAX_IPC_MESSAGES) {
        ipc_unlock_task_queue(target_task);
        mutex_enter_blocking(&kernel.ipc_manager.lock);
        msg->in_use = false;
        kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
        mutex_exit(&kernel.ipc_manager.lock);
        ipc_stats.messages_dropped_task_full++;
        return false;
    }

    uint16_t* head = &target_task->ipc.priority_lists_head[priority];
    msg->next = *head;
    *head = msg_index;
    target_task->ipc.priority_bitmap |= (1U << priority);
    target_task->ipc.message_count++;
    ipc_unlock_task_queue(target_task);

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

    if (target_id == IPC_TARGET_BROADCAST) {
        bool all_ok = true;
        ipc_stats.broadcasts_sent++;
        for(uint32_t i=0; i < kernel.task_count; i++) {
            if (kernel.tasks[i].id != sender_id && kernel.tasks[i].state != TASK_ZOMBIE) {
                if (!ipc_send_raw(sender_id, kernel.tasks[i].id, type, data, size, priority)) {
                    all_ok = false;
                }
            }
        }
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        for(uint32_t i=0; i < kernel.core1.task_count; i++) {
            if (kernel.core1.tasks[i].id != sender_id) {
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
    if (get_core_num() == 0) {
        task = &kernel.tasks[kernel.current_task];
    } else {
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
        task = &kernel.core1.tasks[core1_sched.current_task];
        mutex_exit(&kernel.core1.scheduler_lock);
    }
    if (task == NULL) return false;

    ipc_lock_task_queue(task);
    if (task->ipc.priority_bitmap == 0) {
        ipc_unlock_task_queue(task);
        return false;
    }

    int highest_priority = 31 - __builtin_clz(task->ipc.priority_bitmap);
    uint16_t* head = &task->ipc.priority_lists_head[highest_priority];
    uint16_t msg_index = *head;

    if (msg_index == IPC_NULL_MSG) {
        ipc_unlock_task_queue(task);
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
    ipc_unlock_task_queue(task);
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

    TCB* owner_task = ipc_get_tcb_by_id(mutex->owner_id);
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

void mem_delete_block(uint32_t index) {
    if (index >= kernel.mem_block_count) return;
    kernel.mem_blocks[index] = kernel.mem_blocks[kernel.mem_block_count - 1];
    memset(&kernel.mem_blocks[kernel.mem_block_count - 1], 0, sizeof(MemBlock));
    kernel.mem_block_count--;
}

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
    uint32_t n = kernel.mem_block_count;
    if (n <= 1) {
        mutex_exit(&kernel.mem_lock);
        return;
    }

    for (uint32_t i = 1; i < n; i++) {
        MemBlock key = kernel.mem_blocks[i];
        int32_t j = i - 1;
        while (j >= 0 && kernel.mem_blocks[j].addr > key.addr) {
            kernel.mem_blocks[j + 1] = kernel.mem_blocks[j];
            j = j - 1;
        }
        kernel.mem_blocks[j + 1] = key;
    }

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
    mutex_exit(&kernel.mem_lock);
}

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
    score += (mem_used / 1024);
    score += (task->oom_priority * 100);

    uint64_t idle_time = get_time_ms() - task->last_run;
    if (idle_time > 5000) score += 200;
    else if (idle_time > 1000) score += 50;

    oom_callback_t handler = oom_get_handler(task->id);
    if (handler) score -= 50;

    if (task->flags & TASK_FLAG_CRITICAL) score = -10000;
    if (task->task_type != TASK_TYPE_APPLICATION) score = -10000;
    return score;
}

OOMVictim oom_select_victim(size_t bytes_needed) {
    OOMVictim victim = {0};
    victim.task_id = 0xFFFFFFFF;
    victim.score = -10000;

    kout.println("[OOM] Selecting victim...");

    for (uint32_t i = 1; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) continue;
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

    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
        TCB* task = &kernel.core1.tasks[i];
        if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) continue;
        if (task->task_type != TASK_TYPE_APPLICATION) continue;
        if (task->flags & TASK_FLAG_CRITICAL) continue;

        uint32_t task_mem = 10 * 1024;
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
    TCB* task = ipc_get_tcb_by_id(victim->task_id);
    if (!task) return false;

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
    oom_stats.requests_sent++;
    return true;
}

void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed) {
    if (!oom_current_request.request_sent) return;
    if (oom_current_request.target_task_id != task_id) return;
    TCB* task = ipc_get_tcb_by_id(task_id);
    if (!task) return;

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

    if (oom_prevent(bytes_needed)) {
        return false;
    }

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

    OOMVictim victim = oom_select_victim(bytes_needed);
    if (victim.task_id == 0xFFFFFFFF) {
        kout.println("OOM: NO KILLABLE APPLICATIONS!");
        klog(3, "OOM: No victims, PANIC!");
        kernel_panic("OOM: No killable victims");
        return false;
    }

    TCB* victim_task = ipc_get_tcb_by_id(victim.task_id);
    kout.print("[OOM] Selected victim: '");
    kout.print(victim_task->name);
    kout.print("' (");
    kout.print(victim.memory_used / 1024);
    kout.print(" KB, score=");
    kout.print(victim.score);
    kout.println(")");

    if (victim.has_handler && oom_request_cleanup(&victim, bytes_needed)) {
        return true;
    }

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
    return false;
}

void* kmalloc(size_t size, uint32_t task_id) {
    if (size == 0) return NULL;
    size = (size + 3) & ~3;

    for (int attempt = 0; attempt < 2; attempt++) {
        mutex_enter_blocking(&kernel.mem_lock);

        if (task_id < MAX_TASKS) {
            TCB* task = &kernel.tasks[task_id];
            task->page_faults++;
            if (task->task_type == TASK_TYPE_APPLICATION && task->mem_limit > 0) {
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

        if (best_block_idx != 0xFFFFFFFF) {
            MemBlock* block = &kernel.mem_blocks[best_block_idx];
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

        mutex_exit(&kernel.mem_lock);

        if (oom_killer(size)) {
            kout.println("[MEM] kmalloc: Sleeping, awaiting OOM cleanup...");
            task_sleep(OOM_REQUEST_TIMEOUT_MS + 500);
        } else {
        }
    }
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    mutex_enter_blocking(&kernel.mem_lock);

    int freed_block_idx = -1;
    uint32_t task_owner = 0;

    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].addr == ptr) {
            if (kernel.mem_blocks[i].free) {
                mutex_exit(&kernel.mem_lock);
                klog(2, "MEM: Double free attempt");
                return;
            }
            kernel.mem_blocks[i].free = true;
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

    MemBlock* freed_block = &kernel.mem_blocks[freed_block_idx];

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

void vfs_init() {
    kernel.vfs_sb = NULL;
    kernel.vfs_data = NULL;
    kernel.vfs_mounted = false;
    kernel.vfs_active = false;
    kernel.vfs_writes = 0;
    kernel.vfs_reads = 0;
    kout.println("[VFS] Initialized (inactive, use vfscreate)");
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

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (kernel.vfs_sb->files[i].in_use &&
                strcmp(kernel.vfs_sb->files[i].name, name) == 0) {
            kout.println("[VFS] File exists");
            return -1;
        }
    }

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

    file->chain.block_count = 0;
    uint32_t bytes_written = 0;
    for (uint32_t i = 0; i < allocated_count; i++) {
        uint16_t block_num = allocated_blocks[i];
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
    kout.println("ID Name Type Size Blks Owner");
    kout.println("-- --------------- ----- ------ ---- -----");
    const char* type_str[] = {"", "TEXT", "LOG", "DATA", "CONF"};

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        VFSFile* file = &kernel.vfs_sb->files[i];
        if (file->in_use) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%2d %-15s %-5s %6d %4d %5d",
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
    kout.print("Total blocks: "); kout.println(kernel.vfs_sb->total_blocks);
    kout.print("Free blocks: "); kout.println(kernel.vfs_sb->free_blocks);
    kout.print("Files: ");
    kout.print(kernel.vfs_sb->file_count);
    kout.print("/");
    kout.println(VFS_MAX_FILES);
    kout.print("Total writes: "); kout.println(kernel.vfs_writes);
    kout.print("Total reads: "); kout.println(kernel.vfs_reads);
}

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
        
        kernel.sd_info.card_type = 2;
        kernel.sd_info.card_size = 0;
        kernel.sd_info.sector_count = 0;
        kernel.sd_info.sector_size = 512;
        kernel.sd_info.valid = true;

        kout.print("[FS] Card Type: ");
        if (kernel.sd_info.card_type == 1) kout.println("SD1");
        else if (kernel.sd_info.card_type == 2) kout.println("SD2/SDHC");
        else kout.println("Unknown");
        
        kout.print("[FS] Detected card size: ");
        if (kernel.sd_info.card_size > 0) {
            if (kernel.sd_info.card_size >= 1024ULL * 1024 * 1024) {
                kout.print((float)kernel.sd_info.card_size / (1024.0f * 1024.0f * 1024.0f), 2);
                kout.println(" GB");
            } else {
                kout.print((float)kernel.sd_info.card_size / (1024.0f * 1024.0f), 2);
                kout.println(" MB");
            }
        } else {
            kout.println("N/A (RP2040)");
        }

    } else {
        kout.println("[FS] SD card not detected");
        klog(1, "FS: No SD card");
        return;
    }
    klog(0, "FS: Init OK");
}

void fs_log_init() {
    if (!kernel.fs_available) return;

    File logFile = SD.open(FS_LOG_FILE, "r"); 
    if (!logFile) {
        kout.println("[FS] Creating LogRecord file");
        logFile = SD.open(FS_LOG_FILE, "w"); 
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
    
    File logFile = SD.open(FS_LOG_FILE, "a");
    
    if (!logFile) {
        mutex_exit(&kernel.fs_lock);
        return;
    }
    
    // logFile.seek(logFile.size()); 
    
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
    kout.println("Name Type Size");
    kout.println("------------------------------- ----- --------");
    File file = root.openNextFile();
    while (file) {
        char filename_buf[40];
        strncpy(filename_buf, file.name(), sizeof(filename_buf) - 1);
        filename_buf[sizeof(filename_buf) - 1] = '\0';
        
        bool is_dir = file.isDirectory();
        
        int name_len = strlen(filename_buf);
        if (name_len > 0 && filename_buf[name_len - 1] == '/') {
            is_dir = true;
            filename_buf[name_len - 1] = '\0';
        }
        // } else if (file.size() == 0) { // <-- This logic was buggy
        //     is_dir = !is_dir;
        // }

        char buf[80];
        snprintf(buf, sizeof(buf), "%-31s %-5s %8d",
                 filename_buf,
                 is_dir ? "DIR" : "FILE",
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

    kout.println("\n=== FS Statistics ===");
    if (kernel.sd_info.valid && kernel.sd_info.card_size > 0) {
        kout.print("Total space: ");
        kout.print((float)kernel.sd_info.card_size / (1024.0f * 1024.0f * 1024.0f), 2);
        kout.println(" GB");
        
        kout.println("Used space: N/A (RP2040)");

    } else if (kernel.sd_info.valid) {
        kout.println("Card size: N/A (RP2040)");
        kout.println("Used space: N/A (RP2040)");
    } else {
        kout.println("Card size: Unknown");
    }
    kout.print("Total reads (cached): "); kout.println(kernel.fs_reads);
    kout.print("Total writes (cached): "); kout.println(kernel.fs_writes);
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

int fs_write_str(int fd, const char* data) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return -1;
    if (!kernel.fs_open_files[fd].open) return -1;
    if (!kernel.fs_open_files[fd].write_mode) return -1;
    int written = kernel.fs_open_files[fd].handle.print(data);
    kernel.fs_writes++;
    return written;
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

void scheduler_init_core0() {
    memset(&core0_sched, 0, sizeof(core0_sched));
    mutex_init(&core0_sched.lock);
    core0_sched.idle_task = 0;
    core0_sched.current_task = 0;
    core0_sched.last_aging = get_time_ms();
    kout.println("[SCHED] Core0 initialized (Preemptive O(1) bitmap scheduler)");
}

void scheduler_init_core1() {
    memset(&core1_sched, 0, sizeof(core1_sched));
    mutex_init(&core1_sched.lock);
    core1_sched.last_aging = get_time_ms();
    kout.println("[SCHED] Core1 initialized (O(1) bitmap scheduler)");
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
    for(int p = 0; p < SCHED_NUM_PRIORITY_LEVELS; p++) {
        if (core0_sched.runnable.task_masks[p] & (1U << task->id)) {
            sched_bitmap_remove(&core0_sched.runnable, task->id, p);
            break;
        }
    }
    if (task->state == TASK_READY) {
        sched_bitmap_add(&core0_sched.runnable, task->id, task->priority);
    }
    enable_all_interrupts();
}

static void sched_adjust_quantum(TCB* task, bool yielded_early) {
    if (!task) return;
    if (task->original_priority != 0) return;
    uint8_t prio = task->priority;
    if (yielded_early && prio < SCHED_NUM_PRIORITY_LEVELS - 1) {
        if (task->priority < 30) task->priority++;
    } else if (!yielded_early && prio > 0) {
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
    kernel.vfs_alive = false;
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
                     uint32_t mem_limit, ModuleCallbacks* callbacks,
                     const char* description, CoreAffinity affinity) {
    if (kernel.task_count >= MAX_TASKS) {
        kout.println("ERROR: Maximum tasks reached!");
        return 0;
    }

    if (task_type != TASK_TYPE_APPLICATION) {
        oom_priority = OOM_PRIORITY_NEVER;
    }

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
    task->cpu_time = 0;
    task->last_run = get_time_ms();
    task->page_faults = 0;
    task->context_switches = 0;
    task->callbacks = callbacks;
    task->description = description;
    task->oom_bytes_requested = 0;
    task_init_ipc_queue(task);
    task->original_priority = 0;

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

    if (task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks++;
    else if (task_type == TASK_TYPE_DRIVER) kernel.driver_tasks++;
    else if (task_type == TASK_TYPE_SERVICE) kernel.service_tasks++;
    else if (task_type == TASK_TYPE_MODULE) kernel.module_tasks++;
    else if (task_type == TASK_TYPE_APPLICATION) kernel.application_tasks++;

    disable_all_interrupts();
    if (affinity == CORE_0 || affinity == CORE_ANY) {
        sched_bitmap_add(&core0_sched.runnable, id, priority);
    }
    enable_all_interrupts();

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
    if (task->state == TASK_WAITING) {
        task->state = TASK_READY;
        task->wake_time = 0;
        if (task->affinity == CORE_0 || task->affinity == CORE_ANY) {
            sched_bitmap_add(&core0_sched.runnable, task->id, task->priority);
        }
        sched_check_preemption();
    }
    enable_all_interrupts();
}

void scheduler_tick() {
    uint64_t now = get_time_ms();
    kernel.uptime_ms = now;
    if (kernel.task_count == 0 || kernel.task_count > MAX_TASKS) return;

    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];

        if (task->state == TASK_WAITING && task->wake_time != 0 && now >= task->wake_time) {
            task_wake(i);
        }

        if (task->max_runtime > 0 && task->state != TASK_TERMINATED && task->state != TASK_ZOMBIE) {
            if ((now - task->start_time) > task->max_runtime) {
                char buf[64];
                snprintf(buf, sizeof(buf), "TIMEOUT: %s", task->name);
                klog(1, buf);
                brutal_task_kill(task->id);
            }
        }

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

    sched_age_tasks(&core0_sched, kernel.tasks, kernel.task_count);

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
    }

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
        bool yielded_early = (prev_task->state == TASK_WAITING);
        sched_adjust_quantum(prev_task, yielded_early);
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

void brutal_task_kill(uint32_t id) {
    if (id >= kernel.task_count) return;
    TCB* task = &kernel.tasks[id];
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) return;

    kout.print("[KILL] '");
    kout.print(task->name);
    kout.println("'");

    if (task->task_type == TASK_TYPE_KERNEL) {
        kernel_panic("KERNEL TASK KILLED");
    }

    if (kernel.gui_focus_task_id == (int32_t)id) {
        extern void k_release_gui_focus(uint32_t task_id);
        k_release_gui_focus(id);
    }
    extern void k_unregister_gui_app(uint32_t task_id);
    k_unregister_gui_app(id);
    k_unregister_oom_handler(id);

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

    // --- BEGIN PATCH ---
    // Free the task's memory IMMEDIATELY. This is critical for the OOM killer.
    // Do not wait for the reaper task.
    uint32_t freed_bytes = 0;
    mutex_enter_blocking(&kernel.mem_lock);
    for (uint32_t b = 0; b < kernel.mem_block_count; b++) {
        if (kernel.mem_blocks[b].owner_id == task->id) {
            freed_bytes += kernel.mem_blocks[b].size;
            kernel.mem_blocks[b].free = true;
            kernel.mem_blocks[b].owner_id = 0;
        }
    }
    mutex_exit(&kernel.mem_lock);

    if (freed_bytes > 0) {
        kout.print(" > Reclaimed ");
        kout.print(freed_bytes / 1024);
        kout.println(" KB");
        // We must also compact memory right after freeing
        mem_compact();
    }
    // --- END PATCH ---

    if (task->callbacks && task->callbacks->deinit) {
        task->callbacks->deinit();
    }

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

    if (task->task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks--;
    else if (task->task_type == TASK_TYPE_DRIVER) kernel.driver_tasks--;
    else if (task->task_type == TASK_TYPE_SERVICE) kernel.service_tasks--;
    else if (task->task_type == TASK_TYPE_MODULE) kernel.module_tasks--;
    else if (task->task_type == TASK_TYPE_APPLICATION) kernel.application_tasks--;

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

    disable_all_interrupts();
    sched_bitmap_remove(&core0_sched.runnable, id, task->priority);
    enable_all_interrupts();

    char buf[64];
    snprintf(buf, sizeof(buf), "KILL: %s marked as %s", task->name, (task->state == TASK_ZOMBIE) ? "ZOMBIE" : "TERMINATED");
    klog(2, buf);
}

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
        uint64_t now_ms = get_time_ms();

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

        sched_age_tasks(&core1_sched, kernel.core1.tasks, kernel.core1.task_count);

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
        if (task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED) {
            oom_callback_t handler = oom_get_handler(task->id);
            if (handler) {
                handler(task->oom_bytes_requested);
            }
            task->flags &= ~TASK_FLAG_OOM_CLEANUP_REQUESTED;
            task->oom_bytes_requested = 0;
        }

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
            task->sched_info.last_run = get_time_ms();
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

        static uint64_t last_total_time_c1 = 0;
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
    task_init_ipc_queue(task);
    task->original_priority = 0;

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

float k_get_core0_usage() {
    return kernel.cpu_usage;
}

float k_get_core1_usage() {
    return kernel.core1.cpu_usage;
}

uint32_t k_get_task_memory_api(uint32_t task_id) {
    return get_task_memory(task_id);
}

void k_reaper_task(void* arg) {
    task_sleep(REAPER_INTERVAL_MS);
    if (kernel.zombie_tasks == 0) {
        return;
    }
    klog(0, "REAPER: Cleaning up zombie tasks...");
    uint32_t reclaimed_bytes = 0;
    uint32_t zombies_cleaned = 0;

    for (uint32_t i = 1; i < kernel.task_count; i++) {
        disable_all_interrupts();
        TCB* task = &kernel.tasks[i];
        if (task->state == TASK_ZOMBIE) {
            enable_all_interrupts();
            
            // --- BEGIN PATCH ---
            // Memory is now freed by brutal_task_kill().
            // The reaper just cleans up the TCB state.
            /*
            uint32_t freed = 0;
            mutex_enter_blocking(&kernel.mem_lock);
            for (uint32_t b = 0; b < kernel.mem_block_count; b++) {
                if (kernel.mem_blocks[b].owner_id == task->id) {
                    freed += kernel.mem_blocks[b].size;
                    kernel.mem_blocks[b].free = true;
                    kernel.mem_blocks[b].owner_id = 0;
                }
            }
            mutex_exit(&kernel.mem_lock);

            if (freed > 0) {
                reclaimed_bytes += freed;
                mem_compact();
            }
            */
            // --- END PATCH ---

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
        // snprintf(buf, sizeof(buf), "REAPER: Cleaned %d zombies, reclaimed %dKB", (int)zombies_cleaned, (int)(reclaimed_bytes / 1024));
        snprintf(buf, sizeof(buf), "REAPER: Cleaned %d zombie TCBs", (int)zombies_cleaned); // Updated log message
        klog(0, buf);
    }
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
        kout.print("[CORE1] Task exit requested: "); kout.println(task->name);
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

    // Check every 5 seconds
    task_sleep(5000);

    if (kernel.cpu_usage > 95.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "CPUMON: High CPU Load! %.1f%%", kernel.cpu_usage);
        klog(2, buf);
    }
    
    if (kernel.core1_initialized && kernel.core1.cpu_usage > 95.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "CPUMON: High CPU Load (Core 1)! %.1f%%", kernel.core1.cpu_usage);
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
    
    // This is a more logical place for this check.
    if (kernel.temperature > 70.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TEMPMON: High Temperature! %.1f C", kernel.temperature);
        klog(2, buf);
    }
    
    task_sleep(2000);
}

void tempmon_deinit() {
    kout.println("[TEMPMON] DEINIT");
    kernel.tempmon_alive = false;
}

void vfs_task(void* arg) {
    // There is no background work (like flash saving) to implement here without more context.
    if (!kernel.vfs_alive) {
        task_sleep(10000);
        return;
    }
    task_sleep(5000);
}

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

    // Run every 30 seconds
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
