/**
 * PICOMIMI Scheduler Implementation - O(1) Preemptive Priority Bitmap
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - O(1) task selection using priority bitmaps (find highest priority in constant time)
 * - Priority aging to prevent starvation (boost starving tasks)
 * - Preemption support for high-priority tasks
 * - Real-time task support with separate queue
 * - Dual-core scheduling with work stealing
 * - CPU load tracking and idle injection for thermal management
 */

#include "kernel/scheduler.h"
#include "api/picomimi_kernel.h"
#include "pico/sync.h"
#include "pico/time.h"
#include <string.h>

// External kernel state
extern pm_kernel_state_t g_kernel;

// Per-core scheduler instances
pm_core_scheduler_t g_core0_sched;
#if PICOMIMI_DUAL_CORE
pm_core_scheduler_t g_core1_sched;
#endif

// ============================================================================
// INTERRUPT CONTROL (platform abstraction)
// ============================================================================

static inline uint32_t disable_all_interrupts(void) {
    return save_and_disable_interrupts();
}

static inline void enable_all_interrupts(uint32_t state) {
    restore_interrupts(state);
}

// ============================================================================
// BITMAP OPERATIONS (O(1) task selection)
// ============================================================================

/**
 * Add a task to the priority bitmap at given priority level
 * O(1) operation
 */
void __attribute__((hot)) pm_sched_bitmap_add(pm_priority_bitmap_t* bm, pm_task_id_t task_id, uint8_t priority) {
    if (task_id >= PICOMIMI_MAX_TASKS || priority >= PICOMIMI_SCHED_PRIORITY_LEVELS) return;
    
    pm_bitmap_set(&bm->task_masks[priority], task_id);
    pm_bitmap_set(&bm->level_mask, priority);
}

/**
 * Remove a task from the priority bitmap
 * O(1) operation
 */
void __attribute__((hot)) pm_sched_bitmap_remove(pm_priority_bitmap_t* bm, pm_task_id_t task_id, uint8_t priority) {
    if (task_id >= PICOMIMI_MAX_TASKS || priority >= PICOMIMI_SCHED_PRIORITY_LEVELS) return;
    
    pm_bitmap_clear(&bm->task_masks[priority], task_id);
    
    // Clear level mask if no more tasks at this priority
    if (bm->task_masks[priority] == 0) {
        pm_bitmap_clear(&bm->level_mask, priority);
    }
}

/**
 * Find the highest priority task in the bitmap
 * O(1) operation using bit manipulation
 * Returns priority level (0 = highest), or -1 if empty
 */
int __attribute__((hot)) pm_sched_bitmap_find_highest(pm_priority_bitmap_t* bm, pm_task_id_t* task_id_out) {
    if (bm->level_mask == 0) {
        *task_id_out = PM_INVALID_TASK;
        return -1;
    }
    
    // Find highest set priority level (FLS = find last set = highest bit)
    int level = pm_bitmap_fls(bm->level_mask) - 1;
    if (level < 0 || level >= PICOMIMI_SCHED_PRIORITY_LEVELS) {
        *task_id_out = PM_INVALID_TASK;
        return -1;
    }
    
    // Find first task at that level (FFS = find first set)
    int task_bit = pm_bitmap_ffs(bm->task_masks[level]);
    if (task_bit == 0) {
        *task_id_out = PM_INVALID_TASK;
        return -1;
    }
    
    *task_id_out = task_bit - 1;  // FFS is 1-indexed
    return level;
}

// ============================================================================
// CORE SCHEDULER INITIALIZATION
// ============================================================================

pm_result_t pm_scheduler_init_core(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched : 
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return PM_ERROR_INVALID;
#endif
    
    memset(sched, 0, sizeof(pm_core_scheduler_t));
    mutex_init(&sched->lock);
    critical_section_init(&sched->rt_lock);
    
    sched->idle_task = 0;  // Task 0 is always idle
    sched->current_task = 0;
    sched->last_aging_ms = to_ms_since_boot(get_absolute_time());
    sched->work_steal_enabled = true;
    sched->initialized = true;
    
    return PM_OK;
}

pm_result_t pm_scheduler_init(void) {
    pm_result_t result = pm_scheduler_init_core(0);
    if (result != PM_OK) return result;
    
#if PICOMIMI_DUAL_CORE
    result = pm_scheduler_init_core(1);
    if (result != PM_OK) return result;
#endif
    
    return PM_OK;
}

void pm_scheduler_start(void) {
    g_core0_sched.last_switch_ms = to_ms_since_boot(get_absolute_time());
#if PICOMIMI_DUAL_CORE
    g_core1_sched.last_switch_ms = to_ms_since_boot(get_absolute_time());
#endif
}

void pm_scheduler_stop(void) {
    // Mark scheduler as stopped
    g_core0_sched.initialized = false;
#if PICOMIMI_DUAL_CORE
    g_core1_sched.initialized = false;
#endif
}

// ============================================================================
// TASK STATE MANAGEMENT
// ============================================================================

pm_result_t pm_scheduler_ready(pm_task_id_t task_id, uint8_t priority) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    if (priority >= PICOMIMI_SCHED_PRIORITY_LEVELS) return PM_ERROR_INVALID;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    
    uint32_t irq = disable_all_interrupts();
    
    task->state = TASK_STATE_READY;
    task->priority = priority;
    task->sched_info.effective_priority = priority;
    
    // Add to appropriate bitmap
    pm_core_scheduler_t* sched = &g_core0_sched;
    
    if (pm_is_realtime_priority(priority)) {
        pm_sched_bitmap_add(&sched->rt_runnable, task_id, priority);
        sched->rt_task_count++;
    } else {
        pm_sched_bitmap_add(&sched->runnable, task_id, priority);
    }
    
    enable_all_interrupts(irq);
    return PM_OK;
}

pm_result_t pm_scheduler_remove(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    
    uint32_t irq = disable_all_interrupts();
    
    pm_core_scheduler_t* sched = &g_core0_sched;
    
    // Remove from all bitmaps (we don't know which one it's in)
    for (uint8_t p = 0; p < PICOMIMI_SCHED_PRIORITY_LEVELS; p++) {
        pm_sched_bitmap_remove(&sched->runnable, task_id, p);
        pm_sched_bitmap_remove(&sched->waiting, task_id, p);
        pm_sched_bitmap_remove(&sched->rt_runnable, task_id, p);
    }
    
    if (pm_is_realtime_priority(task->priority) && sched->rt_task_count > 0) {
        sched->rt_task_count--;
    }
    
    task->state = TASK_STATE_TERMINATED;
    
    enable_all_interrupts(irq);
    return PM_OK;
}

pm_result_t pm_scheduler_set_priority(pm_task_id_t task_id, uint8_t new_priority) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    if (new_priority >= PICOMIMI_SCHED_PRIORITY_LEVELS) return PM_ERROR_INVALID;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    
    uint32_t irq = disable_all_interrupts();
    
    pm_core_scheduler_t* sched = &g_core0_sched;
    uint8_t old_priority = task->priority;
    
    if (task->state == TASK_STATE_READY) {
        // Remove from old priority
        if (pm_is_realtime_priority(old_priority)) {
            pm_sched_bitmap_remove(&sched->rt_runnable, task_id, old_priority);
        } else {
            pm_sched_bitmap_remove(&sched->runnable, task_id, old_priority);
        }
        
        // Add to new priority
        if (pm_is_realtime_priority(new_priority)) {
            pm_sched_bitmap_add(&sched->rt_runnable, task_id, new_priority);
        } else {
            pm_sched_bitmap_add(&sched->runnable, task_id, new_priority);
        }
    }
    
    task->priority = new_priority;
    task->sched_info.effective_priority = new_priority;
    
    enable_all_interrupts(irq);
    return PM_OK;
}

// ============================================================================
// BLOCKING AND UNBLOCKING
// ============================================================================

void pm_scheduler_block(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return;
    
    uint32_t irq = disable_all_interrupts();
    
    pm_core_scheduler_t* sched = &g_core0_sched;
    uint8_t priority = task->priority;
    
    // Remove from runnable
    if (pm_is_realtime_priority(priority)) {
        pm_sched_bitmap_remove(&sched->rt_runnable, task_id, priority);
    } else {
        pm_sched_bitmap_remove(&sched->runnable, task_id, priority);
    }
    
    // Add to waiting
    pm_sched_bitmap_add(&sched->waiting, task_id, priority);
    
    task->state = TASK_STATE_WAITING;
    
    enable_all_interrupts(irq);
}

void pm_scheduler_unblock(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return;
    
    uint32_t irq = disable_all_interrupts();
    
    pm_core_scheduler_t* sched = &g_core0_sched;
    uint8_t priority = task->priority;
    
    // Remove from waiting
    pm_sched_bitmap_remove(&sched->waiting, task_id, priority);
    
    // Add to runnable
    if (pm_is_realtime_priority(priority)) {
        pm_sched_bitmap_add(&sched->rt_runnable, task_id, priority);
    } else {
        pm_sched_bitmap_add(&sched->runnable, task_id, priority);
    }
    
    task->state = TASK_STATE_READY;
    task->sched_info.last_wakeup = to_ms_since_boot(get_absolute_time());
    
    enable_all_interrupts(irq);
}

// ============================================================================
// PRIORITY AGING (prevents starvation)
// ============================================================================

void pm_scheduler_age_tasks(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return;
#endif
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Don't age too frequently
    if (now - sched->last_aging_ms < SCHED_AGING_INTERVAL_MS) return;
    sched->last_aging_ms = now;
    
    uint32_t irq = disable_all_interrupts();
    
    for (uint32_t i = 1; i < PICOMIMI_MAX_TASKS; i++) {  // Skip idle task (0)
        pm_tcb_t* task = &g_kernel.tasks[i];
        
        if (task->id == PM_INVALID_TASK) continue;
        if (task->state != TASK_STATE_READY) continue;
        
        // Don't age real-time tasks
        if (pm_is_realtime_priority(task->priority)) continue;
        
        // Don't age tasks with original priority (system tasks)
        if (task->sched_info.original_priority != 0) continue;
        
        // Check how long task has been waiting
        uint32_t wait_time = now - task->sched_info.last_run;
        
        // Boost priority if starving (lower number = higher priority)
        if (wait_time > SCHED_STARVATION_THRESHOLD_MS && task->priority > 0) {
            uint8_t old_prio = task->priority;
            task->priority--;  // Boost priority
            
            // Update bitmap
            pm_sched_bitmap_remove(&sched->runnable, i, old_prio);
            pm_sched_bitmap_add(&sched->runnable, i, task->priority);
        }
    }
    
    enable_all_interrupts(irq);
}

void pm_scheduler_update_task_priority(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return;
    
    pm_core_scheduler_t* sched = &g_core0_sched;
    
    uint32_t irq = disable_all_interrupts();
    
    // Find and remove from old position
    for (uint8_t p = 0; p < PICOMIMI_SCHED_PRIORITY_LEVELS; p++) {
        if (pm_bitmap_test(sched->runnable.task_masks[p], task_id)) {
            pm_sched_bitmap_remove(&sched->runnable, task_id, p);
            break;
        }
    }
    
    // Add to new position
    if (task->state == TASK_STATE_READY) {
        pm_sched_bitmap_add(&sched->runnable, task_id, task->priority);
    }
    
    enable_all_interrupts(irq);
}

// ============================================================================
// PREEMPTION
// ============================================================================

void pm_scheduler_check_preemption(void) {
    if (g_kernel.preemption_pending) return;
    
    pm_task_id_t task_id;
    int highest_prio = pm_sched_bitmap_find_highest(&g_core0_sched.runnable, &task_id);
    
    if (highest_prio < 0) return;
    
    pm_tcb_t* current = &g_kernel.tasks[g_kernel.current_task];
    
    // Preempt if higher priority task is ready
    if (highest_prio > (int)current->priority) {
        g_kernel.preemption_pending = true;
    }
}

bool pm_scheduler_is_preemption_pending(void) {
    return g_kernel.preemption_pending;
}

void pm_scheduler_handle_preemption(void) {
    if (!g_kernel.preemption_pending) return;
    
    g_kernel.preemption_pending = false;
    g_core0_sched.preemptions++;
    
    // Force reschedule on next tick
    pm_scheduler_yield();
}

// ============================================================================
// CPU LOAD MANAGEMENT
// ============================================================================

void pm_scheduler_update_load(uint8_t core, uint64_t idle_time_us, uint64_t total_time_us) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return;
#endif
    
    if (total_time_us == 0) return;
    
    float instant_load = 100.0f * (1.0f - ((float)idle_time_us / (float)total_time_us));
    if (instant_load < 0.0f) instant_load = 0.0f;
    if (instant_load > 100.0f) instant_load = 100.0f;
    
    sched->cpu_load_instant = instant_load;
    
    // Exponential moving average (90% old, 10% new)
    sched->cpu_load = (sched->cpu_load * 0.9f) + (instant_load * 0.1f);
    
    // Track peak
    if (instant_load > sched->cpu_load_peak) {
        sched->cpu_load_peak = instant_load;
    }
}

float pm_scheduler_get_cpu_load(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return 0.0f;
#endif
    return sched->cpu_load;
}

float pm_scheduler_get_instant_load(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return 0.0f;
#endif
    return sched->cpu_load_instant;
}

bool pm_scheduler_should_inject_idle(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return false;
#endif
    
    if (sched->cpu_load < SCHED_IDLE_INJECTION_THRESHOLD) {
        sched->idle_injection_active = false;
        return false;
    }
    
    // Inject idle every N ticks when CPU is overloaded
    static uint32_t idle_counter = 0;
    if (++idle_counter >= SCHED_IDLE_INJECTION_INTERVAL) {
        idle_counter = 0;
        sched->idle_injections++;
        sched->idle_injection_active = true;
        return true;
    }
    
    return false;
}

// ============================================================================
// TASK SELECTION (O(1) - the main scheduler algorithm)
// ============================================================================

pm_task_id_t __attribute__((hot)) pm_scheduler_select_next_core0(void) {
    uint32_t irq = disable_all_interrupts();
    
    // Check for idle injection (thermal throttling)
    if (pm_scheduler_should_inject_idle(0)) {
        enable_all_interrupts(irq);
        return g_core0_sched.idle_task;
    }
    
    // Try RT queue first
    if (g_core0_sched.rt_task_count > 0) {
        pm_task_id_t task_id;
        int priority = pm_sched_bitmap_find_highest(&g_core0_sched.rt_runnable, &task_id);
        
        if (priority >= 0 && task_id < PICOMIMI_MAX_TASKS) {
            pm_tcb_t* task = &g_kernel.tasks[task_id];
            
            if (task->state == TASK_STATE_READY || task->state == TASK_STATE_RUNNING) {
                if (task_id != g_core0_sched.current_task) {
                    g_core0_sched.switches++;
                    g_core0_sched.rt_switches++;
                }
                g_core0_sched.current_task = task_id;
                g_core0_sched.current_priority = priority;
                enable_all_interrupts(irq);
                return task_id;
            }
        }
    }
    
    // Search normal queue with retry loop (in case of stale entries)
    for (int attempts = 0; attempts < PICOMIMI_MAX_TASKS; attempts++) {
        pm_task_id_t task_id;
        int priority = pm_sched_bitmap_find_highest(&g_core0_sched.runnable, &task_id);
        
        if (priority < 0 || task_id >= PICOMIMI_MAX_TASKS) {
            enable_all_interrupts(irq);
            return g_core0_sched.idle_task;
        }
        
        pm_tcb_t* task = &g_kernel.tasks[task_id];
        
        if (task->state == TASK_STATE_READY || task->state == TASK_STATE_RUNNING) {
            if (task_id != g_core0_sched.current_task) {
                g_core0_sched.switches++;
            }
            g_core0_sched.current_task = task_id;
            g_core0_sched.current_priority = priority;
            task->sched_info.last_run = to_ms_since_boot(get_absolute_time());
            enable_all_interrupts(irq);
            return task_id;
        }
        
        // Task not runnable, remove from bitmap and retry
        pm_sched_bitmap_remove(&g_core0_sched.runnable, task_id, priority);
    }
    
    enable_all_interrupts(irq);
    return g_core0_sched.idle_task;
}

#if PICOMIMI_DUAL_CORE
pm_task_id_t __attribute__((hot)) pm_scheduler_select_next_core1(void) {
    uint32_t irq = disable_all_interrupts();
    
    // Similar to core0 but uses core1 scheduler
    if (pm_scheduler_should_inject_idle(1)) {
        enable_all_interrupts(irq);
        return g_core1_sched.idle_task;
    }
    
    for (int attempts = 0; attempts < PICOMIMI_MAX_TASKS; attempts++) {
        pm_task_id_t task_id;
        int priority = pm_sched_bitmap_find_highest(&g_core1_sched.runnable, &task_id);
        
        if (priority < 0 || task_id >= PICOMIMI_MAX_TASKS) {
            enable_all_interrupts(irq);
            return g_core1_sched.idle_task;
        }
        
        pm_tcb_t* task = &g_kernel.tasks[task_id];  // Simplified: use same task array
        
        if (task->state == TASK_STATE_READY || task->state == TASK_STATE_RUNNING) {
            if (task_id != g_core1_sched.current_task) {
                g_core1_sched.switches++;
            }
            g_core1_sched.current_task = task_id;
            g_core1_sched.current_priority = priority;
            task->sched_info.last_run = to_ms_since_boot(get_absolute_time());
            enable_all_interrupts(irq);
            return task_id;
        }
        
        pm_sched_bitmap_remove(&g_core1_sched.runnable, task_id, priority);
    }
    
    enable_all_interrupts(irq);
    return g_core1_sched.idle_task;
}
#endif

pm_task_id_t pm_scheduler_select_next(uint8_t core) {
    if (core == 0) {
        return pm_scheduler_select_next_core0();
    }
#if PICOMIMI_DUAL_CORE
    else if (core == 1) {
        return pm_scheduler_select_next_core1();
    }
#endif
    return 0;  // Idle task
}

// ============================================================================
// WORK STEALING (RP2350 dual-core optimization)
// ============================================================================

#if PICOMIMI_DUAL_CORE
bool pm_scheduler_work_steal(uint8_t from_core, uint8_t to_core) {
    if (!g_core0_sched.work_steal_enabled) return false;
    if (from_core == to_core) return false;
    
    pm_core_scheduler_t* from_sched = (from_core == 0) ? &g_core0_sched : &g_core1_sched;
    pm_core_scheduler_t* to_sched = (to_core == 0) ? &g_core0_sched : &g_core1_sched;
    
    uint32_t irq = disable_all_interrupts();
    
    // Count tasks in from_core
    uint32_t task_count = 0;
    for (uint8_t p = 0; p < PICOMIMI_SCHED_PRIORITY_LEVELS; p++) {
        task_count += __builtin_popcount(from_sched->runnable.task_masks[p]);
    }
    
    // Only steal if source has more than threshold
    if (task_count <= SCHED_WORK_STEAL_THRESHOLD) {
        enable_all_interrupts(irq);
        return false;
    }
    
    // Find a task to steal (lowest priority first)
    for (int p = 0; p < PICOMIMI_SCHED_PRIORITY_LEVELS; p++) {
        if (from_sched->runnable.task_masks[p] == 0) continue;
        
        int task_bit = pm_bitmap_ffs(from_sched->runnable.task_masks[p]);
        if (task_bit == 0) continue;
        
        pm_task_id_t task_id = task_bit - 1;
        pm_tcb_t* task = &g_kernel.tasks[task_id];
        
        // Don't steal critical or pinned tasks
        if (task->flags & TASK_FLAG_CRITICAL) continue;
        if (task->flags & TASK_FLAG_PINNED) continue;
        
        // Steal!
        pm_sched_bitmap_remove(&from_sched->runnable, task_id, p);
        pm_sched_bitmap_add(&to_sched->runnable, task_id, p);
        
        to_sched->work_stolen++;
        
        enable_all_interrupts(irq);
        return true;
    }
    
    enable_all_interrupts(irq);
    return false;
}

void pm_scheduler_enable_work_steal(bool enable) {
    g_core0_sched.work_steal_enabled = enable;
    g_core1_sched.work_steal_enabled = enable;
}
#else
bool pm_scheduler_work_steal(uint8_t from_core, uint8_t to_core) {
    (void)from_core; (void)to_core;
    return false;
}

void pm_scheduler_enable_work_steal(bool enable) {
    (void)enable;
}
#endif

// ============================================================================
// SCHEDULER TICK AND YIELD
// ============================================================================

void pm_scheduler_tick(void) {
    // Age tasks periodically
    pm_scheduler_age_tasks(0);
#if PICOMIMI_DUAL_CORE
    pm_scheduler_age_tasks(1);
#endif
    
    // Check for preemption
    pm_scheduler_check_preemption();
    
    // Handle preemption if pending
    if (g_kernel.preemption_pending) {
        pm_scheduler_handle_preemption();
    }
}

void pm_scheduler_yield(void) {
    g_core0_sched.switches++;
    
    // Mark current task's last run time
    pm_task_id_t current = g_core0_sched.current_task;
    if (current < PICOMIMI_MAX_TASKS) {
        g_kernel.tasks[current].sched_info.last_run = to_ms_since_boot(get_absolute_time());
    }
}

// ============================================================================
// STATISTICS
// ============================================================================

uint32_t pm_scheduler_get_context_switches(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return 0;
#endif
    return sched->switches;
}

uint32_t pm_scheduler_get_preemptions(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return 0;
#endif
    return sched->preemptions;
}

uint32_t pm_scheduler_get_idle_injections(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return 0;
#endif
    return sched->idle_injections;
}

pm_task_id_t pm_scheduler_get_current_task(uint8_t core) {
    pm_core_scheduler_t* sched = (core == 0) ? &g_core0_sched :
#if PICOMIMI_DUAL_CORE
        &g_core1_sched;
#else
        NULL;
    if (!sched) return 0;
#endif
    return sched->current_task;
}
