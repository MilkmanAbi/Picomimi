/**
 * PICOMIMI Scheduler Header - O(1) Preemptive Priority Bitmap
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - O(1) task selection using priority bitmaps
 * - Priority aging to prevent starvation
 * - Preemption support for high-priority tasks
 * - Real-time task support with separate queue
 * - Dual-core scheduling with work stealing
 * - CPU load tracking and idle injection
 */
#ifndef PICOMIMI_SCHEDULER_H
#define PICOMIMI_SCHEDULER_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"
#include "pico/sync.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// SCHEDULER CONFIGURATION
// ============================================================================

#define SCHED_AGING_INTERVAL_MS         300     // How often to check for aging
#define SCHED_STARVATION_THRESHOLD_MS   1000    // Boost priority after this wait
#define SCHED_IDLE_INJECTION_THRESHOLD  95.0f   // Inject idle when CPU > this %
#define SCHED_IDLE_INJECTION_INTERVAL   10      // Inject every N ticks when hot
#define SCHED_WORK_STEAL_THRESHOLD      3       // Steal when queue > N tasks

// ============================================================================
// PRIORITY BITMAP STRUCTURE (O(1) lookup)
// ============================================================================

/**
 * Priority bitmap for O(1) task selection
 * level_mask: Bitmap of priority levels that have runnable tasks
 * task_masks: For each priority level, bitmap of runnable task IDs
 */
typedef struct {
    uint32_t level_mask;                                    // Which priority levels have tasks
    uint32_t task_masks[PICOMIMI_SCHED_PRIORITY_LEVELS];    // Tasks at each priority level
} pm_priority_bitmap_t;

// ============================================================================
// PER-CORE SCHEDULER STRUCTURE
// ============================================================================

typedef struct {
    // O(1) Bitmaps
    pm_priority_bitmap_t runnable;      // Tasks ready to run
    pm_priority_bitmap_t waiting;       // Tasks waiting (blocked)
    pm_priority_bitmap_t rt_runnable;   // Real-time tasks (RP2350 enhancement)
    
    // Synchronization
    mutex_t lock;                       // Scheduler lock
    critical_section_t rt_lock;         // Fast RT lock
    
    // Timing and statistics
    uint32_t last_switch_ms;            // Last context switch time
    uint32_t total_runtime_ms;          // Total runtime
    uint32_t idle_time_ms;              // Total idle time
    uint32_t last_aging_ms;             // Last aging check
    uint32_t switches;                  // Context switch count
    uint32_t preemptions;               // Preemption count
    uint32_t idle_injections;           // Idle injection count
    uint32_t work_stolen;               // Work stealing count (RP2350)
    uint32_t rt_switches;               // RT task switches (RP2350)
    
    // CPU load tracking
    float cpu_load;                     // Smoothed CPU load (0-100)
    float cpu_load_instant;             // Instantaneous CPU load
    float cpu_load_peak;                // Peak CPU load (RP2350)
    
    // Current state
    pm_task_id_t current_task;          // Currently running task
    pm_task_id_t idle_task;             // Idle task ID for this core
    uint8_t current_priority;           // Priority of current task
    uint8_t rt_task_count;              // RT task count (RP2350)
    
    // Flags
    bool idle_injection_active;         // Idle injection in progress
    bool work_steal_enabled;            // Work stealing enabled (RP2350)
    bool initialized;                   // Core scheduler initialized
} pm_core_scheduler_t;

// ============================================================================
// BITMAP OPERATIONS (inline for speed)
// ============================================================================

/**
 * Find first set bit (1-indexed, 0 if none)
 */
static inline int pm_bitmap_ffs(uint32_t bitmap) {
    if (bitmap == 0) return 0;
    return __builtin_ffs(bitmap);
}

/**
 * Find last set bit (highest priority level with tasks)
 */
static inline int pm_bitmap_fls(uint32_t bitmap) {
    if (bitmap == 0) return 0;
    return 32 - __builtin_clz(bitmap);
}

/**
 * Set a bit in bitmap
 */
static inline void pm_bitmap_set(uint32_t* bitmap, uint32_t bit) {
    if (bit < 32) *bitmap |= (1U << bit);
}

/**
 * Clear a bit in bitmap
 */
static inline void pm_bitmap_clear(uint32_t* bitmap, uint32_t bit) {
    if (bit < 32) *bitmap &= ~(1U << bit);
}

/**
 * Test if a bit is set
 */
static inline bool pm_bitmap_test(uint32_t bitmap, uint32_t bit) {
    return (bit < 32) && (bitmap & (1U << bit));
}

// ============================================================================
// SCHEDULER API
// ============================================================================

// Initialization
pm_result_t pm_scheduler_init(void);
pm_result_t pm_scheduler_init_core(uint8_t core);
void pm_scheduler_start(void);
void pm_scheduler_stop(void);

// Task management
pm_result_t pm_scheduler_ready(pm_task_id_t task_id, uint8_t priority);
pm_result_t pm_scheduler_remove(pm_task_id_t task_id);
pm_result_t pm_scheduler_set_priority(pm_task_id_t task_id, uint8_t new_priority);

// Task selection (O(1))
pm_task_id_t pm_scheduler_select_next(uint8_t core);
pm_task_id_t pm_scheduler_select_next_core0(void);
pm_task_id_t pm_scheduler_select_next_core1(void);

// Scheduler events
void pm_scheduler_tick(void);
void pm_scheduler_yield(void);
void pm_scheduler_block(pm_task_id_t task_id);
void pm_scheduler_unblock(pm_task_id_t task_id);

// Preemption
void pm_scheduler_check_preemption(void);
bool pm_scheduler_is_preemption_pending(void);
void pm_scheduler_handle_preemption(void);

// Priority aging
void pm_scheduler_age_tasks(uint8_t core);
void pm_scheduler_update_task_priority(pm_task_id_t task_id);

// CPU load management
void pm_scheduler_update_load(uint8_t core, uint64_t idle_time_us, uint64_t total_time_us);
float pm_scheduler_get_cpu_load(uint8_t core);
float pm_scheduler_get_instant_load(uint8_t core);
bool pm_scheduler_should_inject_idle(uint8_t core);

// Work stealing (RP2350 dual-core)
bool pm_scheduler_work_steal(uint8_t from_core, uint8_t to_core);
void pm_scheduler_enable_work_steal(bool enable);

// Statistics
uint32_t pm_scheduler_get_context_switches(uint8_t core);
uint32_t pm_scheduler_get_preemptions(uint8_t core);
uint32_t pm_scheduler_get_idle_injections(uint8_t core);
pm_task_id_t pm_scheduler_get_current_task(uint8_t core);

// Bitmap operations (exposed for debugging)
void pm_sched_bitmap_add(pm_priority_bitmap_t* bm, pm_task_id_t task_id, uint8_t priority);
void pm_sched_bitmap_remove(pm_priority_bitmap_t* bm, pm_task_id_t task_id, uint8_t priority);
int pm_sched_bitmap_find_highest(pm_priority_bitmap_t* bm, pm_task_id_t* task_id_out);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static inline bool pm_is_realtime_priority(uint8_t priority) {
    return priority >= PICOMIMI_SCHED_RT_THRESHOLD;
}

static inline bool pm_is_idle_task(pm_task_id_t task_id) {
    return task_id == 0;
}

// ============================================================================
// GLOBAL SCHEDULER INSTANCES
// ============================================================================

extern pm_core_scheduler_t g_core0_sched;
#if PICOMIMI_DUAL_CORE
extern pm_core_scheduler_t g_core1_sched;
#endif

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_SCHEDULER_H
