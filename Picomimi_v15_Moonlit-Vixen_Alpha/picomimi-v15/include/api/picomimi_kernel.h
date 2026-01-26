/**
 * PICOMIMI Kernel API Header
 */
#ifndef PICOMIMI_KERNEL_H
#define PICOMIMI_KERNEL_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"
#include "pico/mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cache alignment for kernel state (if not defined in config)
#ifndef PICOMIMI_CACHE_ALIGNED
#define PICOMIMI_CACHE_ALIGNED
#endif

// ============================================================================
// KERNEL STATE
// ============================================================================

typedef struct {
    // Tasks
    pm_tcb_t tasks[PICOMIMI_MAX_TASKS];
    uint32_t task_count;
    pm_task_id_t current_task;
    
    // Memory
    uint8_t heap[PICOMIMI_HEAP_SIZE];
    uint32_t free_memory;
    uint32_t used_memory;
    pm_mem_stats_t mem_stats;
    
    // Governor
    pm_governor_state_t governor;
    
    // Logging
    pm_log_entry_t log_buffer[PICOMIMI_MAX_LOG_ENTRIES];
    uint32_t log_head;
    uint32_t log_tail;
    uint32_t log_count;
    uint8_t log_level;
    
    // Timing
    uint64_t boot_time_us;
    uint32_t uptime_ms;
    
    // State flags
    bool initialized;
    bool running;
    bool panic_mode;
} pm_kernel_state_t;

// ============================================================================
// KERNEL API
// ============================================================================

pm_result_t pm_kernel_init(void);
pm_result_t pm_kernel_start(void);
void pm_kernel_tick(void);
bool pm_kernel_is_running(void);

// ============================================================================
// TASK API
// ============================================================================

pm_task_id_t pm_task_create(const char* name, pm_task_entry_t entry, void* arg,
                            uint8_t priority, pm_core_affinity_t affinity);
pm_task_id_t pm_task_create_callback(const char* name, pm_module_callbacks_t* callbacks,
                                      void* arg, uint8_t priority, pm_core_affinity_t affinity);
pm_result_t pm_task_terminate(pm_task_id_t task_id);
pm_result_t pm_task_suspend(pm_task_id_t task_id);
pm_result_t pm_task_resume(pm_task_id_t task_id);
pm_result_t pm_task_sleep(uint32_t ms);
void pm_task_yield(void);
void pm_task_exit(void);
pm_task_id_t pm_task_get_current(void);
pm_tcb_t* pm_task_get(pm_task_id_t task_id);
pm_result_t pm_task_set_priority(pm_task_id_t task_id, uint8_t priority);

// ============================================================================
// MEMORY API
// ============================================================================

void* pm_kmalloc(size_t size);
void* pm_kcalloc(size_t count, size_t size);
void pm_kfree(void* ptr);
void* pm_krealloc(void* ptr, size_t new_size);
uint32_t pm_get_free_memory(void);
void pm_get_memory_stats(pm_mem_stats_t* stats);
pm_mem_pressure_t pm_get_memory_pressure(void);

// ============================================================================
// LOGGING API
// ============================================================================

void pm_klog(uint8_t level, const char* fmt, ...);
void pm_kprint(const char* str);
void pm_kprintf(const char* fmt, ...);

// ============================================================================
// TIME API
// ============================================================================

uint64_t pm_get_time_us(void);
uint32_t pm_get_time_ms(void);
void pm_delay_us(uint32_t us);
void pm_delay_ms(uint32_t ms);

// ============================================================================
// PANIC API
// ============================================================================

void pm_kernel_panic(const char* reason);
pm_panic_info_t* pm_get_panic_info(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_KERNEL_H
