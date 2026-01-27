/**
 * PICOMIMI IPC Header - Full Message Passing with Wait Queues
 * Ported from v14.3.1 "Quiet Otter"
 */
#ifndef PICOMIMI_IPC_H
#define PICOMIMI_IPC_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// IPC INITIALIZATION
// ============================================================================

pm_result_t pm_ipc_init(void);
void pm_ipc_maintenance(void);

// ============================================================================
// MESSAGE PASSING
// ============================================================================

pm_result_t pm_ipc_send(pm_task_id_t target, pm_ipc_msg_type_t type,
                        const void* data, size_t len, uint32_t timeout_ms);
pm_result_t pm_ipc_send_urgent(pm_task_id_t target, pm_ipc_msg_type_t type,
                               const void* data, size_t len);
pm_result_t pm_ipc_receive(pm_ipc_message_t* msg, uint32_t timeout_ms);
pm_result_t pm_ipc_peek(pm_ipc_message_t* msg);
uint32_t pm_ipc_pending_count(void);

// Task queue management
void pm_ipc_init_task_queue(pm_task_id_t task_id);
void pm_ipc_cleanup_task(pm_task_id_t task_id);

// Statistics
const pm_ipc_stats_t* pm_ipc_get_stats(void);
void pm_ipc_print_stats(void);

// ============================================================================
// WAIT LIST OPERATIONS
// ============================================================================

void pm_wait_list_add(pm_task_wait_node_t** head, pm_tcb_t* task);
pm_task_id_t pm_wait_list_pop(pm_task_wait_node_t** head);
bool pm_wait_list_remove(pm_task_wait_node_t** head, pm_task_id_t task_id);

// ============================================================================
// MUTEX (with priority inheritance)
// ============================================================================

pm_result_t pm_mutex_init(pm_kmutex_t* mtx);
pm_result_t pm_mutex_lock(pm_kmutex_t* mtx, uint32_t timeout_ms);
pm_result_t pm_mutex_unlock(pm_kmutex_t* mtx);
bool pm_mutex_is_locked(pm_kmutex_t* mtx);
pm_task_id_t pm_mutex_get_owner(pm_kmutex_t* mtx);

// ============================================================================
// SEMAPHORE (counting)
// ============================================================================

pm_result_t pm_semaphore_init(pm_ksemaphore_t* sem, uint32_t initial_count, uint32_t max_count);
pm_result_t pm_semaphore_wait(pm_ksemaphore_t* sem, uint32_t timeout_ms);
pm_result_t pm_semaphore_signal(pm_ksemaphore_t* sem);
uint32_t pm_semaphore_get_count(pm_ksemaphore_t* sem);

// ============================================================================
// EVENT FLAGS
// ============================================================================

pm_result_t pm_event_init(pm_kevent_t* event);
pm_result_t pm_event_set(pm_kevent_t* event, uint32_t flags);
pm_result_t pm_event_clear(pm_kevent_t* event, uint32_t flags);
pm_result_t pm_event_wait(pm_kevent_t* event, uint32_t flags, bool wait_all,
                          uint32_t* actual_flags, uint32_t timeout_ms);
uint32_t pm_event_get(pm_kevent_t* event);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_IPC_H
