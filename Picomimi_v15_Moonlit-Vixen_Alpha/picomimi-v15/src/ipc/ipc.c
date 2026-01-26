/**
 * PICOMIMI IPC Implementation - Full Message Passing with Wait Queues
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - O(1) message pool allocation
 * - Per-task priority message queues
 * - Broadcast messaging
 * - Wait queues for synchronization primitives
 * - Priority inheritance for mutexes
 * - Counting semaphores with wait lists
 * - Event flags with multiple wait modes
 */

#include "ipc/ipc.h"
#include "api/picomimi_kernel.h"
#include "kernel/scheduler.h"
#include "pico/sync.h"
#include "pico/time.h"
#include <string.h>

// External kernel state
extern pm_kernel_state_t g_kernel;
extern pm_core_scheduler_t g_core0_sched;

// IPC constants
#define IPC_NULL_MSG            0xFFFF
#define IPC_TARGET_BROADCAST    0xFFFFFFFF
#define IPC_PRIORITY_RT         15

// Global IPC statistics
static pm_ipc_stats_t g_ipc_stats;

// ============================================================================
// INTERRUPT CONTROL
// ============================================================================

static inline uint32_t disable_all_interrupts(void) {
    return save_and_disable_interrupts();
}

static inline void enable_all_interrupts(uint32_t state) {
    restore_interrupts(state);
}

// ============================================================================
// WAIT LIST OPERATIONS
// ============================================================================

/**
 * Add a task to a wait list (FIFO order)
 */
void pm_wait_list_add(pm_task_wait_node_t** head, pm_tcb_t* task) {
    pm_task_wait_node_t* node = &task->wait_node;
    node->task_id = task->id;
    node->next = NULL;
    
    if (*head == NULL) {
        *head = node;
    } else {
        pm_task_wait_node_t* current = *head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = node;
    }
}

/**
 * Pop the first task from a wait list
 * Returns task ID or PM_INVALID_TASK if empty
 */
pm_task_id_t pm_wait_list_pop(pm_task_wait_node_t** head) {
    if (*head == NULL) {
        return PM_INVALID_TASK;
    }
    
    pm_task_wait_node_t* node = *head;
    pm_task_id_t task_id = node->task_id;
    *head = node->next;
    
    return task_id;
}

/**
 * Remove a specific task from a wait list
 */
bool pm_wait_list_remove(pm_task_wait_node_t** head, pm_task_id_t task_id) {
    pm_task_wait_node_t* prev = NULL;
    pm_task_wait_node_t* node = *head;
    
    while (node != NULL) {
        if (node->task_id == task_id) {
            if (prev == NULL) {
                *head = node->next;
            } else {
                prev->next = node->next;
            }
            return true;
        }
        prev = node;
        node = node->next;
    }
    
    return false;
}

// ============================================================================
// IPC INITIALIZATION
// ============================================================================

pm_result_t pm_ipc_init(void) {
    mutex_init(&g_kernel.ipc_manager.lock);
    critical_section_init(&g_kernel.ipc_manager.rt_section);
    
    g_kernel.ipc_manager.sequence_counter = 0;
    g_kernel.ipc_manager.dropped_messages = 0;
    g_kernel.ipc_manager.total_sent = 0;
    g_kernel.ipc_manager.total_received = 0;
    g_kernel.ipc_manager.free_list_head = PICOMIMI_MAX_IPC_MESSAGES - 1;
    
    // Initialize message pool and free list
    for (uint32_t i = 0; i < PICOMIMI_MAX_IPC_MESSAGES; i++) {
        g_kernel.ipc_manager.message_pool[i].in_use = false;
        g_kernel.ipc_manager.free_list[i] = i;
    }
    
    memset(&g_ipc_stats, 0, sizeof(g_ipc_stats));
    
    return PM_OK;
}

// ============================================================================
// MESSAGE POOL MANAGEMENT
// ============================================================================

/**
 * Allocate a message from the pool (O(1))
 */
static int16_t ipc_alloc_message(void) {
    mutex_enter_blocking(&g_kernel.ipc_manager.lock);
    
    if (g_kernel.ipc_manager.free_list_head < 0) {
        mutex_exit(&g_kernel.ipc_manager.lock);
        g_ipc_stats.messages_dropped_pool_full++;
        return -1;
    }
    
    int16_t index = g_kernel.ipc_manager.free_list[g_kernel.ipc_manager.free_list_head--];
    g_kernel.ipc_manager.message_pool[index].in_use = true;
    
    mutex_exit(&g_kernel.ipc_manager.lock);
    return index;
}

/**
 * Free a message back to the pool (O(1))
 */
static void ipc_free_message(uint16_t index) {
    if (index >= PICOMIMI_MAX_IPC_MESSAGES) return;
    
    mutex_enter_blocking(&g_kernel.ipc_manager.lock);
    
    pm_ipc_message_t* msg = &g_kernel.ipc_manager.message_pool[index];
    msg->in_use = false;
    msg->next = IPC_NULL_MSG;
    g_kernel.ipc_manager.free_list[++g_kernel.ipc_manager.free_list_head] = index;
    
    mutex_exit(&g_kernel.ipc_manager.lock);
}

// ============================================================================
// MESSAGE PASSING
// ============================================================================

/**
 * Send a message to a specific task (internal)
 */
static bool ipc_send_raw(pm_task_id_t sender_id, pm_task_id_t target_id,
                         pm_ipc_msg_type_t type, const void* data, 
                         size_t size, uint8_t priority) {
    
    // Allocate message
    int16_t msg_index = ipc_alloc_message();
    if (msg_index < 0) {
        return false;
    }
    
    // Fill message
    pm_ipc_message_t* msg = &g_kernel.ipc_manager.message_pool[msg_index];
    msg->sender_id = sender_id;
    msg->target_id = target_id;
    msg->type = type;
    msg->priority = priority;
    msg->timestamp_ms = to_ms_since_boot(get_absolute_time());
    msg->sequence = g_kernel.ipc_manager.sequence_counter++;
    msg->next = IPC_NULL_MSG;
    
    if (data && size > 0) {
        size_t copy_size = (size > PICOMIMI_IPC_MSG_SIZE) ? PICOMIMI_IPC_MSG_SIZE : size;
        memcpy(msg->data, data, copy_size);
    }
    
    // Find target task
    if (target_id >= PICOMIMI_MAX_TASKS) {
        ipc_free_message(msg_index);
        return false;
    }
    
    pm_tcb_t* target_task = &g_kernel.tasks[target_id];
    
    if (target_task->id == PM_INVALID_TASK || 
        target_task->state == TASK_STATE_TERMINATED ||
        target_task->state == TASK_STATE_ZOMBIE) {
        ipc_free_message(msg_index);
        return false;
    }
    
    uint32_t irq = disable_all_interrupts();
    
    // Check if target queue is full
    if (target_task->ipc.message_count >= PICOMIMI_MAX_IPC_MESSAGES) {
        enable_all_interrupts(irq);
        ipc_free_message(msg_index);
        g_ipc_stats.messages_dropped_task_full++;
        return false;
    }
    
    // Add to target's priority queue
    uint16_t* head = &target_task->ipc.priority_lists_head[priority];
    msg->next = *head;
    *head = msg_index;
    
    target_task->ipc.priority_bitmap |= (1U << priority);
    target_task->ipc.message_count++;
    
    // Wake target if waiting
    if (target_task->state == TASK_STATE_WAITING) {
        target_task->state = TASK_STATE_READY;
        pm_sched_bitmap_add(&g_core0_sched.runnable, target_id, target_task->priority);
    }
    
    enable_all_interrupts(irq);
    
    g_kernel.ipc_manager.total_sent++;
    g_ipc_stats.messages_sent++;
    
    if (priority >= IPC_PRIORITY_RT) {
        g_ipc_stats.rt_messages_sent++;
    }
    
    return true;
}

/**
 * Public API: Send a message
 */
pm_result_t pm_ipc_send(pm_task_id_t target, pm_ipc_msg_type_t type,
                        const void* data, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;  // TODO: implement timeout
    
    if (len > PICOMIMI_IPC_MSG_SIZE) {
        return PM_ERROR_INVALID;
    }
    
    pm_task_id_t sender_id = g_kernel.current_task;
    uint8_t priority = g_kernel.tasks[sender_id].priority;
    
    // Handle broadcast
    if (target == IPC_TARGET_BROADCAST) {
        bool all_ok = true;
        g_ipc_stats.broadcasts_sent++;
        
        for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
            pm_tcb_t* task = &g_kernel.tasks[i];
            if (task->id != PM_INVALID_TASK && 
                task->id != sender_id &&
                task->state != TASK_STATE_TERMINATED &&
                task->state != TASK_STATE_ZOMBIE) {
                if (!ipc_send_raw(sender_id, task->id, type, data, len, priority)) {
                    all_ok = false;
                }
            }
        }
        
        return all_ok ? PM_OK : PM_ERROR_BUSY;
    }
    
    // Single target
    if (ipc_send_raw(sender_id, target, type, data, len, priority)) {
        return PM_OK;
    }
    
    return PM_ERROR_FULL;
}

/**
 * Public API: Send urgent message (high priority)
 */
pm_result_t pm_ipc_send_urgent(pm_task_id_t target, pm_ipc_msg_type_t type,
                               const void* data, size_t len) {
    if (len > PICOMIMI_IPC_MSG_SIZE) {
        return PM_ERROR_INVALID;
    }
    
    pm_task_id_t sender_id = g_kernel.current_task;
    
    if (ipc_send_raw(sender_id, target, type, data, len, IPC_PRIORITY_RT)) {
        g_kernel.ipc_manager.rt_messages++;
        return PM_OK;
    }
    
    return PM_ERROR_FULL;
}

/**
 * Public API: Receive a message
 */
pm_result_t pm_ipc_receive(pm_ipc_message_t* msg, uint32_t timeout_ms) {
    if (!msg) return PM_ERROR_INVALID;
    
    pm_task_id_t task_id = g_kernel.current_task;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    
    while (1) {
        uint32_t irq = disable_all_interrupts();
        
        // Check for messages
        if (task->ipc.priority_bitmap != 0) {
            // Get highest priority message (FLS finds highest set bit)
            int highest_priority = 31 - __builtin_clz(task->ipc.priority_bitmap);
            uint16_t* head = &task->ipc.priority_lists_head[highest_priority];
            uint16_t msg_index = *head;
            
            if (msg_index != IPC_NULL_MSG) {
                pm_ipc_message_t* pool_msg = &g_kernel.ipc_manager.message_pool[msg_index];
                
                // Remove from queue
                *head = pool_msg->next;
                if (*head == IPC_NULL_MSG) {
                    task->ipc.priority_bitmap &= ~(1U << highest_priority);
                }
                task->ipc.message_count--;
                
                // Copy message out
                memcpy(msg, pool_msg, sizeof(pm_ipc_message_t));
                
                enable_all_interrupts(irq);
                
                // Free the message
                ipc_free_message(msg_index);
                
                g_kernel.ipc_manager.total_received++;
                g_ipc_stats.messages_received++;
                
                if (highest_priority >= IPC_PRIORITY_RT) {
                    g_ipc_stats.rt_messages_received++;
                }
                
                return PM_OK;
            }
            
            // Bitmap/queue mismatch, fix it
            task->ipc.priority_bitmap &= ~(1U << highest_priority);
        }
        
        enable_all_interrupts(irq);
        
        // Check timeout
        if (timeout_ms == 0) {
            return PM_ERROR_EMPTY;
        }
        
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - start_ms;
        if (elapsed >= timeout_ms) {
            return PM_ERROR_TIMEOUT;
        }
        
        // Yield and retry
        pm_task_yield();
    }
}

/**
 * Public API: Peek at next message without removing
 */
pm_result_t pm_ipc_peek(pm_ipc_message_t* msg) {
    if (!msg) return PM_ERROR_INVALID;
    
    pm_task_id_t task_id = g_kernel.current_task;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t irq = disable_all_interrupts();
    
    if (task->ipc.priority_bitmap == 0) {
        enable_all_interrupts(irq);
        return PM_ERROR_EMPTY;
    }
    
    int highest_priority = 31 - __builtin_clz(task->ipc.priority_bitmap);
    uint16_t msg_index = task->ipc.priority_lists_head[highest_priority];
    
    if (msg_index == IPC_NULL_MSG) {
        enable_all_interrupts(irq);
        return PM_ERROR_EMPTY;
    }
    
    pm_ipc_message_t* pool_msg = &g_kernel.ipc_manager.message_pool[msg_index];
    memcpy(msg, pool_msg, sizeof(pm_ipc_message_t));
    
    enable_all_interrupts(irq);
    return PM_OK;
}

/**
 * Public API: Get pending message count
 */
uint32_t pm_ipc_pending_count(void) {
    pm_task_id_t task_id = g_kernel.current_task;
    return g_kernel.tasks[task_id].ipc.message_count;
}

/**
 * Initialize a task's IPC queue
 */
void pm_ipc_init_task_queue(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    task->ipc.priority_bitmap = 0;
    task->ipc.message_count = 0;
    task->ipc.rt_message_count = 0;
    
    for (int i = 0; i < PICOMIMI_SCHED_PRIORITY_LEVELS; i++) {
        task->ipc.priority_lists_head[i] = IPC_NULL_MSG;
    }
}

/**
 * Clean up a task's pending messages
 */
void pm_ipc_cleanup_task(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return;
    
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t irq = disable_all_interrupts();
    
    // Free all pending messages
    for (int p = 0; p < PICOMIMI_SCHED_PRIORITY_LEVELS; p++) {
        uint16_t msg_index = task->ipc.priority_lists_head[p];
        while (msg_index != IPC_NULL_MSG) {
            pm_ipc_message_t* msg = &g_kernel.ipc_manager.message_pool[msg_index];
            uint16_t next = msg->next;
            
            enable_all_interrupts(irq);
            ipc_free_message(msg_index);
            irq = disable_all_interrupts();
            
            msg_index = next;
        }
        task->ipc.priority_lists_head[p] = IPC_NULL_MSG;
    }
    
    task->ipc.priority_bitmap = 0;
    task->ipc.message_count = 0;
    
    enable_all_interrupts(irq);
}

// ============================================================================
// MUTEX WITH PRIORITY INHERITANCE
// ============================================================================

pm_result_t pm_mutex_init(pm_kmutex_t* mtx) {
    if (!mtx) return PM_ERROR_INVALID;
    
    mtx->locked = false;
    mtx->owner_id = PM_INVALID_TASK;
    mtx->original_priority = 0;
    mtx->wait_list_head = NULL;
    
    return PM_OK;
}

pm_result_t pm_mutex_lock(pm_kmutex_t* mtx, uint32_t timeout_ms) {
    if (!mtx) return PM_ERROR_INVALID;
    
    pm_task_id_t task_id = g_kernel.current_task;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t irq = disable_all_interrupts();
    
    // Fast path: mutex is free
    if (!mtx->locked) {
        mtx->locked = true;
        mtx->owner_id = task_id;
        enable_all_interrupts(irq);
        return PM_OK;
    }
    
    // Recursive lock check
    if (mtx->owner_id == task_id) {
        enable_all_interrupts(irq);
        return PM_OK;  // Already own it
    }
    
    // Try without blocking
    if (timeout_ms == 0) {
        enable_all_interrupts(irq);
        return PM_ERROR_TIMEOUT;
    }
    
    // Priority inheritance: boost owner if needed
    if (mtx->owner_id < PICOMIMI_MAX_TASKS) {
        pm_tcb_t* owner_task = &g_kernel.tasks[mtx->owner_id];
        
        if (task->priority > owner_task->priority) {
            if (mtx->original_priority == 0) {
                mtx->original_priority = owner_task->priority;
            }
            owner_task->priority = task->priority;
            pm_scheduler_update_task_priority(mtx->owner_id);
        }
    }
    
    // Add to wait list and block
    pm_wait_list_add(&mtx->wait_list_head, task);
    task->state = TASK_STATE_WAITING;
    task->wake_time_ms = (timeout_ms == UINT32_MAX) ? 0 : 
                         to_ms_since_boot(get_absolute_time()) + timeout_ms;
    
    pm_sched_bitmap_remove(&g_core0_sched.runnable, task_id, task->priority);
    
    enable_all_interrupts(irq);
    
    // Yield to let others run
    pm_task_yield();
    
    // Check if we got the mutex
    if (mtx->owner_id == task_id) {
        return PM_OK;
    }
    
    return PM_ERROR_TIMEOUT;
}

pm_result_t pm_mutex_unlock(pm_kmutex_t* mtx) {
    if (!mtx) return PM_ERROR_INVALID;
    
    pm_task_id_t task_id = g_kernel.current_task;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t irq = disable_all_interrupts();
    
    // Verify ownership
    if (!mtx->locked || mtx->owner_id != task_id) {
        enable_all_interrupts(irq);
        return PM_ERROR_DENIED;
    }
    
    // Restore original priority if it was boosted
    if (mtx->original_priority != 0) {
        task->priority = mtx->original_priority;
        mtx->original_priority = 0;
        pm_scheduler_update_task_priority(task_id);
    }
    
    // Wake next waiter
    pm_task_id_t next_task_id = pm_wait_list_pop(&mtx->wait_list_head);
    
    if (next_task_id != PM_INVALID_TASK) {
        mtx->owner_id = next_task_id;
        pm_task_wake(next_task_id);
    } else {
        mtx->locked = false;
        mtx->owner_id = PM_INVALID_TASK;
    }
    
    enable_all_interrupts(irq);
    
    pm_scheduler_check_preemption();
    
    return PM_OK;
}

bool pm_mutex_is_locked(pm_kmutex_t* mtx) {
    return mtx ? mtx->locked : false;
}

pm_task_id_t pm_mutex_get_owner(pm_kmutex_t* mtx) {
    return mtx ? mtx->owner_id : PM_INVALID_TASK;
}

// ============================================================================
// COUNTING SEMAPHORE
// ============================================================================

pm_result_t pm_semaphore_init(pm_ksemaphore_t* sem, uint32_t initial_count, uint32_t max_count) {
    if (!sem || max_count == 0) return PM_ERROR_INVALID;
    
    sem->count = (int32_t)initial_count;
    sem->max_count = max_count;
    sem->wait_list_head = NULL;
    
    return PM_OK;
}

pm_result_t pm_semaphore_wait(pm_ksemaphore_t* sem, uint32_t timeout_ms) {
    if (!sem) return PM_ERROR_INVALID;
    
    pm_task_id_t task_id = g_kernel.current_task;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t irq = disable_all_interrupts();
    
    // Decrement and check
    sem->count--;
    
    if (sem->count >= 0) {
        enable_all_interrupts(irq);
        return PM_OK;
    }
    
    // Need to wait
    if (timeout_ms == 0) {
        sem->count++;  // Restore count
        enable_all_interrupts(irq);
        return PM_ERROR_TIMEOUT;
    }
    
    // Add to wait list and block
    pm_wait_list_add(&sem->wait_list_head, task);
    task->state = TASK_STATE_WAITING;
    task->wake_time_ms = (timeout_ms == UINT32_MAX) ? 0 :
                         to_ms_since_boot(get_absolute_time()) + timeout_ms;
    
    pm_sched_bitmap_remove(&g_core0_sched.runnable, task_id, task->priority);
    
    enable_all_interrupts(irq);
    
    pm_task_yield();
    
    // Check if we got the semaphore
    if (sem->count >= 0) {
        return PM_OK;
    }
    
    // Timeout - restore count
    irq = disable_all_interrupts();
    sem->count++;
    enable_all_interrupts(irq);
    
    return PM_ERROR_TIMEOUT;
}

pm_result_t pm_semaphore_signal(pm_ksemaphore_t* sem) {
    if (!sem) return PM_ERROR_INVALID;
    
    uint32_t irq = disable_all_interrupts();
    
    sem->count++;
    if (sem->count > (int32_t)sem->max_count) {
        sem->count = (int32_t)sem->max_count;
    }
    
    // Wake a waiter if count was negative
    if (sem->count <= 0) {
        pm_task_id_t task_to_wake = pm_wait_list_pop(&sem->wait_list_head);
        if (task_to_wake != PM_INVALID_TASK) {
            pm_task_wake(task_to_wake);
        }
    }
    
    enable_all_interrupts(irq);
    
    pm_scheduler_check_preemption();
    
    return PM_OK;
}

uint32_t pm_semaphore_get_count(pm_ksemaphore_t* sem) {
    if (!sem) return 0;
    return (sem->count >= 0) ? (uint32_t)sem->count : 0;
}

// ============================================================================
// EVENT FLAGS
// ============================================================================

pm_result_t pm_event_init(pm_kevent_t* event) {
    if (!event) return PM_ERROR_INVALID;
    
    event->flags = 0;
    event->wait_list_head = NULL;
    
    return PM_OK;
}

pm_result_t pm_event_set(pm_kevent_t* event, uint32_t flags) {
    if (!event) return PM_ERROR_INVALID;
    
    uint32_t irq = disable_all_interrupts();
    
    event->flags |= flags;
    
    // Check waiters
    pm_task_wait_node_t* node = event->wait_list_head;
    pm_task_wait_node_t* prev = NULL;
    
    while (node != NULL) {
        bool condition_met = false;
        
        if (node->wait_mode == 0) {  // WAIT_ANY
            condition_met = (event->flags & node->wait_flags) != 0;
        } else {  // WAIT_ALL
            condition_met = (event->flags & node->wait_flags) == node->wait_flags;
        }
        
        if (condition_met) {
            pm_task_wake(node->task_id);
            
            if (node->clear_on_exit) {
                event->flags &= ~node->wait_flags;
            }
            
            // Remove from list
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
    
    enable_all_interrupts(irq);
    
    pm_scheduler_check_preemption();
    
    return PM_OK;
}

pm_result_t pm_event_clear(pm_kevent_t* event, uint32_t flags) {
    if (!event) return PM_ERROR_INVALID;
    
    uint32_t irq = disable_all_interrupts();
    event->flags &= ~flags;
    enable_all_interrupts(irq);
    
    return PM_OK;
}

pm_result_t pm_event_wait(pm_kevent_t* event, uint32_t flags, bool wait_all,
                          uint32_t* actual_flags, uint32_t timeout_ms) {
    if (!event) return PM_ERROR_INVALID;
    
    pm_task_id_t task_id = g_kernel.current_task;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    
    uint32_t irq = disable_all_interrupts();
    
    // Check if condition already met
    uint32_t current_flags = event->flags;
    bool condition_met = wait_all ? 
                         ((current_flags & flags) == flags) :
                         ((current_flags & flags) != 0);
    
    if (condition_met) {
        if (actual_flags) *actual_flags = current_flags & flags;
        enable_all_interrupts(irq);
        return PM_OK;
    }
    
    // Need to wait
    if (timeout_ms == 0) {
        enable_all_interrupts(irq);
        return PM_ERROR_TIMEOUT;
    }
    
    // Set up wait node
    pm_task_wait_node_t* node = &task->wait_node;
    node->task_id = task_id;
    node->wait_flags = flags;
    node->wait_mode = wait_all ? 1 : 0;
    node->clear_on_exit = false;  // Don't auto-clear
    node->next = event->wait_list_head;
    event->wait_list_head = node;
    
    task->state = TASK_STATE_WAITING;
    task->wake_time_ms = (timeout_ms == UINT32_MAX) ? 0 :
                         to_ms_since_boot(get_absolute_time()) + timeout_ms;
    
    pm_sched_bitmap_remove(&g_core0_sched.runnable, task_id, task->priority);
    
    enable_all_interrupts(irq);
    
    pm_task_yield();
    
    // Check result
    if (actual_flags) *actual_flags = event->flags & flags;
    
    condition_met = wait_all ?
                    ((event->flags & flags) == flags) :
                    ((event->flags & flags) != 0);
    
    return condition_met ? PM_OK : PM_ERROR_TIMEOUT;
}

uint32_t pm_event_get(pm_kevent_t* event) {
    return event ? event->flags : 0;
}

// ============================================================================
// IPC STATISTICS
// ============================================================================

const pm_ipc_stats_t* pm_ipc_get_stats(void) {
    return &g_ipc_stats;
}

void pm_ipc_print_stats(void) {
    mutex_enter_blocking(&g_kernel.ipc_manager.lock);
    
    uint16_t total_in_use = PICOMIMI_MAX_IPC_MESSAGES - 
                            (g_kernel.ipc_manager.free_list_head + 1);
    
    mutex_exit(&g_kernel.ipc_manager.lock);
    
    pm_kprintf("=== IPC Statistics ===\n");
    pm_kprintf("Messages sent:     %lu\n", g_ipc_stats.messages_sent);
    pm_kprintf("Messages received: %lu\n", g_ipc_stats.messages_received);
    pm_kprintf("RT sent:           %lu\n", g_ipc_stats.rt_messages_sent);
    pm_kprintf("RT received:       %lu\n", g_ipc_stats.rt_messages_received);
    pm_kprintf("Broadcasts:        %u\n", g_ipc_stats.broadcasts_sent);
    pm_kprintf("Dropped (pool):    %u\n", g_ipc_stats.messages_dropped_pool_full);
    pm_kprintf("Dropped (task):    %u\n", g_ipc_stats.messages_dropped_task_full);
    pm_kprintf("Pool in use:       %u/%u\n", total_in_use, PICOMIMI_MAX_IPC_MESSAGES);
    pm_kprintf("Max queue depth:   %u\n", g_ipc_stats.max_queue_depth_global);
}

void pm_ipc_maintenance(void) {
    mutex_enter_blocking(&g_kernel.ipc_manager.lock);
    
    uint16_t total_in_use = PICOMIMI_MAX_IPC_MESSAGES - 
                            (g_kernel.ipc_manager.free_list_head + 1);
    
    mutex_exit(&g_kernel.ipc_manager.lock);
    
    // Update statistics
    g_ipc_stats.avg_queue_depth_global = (g_ipc_stats.avg_queue_depth_global * 0.95f) +
                                          (total_in_use * 0.05f);
    
    if (total_in_use > g_ipc_stats.max_queue_depth_global) {
        g_ipc_stats.max_queue_depth_global = total_in_use;
    }
}
