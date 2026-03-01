/**
 * Picomimi Memory Management
 * 
 * Simple memory pools and allocator for embedded systems.
 * RP2040: 264KB SRAM
 * RP2350: 520KB SRAM
 */

#include "picomimi.h"

// ============================================================================
// MEMORY MAP (RP2040)
// ============================================================================

#define SRAM_BASE       0x20000000
#define SRAM_SIZE       (264 * 1024)
#define HEAP_START      0x20010000      // After kernel data
#define HEAP_SIZE       (128 * 1024)    // 128KB heap

// ============================================================================
// SIMPLE ALLOCATOR
// ============================================================================

typedef struct block {
    u32 size;
    bool free;
    struct block *next;
} block_t;

static block_t *heap_start = NULL;
static spinlock_t heap_lock = SPINLOCK_INIT;

void heap_init(void) {
    heap_start = (block_t *)HEAP_START;
    heap_start->size = HEAP_SIZE - sizeof(block_t);
    heap_start->free = true;
    heap_start->next = NULL;
}

void *pmimi_alloc(u32 size) {
    spin_lock(&heap_lock);
    
    // Align to 4 bytes
    size = (size + 3) & ~3;
    
    block_t *curr = heap_start;
    while (curr) {
        if (curr->free && curr->size >= size) {
            // Found a fit
            if (curr->size > size + sizeof(block_t) + 16) {
                // Split block
                block_t *new_block = (block_t *)((u8 *)curr + sizeof(block_t) + size);
                new_block->size = curr->size - size - sizeof(block_t);
                new_block->free = true;
                new_block->next = curr->next;
                
                curr->size = size;
                curr->next = new_block;
            }
            
            curr->free = false;
            spin_unlock(&heap_lock);
            return (void *)((u8 *)curr + sizeof(block_t));
        }
        curr = curr->next;
    }
    
    spin_unlock(&heap_lock);
    return NULL;  // Out of memory
}

void pmimi_free(void *ptr) {
    if (!ptr) return;
    
    spin_lock(&heap_lock);
    
    block_t *block = (block_t *)((u8 *)ptr - sizeof(block_t));
    block->free = true;
    
    // Coalesce with next block if free
    if (block->next && block->next->free) {
        block->size += sizeof(block_t) + block->next->size;
        block->next = block->next->next;
    }
    
    // Coalesce with previous block if free
    block_t *prev = heap_start;
    while (prev && prev->next != block) {
        prev = prev->next;
    }
    if (prev && prev->free) {
        prev->size += sizeof(block_t) + block->size;
        prev->next = block->next;
    }
    
    spin_unlock(&heap_lock);
}

// ============================================================================
// MEMORY POOLS (Fixed-size fast allocator)
// ============================================================================

void mempool_init(mempool_t *pool, void *base, u32 block_size, u32 num_blocks) {
    pool->base = (u8 *)base;
    pool->block_size = (block_size + 3) & ~3;  // Align
    pool->num_blocks = num_blocks;
    pool->lock = (spinlock_t)SPINLOCK_INIT;
    
    // Clear bitmap
    for (u32 i = 0; i < (num_blocks + 31) / 32; i++) {
        pool->bitmap[i] = 0;
    }
}

void *mempool_alloc(mempool_t *pool) {
    spin_lock(&pool->lock);
    
    for (u32 i = 0; i < pool->num_blocks; i++) {
        u32 word = i / 32;
        u32 bit = i % 32;
        
        if (!(pool->bitmap[word] & (1 << bit))) {
            pool->bitmap[word] |= (1 << bit);
            spin_unlock(&pool->lock);
            return pool->base + (i * pool->block_size);
        }
    }
    
    spin_unlock(&pool->lock);
    return NULL;
}

void mempool_free(mempool_t *pool, void *ptr) {
    if (!ptr) return;
    
    u32 offset = (u8 *)ptr - pool->base;
    u32 idx = offset / pool->block_size;
    
    if (idx < pool->num_blocks) {
        spin_lock(&pool->lock);
        pool->bitmap[idx / 32] &= ~(1 << (idx % 32));
        spin_unlock(&pool->lock);
    }
}

// ============================================================================
// STACK ALLOCATOR (for tasks)
// ============================================================================

#define STACK_POOL_SIZE     (32 * 1024)
#define STACK_SIZE_DEFAULT  1024

static u8 stack_pool[STACK_POOL_SIZE] __attribute__((aligned(8)));
static u32 stack_pool_idx = 0;

u32 *stack_alloc(u32 size) {
    size = (size + 7) & ~7;  // 8-byte align
    
    if (stack_pool_idx + size > STACK_POOL_SIZE) {
        return NULL;
    }
    
    u32 *sp = (u32 *)&stack_pool[stack_pool_idx + size];  // Stack grows down
    stack_pool_idx += size;
    return sp;
}

// ============================================================================
// DMA-SAFE BUFFER ALLOCATION
// ============================================================================

// RP2040 DMA requires 4-byte alignment
void *dma_alloc(u32 size) {
    size = (size + 3) & ~3;
    return pmimi_alloc(size);
}

void dma_free(void *ptr) {
    pmimi_free(ptr);
}

// ============================================================================
// MEMORY STATS
// ============================================================================

typedef struct {
    u32 total;
    u32 used;
    u32 free;
    u32 largest_free;
} mem_stats_t;

void mem_stats(mem_stats_t *stats) {
    stats->total = HEAP_SIZE;
    stats->used = 0;
    stats->free = 0;
    stats->largest_free = 0;
    
    block_t *curr = heap_start;
    while (curr) {
        if (curr->free) {
            stats->free += curr->size;
            if (curr->size > stats->largest_free) {
                stats->largest_free = curr->size;
            }
        } else {
            stats->used += curr->size;
        }
        curr = curr->next;
    }
}
