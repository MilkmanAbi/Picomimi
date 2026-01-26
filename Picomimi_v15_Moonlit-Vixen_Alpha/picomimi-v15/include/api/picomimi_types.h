/**
 * PICOMIMI-AXISOS v15.0.0-Alpha Type Definitions
 * Complete port from v14.3.1 "Quiet Otter"
 * 
 * ALL structures from the original 12,000 line codebase.
 */
#ifndef PICOMIMI_TYPES_H
#define PICOMIMI_TYPES_H

#include "config/picomimi_config.h"
#include "pico/mutex.h"
#include "pico/critical_section.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// ENUMERATIONS
// ============================================================================

// Task states
typedef enum {
    TASK_STATE_READY = 0,
    TASK_STATE_RUNNING = 1,
    TASK_STATE_WAITING = 2,
    TASK_STATE_SUSPENDED = 3,
    TASK_STATE_TERMINATED = 4,
    TASK_STATE_ZOMBIE = 5
} pm_task_state_t;

// Core affinity
typedef enum {
    CORE_AFFINITY_ANY = 0,
    CORE_AFFINITY_CORE0 = 1,
    CORE_AFFINITY_CORE1 = 2
} pm_core_affinity_t;

// CPU profiles
typedef enum {
    CPU_PROFILE_ULTRA_LOW = 0,
    CPU_PROFILE_POWERSAVE = 1,
    CPU_PROFILE_BALANCED = 2,
    CPU_PROFILE_PERFORMANCE = 3,
    CPU_PROFILE_TURBO = 4,
    CPU_PROFILE_COUNT = 5
} pm_cpu_profile_t;

// Governor modes
typedef enum {
    GOV_MODE_DISABLED = 0,
    GOV_MODE_MANUAL = 1,
    GOV_MODE_ONDEMAND = 2,
    GOV_MODE_PERFORMANCE = 3,
    GOV_MODE_POWERSAVE = 4
} pm_governor_mode_t;

// Memory pressure levels
typedef enum {
    MEM_PRESSURE_NONE = 0,
    MEM_PRESSURE_LOW = 1,
    MEM_PRESSURE_MODERATE = 2,
    MEM_PRESSURE_HIGH = 3,
    MEM_PRESSURE_CRITICAL = 4,
    MEM_PRESSURE_EMERGENCY = 5
} pm_mem_pressure_t;

// Allocation failure reasons
typedef enum {
    ALLOC_FAIL_NONE = 0,
    ALLOC_FAIL_NO_MEMORY,
    ALLOC_FAIL_FRAGMENTED,
    ALLOC_FAIL_TASK_BLOCKED,
    ALLOC_FAIL_TASK_LIMIT,
    ALLOC_FAIL_VELOCITY_THROTTLE,
    ALLOC_FAIL_EMERGENCY_RESERVE,
    ALLOC_FAIL_OOM_KILL_PENDING,
    ALLOC_FAIL_THERMAL_THROTTLE
} pm_alloc_fail_t;

// Allocation flags
typedef enum {
    ALLOC_NORMAL = 0x00,
    ALLOC_ZERO = 0x01,
    ALLOC_DMA = 0x02,
    ALLOC_URGENT = 0x04,
    ALLOC_PINNED = 0x08,
    ALLOC_CACHE_ALIGN = 0x10
} pm_alloc_flags_t;

// IPC message types
typedef enum {
    IPC_NONE = 0,
    IPC_RENDER_FRAME,
    IPC_PROCESS_INPUT,
    IPC_COMPUTE_DATA,
    IPC_AUDIO_SAMPLE,
    IPC_USER_DEFINED
} pm_ipc_msg_type_t;

// Resource types
typedef enum {
    RES_TYPE_GPIO = 0,
    RES_TYPE_SPI,
    RES_TYPE_I2C,
    RES_TYPE_ADC,
    RES_TYPE_PWM,
    RES_TYPE_PIO,
    RES_TYPE_UART,
    RES_TYPE_DMA,
    RES_TYPE_TIMER,
    RES_TYPE_COUNT
} pm_resource_type_t;

// Resource state
typedef enum {
    RES_STATE_FREE = 0,
    RES_STATE_CLAIMED,
    RES_STATE_SHARED,
    RES_STATE_KERNEL_RESERVED
} pm_resource_state_t;

// Resource mode
typedef enum {
    RES_MODE_EXCLUSIVE = 0,
    RES_MODE_SHARED_READ,
    RES_MODE_KERNEL_ONLY
} pm_resource_mode_t;

// GPIO direction
typedef enum {
    GPIO_DIR_INPUT = 0,
    GPIO_DIR_OUTPUT = 1
} pm_gpio_dir_t;

// GPIO drive strength
typedef enum {
    GPIO_DRIVE_2MA = 0,
    GPIO_DRIVE_4MA = 1,
    GPIO_DRIVE_8MA = 2,
    GPIO_DRIVE_12MA = 3
} pm_gpio_drive_t;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

typedef struct pm_tcb pm_tcb_t;
typedef struct pm_module_callbacks pm_module_callbacks_t;
typedef struct pm_task_wait_node pm_task_wait_node_t;

// ============================================================================
// RESOURCE MANAGER STRUCTURES
// ============================================================================

typedef struct {
    uint32_t owner_task_id;
    uint32_t claim_time_ms;
    uint32_t last_access_ms;
    uint32_t access_count;
    uint16_t handle;
    uint16_t share_count;
    uint8_t type;
    uint8_t id;
    uint8_t state;
    uint8_t mode;
    uint8_t sub_id;
    uint8_t config_flags;
    bool configured;
    bool in_transaction;
} PICOMIMI_PACKED pm_resource_desc_t;

typedef struct {
    uint8_t direction;
    uint8_t drive;
    uint8_t function;
    bool output_state;
    bool has_interrupt;
    uint8_t irq_edge;
} PICOMIMI_PACKED pm_gpio_info_t;

typedef struct {
    uint32_t frequency;
    uint8_t mode;
    uint8_t bit_order;
    uint8_t cs_pin;
    bool cs_active_low;
} PICOMIMI_PACKED pm_spi_info_t;

typedef struct {
    uint32_t frequency;
    uint8_t device_addr;
    bool is_master;
} PICOMIMI_PACKED pm_i2c_info_t;

typedef struct {
    uint32_t frequency;
    uint16_t duty_cycle;
    uint8_t slice;
    uint8_t channel;
    bool enabled;
} PICOMIMI_PACKED pm_pwm_info_t;

typedef struct {
    uint8_t pio_num;
    uint8_t sm_num;
    uint8_t program_offset;
    uint8_t program_size;
    bool running;
} PICOMIMI_PACKED pm_pio_info_t;

typedef struct {
    uint32_t task_id;
    uint32_t timestamp_ms;
    uint16_t resource_type;
    uint16_t resource_id;
    uint32_t violation_count;
    bool warned;
} PICOMIMI_PACKED pm_resource_violation_t;

typedef struct {
    uint32_t owner_task_id;
    uint32_t start_time_ms;
    uint32_t timeout_ms;
    pm_res_handle_t handles[8];
    uint8_t handle_count;
    bool active;
} PICOMIMI_PACKED pm_resource_transaction_t;

// ============================================================================
// IPC STRUCTURES
// ============================================================================

typedef struct {
    uint32_t sender_id;
    uint32_t target_id;
    uint32_t timestamp_ms;
    uint16_t sequence;
    uint16_t next;
    pm_ipc_msg_type_t type;
    uint8_t priority;
    uint8_t flags;
    uint8_t _reserved;
    uint8_t data[PICOMIMI_IPC_MSG_SIZE];
    bool in_use;
    uint8_t _pad[3];
} pm_ipc_message_t;

typedef struct {
    pm_ipc_message_t message_pool[PICOMIMI_MAX_IPC_MESSAGES];
    uint16_t free_list[PICOMIMI_MAX_IPC_MESSAGES];
    int16_t free_list_head;
    uint16_t sequence_counter;
    uint32_t dropped_messages;
    uint32_t total_sent;
    uint32_t total_received;
    uint32_t rt_messages;
    mutex_t lock;
    critical_section_t rt_section;
} pm_ipc_manager_t;

typedef struct {
    uint32_t priority_bitmap;
    uint16_t priority_lists_head[PICOMIMI_SCHED_PRIORITY_LEVELS];
    uint16_t message_count;
    uint16_t rt_message_count;
} pm_task_ipc_queue_t;

typedef struct {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t rt_messages_sent;
    uint32_t rt_messages_received;
    uint16_t messages_dropped_pool_full;
    uint16_t messages_dropped_task_full;
    uint16_t broadcasts_sent;
    uint16_t max_queue_depth_global;
    float avg_queue_depth_global;
    float avg_latency_us;
} pm_ipc_stats_t;

// ============================================================================
// SYNCHRONIZATION PRIMITIVES
// ============================================================================

struct pm_task_wait_node {
    uint32_t task_id;
    pm_task_wait_node_t* next;
    uint32_t wait_flags;
    uint8_t wait_mode;
    bool clear_on_exit;
};

typedef struct {
    bool locked;
    uint32_t owner_id;
    uint8_t original_priority;
    pm_task_wait_node_t* wait_list_head;
} pm_kmutex_t;

typedef struct {
    int32_t count;
    uint32_t max_count;
    pm_task_wait_node_t* wait_list_head;
} pm_ksemaphore_t;

typedef struct {
    uint32_t flags;
    pm_task_wait_node_t* wait_list_head;
} pm_kevent_t;

// ============================================================================
// MEMORY MANAGEMENT STRUCTURES
// ============================================================================

typedef struct {
    void* addr;
    uint32_t size;
    uint32_t owner_id;
    uint16_t alloc_seq;
    uint8_t size_class;
    bool free;
    bool pinned;
    bool dma_safe;
    uint8_t _pad[2];
} pm_mem_block_t;

typedef struct {
    pm_mem_block_t* head;
    uint32_t count;
    uint32_t total_size;
} pm_size_class_list_t;

typedef struct {
    uint8_t pool[PICOMIMI_MEM_SMALL_POOL_SIZE];
    uint32_t bitmap[(PICOMIMI_MEM_SMALL_POOL_SIZE / 32) / 32];
    uint32_t used;
    uint32_t alloc_count;
    mutex_t lock;
} pm_small_alloc_pool_t;

typedef struct {
    uint32_t total_allocs;
    uint32_t total_frees;
    uint32_t failed_allocs;
    uint32_t oom_events;
    uint32_t oom_kills;
    uint32_t emergency_compactions;
    uint32_t velocity_throttles;
    uint32_t peak_usage;
    uint32_t last_defrag_ms;
    uint32_t oom_velocity;
    uint32_t last_oom_time_ms;
    uint32_t small_allocs;
    uint32_t large_allocs;
    uint32_t cache_hits;
    uint16_t active_blocks;
    uint16_t corruptions_detected;
    uint8_t current_pressure;
    uint8_t fragmentation_pct;
    uint8_t size_class_usage[PICOMIMI_MEM_SIZE_CLASS_COUNT];
} pm_mem_stats_t;

// ============================================================================
// OOM STRUCTURES
// ============================================================================

typedef struct {
    uint32_t events[8];
    uint32_t window_start_ms;
    uint8_t event_count;
    uint8_t head;
} pm_oom_velocity_t;

typedef struct {
    uint32_t task_id;
    pm_oom_callback_t callback;
    bool active;
} pm_oom_handler_t;

typedef struct {
    uint32_t allocating_task_id;
    uint32_t target_task_id;
    uint64_t request_time;
    uint32_t bytes_requested;
    bool request_sent;
    bool task_complied;
} pm_oom_request_t;

typedef struct {
    uint32_t total_bytes_reclaimed;
    uint16_t requests_sent;
    uint16_t voluntary_releases;
    uint16_t forced_kills;
    uint16_t prevention_count;
    uint16_t abusive_kills;
} pm_oom_stats_t;

typedef struct {
    uint32_t task_id;
    uint32_t memory_used;
    int16_t score;
    uint8_t oom_priority;
    bool has_handler;
} pm_oom_victim_t;

// ============================================================================
// SCHEDULER STRUCTURES
// ============================================================================

typedef struct {
    uint32_t quantum_us;
    uint32_t last_run;
    uint32_t total_runtime_ms;
    uint32_t deadline_us;
    uint16_t voluntary_yields;
    uint16_t preemptions;
    float cpu_usage_percent;
    uint8_t effective_priority;
    uint8_t base_priority;
    uint8_t cpu_affinity;
    uint8_t age;
    uint8_t cpu_burst_counter;
    uint8_t cpu_sample_index;
    uint8_t preferred_core;
    bool is_realtime;
    float cpu_samples[5];  // CPU_ABUSE_SAMPLE_COUNT
} pm_task_sched_info_t;

typedef struct {
    uint32_t level_mask;
    uint32_t task_masks[PICOMIMI_SCHED_PRIORITY_LEVELS];
} pm_priority_bitmap_t;

typedef struct {
    pm_priority_bitmap_t runnable;
    pm_priority_bitmap_t waiting;
    pm_priority_bitmap_t rt_runnable;
    mutex_t lock;
    critical_section_t rt_lock;
    uint32_t last_switch;
    uint32_t total_runtime_ms;
    uint32_t idle_time_ms;
    uint32_t switches;
    uint32_t last_aging;
    uint32_t work_stolen;
    uint32_t rt_switches;
    float cpu_load;
    float cpu_load_instant;
    float cpu_load_peak;
    uint16_t preemptions;
    uint16_t idle_injections;
    uint8_t current_task;
    uint8_t idle_task;
    uint8_t current_priority;
    uint8_t rt_task_count;
    bool idle_injection_active;
    bool work_steal_enabled;
} pm_core_scheduler_t;

// ============================================================================
// MODULE/APP CALLBACKS
// ============================================================================

struct pm_module_callbacks {
    void (*init)(pm_task_id_t id);
    void (*tick)(void* arg);
    void (*deinit)(void);
    void (*suspend)(void);
    void (*resume)(void);
    void (*on_message)(pm_ipc_message_t* msg);
};

typedef struct {
    char name[PICOMIMI_TASK_NAME_LEN];
    void (*spawn_func)(void);
} pm_app_entry_t;

typedef struct {
    char name[PICOMIMI_TASK_NAME_LEN];
    pm_module_callbacks_t callbacks;
    uint8_t priority;
    bool auto_start;
} pm_driver_entry_t;

typedef struct {
    char name[PICOMIMI_TASK_NAME_LEN];
    pm_module_callbacks_t callbacks;
    uint8_t priority;
    uint32_t mem_limit_kb;
    bool auto_start;
} pm_service_entry_t;

// ============================================================================
// TASK CONTROL BLOCK (TCB)
// ============================================================================

struct pm_tcb {
    uint32_t id;
    pm_task_entry_t entry;
    void* arg;
    pm_module_callbacks_t* callbacks;
    uint32_t flags;
    uint32_t wake_time_ms;
    uint32_t mem_used;
    uint32_t mem_peak;
    uint32_t mem_limit;
    uint32_t start_time_ms;
    uint32_t max_runtime;
    uint32_t last_respawn;
    uint32_t cpu_time_ms;
    uint32_t last_run;
    uint32_t oom_bytes_requested;
    uint32_t alloc_velocity;
    uint32_t last_alloc_time;
    uint32_t mem_throttle_mark;
    uint32_t total_cpu_time_ms;
    uint32_t stack_high_water;
    
    uint64_t wall_time_us;
    uint64_t scheduled_at_us;
    
    const char* description;
    pm_task_ipc_queue_t ipc;
    pm_task_sched_info_t sched_info;
    pm_task_wait_node_t wait_node;
    char name[PICOMIMI_TASK_NAME_LEN];
    pm_task_state_t state;
    uint8_t task_type;
    uint8_t oom_priority;
    uint8_t priority;
    pm_core_affinity_t affinity;
    uint8_t original_priority;
    uint8_t running_on_core;
    uint8_t cpu_profile_hint;
    uint16_t respawn_count;
    uint16_t page_faults;
    uint16_t context_switches;
    bool mem_blocked;
    bool is_cpu_abuser;
    bool is_rt_task;
    bool preempt_pending;
    uint8_t _pad[1];
};

// ============================================================================
// LOGGING
// ============================================================================

typedef struct {
    uint32_t timestamp;
    char message[48];
    uint8_t level;
    uint8_t core_id;
    uint8_t _pad[2];
} pm_log_entry_t;

// ============================================================================
// FILESYSTEM
// ============================================================================

typedef struct {
    void* handle;  // SD file handle
    char path[PICOMIMI_PMFS_MAX_FILENAME];
    uint32_t owner_task_id;
    uint32_t bytes_read;
    uint32_t bytes_written;
    bool open;
    bool write_mode;
    bool cached;
    uint8_t _pad;
} pm_fs_file_t;

typedef struct {
    uint8_t card_type;
    uint64_t card_size;
    uint32_t sector_count;
    uint16_t sector_size;
    bool valid;
    bool high_speed;
} pm_sd_info_t;

// ============================================================================
// CPU GOVERNOR STATE
// ============================================================================

typedef struct {
    pm_cpu_profile_t current_profile;
    pm_cpu_profile_t requested_profile;
    pm_cpu_profile_t pre_throttle_profile;
    pm_governor_mode_t mode;
    pm_cpu_profile_t min_profile;
    pm_cpu_profile_t max_profile;
    uint32_t current_freq_khz;
    uint32_t target_freq_khz;
    uint32_t last_check_ms;
    uint32_t check_interval_ms;
    uint32_t transition_count;
    uint32_t throttle_count;
    uint32_t turbo_start_ms;
    uint32_t total_turbo_time_ms;
    uint32_t last_idle_ms;
    uint32_t wfi_total_us;
    float temperature;
    float temperature_peak;
    uint8_t cpu_load;
    uint8_t cpu_load_avg;
    bool enabled;
    bool turbo_active;
    bool turbo_available;
    bool thermal_throttled;
    bool user_override;
    bool wfi_enabled;
    bool in_wfi;
    bool instant_turbo_pending;
    bool input_boost_active;
    uint32_t input_boost_start_ms;
    uint8_t load_history[16];
    uint8_t load_idx;
} pm_governor_state_t;

typedef struct {
    uint32_t transitions;
    uint32_t thermal_throttles;
    float avg_temperature;
    uint8_t avg_load;
} pm_governor_stats_t;

// ============================================================================
// CORE 1 STATE
// ============================================================================

typedef struct {
    pm_tcb_t tasks[PICOMIMI_MAX_CORE1_TASKS];
    mutex_t scheduler_lock;
    critical_section_t rt_section;
    uint32_t uptime_ms;
    uint32_t context_switches;
    uint32_t work_stolen;
    float cpu_usage;
    float cpu_usage_peak;
    uint8_t task_count;
    uint8_t current_task;
    bool running;
    bool rt_mode_active;
} pm_core1_state_t;

// ============================================================================
// PANIC INFO
// ============================================================================

typedef struct {
    const char* reason;
    uint32_t pc;
    uint32_t lr;
    uint32_t sp;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t timestamp_ms;
    pm_task_id_t task_id;
    bool is_core1;
    uint8_t fault_type;
} pm_panic_info_t;

// ============================================================================
// WATCHDOG STATE
// ============================================================================

typedef struct {
    uint32_t last_feed;
    uint32_t last_task_check;
    uint16_t timeout_ms;
    uint8_t triggers;
    bool enabled;
    bool in_panic;
    bool task_watchdog_enabled;
} pm_watchdog_state_t;

// ============================================================================
// KERNEL STATE - THE MAIN STATE STRUCTURE
// ============================================================================

typedef struct {
    // Tasks
    pm_tcb_t tasks[PICOMIMI_MAX_TASKS];
    
    // Memory management
    pm_mem_block_t mem_blocks[PICOMIMI_MAX_MEMORY_BLOCKS];
    pm_size_class_list_t size_class_lists[PICOMIMI_MEM_SIZE_CLASS_COUNT];
    mutex_t mem_lock;
    critical_section_t mem_fast_lock;
    pm_mem_stats_t mem_stats;
    
    // Logging
    pm_log_entry_t log[PICOMIMI_MAX_LOG_ENTRIES];
    mutex_t log_lock;
    uint16_t log_head;
    uint16_t log_tail;
    uint16_t log_count;
    uint8_t log_level;
    
    // Filesystem
    pm_fs_file_t fs_open_files[PICOMIMI_PMFS_MAX_OPEN_FILES];
    mutex_t fs_lock;
    pm_sd_info_t sd_info;
    
    // Core 1
    pm_core1_state_t core1;
    
    // IPC
    pm_ipc_manager_t ipc_manager;
    
    // CPU Governor
    pm_governor_state_t governor;
    
    // Synchronization primitives
    pm_kmutex_t kernel_mutexes[PICOMIMI_MAX_KERNEL_MUTEXES];
    pm_ksemaphore_t kernel_semaphores[PICOMIMI_MAX_SEMAPHORES];
    pm_kevent_t kernel_event_flags[PICOMIMI_MAX_EVENT_FLAGS];
    
    // Resource manager - GPIO
    pm_resource_desc_t gpio_resources[PICOMIMI_RES_GPIO_COUNT];
    pm_gpio_info_t gpio_info[PICOMIMI_RES_GPIO_COUNT];
    
    // Resource manager - SPI
    pm_resource_desc_t spi_resources[PICOMIMI_RES_SPI_COUNT];
    pm_spi_info_t spi_info[PICOMIMI_RES_SPI_COUNT];
    
    // Resource manager - I2C
    pm_resource_desc_t i2c_resources[PICOMIMI_RES_I2C_COUNT * 16];
    pm_i2c_info_t i2c_info[PICOMIMI_RES_I2C_COUNT * 16];
    
    // Resource manager - ADC
    pm_resource_desc_t adc_resources[PICOMIMI_RES_ADC_COUNT];
    
    // Resource manager - PWM
    pm_resource_desc_t pwm_resources[PICOMIMI_RES_PWM_CHANNEL_COUNT];
    pm_pwm_info_t pwm_info[PICOMIMI_RES_PWM_CHANNEL_COUNT];
    
    // Resource manager - PIO
    pm_resource_desc_t pio_resources[PICOMIMI_RES_PIO_COUNT * PICOMIMI_RES_PIO_SM_COUNT];
    pm_pio_info_t pio_info[PICOMIMI_RES_PIO_COUNT * PICOMIMI_RES_PIO_SM_COUNT];
    
    // Resource manager - UART
    pm_resource_desc_t uart_resources[PICOMIMI_RES_UART_COUNT];
    
    // Resource manager - DMA
    pm_resource_desc_t dma_resources[PICOMIMI_RES_DMA_COUNT];
    
    // Resource manager - Timer
    pm_resource_desc_t timer_resources[PICOMIMI_RES_TIMER_ALARM_COUNT];
    
    // Violation tracking
    pm_resource_violation_t violations[PICOMIMI_MAX_RESOURCE_VIOLATIONS];
    uint16_t violation_head;
    uint16_t violation_count;
    uint32_t total_violations;
    
    // Transaction tracking
    pm_resource_transaction_t transactions[PICOMIMI_MAX_TRANSACTIONS];
    
    // Resource manager statistics
    uint32_t res_total_claims;
    uint32_t res_total_releases;
    uint32_t res_total_conflicts;
    uint32_t res_total_auto_releases;
    uint16_t next_handle_id;
    mutex_t res_lock;
    bool res_manager_initialized;
    
    // Kernel state
    uint64_t boot_time_us;
    uint32_t uptime_ms;
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t alloc_sequence;
    uint32_t largest_free_block;
    uint32_t free_memory;
    uint32_t used_memory;
    uint32_t total_context_switches;
    uint32_t fs_used_bytes;
    uint32_t fs_reads;
    uint32_t fs_writes;
    uint32_t fs_log_counter;
    uint32_t last_velocity_check_ms;
    
    float cpu_usage;
    float temperature;
    
    void (*app_write_char)(char);
    int32_t gui_focus_task_id;
    
    uint16_t task_count;
    uint16_t mem_block_count;
    uint16_t oom_kills;
    
    uint8_t current_task;
    uint8_t fragmentation_pct;
    uint8_t kernel_tasks;
    uint8_t driver_tasks;
    uint8_t service_tasks;
    uint8_t module_tasks;
    uint8_t application_tasks;
    uint8_t zombie_tasks;
    uint8_t rt_task_count;
    
    bool initialized;
    bool running;
    bool panic_mode;
    volatile bool preemption_pending;
    bool shell_alive;
    bool cpumon_alive;
    bool tempmon_alive;
    bool fs_alive;
    bool root_mode;
    bool fs_available;
    bool fs_mounted;
    bool core1_initialized;
    bool turbo_enabled;
    bool thermal_throttled;
    
    // USB Serial Stability (v14.1.1 FIX)
    uint32_t usb_last_activity_ms;
    uint32_t usb_last_poll_ms;
    uint32_t usb_bytes_rx;
    uint32_t usb_bytes_tx;
    uint32_t usb_lockup_recoveries;
    bool usb_connected;
    bool usb_was_connected;
    bool usb_blocking_lowpower;
    
    // Small allocation pools
    pm_small_alloc_pool_t small_pool_core0;
    pm_small_alloc_pool_t small_pool_core1;
    
    // Heap (must be last)
    uint8_t heap[PICOMIMI_HEAP_SIZE];
} pm_kernel_state_t;

// ============================================================================
// SHELL COMMAND TYPES
// ============================================================================

typedef void (*pm_shell_cmd_handler_t)(const char* args);

typedef struct {
    const char* name;
    pm_shell_cmd_handler_t handler;
    const char* help;
} pm_shell_cmd_t;

#endif // PICOMIMI_TYPES_H
