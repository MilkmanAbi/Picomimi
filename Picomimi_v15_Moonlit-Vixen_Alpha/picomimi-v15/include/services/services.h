/**
 * PICOMIMI System Services Header
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Includes:
 * - Watchdog management
 * - System tasks (idle, reaper, monitors)
 * - Timer resources
 * - System utilities
 */
#ifndef PICOMIMI_SERVICES_H
#define PICOMIMI_SERVICES_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// WATCHDOG CONFIGURATION
// ============================================================================

#define PM_WATCHDOG_TIMEOUT_MS      8000    // Hardware watchdog timeout
#define PM_TASK_WATCHDOG_TIMEOUT_MS 5000    // Per-task watchdog timeout

// ============================================================================
// WATCHDOG STATE
// ============================================================================

typedef struct {
    uint32_t last_feed_ms;
    uint32_t timeout_ms;
    uint32_t feed_count;
    bool enabled;
    bool hw_enabled;
    bool task_watchdog_enabled;
    uint32_t task_watchdog_violations;
} pm_watchdog_state_t;

// ============================================================================
// WATCHDOG API
// ============================================================================

/**
 * Initialize the watchdog system
 */
pm_result_t pm_watchdog_init(void);

/**
 * Enable/disable the hardware watchdog
 */
void pm_watchdog_enable(bool enable);

/**
 * Feed the watchdog (reset timer)
 */
void pm_watchdog_feed(void);

/**
 * Check watchdog status and task timeouts
 */
void pm_watchdog_check(void);

/**
 * Get watchdog state
 */
const pm_watchdog_state_t* pm_watchdog_get_state(void);

/**
 * Enable per-task watchdog monitoring
 */
void pm_watchdog_enable_task_monitor(bool enable);

/**
 * Update task's last activity time
 */
void pm_watchdog_task_activity(pm_task_id_t task_id);

// ============================================================================
// SYSTEM TASK IDENTIFIERS
// ============================================================================

typedef enum {
    PM_SYSTASK_IDLE = 0,
    PM_SYSTASK_REAPER,
    PM_SYSTASK_SHELL,
    PM_SYSTASK_CPU_MONITOR,
    PM_SYSTASK_TEMP_MONITOR,
    PM_SYSTASK_INPUT,
    PM_SYSTASK_FS,
    PM_SYSTASK_COUNT
} pm_systask_t;

// ============================================================================
// SYSTEM TASK FUNCTIONS
// ============================================================================

/**
 * Initialize all system tasks
 */
pm_result_t pm_systasks_init(void);

/**
 * System task entry points
 */
void pm_idle_task(void* arg);
void pm_reaper_task(void* arg);
void pm_shell_task(void* arg);
void pm_cpu_monitor_task(void* arg);
void pm_temp_monitor_task(void* arg);
void pm_input_task(void* arg);
void pm_fs_task(void* arg);

/**
 * Get system task ID
 */
pm_task_id_t pm_systask_get_id(pm_systask_t task);

/**
 * Check if a task is a system task
 */
bool pm_is_system_task(pm_task_id_t task_id);

// ============================================================================
// TIMER RESOURCES
// ============================================================================

#define PM_MAX_TIMERS           8
#define PM_TIMER_ONESHOT        0
#define PM_TIMER_PERIODIC       1

typedef void (*pm_timer_callback_t)(uint32_t timer_id, void* user_data);

typedef struct {
    bool active;
    bool periodic;
    uint32_t period_ms;
    uint32_t next_fire_ms;
    pm_timer_callback_t callback;
    void* user_data;
    pm_task_id_t owner;
} pm_timer_t;

/**
 * Initialize timer system
 */
pm_result_t pm_timer_init(void);

/**
 * Create a timer
 * @param period_ms Timer period in milliseconds
 * @param callback Function to call when timer fires
 * @param user_data User data passed to callback
 * @param periodic True for periodic timer, false for one-shot
 * @return Timer ID or -1 on error
 */
int32_t pm_timer_create(uint32_t period_ms, pm_timer_callback_t callback,
                        void* user_data, bool periodic);

/**
 * Start a timer
 */
pm_result_t pm_timer_start(uint32_t timer_id);

/**
 * Stop a timer
 */
pm_result_t pm_timer_stop(uint32_t timer_id);

/**
 * Delete a timer
 */
pm_result_t pm_timer_delete(uint32_t timer_id);

/**
 * Process timer ticks (called from kernel tick)
 */
void pm_timer_tick(void);

/**
 * Get remaining time until timer fires
 */
uint32_t pm_timer_remaining(uint32_t timer_id);

// ============================================================================
// SYSTEM STATISTICS
// ============================================================================

typedef struct {
    // Uptime
    uint32_t uptime_seconds;
    uint64_t uptime_ms;
    
    // Task statistics
    uint32_t total_tasks_created;
    uint32_t total_tasks_terminated;
    uint32_t context_switches;
    uint32_t preemptions;
    
    // Memory statistics
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t peak_memory_usage;
    uint32_t oom_events;
    
    // IPC statistics
    uint32_t ipc_messages_sent;
    uint32_t ipc_messages_received;
    
    // CPU statistics
    float cpu_usage_avg;
    float cpu_usage_peak;
    float temperature_avg;
    float temperature_peak;
    
    // Error statistics
    uint32_t watchdog_resets;
    uint32_t kernel_panics;
    uint32_t stack_overflows;
} pm_system_stats_t;

/**
 * Get system statistics
 */
const pm_system_stats_t* pm_get_system_stats(void);

/**
 * Update system statistics (called periodically)
 */
void pm_update_system_stats(void);

/**
 * Reset system statistics
 */
void pm_reset_system_stats(void);

/**
 * Print system statistics
 */
void pm_print_system_stats(void);

// ============================================================================
// KERNEL PANIC
// ============================================================================

/**
 * Trigger a kernel panic
 * @param reason Reason for the panic
 */
__attribute__((noreturn)) void pm_kernel_panic(const char* reason);

/**
 * Register a panic handler
 */
typedef void (*pm_panic_handler_t)(const char* reason);
void pm_register_panic_handler(pm_panic_handler_t handler);

// ============================================================================
// SYSTEM MAINTENANCE
// ============================================================================

/**
 * Perform periodic system maintenance
 * Called from kernel tick handler
 */
void pm_system_maintenance(void);

/**
 * Request system shutdown
 */
void pm_system_shutdown(void);

/**
 * Request system reboot
 */
void pm_system_reboot(void);

/**
 * Check if shutdown is requested
 */
bool pm_is_shutdown_requested(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_SERVICES_H
