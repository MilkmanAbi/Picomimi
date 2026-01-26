/**
 * PICOMIMI-AXISOS Memory Management Header
 */
#ifndef PICOMIMI_MEMORY_H
#define PICOMIMI_MEMORY_H

#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialization
void pm_mem_init(void);

// Allocation
void* pm_kmalloc(size_t size);
void* pm_kcalloc(size_t count, size_t size);
void* pm_krealloc(void* ptr, size_t new_size);
void* pm_kmalloc_dma(size_t size);
void pm_kfree(void* ptr);

// Stats & Pressure
uint32_t pm_get_free_memory(void);
uint32_t pm_get_used_memory(void);
void pm_get_memory_stats(pm_mem_stats_t* stats);
pm_mem_pressure_t pm_get_memory_pressure(void);
bool pm_is_memory_warning(void);
bool pm_is_memory_critical(void);

// Maintenance
void pm_mem_compact(void);
void pm_calculate_fragmentation(void);

// OOM
void pm_register_oom_handler(pm_task_id_t task_id, pm_oom_callback_t callback);
void pm_unregister_oom_handler(pm_task_id_t task_id);
pm_oom_callback_t pm_get_oom_handler(pm_task_id_t task_id);
void pm_get_oom_stats(pm_oom_stats_t* stats);

// Task memory
uint32_t pm_get_task_memory(pm_task_id_t task_id);
void pm_release_task_memory(pm_task_id_t task_id);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_MEMORY_H
