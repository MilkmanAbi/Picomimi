/**
 * PICOMIMI-AXISOS v15.0.0-Alpha Memory Management
 * Complete port from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - Size-class segregated free lists (TLSF-inspired)
 * - Per-core small allocation pools
 * - Best-fit allocation for large blocks
 * - Memory pressure detection
 * - Coalescing and defragmentation
 * - OOM prevention
 */
#include "api/picomimi_kernel.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/critical_section.h"
#include <string.h>
#include <stdio.h>

extern pm_kernel_state_t g_kernel;

// ============================================================================
// INTERNAL STATE
// ============================================================================

static uint32_t heap_offset = 0;
static pm_oom_stats_t oom_stats = {0};
static pm_oom_velocity_t oom_velocity = {0};
static pm_oom_handler_t oom_handlers[12] = {0};  // MAX_OOM_HANDLERS

// Size class boundaries
static const uint32_t SIZE_CLASSES[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};

// ============================================================================
// SIZE CLASS HELPERS
// ============================================================================

// Find appropriate size class for a given size
static inline int find_size_class(size_t size) {
    if (size <= 64) return (size <= 32) ? 0 : 1;
    if (size <= 256) return (size <= 128) ? 2 : 3;
    if (size <= 1024) return (size <= 512) ? 4 : 5;
    if (size <= 4096) return (size <= 2048) ? 6 : 7;
    return -1;  // Too large for size classes
}

// Initialize size class free lists
static void size_class_init(void) {
    for (int i = 0; i < PICOMIMI_MEM_SIZE_CLASS_COUNT; i++) {
        g_kernel.size_class_lists[i].head = NULL;
        g_kernel.size_class_lists[i].count = 0;
        g_kernel.size_class_lists[i].total_size = 0;
    }
}

// ============================================================================
// SMALL POOL ALLOCATOR (Per-Core)
// ============================================================================

// Initialize small allocation pool
static void small_pool_init(pm_small_alloc_pool_t* pool) {
    memset(pool->pool, 0, PICOMIMI_MEM_SMALL_POOL_SIZE);
    memset(pool->bitmap, 0, sizeof(pool->bitmap));
    pool->used = 0;
    pool->alloc_count = 0;
    mutex_init(&pool->lock);
}

// Allocate from small pool (32-byte aligned slots)
static void* small_pool_alloc(pm_small_alloc_pool_t* pool, size_t size) {
    if (size > 256 || size == 0) return NULL;
    
    // Round up to 32-byte slot
    size_t slots_needed = (size + 31) / 32;
    if (slots_needed > 8) return NULL;  // Max 256 bytes
    
    mutex_enter_blocking(&pool->lock);
    
    uint32_t total_slots = PICOMIMI_MEM_SMALL_POOL_SIZE / 32;
    
    // Find consecutive free slots using bitmap
    for (uint32_t i = 0; i <= total_slots - slots_needed; i++) {
        bool found = true;
        
        for (uint32_t j = 0; j < slots_needed && found; j++) {
            uint32_t slot = i + j;
            uint32_t word = slot / 32;
            uint32_t bit = slot % 32;
            
            if (pool->bitmap[word] & (1U << bit)) {
                found = false;
                i = slot;  // Skip ahead
            }
        }
        
        if (found) {
            // Mark slots as used
            for (uint32_t j = 0; j < slots_needed; j++) {
                uint32_t slot = i + j;
                uint32_t word = slot / 32;
                uint32_t bit = slot % 32;
                pool->bitmap[word] |= (1U << bit);
            }
            
            pool->used += slots_needed * 32;
            pool->alloc_count++;
            g_kernel.mem_stats.small_allocs++;
            
            mutex_exit(&pool->lock);
            return &pool->pool[i * 32];
        }
    }
    
    mutex_exit(&pool->lock);
    return NULL;  // Pool full
}

// Free from small pool
static bool small_pool_free(pm_small_alloc_pool_t* pool, void* ptr, size_t size) {
    uint8_t* p = (uint8_t*)ptr;
    
    // Check if pointer is within pool
    if (p < pool->pool || p >= pool->pool + PICOMIMI_MEM_SMALL_POOL_SIZE) {
        return false;  // Not from this pool
    }
    
    size_t slot = (p - pool->pool) / 32;
    size_t slots_to_free = (size + 31) / 32;
    
    mutex_enter_blocking(&pool->lock);
    
    // Clear bitmap bits
    for (size_t j = 0; j < slots_to_free; j++) {
        uint32_t s = slot + j;
        uint32_t word = s / 32;
        uint32_t bit = s % 32;
        pool->bitmap[word] &= ~(1U << bit);
    }
    
    pool->used -= slots_to_free * 32;
    
    mutex_exit(&pool->lock);
    return true;
}

// ============================================================================
// MAIN HEAP ALLOCATOR (Best-Fit with Size Classes)
// ============================================================================

// Initialize memory manager
void pm_mem_init(void) {
    mutex_init(&g_kernel.mem_lock);
    critical_section_init(&g_kernel.mem_fast_lock);
    
    memset(g_kernel.heap, 0, PICOMIMI_HEAP_SIZE);
    memset(&g_kernel.mem_stats, 0, sizeof(pm_mem_stats_t));
    memset(g_kernel.mem_blocks, 0, sizeof(g_kernel.mem_blocks));
    
    heap_offset = 0;
    g_kernel.free_memory = PICOMIMI_HEAP_SIZE;
    g_kernel.used_memory = 0;
    g_kernel.mem_block_count = 0;
    g_kernel.largest_free_block = PICOMIMI_HEAP_SIZE;
    
    // Initialize size class lists
    size_class_init();
    
    // Initialize small pools
    small_pool_init(&g_kernel.small_pool_core0);
    small_pool_init(&g_kernel.small_pool_core1);
}

// Find a free memory block (best-fit)
static pm_mem_block_t* find_free_block(size_t size) {
    pm_mem_block_t* best = NULL;
    uint32_t best_size = UINT32_MAX;
    
    for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
        pm_mem_block_t* block = &g_kernel.mem_blocks[i];
        if (block->free && block->size >= size) {
            if (block->size < best_size) {
                best = block;
                best_size = block->size;
                if (best_size == size) break;  // Perfect fit
            }
        }
    }
    
    return best;
}

// Find or create a memory block
static pm_mem_block_t* alloc_block(size_t size) {
    // Check size class free lists first
    int sc = find_size_class(size);
    if (sc >= 0 && g_kernel.size_class_lists[sc].head != NULL) {
        pm_mem_block_t* block = g_kernel.size_class_lists[sc].head;
        g_kernel.size_class_lists[sc].head = NULL;  // Simple removal
        g_kernel.size_class_lists[sc].count--;
        g_kernel.mem_stats.cache_hits++;
        block->free = false;
        return block;
    }
    
    // Try best-fit in existing blocks
    pm_mem_block_t* block = find_free_block(size);
    if (block) {
        block->free = false;
        return block;
    }
    
    // Allocate from heap
    if (g_kernel.mem_block_count >= PICOMIMI_MAX_MEMORY_BLOCKS) {
        return NULL;  // No block slots
    }
    
    size_t aligned_size = (size + PICOMIMI_MEM_ALIGNMENT - 1) & ~(PICOMIMI_MEM_ALIGNMENT - 1);
    
    if (heap_offset + aligned_size > PICOMIMI_HEAP_SIZE) {
        return NULL;  // Out of heap
    }
    
    block = &g_kernel.mem_blocks[g_kernel.mem_block_count];
    block->addr = &g_kernel.heap[heap_offset];
    block->size = aligned_size;
    block->owner_id = g_kernel.current_task;
    block->alloc_seq = g_kernel.alloc_sequence++;
    block->size_class = (sc >= 0) ? sc : 0xFF;
    block->free = false;
    block->pinned = false;
    block->dma_safe = false;
    
    heap_offset += aligned_size;
    g_kernel.mem_block_count++;
    
    return block;
}

// Free a memory block
static void free_block(pm_mem_block_t* block) {
    if (!block || block->free) return;
    
    block->free = true;
    block->owner_id = 0;
    
    // Add to size class list if applicable
    int sc = find_size_class(block->size);
    if (sc >= 0) {
        g_kernel.size_class_lists[sc].count++;
        g_kernel.size_class_lists[sc].total_size += block->size;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void* pm_kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    // Align size
    size = (size + PICOMIMI_MEM_ALIGNMENT - 1) & ~(PICOMIMI_MEM_ALIGNMENT - 1);
    
    // Try small pool first for small allocations
    if (size <= 256) {
        uint8_t core = get_core_num();
        pm_small_alloc_pool_t* pool = (core == 0) ? 
            &g_kernel.small_pool_core0 : &g_kernel.small_pool_core1;
        
        void* ptr = small_pool_alloc(pool, size);
        if (ptr) {
            // Track per-task memory
            if (g_kernel.current_task < PICOMIMI_MAX_TASKS) {
                pm_tcb_t* task = &g_kernel.tasks[g_kernel.current_task];
                task->mem_used += size;
                if (task->mem_used > task->mem_peak) {
                    task->mem_peak = task->mem_used;
                }
            }
            return ptr;
        }
    }
    
    // Fall back to main allocator
    mutex_enter_blocking(&g_kernel.mem_lock);
    
    pm_mem_block_t* block = alloc_block(size);
    
    if (block) {
        g_kernel.used_memory += block->size;
        g_kernel.free_memory = PICOMIMI_HEAP_SIZE - heap_offset;
        g_kernel.mem_stats.total_allocs++;
        g_kernel.mem_stats.large_allocs++;
        g_kernel.mem_stats.active_blocks++;
        
        // Track peak usage
        if (g_kernel.used_memory > g_kernel.mem_stats.peak_usage) {
            g_kernel.mem_stats.peak_usage = g_kernel.used_memory;
        }
        
        // Track per-task memory
        if (g_kernel.current_task < PICOMIMI_MAX_TASKS) {
            pm_tcb_t* task = &g_kernel.tasks[g_kernel.current_task];
            task->mem_used += block->size;
            if (task->mem_used > task->mem_peak) {
                task->mem_peak = task->mem_used;
            }
        }
        
        mutex_exit(&g_kernel.mem_lock);
        return block->addr;
    }
    
    mutex_exit(&g_kernel.mem_lock);
    
    // Allocation failed
    g_kernel.mem_stats.failed_allocs++;
    pm_klog(PICOMIMI_LOG_LEVEL_ERROR, "kmalloc failed: %lu bytes", (unsigned long)size);
    
    return NULL;
}

void* pm_kcalloc(size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = pm_kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void pm_kfree(void* ptr) {
    if (!ptr) return;
    
    // Try small pools first
    if (small_pool_free(&g_kernel.small_pool_core0, ptr, 32)) {
        g_kernel.mem_stats.total_frees++;
        return;
    }
    if (small_pool_free(&g_kernel.small_pool_core1, ptr, 32)) {
        g_kernel.mem_stats.total_frees++;
        return;
    }
    
    // Find in main heap
    mutex_enter_blocking(&g_kernel.mem_lock);
    
    for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
        pm_mem_block_t* block = &g_kernel.mem_blocks[i];
        if (block->addr == ptr && !block->free) {
            g_kernel.used_memory -= block->size;
            g_kernel.free_memory += block->size;
            g_kernel.mem_stats.total_frees++;
            g_kernel.mem_stats.active_blocks--;
            
            // Track per-task memory
            if (block->owner_id < PICOMIMI_MAX_TASKS) {
                pm_tcb_t* task = &g_kernel.tasks[block->owner_id];
                if (task->mem_used >= block->size) {
                    task->mem_used -= block->size;
                }
            }
            
            free_block(block);
            mutex_exit(&g_kernel.mem_lock);
            return;
        }
    }
    
    mutex_exit(&g_kernel.mem_lock);
}

void* pm_krealloc(void* ptr, size_t new_size) {
    if (!ptr) return pm_kmalloc(new_size);
    if (new_size == 0) {
        pm_kfree(ptr);
        return NULL;
    }
    
    // Find current block
    mutex_enter_blocking(&g_kernel.mem_lock);
    
    size_t old_size = 0;
    for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
        pm_mem_block_t* block = &g_kernel.mem_blocks[i];
        if (block->addr == ptr && !block->free) {
            old_size = block->size;
            break;
        }
    }
    
    mutex_exit(&g_kernel.mem_lock);
    
    if (old_size == 0) {
        return pm_kmalloc(new_size);  // Wasn't tracked, just alloc new
    }
    
    // Allocate new block
    void* new_ptr = pm_kmalloc(new_size);
    if (new_ptr) {
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
        pm_kfree(ptr);
    }
    
    return new_ptr;
}

void* pm_kmalloc_dma(size_t size) {
    void* ptr = pm_kmalloc(size);
    if (ptr) {
        // Mark as DMA-safe (already aligned to 32 bytes)
        for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
            if (g_kernel.mem_blocks[i].addr == ptr) {
                g_kernel.mem_blocks[i].dma_safe = true;
                break;
            }
        }
    }
    return ptr;
}

// ============================================================================
// MEMORY PRESSURE & STATS
// ============================================================================

uint32_t pm_get_free_memory(void) {
    return g_kernel.free_memory;
}

uint32_t pm_get_used_memory(void) {
    return g_kernel.used_memory;
}

void pm_get_memory_stats(pm_mem_stats_t* stats) {
    if (stats) {
        mutex_enter_blocking(&g_kernel.mem_lock);
        *stats = g_kernel.mem_stats;
        mutex_exit(&g_kernel.mem_lock);
    }
}

pm_mem_pressure_t pm_get_memory_pressure(void) {
    uint32_t free = g_kernel.free_memory;
    
    if (free < PICOMIMI_MEM_CRITICAL_THRESHOLD) return MEM_PRESSURE_EMERGENCY;
    if (free < PICOMIMI_MEM_CRITICAL_THRESHOLD * 2) return MEM_PRESSURE_CRITICAL;
    if (free < PICOMIMI_MEM_WARNING_THRESHOLD) return MEM_PRESSURE_HIGH;
    if (free < PICOMIMI_HEAP_SIZE / 4) return MEM_PRESSURE_MODERATE;
    if (free < PICOMIMI_HEAP_SIZE / 2) return MEM_PRESSURE_LOW;
    return MEM_PRESSURE_NONE;
}

bool pm_is_memory_warning(void) {
    return g_kernel.free_memory < PICOMIMI_MEM_WARNING_THRESHOLD;
}

bool pm_is_memory_critical(void) {
    return g_kernel.free_memory < PICOMIMI_MEM_CRITICAL_THRESHOLD;
}

// ============================================================================
// COALESCING & DEFRAGMENTATION
// ============================================================================

void pm_mem_compact(void) {
    mutex_enter_blocking(&g_kernel.mem_lock);
    
    uint32_t coalesced = 0;
    
    // Simple coalescing: merge adjacent free blocks
    for (uint32_t i = 0; i < g_kernel.mem_block_count - 1; i++) {
        pm_mem_block_t* curr = &g_kernel.mem_blocks[i];
        pm_mem_block_t* next = &g_kernel.mem_blocks[i + 1];
        
        if (curr->free && next->free) {
            // Check if adjacent
            uint8_t* curr_end = (uint8_t*)curr->addr + curr->size;
            if (curr_end == next->addr) {
                // Merge
                curr->size += next->size;
                
                // Remove next block by shifting
                for (uint32_t j = i + 1; j < g_kernel.mem_block_count - 1; j++) {
                    g_kernel.mem_blocks[j] = g_kernel.mem_blocks[j + 1];
                }
                g_kernel.mem_block_count--;
                coalesced++;
                i--;  // Recheck current
            }
        }
    }
    
    // Update largest free block
    g_kernel.largest_free_block = 0;
    for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
        if (g_kernel.mem_blocks[i].free && 
            g_kernel.mem_blocks[i].size > g_kernel.largest_free_block) {
            g_kernel.largest_free_block = g_kernel.mem_blocks[i].size;
        }
    }
    
    g_kernel.mem_stats.emergency_compactions++;
    
    mutex_exit(&g_kernel.mem_lock);
    
    if (coalesced > 0) {
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Memory compact: merged %lu blocks", (unsigned long)coalesced);
    }
}

// Calculate fragmentation percentage
void pm_calculate_fragmentation(void) {
    if (g_kernel.free_memory == 0) {
        g_kernel.fragmentation_pct = 0;
        return;
    }
    
    mutex_enter_blocking(&g_kernel.mem_lock);
    
    uint32_t free_blocks = 0;
    uint32_t total_free = 0;
    
    for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
        if (g_kernel.mem_blocks[i].free) {
            free_blocks++;
            total_free += g_kernel.mem_blocks[i].size;
        }
    }
    
    if (free_blocks <= 1) {
        g_kernel.fragmentation_pct = 0;
    } else {
        // Fragmentation = 100 * (1 - largest_free / total_free)
        g_kernel.fragmentation_pct = (uint8_t)(100 - 
            (100 * g_kernel.largest_free_block / total_free));
    }
    
    g_kernel.mem_stats.fragmentation_pct = g_kernel.fragmentation_pct;
    
    mutex_exit(&g_kernel.mem_lock);
}

// ============================================================================
// OOM HANDLING
// ============================================================================

void pm_register_oom_handler(pm_task_id_t task_id, pm_oom_callback_t callback) {
    for (int i = 0; i < 12; i++) {
        if (!oom_handlers[i].active) {
            oom_handlers[i].task_id = task_id;
            oom_handlers[i].callback = callback;
            oom_handlers[i].active = true;
            return;
        }
    }
}

void pm_unregister_oom_handler(pm_task_id_t task_id) {
    for (int i = 0; i < 12; i++) {
        if (oom_handlers[i].active && oom_handlers[i].task_id == task_id) {
            oom_handlers[i].active = false;
            return;
        }
    }
}

pm_oom_callback_t pm_get_oom_handler(pm_task_id_t task_id) {
    for (int i = 0; i < 12; i++) {
        if (oom_handlers[i].active && oom_handlers[i].task_id == task_id) {
            return oom_handlers[i].callback;
        }
    }
    return NULL;
}

// Get OOM statistics
void pm_get_oom_stats(pm_oom_stats_t* stats) {
    if (stats) {
        *stats = oom_stats;
    }
}

// ============================================================================
// TASK MEMORY TRACKING
// ============================================================================

uint32_t pm_get_task_memory(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return 0;
    return g_kernel.tasks[task_id].mem_used;
}

void pm_release_task_memory(pm_task_id_t task_id) {
    mutex_enter_blocking(&g_kernel.mem_lock);
    
    for (uint32_t i = 0; i < g_kernel.mem_block_count; i++) {
        pm_mem_block_t* block = &g_kernel.mem_blocks[i];
        if (block->owner_id == task_id && !block->free) {
            g_kernel.used_memory -= block->size;
            g_kernel.free_memory += block->size;
            free_block(block);
        }
    }
    
    mutex_exit(&g_kernel.mem_lock);
}
