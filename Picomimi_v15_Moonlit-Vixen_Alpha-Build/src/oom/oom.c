/**
 * PICOMIMI OOM (Out-Of-Memory) Killer Implementation
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - Velocity tracking for allocation patterns
 * - Abusive allocator detection and immediate termination
 * - Victim selection with resource-aware scoring
 * - Graceful cleanup with timeout
 * - Prevention via cache flush and compaction
 */

#include "oom/oom.h"
#include "api/picomimi_kernel.h"
#include "memory/memory.h"
#include "resource/resource.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

// External kernel state
extern pm_kernel_state_t g_kernel;

// ============================================================================
// INTERNAL STATE
// ============================================================================

static pm_oom_handler_t g_oom_handlers[OOM_MAX_HANDLERS];
static pm_oom_request_t g_oom_current_request;
static pm_oom_stats_t g_oom_stats;
static pm_oom_velocity_t g_task_velocity[PICOMIMI_MAX_TASKS];
static bool g_oom_initialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

pm_result_t pm_oom_init(void) {
    memset(g_oom_handlers, 0, sizeof(g_oom_handlers));
    memset(&g_oom_current_request, 0, sizeof(g_oom_current_request));
    memset(&g_oom_stats, 0, sizeof(g_oom_stats));
    memset(g_task_velocity, 0, sizeof(g_task_velocity));
    
    g_oom_initialized = true;
    return PM_OK;
}

// ============================================================================
// HANDLER REGISTRATION
// ============================================================================

void pm_oom_register_handler(pm_task_id_t task_id, pm_oom_callback_t callback) {
    if (!callback) return;
    
    // Check if already registered
    for (int i = 0; i < OOM_MAX_HANDLERS; i++) {
        if (g_oom_handlers[i].active && g_oom_handlers[i].task_id == task_id) {
            g_oom_handlers[i].callback = callback;
            return;
        }
    }
    
    // Find empty slot
    for (int i = 0; i < OOM_MAX_HANDLERS; i++) {
        if (!g_oom_handlers[i].active) {
            g_oom_handlers[i].task_id = task_id;
            g_oom_handlers[i].callback = callback;
            g_oom_handlers[i].active = true;
            return;
        }
    }
}

void pm_oom_unregister_handler(pm_task_id_t task_id) {
    for (int i = 0; i < OOM_MAX_HANDLERS; i++) {
        if (g_oom_handlers[i].active && g_oom_handlers[i].task_id == task_id) {
            g_oom_handlers[i].active = false;
            g_oom_handlers[i].callback = NULL;
            return;
        }
    }
}

pm_oom_callback_t pm_oom_get_handler(pm_task_id_t task_id) {
    for (int i = 0; i < OOM_MAX_HANDLERS; i++) {
        if (g_oom_handlers[i].active && g_oom_handlers[i].task_id == task_id) {
            return g_oom_handlers[i].callback;
        }
    }
    return NULL;
}

// ============================================================================
// VELOCITY TRACKING
// ============================================================================

void pm_oom_track_alloc(pm_task_id_t task_id, size_t bytes) {
    if (task_id >= PICOMIMI_MAX_TASKS) return;
    
    pm_oom_velocity_t* vel = &g_task_velocity[task_id];
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Store sample
    vel->samples[vel->index] = bytes;
    vel->timestamps[vel->index] = now;
    vel->index = (vel->index + 1) % OOM_VELOCITY_WINDOW_SIZE;
    
    // Calculate velocity (bytes per second over window)
    uint32_t total_bytes = 0;
    uint32_t oldest_time = now;
    uint32_t newest_time = 0;
    
    for (int i = 0; i < OOM_VELOCITY_WINDOW_SIZE; i++) {
        total_bytes += vel->samples[i];
        if (vel->timestamps[i] > 0 && vel->timestamps[i] < oldest_time) {
            oldest_time = vel->timestamps[i];
        }
        if (vel->timestamps[i] > newest_time) {
            newest_time = vel->timestamps[i];
        }
    }
    
    uint32_t time_span = newest_time - oldest_time;
    if (time_span > 0) {
        vel->velocity = (total_bytes * 1000) / time_span;  // bytes/second
    }
    vel->total_bytes = total_bytes;
    
    // Update TCB
    if (task_id < PICOMIMI_MAX_TASKS) {
        g_kernel.tasks[task_id].alloc_velocity = vel->velocity;
    }
}

uint32_t pm_oom_get_velocity(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return 0;
    return g_task_velocity[task_id].velocity;
}

bool pm_oom_is_abusive(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return false;
    return g_task_velocity[task_id].velocity > OOM_ABUSIVE_ALLOC_VELOCITY;
}

// ============================================================================
// VICTIM SCORING
// ============================================================================

int32_t pm_oom_calculate_score(pm_task_id_t task_id, uint32_t memory_used) {
    if (task_id >= PICOMIMI_MAX_TASKS) return -10000;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return -10000;
    
    int32_t score = 0;
    
    // Memory usage score (1 point per KB)
    score += (memory_used / 1024);
    
    // OOM priority penalty (higher oom_priority = more killable)
    score += (task->oom_priority * 100);
    
    // Idle time bonus (prefer killing idle tasks)
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t idle_time = now - task->sched_info.last_run;
    if (idle_time > 5000) score += 200;       // Very idle
    else if (idle_time > 1000) score += 50;   // Somewhat idle
    
    // Handler bonus (prefer keeping tasks with handlers)
    if (pm_oom_get_handler(task_id)) score -= 50;
    
    // CPU abuser penalty
    if (task->flags & TASK_FLAG_CPU_ABUSER) score += 150;
    
    // Resource hoarding penalty (tasks with many resources are worse victims)
    uint32_t res_count = pm_resource_count_owned_by(task_id);
    score += (res_count * 30);  // +30 points per owned resource
    
    // Resource violation penalty
    // TODO: Track violations per task
    
    // Critical task protection (unkillable)
    if (task->flags & TASK_FLAG_CRITICAL) score = -10000;
    if (task->type != TASK_TYPE_APP) score = -10000;
    
    return score;
}

pm_oom_victim_t pm_oom_select_victim(size_t bytes_needed) {
    (void)bytes_needed;
    
    pm_oom_victim_t victim = {0};
    victim.task_id = 0xFFFFFFFF;
    victim.score = -10000;
    
    // Search all tasks for best victim
    for (uint32_t i = 1; i < PICOMIMI_MAX_TASKS; i++) {  // Skip idle task (0)
        pm_tcb_t* task = &g_kernel.tasks[i];
        
        // Skip invalid/terminated tasks
        if (task->id == PM_INVALID_TASK) continue;
        if (task->state == TASK_STATE_TERMINATED) continue;
        if (task->state == TASK_STATE_ZOMBIE) continue;
        
        // Skip non-applications
        if (task->type != TASK_TYPE_APP) continue;
        
        // Skip critical tasks
        if (task->flags & TASK_FLAG_CRITICAL) continue;
        
        // Get memory usage for this task
        uint32_t task_mem = pm_get_task_memory(i);
        if (task_mem == 0) continue;
        
        // Calculate score
        int32_t score = pm_oom_calculate_score(i, task_mem);
        
        if (score > victim.score) {
            victim.task_id = i;
            victim.memory_used = task_mem;
            victim.resource_count = pm_resource_count_owned_by(i);
            victim.oom_priority = task->oom_priority;
            victim.score = score;
            victim.has_handler = (pm_oom_get_handler(i) != NULL);
        }
    }
    
    return victim;
}

// ============================================================================
// OOM PREVENTION
// ============================================================================

bool pm_oom_prevent(size_t bytes_needed) {
    g_oom_stats.total_events++;
    
    // Try memory compaction first
    pm_mem_compact();
    
    // Check if we have enough memory now
    size_t free_mem = pm_mem_get_free();
    if (free_mem >= bytes_needed + (OOM_PREVENTION_MIN_FREE_KB * 1024)) {
        g_oom_stats.prevention_count++;
        return true;
    }
    
    // Try flushing caches (if any)
    // TODO: Add cache system
    
    // Try garbage collection on filesystem
    // TODO: pmfs_garbage_collect();
    
    // Final check
    free_mem = pm_mem_get_free();
    if (free_mem >= bytes_needed + (OOM_PREVENTION_MIN_FREE_KB * 1024)) {
        g_oom_stats.prevention_count++;
        return true;
    }
    
    return false;
}

// ============================================================================
// GRACEFUL CLEANUP
// ============================================================================

bool pm_oom_request_cleanup(pm_oom_victim_t* victim, size_t bytes_needed) {
    if (victim->task_id == 0xFFFFFFFF || !victim->has_handler) {
        return false;
    }
    
    pm_tcb_t* task = &g_kernel.tasks[victim->task_id];
    if (task->id == PM_INVALID_TASK) return false;
    
    // Set up cleanup request
    g_oom_current_request.allocating_task_id = g_kernel.current_task;
    g_oom_current_request.target_task_id = victim->task_id;
    g_oom_current_request.request_time_ms = to_ms_since_boot(get_absolute_time());
    g_oom_current_request.bytes_requested = bytes_needed;
    g_oom_current_request.request_sent = true;
    g_oom_current_request.task_complied = false;
    
    // Mark task for cleanup
    task->flags |= TASK_FLAG_OOM_TARGET;
    task->oom_bytes_requested = bytes_needed;
    
    g_oom_stats.requests_sent++;
    
    // Call the handler
    pm_oom_callback_t handler = pm_oom_get_handler(victim->task_id);
    if (handler) {
        handler(bytes_needed);
    }
    
    return true;
}

void pm_oom_cleanup_done(pm_task_id_t task_id, uint32_t bytes_freed) {
    if (!g_oom_current_request.request_sent) return;
    if (g_oom_current_request.target_task_id != task_id) return;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return;
    
    g_oom_current_request.task_complied = true;
    g_oom_stats.voluntary_releases++;
    g_oom_stats.total_bytes_reclaimed += bytes_freed;
    
    // Clear OOM flags
    task->flags &= ~TASK_FLAG_OOM_TARGET;
    task->oom_bytes_requested = 0;
    
    // Wake up allocating task if it was blocked
    pm_task_id_t waiting_task = g_oom_current_request.allocating_task_id;
    if (waiting_task < PICOMIMI_MAX_TASKS) {
        pm_task_wake(waiting_task);
    }
    
    g_oom_current_request.request_sent = false;
}

// ============================================================================
// MAIN OOM KILLER
// ============================================================================

bool pm_oom_killer(size_t bytes_needed) {
    if (!g_oom_initialized) {
        pm_oom_init();
    }
    
    pm_kprintf("\n!!! OUT OF MEMORY !!!\n");
    pm_kprintf("Need: %u KB\n", (unsigned)(bytes_needed / 1024));
    
    // Check for abusive allocator (kill immediately)
    pm_task_id_t allocator_id = g_kernel.current_task;
    if (allocator_id < PICOMIMI_MAX_TASKS) {
        pm_tcb_t* allocator = &g_kernel.tasks[allocator_id];
        
        bool is_abusive = (allocator->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY || 
                          bytes_needed > OOM_ABUSIVE_ALLOC_SIZE);
        
        if (is_abusive && allocator->type == TASK_TYPE_APP) {
            pm_kprintf("[OOM] ABUSIVE ALLOCATOR DETECTED!\n");
            pm_kprintf("[OOM] Killing allocator '%s'\n", allocator->name);
            
            pm_task_kill_brutal(allocator_id);
            g_kernel.oom_kills++;
            g_oom_stats.forced_kills++;
            g_oom_stats.abusive_kills++;
            g_oom_current_request.request_sent = false;
            
            pm_mem_compact();
            return false;
        }
    }
    
    // Try prevention first
    if (pm_oom_prevent(bytes_needed)) {
        pm_kprintf("[OOM] Prevention succeeded\n");
        return false;
    }
    
    // Check if there's an active OOM request waiting
    if (g_oom_current_request.request_sent) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint32_t elapsed = now - g_oom_current_request.request_time_ms;
        
        if (elapsed < OOM_REQUEST_TIMEOUT_MS) {
            pm_kprintf("[OOM] Waiting for graceful cleanup...\n");
            return true;  // Caller should retry
        }
        
        pm_kprintf("[OOM] Graceful cleanup timed out!\n");
        pm_kprintf("[OOM] Killing victim: %u\n", g_oom_current_request.target_task_id);
        
        pm_task_kill_brutal(g_oom_current_request.target_task_id);
        g_oom_stats.forced_kills++;
        g_oom_current_request.request_sent = false;
        
        return false;
    }
    
    // Select victim
    pm_oom_victim_t victim = pm_oom_select_victim(bytes_needed);
    
    if (victim.task_id == 0xFFFFFFFF) {
        pm_kprintf("[OOM] NO KILLABLE APPLICATIONS!\n");
        pm_kernel_panic("OOM: No killable victims");
        return false;
    }
    
    pm_tcb_t* victim_task = &g_kernel.tasks[victim.task_id];
    
    pm_kprintf("[OOM] Selected victim: '%s' (%u KB, score=%d)\n",
               victim_task->name,
               (unsigned)(victim.memory_used / 1024),
               (int)victim.score);
    
    // Check if victim is also an abuser (no handler)
    if (victim_task->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY) {
        pm_kprintf("[OOM] Victim is also an abusive allocator. No handler.\n");
        victim.has_handler = false;
    }
    
    // Try graceful cleanup if handler exists
    if (victim.has_handler && pm_oom_request_cleanup(&victim, bytes_needed)) {
        return true;  // Waiting for cleanup
    }
    
    // Kill the victim
    pm_kprintf("[OOM] Killing '%s' (%u KB)\n",
               victim_task->name,
               (unsigned)(victim.memory_used / 1024));
    
    pm_task_kill_brutal(victim.task_id);
    g_kernel.oom_kills++;
    g_oom_stats.forced_kills++;
    g_oom_stats.total_bytes_reclaimed += victim.memory_used;
    
    return false;
}

// ============================================================================
// STATISTICS
// ============================================================================

const pm_oom_stats_t* pm_oom_get_stats(void) {
    return &g_oom_stats;
}

void pm_oom_print_stats(void) {
    pm_kprintf("=== OOM Statistics ===\n");
    pm_kprintf("Total events:    %u\n", g_oom_stats.total_events);
    pm_kprintf("Prevented:       %u\n", g_oom_stats.prevention_count);
    pm_kprintf("Requests sent:   %u\n", g_oom_stats.requests_sent);
    pm_kprintf("Voluntary:       %u\n", g_oom_stats.voluntary_releases);
    pm_kprintf("Forced kills:    %u\n", g_oom_stats.forced_kills);
    pm_kprintf("Abusive kills:   %u\n", g_oom_stats.abusive_kills);
    pm_kprintf("Bytes reclaimed: %u KB\n", g_oom_stats.total_bytes_reclaimed / 1024);
}

void pm_oom_reset_stats(void) {
    memset(&g_oom_stats, 0, sizeof(g_oom_stats));
}
