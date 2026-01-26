/**
 * PICOMIMI System Services Implementation
 * Ported from v14.3.1 "Quiet Otter"
 */

#include "services/services.h"
#include "api/picomimi_kernel.h"
#include "kernel/scheduler.h"
#include "memory/memory.h"
#include "ipc/ipc.h"
#include "power/governor.h"
#include "shell/shell.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/resets.h"
#include <string.h>
#include <stdio.h>

// External kernel state
extern pm_kernel_state_t g_kernel;
extern pm_core_scheduler_t g_core0_sched;

// ============================================================================
// WATCHDOG STATE
// ============================================================================

static pm_watchdog_state_t g_watchdog = {
    .last_feed_ms = 0,
    .timeout_ms = PM_WATCHDOG_TIMEOUT_MS,
    .feed_count = 0,
    .enabled = false,
    .hw_enabled = false,
    .task_watchdog_enabled = false,
    .task_watchdog_violations = 0
};

// ============================================================================
// SYSTEM TASK IDS
// ============================================================================

static pm_task_id_t g_systask_ids[PM_SYSTASK_COUNT] = {
    PM_INVALID_TASK, PM_INVALID_TASK, PM_INVALID_TASK, PM_INVALID_TASK,
    PM_INVALID_TASK, PM_INVALID_TASK, PM_INVALID_TASK
};

// ============================================================================
// TIMER STATE
// ============================================================================

static pm_timer_t g_timers[PM_MAX_TIMERS];
static mutex_t g_timer_lock;

// ============================================================================
// SYSTEM STATISTICS
// ============================================================================

static pm_system_stats_t g_system_stats;

// ============================================================================
// PANIC HANDLER
// ============================================================================

static pm_panic_handler_t g_panic_handler = NULL;
static bool g_shutdown_requested = false;

// ============================================================================
// WATCHDOG IMPLEMENTATION
// ============================================================================

pm_result_t pm_watchdog_init(void) {
    g_watchdog.last_feed_ms = to_ms_since_boot(get_absolute_time());
    g_watchdog.enabled = true;
    g_watchdog.feed_count = 0;
    
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Watchdog initialized");
    
    return PM_OK;
}

void pm_watchdog_enable(bool enable) {
    if (enable && !g_watchdog.hw_enabled) {
        // Enable hardware watchdog
        watchdog_enable(g_watchdog.timeout_ms, true);
        g_watchdog.hw_enabled = true;
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Hardware watchdog enabled");
    } else if (!enable && g_watchdog.hw_enabled) {
        // Cannot disable hardware watchdog once enabled on RP2040
        pm_klog(PICOMIMI_LOG_LEVEL_WARNING, "Cannot disable hardware watchdog");
    }
    
    g_watchdog.enabled = enable;
}

void pm_watchdog_feed(void) {
    g_watchdog.last_feed_ms = to_ms_since_boot(get_absolute_time());
    g_watchdog.feed_count++;
    
    if (g_watchdog.hw_enabled) {
        watchdog_update();
    }
}

void pm_watchdog_check(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Check main watchdog
    if (g_watchdog.enabled) {
        uint32_t elapsed = now - g_watchdog.last_feed_ms;
        
        if (elapsed > (g_watchdog.timeout_ms / 2)) {
            pm_klog(PICOMIMI_LOG_LEVEL_WARNING, "Watchdog not fed recently");
        }
    }
    
    // Check per-task watchdog
    if (g_watchdog.task_watchdog_enabled) {
        for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
            pm_tcb_t* task = &g_kernel.tasks[i];
            
            if (task->id == PM_INVALID_TASK) continue;
            if (task->state == TASK_STATE_TERMINATED) continue;
            if (task->state == TASK_STATE_ZOMBIE) continue;
            if (task->flags & TASK_FLAG_SYSTEM) continue;  // Skip system tasks
            
            uint32_t task_elapsed = now - task->last_activity_ms;
            
            if (task->state == TASK_STATE_RUNNING && 
                task_elapsed > PM_TASK_WATCHDOG_TIMEOUT_MS) {
                g_watchdog.task_watchdog_violations++;
                pm_kprintf("WATCHDOG: Task %d '%s' unresponsive (%lu ms)\n",
                          task->id, task->name, task_elapsed);
            }
        }
    }
}

const pm_watchdog_state_t* pm_watchdog_get_state(void) {
    return &g_watchdog;
}

void pm_watchdog_enable_task_monitor(bool enable) {
    g_watchdog.task_watchdog_enabled = enable;
}

void pm_watchdog_task_activity(pm_task_id_t task_id) {
    if (task_id < PICOMIMI_MAX_TASKS) {
        g_kernel.tasks[task_id].last_activity_ms = to_ms_since_boot(get_absolute_time());
    }
}

// ============================================================================
// SYSTEM TASKS
// ============================================================================

/**
 * Idle task - runs when nothing else is ready
 * Uses WFI instruction for power savings
 */
void pm_idle_task(void* arg) {
    (void)arg;
    
    while (1) {
        // Feed watchdog
        pm_watchdog_feed();
        
        // Wait for interrupt (low power)
        __asm__ volatile("wfi");
        
        // Update idle time tracking
        g_kernel.idle_time_us += 100;  // Approximate
    }
}

/**
 * Reaper task - cleans up zombie tasks
 */
void pm_reaper_task(void* arg) {
    (void)arg;
    
    while (1) {
        uint32_t zombies_cleaned = 0;
        uint32_t reclaimed_bytes = 0;
        
        for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
            pm_tcb_t* task = &g_kernel.tasks[i];
            
            uint32_t irq = save_and_disable_interrupts();
            
            if (task->state == TASK_STATE_ZOMBIE) {
                // Clean up task memory
                uint32_t freed = pm_mem_free_task_memory(task->id);
                reclaimed_bytes += freed;
                
                // Clean up IPC queues
                pm_ipc_cleanup_task(task->id);
                
                // Mark as terminated
                task->state = TASK_STATE_TERMINATED;
                g_kernel.zombie_count--;
                zombies_cleaned++;
                
                g_system_stats.total_tasks_terminated++;
            }
            
            restore_interrupts(irq);
        }
        
        if (zombies_cleaned > 0) {
            pm_kprintf("REAPER: Cleaned %lu zombies, %lu bytes reclaimed\n",
                      zombies_cleaned, reclaimed_bytes);
            
            // Compact memory after cleanup
            pm_mem_compact();
        }
        
        pm_task_sleep(1000);  // Run every second
    }
}

/**
 * CPU monitor task - tracks CPU usage and warns on high load
 */
void pm_cpu_monitor_task(void* arg) {
    (void)arg;
    
    while (1) {
        pm_task_sleep(5000);  // Check every 5 seconds
        
        if (g_kernel.cpu_usage > 95.0f) {
            pm_kprintf("CPUMON: High CPU load! %.1f%%\n", g_kernel.cpu_usage);
        }
        
        // Update statistics
        if (g_kernel.cpu_usage > g_system_stats.cpu_usage_peak) {
            g_system_stats.cpu_usage_peak = g_kernel.cpu_usage;
        }
        
        g_system_stats.cpu_usage_avg = 
            (g_system_stats.cpu_usage_avg * 0.9f) + (g_kernel.cpu_usage * 0.1f);
    }
}

/**
 * Temperature monitor task - tracks temperature and warns on high temps
 */
void pm_temp_monitor_task(void* arg) {
    (void)arg;
    
    while (1) {
        pm_task_sleep(2000);  // Check every 2 seconds
        
        float temp = pm_governor_read_temp(&g_kernel.governor);
        
        if (temp > 70.0f) {
            pm_kprintf("TEMPMON: High temperature! %.1f C\n", temp);
        }
        
        // Update statistics
        if (temp > g_system_stats.temperature_peak) {
            g_system_stats.temperature_peak = temp;
        }
        
        g_system_stats.temperature_avg = 
            (g_system_stats.temperature_avg * 0.9f) + (temp * 0.1f);
    }
}

/**
 * Filesystem task - periodic filesystem maintenance
 */
void pm_fs_task(void* arg) {
    (void)arg;
    
    while (1) {
        pm_task_sleep(30000);  // Run every 30 seconds
        
#if PICOMIMI_SD_ENABLED
        // Flush any pending writes
        // pm_pmfs_sync();
#endif
    }
}

/**
 * Input task - poll input devices
 */
void pm_input_task(void* arg) {
    (void)arg;
    
    // Input handling is done in drivers/drivers.c
    // This task just ensures the input system runs periodically
    
    while (1) {
        pm_task_sleep(20);  // Poll every 20ms
    }
}

/**
 * Shell task wrapper
 */
void pm_shell_task(void* arg) {
    // Shell is implemented in shell/shell.c
    pm_shell_run(arg);
}

pm_result_t pm_systasks_init(void) {
    // Create idle task (highest priority for power management)
    g_systask_ids[PM_SYSTASK_IDLE] = pm_task_create(
        "idle", pm_idle_task, NULL,
        PICOMIMI_DEFAULT_STACK_SIZE / 2,
        0,  // Lowest priority
        TASK_FLAG_SYSTEM | TASK_FLAG_CRITICAL
    );
    
    // Create reaper task
    g_systask_ids[PM_SYSTASK_REAPER] = pm_task_create(
        "reaper", pm_reaper_task, NULL,
        PICOMIMI_DEFAULT_STACK_SIZE,
        1,  // Low priority
        TASK_FLAG_SYSTEM
    );
    
    // Create CPU monitor
    g_systask_ids[PM_SYSTASK_CPU_MONITOR] = pm_task_create(
        "cpumon", pm_cpu_monitor_task, NULL,
        PICOMIMI_DEFAULT_STACK_SIZE / 2,
        2,
        TASK_FLAG_SYSTEM
    );
    
    // Create temperature monitor
    g_systask_ids[PM_SYSTASK_TEMP_MONITOR] = pm_task_create(
        "tempmon", pm_temp_monitor_task, NULL,
        PICOMIMI_DEFAULT_STACK_SIZE / 2,
        2,
        TASK_FLAG_SYSTEM
    );
    
    // Create filesystem task
    g_systask_ids[PM_SYSTASK_FS] = pm_task_create(
        "fs", pm_fs_task, NULL,
        PICOMIMI_DEFAULT_STACK_SIZE,
        3,
        TASK_FLAG_SYSTEM
    );
    
    // Create shell task
    g_systask_ids[PM_SYSTASK_SHELL] = pm_task_create(
        "shell", pm_shell_task, NULL,
        PICOMIMI_DEFAULT_STACK_SIZE * 2,
        5,  // Medium-high priority for responsiveness
        TASK_FLAG_SYSTEM
    );
    
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "System tasks initialized");
    
    return PM_OK;
}

pm_task_id_t pm_systask_get_id(pm_systask_t task) {
    if (task >= PM_SYSTASK_COUNT) return PM_INVALID_TASK;
    return g_systask_ids[task];
}

bool pm_is_system_task(pm_task_id_t task_id) {
    for (int i = 0; i < PM_SYSTASK_COUNT; i++) {
        if (g_systask_ids[i] == task_id) return true;
    }
    return false;
}

// ============================================================================
// TIMER IMPLEMENTATION
// ============================================================================

pm_result_t pm_timer_init(void) {
    mutex_init(&g_timer_lock);
    
    for (int i = 0; i < PM_MAX_TIMERS; i++) {
        g_timers[i].active = false;
        g_timers[i].callback = NULL;
        g_timers[i].owner = PM_INVALID_TASK;
    }
    
    return PM_OK;
}

int32_t pm_timer_create(uint32_t period_ms, pm_timer_callback_t callback,
                        void* user_data, bool periodic) {
    if (!callback || period_ms == 0) return -1;
    
    mutex_enter_blocking(&g_timer_lock);
    
    int32_t timer_id = -1;
    for (int i = 0; i < PM_MAX_TIMERS; i++) {
        if (!g_timers[i].active) {
            g_timers[i].active = true;
            g_timers[i].periodic = periodic;
            g_timers[i].period_ms = period_ms;
            g_timers[i].next_fire_ms = 0;  // Not started yet
            g_timers[i].callback = callback;
            g_timers[i].user_data = user_data;
            g_timers[i].owner = g_kernel.current_task;
            timer_id = i;
            break;
        }
    }
    
    mutex_exit(&g_timer_lock);
    return timer_id;
}

pm_result_t pm_timer_start(uint32_t timer_id) {
    if (timer_id >= PM_MAX_TIMERS) return PM_ERROR_INVALID;
    
    mutex_enter_blocking(&g_timer_lock);
    
    if (!g_timers[timer_id].active) {
        mutex_exit(&g_timer_lock);
        return PM_ERROR_NOTFOUND;
    }
    
    g_timers[timer_id].next_fire_ms = 
        to_ms_since_boot(get_absolute_time()) + g_timers[timer_id].period_ms;
    
    mutex_exit(&g_timer_lock);
    return PM_OK;
}

pm_result_t pm_timer_stop(uint32_t timer_id) {
    if (timer_id >= PM_MAX_TIMERS) return PM_ERROR_INVALID;
    
    mutex_enter_blocking(&g_timer_lock);
    
    if (!g_timers[timer_id].active) {
        mutex_exit(&g_timer_lock);
        return PM_ERROR_NOTFOUND;
    }
    
    g_timers[timer_id].next_fire_ms = 0;
    
    mutex_exit(&g_timer_lock);
    return PM_OK;
}

pm_result_t pm_timer_delete(uint32_t timer_id) {
    if (timer_id >= PM_MAX_TIMERS) return PM_ERROR_INVALID;
    
    mutex_enter_blocking(&g_timer_lock);
    
    g_timers[timer_id].active = false;
    g_timers[timer_id].callback = NULL;
    g_timers[timer_id].owner = PM_INVALID_TASK;
    
    mutex_exit(&g_timer_lock);
    return PM_OK;
}

void pm_timer_tick(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    for (int i = 0; i < PM_MAX_TIMERS; i++) {
        if (!g_timers[i].active) continue;
        if (g_timers[i].next_fire_ms == 0) continue;  // Not started
        
        if (now >= g_timers[i].next_fire_ms) {
            // Timer fired
            if (g_timers[i].callback) {
                g_timers[i].callback(i, g_timers[i].user_data);
            }
            
            if (g_timers[i].periodic) {
                g_timers[i].next_fire_ms = now + g_timers[i].period_ms;
            } else {
                g_timers[i].next_fire_ms = 0;  // One-shot, stop
            }
        }
    }
}

uint32_t pm_timer_remaining(uint32_t timer_id) {
    if (timer_id >= PM_MAX_TIMERS) return 0;
    if (!g_timers[timer_id].active) return 0;
    if (g_timers[timer_id].next_fire_ms == 0) return 0;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now >= g_timers[timer_id].next_fire_ms) return 0;
    
    return g_timers[timer_id].next_fire_ms - now;
}

// ============================================================================
// SYSTEM STATISTICS
// ============================================================================

const pm_system_stats_t* pm_get_system_stats(void) {
    return &g_system_stats;
}

void pm_update_system_stats(void) {
    uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    
    g_system_stats.uptime_ms = now_ms;
    g_system_stats.uptime_seconds = now_ms / 1000;
    
    // Update context switch count from scheduler
    g_system_stats.context_switches = g_core0_sched.context_switches;
    g_system_stats.preemptions = g_core0_sched.preemptions;
}

void pm_reset_system_stats(void) {
    memset(&g_system_stats, 0, sizeof(g_system_stats));
}

void pm_print_system_stats(void) {
    pm_update_system_stats();
    
    pm_kprintf("\n=== System Statistics ===\n");
    pm_kprintf("Uptime:           %lu seconds\n", g_system_stats.uptime_seconds);
    pm_kprintf("Tasks created:    %lu\n", g_system_stats.total_tasks_created);
    pm_kprintf("Tasks terminated: %lu\n", g_system_stats.total_tasks_terminated);
    pm_kprintf("Context switches: %lu\n", g_system_stats.context_switches);
    pm_kprintf("Preemptions:      %lu\n", g_system_stats.preemptions);
    pm_kprintf("CPU avg:          %.1f%%\n", g_system_stats.cpu_usage_avg);
    pm_kprintf("CPU peak:         %.1f%%\n", g_system_stats.cpu_usage_peak);
    pm_kprintf("Temp avg:         %.1f C\n", g_system_stats.temperature_avg);
    pm_kprintf("Temp peak:        %.1f C\n", g_system_stats.temperature_peak);
    pm_kprintf("OOM events:       %lu\n", g_system_stats.oom_events);
}

// ============================================================================
// KERNEL PANIC
// ============================================================================

__attribute__((noreturn)) void pm_kernel_panic(const char* reason) {
    // Disable interrupts
    __asm__ volatile("cpsid i");
    
    // Print panic message
    pm_kprintf("\n\n");
    pm_kprintf("╔════════════════════════════════════════╗\n");
    pm_kprintf("║         PICOMIMI KERNEL PANIC          ║\n");
    pm_kprintf("╠════════════════════════════════════════╣\n");
    pm_kprintf("║ Reason: %-30s ║\n", reason ? reason : "Unknown");
    pm_kprintf("║ Task:   %-30d ║\n", g_kernel.current_task);
    pm_kprintf("║ Uptime: %-30lu ║\n", 
               (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000));
    pm_kprintf("╚════════════════════════════════════════╝\n");
    
    // Call user panic handler if registered
    if (g_panic_handler) {
        g_panic_handler(reason);
    }
    
    // Increment panic counter
    g_system_stats.kernel_panics++;
    
    // Halt system
    while (1) {
        // Feed watchdog to prevent reset (for debugging)
        if (g_watchdog.hw_enabled) {
            watchdog_update();
        }
        
        tight_loop_contents();
    }
}

void pm_register_panic_handler(pm_panic_handler_t handler) {
    g_panic_handler = handler;
}

// ============================================================================
// SYSTEM MAINTENANCE
// ============================================================================

void pm_system_maintenance(void) {
    static uint32_t last_maintenance_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Run maintenance every 100ms
    if (now - last_maintenance_ms < 100) return;
    last_maintenance_ms = now;
    
    // Feed watchdog
    pm_watchdog_feed();
    
    // Check watchdog violations
    pm_watchdog_check();
    
    // Process timers
    pm_timer_tick();
    
    // Update statistics
    pm_update_system_stats();
    
    // IPC maintenance
    pm_ipc_maintenance();
}

void pm_system_shutdown(void) {
    pm_kprintf("System shutdown requested\n");
    g_shutdown_requested = true;
    
    // Set all tasks to terminate
    for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
        if (g_kernel.tasks[i].id != PM_INVALID_TASK &&
            !(g_kernel.tasks[i].flags & TASK_FLAG_CRITICAL)) {
            g_kernel.tasks[i].state = TASK_STATE_ZOMBIE;
        }
    }
}

void pm_system_reboot(void) {
    pm_kprintf("System reboot requested\n");
    
    // Use hardware watchdog to force reboot
    watchdog_reboot(0, 0, 0);
}

bool pm_is_shutdown_requested(void) {
    return g_shutdown_requested;
}
