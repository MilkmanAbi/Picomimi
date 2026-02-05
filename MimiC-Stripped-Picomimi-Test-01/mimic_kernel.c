/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Kernel - Core Implementation                                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Stripped from Picomimi v14.3.1                                           ║
 * ║  Memory management + Task loading + Syscall dispatch                      ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/critical_section.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"

#include <string.h>
#include <stdio.h>

#include "mimic.h"
#include "mimic_fat32.h"

// ============================================================================
// COMPILER HINTS
// ============================================================================

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define HOT_FUNC        __attribute__((hot, optimize("O3")))
#define ALIGNED(n)      __attribute__((aligned(n)))

// ============================================================================
// STATIC MEMORY POOLS
// ============================================================================

// Kernel heap - for kernel data structures
static uint8_t ALIGNED(32) kernel_heap[MIMIC_KERNEL_HEAP];

// User heap - for loaded programs
static uint8_t ALIGNED(32) user_heap[MIMIC_USER_HEAP];

// ============================================================================
// KERNEL STATE
// ============================================================================

typedef struct {
    // Memory management
    MimicMemBlock   mem_blocks[MIMIC_MAX_MEM_BLOCKS];
    uint32_t        mem_block_count;
    mutex_t         mem_lock;
    
    // Separate user memory tracking
    MimicMemBlock   user_blocks[MIMIC_MAX_MEM_BLOCKS];
    uint32_t        user_block_count;
    mutex_t         user_lock;
    
    // Task management
    MimicTCB        tasks[MIMIC_MAX_TASKS];
    uint8_t         task_count;
    uint8_t         current_task;
    mutex_t         task_lock;
    critical_section_t sched_cs;
    
    // Scheduler
    uint64_t        tick_count;
    uint64_t        last_schedule_us;
    bool            running;
    bool            preempt_pending;
    
    // Timing
    uint64_t        boot_time_us;
    
    // Statistics
    uint32_t        total_allocs;
    uint32_t        total_frees;
    uint32_t        failed_allocs;
    uint32_t        programs_loaded;
    uint32_t        syscalls_handled;
    uint32_t        context_switches;
    
    // Free memory tracking
    uint32_t        kernel_free;
    uint32_t        user_free;
    
    // Filesystem
    bool            fs_mounted;
} MimicKernel;

static MimicKernel kernel;

// ============================================================================
// MEMORY MANAGEMENT - KERNEL HEAP
// ============================================================================

static void mem_init(void) {
    mutex_init(&kernel.mem_lock);
    mutex_init(&kernel.user_lock);
    
    // Initialize kernel heap with single free block
    kernel.mem_blocks[0].addr = kernel_heap;
    kernel.mem_blocks[0].size = MIMIC_KERNEL_HEAP;
    kernel.mem_blocks[0].task_id = 0;
    kernel.mem_blocks[0].free = true;
    kernel.mem_blocks[0].pinned = false;
    kernel.mem_block_count = 1;
    kernel.kernel_free = MIMIC_KERNEL_HEAP;
    
    // Initialize user heap with single free block
    kernel.user_blocks[0].addr = user_heap;
    kernel.user_blocks[0].size = MIMIC_USER_HEAP;
    kernel.user_blocks[0].task_id = 0;
    kernel.user_blocks[0].free = true;
    kernel.user_blocks[0].pinned = false;
    kernel.user_block_count = 1;
    kernel.user_free = MIMIC_USER_HEAP;
}

// Best-fit allocation from specified pool
static void* mem_alloc_from_pool(MimicMemBlock* blocks, uint32_t* block_count,
                                  uint32_t* free_bytes, mutex_t* lock,
                                  size_t size, uint32_t task_id, uint32_t max_blocks) {
    if (size == 0) return NULL;
    
    // Align size
    size = (size + MIMIC_MEM_ALIGN - 1) & ~(MIMIC_MEM_ALIGN - 1);
    
    mutex_enter_blocking(lock);
    
    void* result = NULL;
    uint32_t best_idx = 0xFFFFFFFF;
    uint32_t best_size = 0xFFFFFFFF;
    
    // Best-fit search
    for (uint32_t i = 0; i < *block_count; i++) {
        if (blocks[i].free && blocks[i].size >= size) {
            if (blocks[i].size < best_size) {
                best_idx = i;
                best_size = blocks[i].size;
                // Exact match is perfect
                if (best_size == size) break;
            }
        }
    }
    
    if (best_idx != 0xFFFFFFFF) {
        MimicMemBlock* block = &blocks[best_idx];
        
        // Split if large enough
        if (block->size > size + MIMIC_MIN_BLOCK_SPLIT && 
            *block_count < max_blocks) {
            // Create new free block for remainder
            MimicMemBlock* new_block = &blocks[(*block_count)++];
            new_block->addr = (uint8_t*)block->addr + size;
            new_block->size = block->size - size;
            new_block->task_id = 0;
            new_block->free = true;
            new_block->pinned = false;
            
            block->size = size;
        }
        
        block->free = false;
        block->task_id = task_id;
        *free_bytes -= block->size;
        kernel.total_allocs++;
        
        result = block->addr;
    } else {
        kernel.failed_allocs++;
    }
    
    mutex_exit(lock);
    return result;
}

// Free memory in specified pool
static void mem_free_in_pool(MimicMemBlock* blocks, uint32_t* block_count,
                              uint32_t* free_bytes, mutex_t* lock, void* ptr) {
    if (!ptr) return;
    
    mutex_enter_blocking(lock);
    
    // Find block
    for (uint32_t i = 0; i < *block_count; i++) {
        if (blocks[i].addr == ptr && !blocks[i].free) {
            if (blocks[i].pinned) {
                // Cannot free pinned block
                mutex_exit(lock);
                return;
            }
            
            blocks[i].free = true;
            *free_bytes += blocks[i].size;
            kernel.total_frees++;
            
            // Coalesce with adjacent free blocks
            // (simplified - just mark free, full coalesce on demand)
            
            break;
        }
    }
    
    mutex_exit(lock);
}

// Coalesce free blocks (call periodically or when allocation fails)
static void mem_coalesce_pool(MimicMemBlock* blocks, uint32_t* block_count,
                               mutex_t* lock) {
    mutex_enter_blocking(lock);
    
    // Sort by address (simple bubble sort - blocks are small)
    for (uint32_t i = 0; i < *block_count - 1; i++) {
        for (uint32_t j = 0; j < *block_count - i - 1; j++) {
            if (blocks[j].addr > blocks[j+1].addr) {
                MimicMemBlock tmp = blocks[j];
                blocks[j] = blocks[j+1];
                blocks[j+1] = tmp;
            }
        }
    }
    
    // Merge adjacent free blocks
    uint32_t write_idx = 0;
    for (uint32_t i = 0; i < *block_count; i++) {
        if (i > 0 && blocks[write_idx-1].free && blocks[i].free &&
            (uint8_t*)blocks[write_idx-1].addr + blocks[write_idx-1].size == blocks[i].addr) {
            // Merge with previous
            blocks[write_idx-1].size += blocks[i].size;
        } else {
            if (write_idx != i) {
                blocks[write_idx] = blocks[i];
            }
            write_idx++;
        }
    }
    *block_count = write_idx;
    
    mutex_exit(lock);
}

// Public API - kernel malloc
void* HOT_FUNC mimic_kmalloc(size_t size) {
    return mem_alloc_from_pool(kernel.mem_blocks, &kernel.mem_block_count,
                                &kernel.kernel_free, &kernel.mem_lock,
                                size, 0, MIMIC_MAX_MEM_BLOCKS);
}

void HOT_FUNC mimic_kfree(void* ptr) {
    mem_free_in_pool(kernel.mem_blocks, &kernel.mem_block_count,
                      &kernel.kernel_free, &kernel.mem_lock, ptr);
}

// ============================================================================
// USER MEMORY MANAGEMENT (per-task)
// ============================================================================

void* HOT_FUNC mimic_umalloc(uint32_t task_id, size_t size) {
    return mem_alloc_from_pool(kernel.user_blocks, &kernel.user_block_count,
                                &kernel.user_free, &kernel.user_lock,
                                size, task_id, MIMIC_MAX_MEM_BLOCKS);
}

void HOT_FUNC mimic_ufree(uint32_t task_id, void* ptr) {
    // Verify task owns this memory
    mutex_enter_blocking(&kernel.user_lock);
    bool valid = false;
    for (uint32_t i = 0; i < kernel.user_block_count; i++) {
        if (kernel.user_blocks[i].addr == ptr && 
            kernel.user_blocks[i].task_id == task_id) {
            valid = true;
            break;
        }
    }
    mutex_exit(&kernel.user_lock);
    
    if (valid) {
        mem_free_in_pool(kernel.user_blocks, &kernel.user_block_count,
                          &kernel.user_free, &kernel.user_lock, ptr);
    }
}

// Free ALL memory owned by a task (on task death)
void mimic_task_free_all_memory(uint32_t task_id) {
    mutex_enter_blocking(&kernel.user_lock);
    
    for (uint32_t i = 0; i < kernel.user_block_count; i++) {
        if (kernel.user_blocks[i].task_id == task_id && !kernel.user_blocks[i].free) {
            kernel.user_blocks[i].free = true;
            kernel.user_free += kernel.user_blocks[i].size;
            kernel.total_frees++;
        }
    }
    
    mutex_exit(&kernel.user_lock);
    
    // Coalesce freed blocks
    mem_coalesce_pool(kernel.user_blocks, &kernel.user_block_count, &kernel.user_lock);
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

static void task_init(void) {
    mutex_init(&kernel.task_lock);
    critical_section_init(&kernel.sched_cs);
    
    memset(kernel.tasks, 0, sizeof(kernel.tasks));
    kernel.task_count = 0;
    kernel.current_task = 0;
    
    // Task 0 is the kernel/idle task
    MimicTCB* idle = &kernel.tasks[0];
    idle->id = 0;
    strncpy(idle->name, "kernel", sizeof(idle->name));
    idle->state = TASK_STATE_READY;
    idle->priority = 255;  // Lowest priority
    kernel.task_count = 1;
}

// Allocate a new task slot
static MimicTCB* task_alloc(void) {
    mutex_enter_blocking(&kernel.task_lock);
    
    MimicTCB* task = NULL;
    for (uint8_t i = 1; i < MIMIC_MAX_TASKS; i++) {
        if (kernel.tasks[i].state == TASK_STATE_FREE) {
            task = &kernel.tasks[i];
            task->id = i;
            task->state = TASK_STATE_BLOCKED;
            kernel.task_count++;
            break;
        }
    }
    
    mutex_exit(&kernel.task_lock);
    return task;
}

// Kill a task and free its resources
void mimic_task_kill(uint32_t task_id) {
    if (task_id == 0 || task_id >= MIMIC_MAX_TASKS) return;
    
    mutex_enter_blocking(&kernel.task_lock);
    
    MimicTCB* task = &kernel.tasks[task_id];
    if (task->state != TASK_STATE_FREE) {
        task->state = TASK_STATE_ZOMBIE;
        
        // Free all memory owned by task
        mimic_task_free_all_memory(task_id);
        
        task->state = TASK_STATE_FREE;
        kernel.task_count--;
    }
    
    mutex_exit(&kernel.task_lock);
}

// ============================================================================
// BINARY LOADER
// ============================================================================

int mimic_validate_header(const MimiHeader* hdr) {
    if (hdr->magic != MIMI_MAGIC) {
        return MIMIC_ERR_CORRUPT;
    }
    
    if (hdr->version != MIMI_VERSION) {
        return MIMIC_ERR_INVAL;
    }
    
    // Check architecture
#if MIMIC_TARGET_RP2350
    if (hdr->arch != MIMI_ARCH_CORTEX_M33 && hdr->arch != MIMI_ARCH_RISCV) {
        return MIMIC_ERR_NOEXEC;
    }
#else
    if (hdr->arch != MIMI_ARCH_CORTEX_M0P) {
        return MIMIC_ERR_NOEXEC;
    }
#endif
    
    if (hdr->text_size == 0) {
        return MIMIC_ERR_INVAL;
    }
    
    if (hdr->entry_offset >= hdr->text_size) {
        return MIMIC_ERR_INVAL;
    }
    
    return MIMIC_OK;
}

// Load a .mimi binary from file
int mimic_load_binary(const char* path, MimicTCB* task) {
    int fd = mimic_fopen(path, MIMIC_FILE_READ);
    if (fd < 0) return fd;
    
    // Read header
    MimiHeader hdr;
    int n = mimic_fread(fd, &hdr, sizeof(hdr));
    if (n != sizeof(hdr)) {
        mimic_fclose(fd);
        return MIMIC_ERR_CORRUPT;
    }
    
    // Validate
    int err = mimic_validate_header(&hdr);
    if (err != MIMIC_OK) {
        mimic_fclose(fd);
        return err;
    }
    
    // Calculate total memory needed
    uint32_t total_size = hdr.text_size + hdr.rodata_size + hdr.data_size + hdr.bss_size;
    uint32_t stack_size = hdr.stack_request ? hdr.stack_request : 4096;
    uint32_t heap_size = hdr.heap_request ? hdr.heap_request : 8192;
    total_size += stack_size + heap_size;
    
    // Align to 32 bytes
    total_size = (total_size + 31) & ~31;
    
    // Allocate memory for the program
    void* base = mimic_umalloc(task->id, total_size);
    if (!base) {
        mimic_fclose(fd);
        return MIMIC_ERR_NOMEM;
    }
    
    // Setup memory layout
    uintptr_t addr = (uintptr_t)base;
    
    task->mem.base = addr;
    task->mem.total_size = total_size;
    
    task->mem.text_start = 0;
    task->mem.text_size = hdr.text_size;
    
    task->mem.rodata_start = hdr.text_size;
    task->mem.rodata_size = hdr.rodata_size;
    
    task->mem.data_start = task->mem.rodata_start + hdr.rodata_size;
    task->mem.data_size = hdr.data_size;
    
    task->mem.bss_start = task->mem.data_start + hdr.data_size;
    task->mem.bss_size = hdr.bss_size;
    
    uint32_t sections_end = task->mem.bss_start + hdr.bss_size;
    
    task->mem.heap_start = sections_end;
    task->mem.heap_size = heap_size;
    task->mem.heap_used = 0;
    
    task->mem.stack_size = stack_size;
    task->mem.stack_top = total_size;  // Stack grows down
    
    // Read sections
    uint8_t* ptr = (uint8_t*)base;
    
    // .text
    n = mimic_fread(fd, ptr + task->mem.text_start, hdr.text_size);
    if (n != (int)hdr.text_size) {
        mimic_ufree(task->id, base);
        mimic_fclose(fd);
        return MIMIC_ERR_IO;
    }
    
    // .rodata
    if (hdr.rodata_size > 0) {
        n = mimic_fread(fd, ptr + task->mem.rodata_start, hdr.rodata_size);
        if (n != (int)hdr.rodata_size) {
            mimic_ufree(task->id, base);
            mimic_fclose(fd);
            return MIMIC_ERR_IO;
        }
    }
    
    // .data
    if (hdr.data_size > 0) {
        n = mimic_fread(fd, ptr + task->mem.data_start, hdr.data_size);
        if (n != (int)hdr.data_size) {
            mimic_ufree(task->id, base);
            mimic_fclose(fd);
            return MIMIC_ERR_IO;
        }
    }
    
    // Zero .bss
    memset(ptr + task->mem.bss_start, 0, hdr.bss_size);
    
    // Read and apply relocations
    if (hdr.reloc_count > 0) {
        // Read symbol table first if present
        MimiSymbol* symtab = NULL;
        if (hdr.symbol_count > 0) {
            symtab = mimic_kmalloc(hdr.symbol_count * sizeof(MimiSymbol));
            if (!symtab) {
                mimic_ufree(task->id, base);
                mimic_fclose(fd);
                return MIMIC_ERR_NOMEM;
            }
            
            // Seek to symbol table (after sections and relocs)
            int64_t symtab_offset = sizeof(MimiHeader) + 
                                     hdr.text_size + hdr.rodata_size + hdr.data_size +
                                     hdr.reloc_count * sizeof(MimiReloc);
            mimic_fseek(fd, symtab_offset, MIMIC_SEEK_SET);
            
            n = mimic_fread(fd, symtab, hdr.symbol_count * sizeof(MimiSymbol));
            if (n != (int)(hdr.symbol_count * sizeof(MimiSymbol))) {
                mimic_kfree(symtab);
                mimic_ufree(task->id, base);
                mimic_fclose(fd);
                return MIMIC_ERR_CORRUPT;
            }
        }
        
        // Seek back to relocation table
        int64_t reloc_offset = sizeof(MimiHeader) + 
                                hdr.text_size + hdr.rodata_size + hdr.data_size;
        mimic_fseek(fd, reloc_offset, MIMIC_SEEK_SET);
        
        // Process relocations one at a time
        for (uint32_t i = 0; i < hdr.reloc_count; i++) {
            MimiReloc reloc;
            n = mimic_fread(fd, &reloc, sizeof(reloc));
            if (n != sizeof(reloc)) {
                if (symtab) mimic_kfree(symtab);
                mimic_ufree(task->id, base);
                mimic_fclose(fd);
                return MIMIC_ERR_CORRUPT;
            }
            
            // Apply relocation
            uintptr_t patch_addr = 0;
            switch (reloc.section) {
                case MIMI_SECT_TEXT:
                    patch_addr = addr + task->mem.text_start + reloc.offset;
                    break;
                case MIMI_SECT_RODATA:
                    patch_addr = addr + task->mem.rodata_start + reloc.offset;
                    break;
                case MIMI_SECT_DATA:
                    patch_addr = addr + task->mem.data_start + reloc.offset;
                    break;
                default:
                    continue;
            }
            
            // Get symbol value
            uint32_t sym_value = 0;
            if (reloc.symbol_idx < hdr.symbol_count && symtab) {
                MimiSymbol* sym = &symtab[reloc.symbol_idx];
                
                switch (sym->section) {
                    case MIMI_SECT_TEXT:
                        sym_value = addr + task->mem.text_start + sym->value;
                        break;
                    case MIMI_SECT_RODATA:
                        sym_value = addr + task->mem.rodata_start + sym->value;
                        break;
                    case MIMI_SECT_DATA:
                        sym_value = addr + task->mem.data_start + sym->value;
                        break;
                    case MIMI_SECT_BSS:
                        sym_value = addr + task->mem.bss_start + sym->value;
                        break;
                    default:
                        // External/syscall - handled specially
                        sym_value = sym->value;
                }
            }
            
            // Apply based on type
            switch (reloc.type) {
                case MIMI_RELOC_ABS32: {
                    uint32_t* target = (uint32_t*)patch_addr;
                    *target = sym_value;
                    break;
                }
                case MIMI_RELOC_REL32: {
                    int32_t* target = (int32_t*)patch_addr;
                    *target = (int32_t)(sym_value - patch_addr - 4);
                    break;
                }
                case MIMI_RELOC_THUMB_CALL: {
                    // Thumb BL instruction encoding
                    int32_t offset = (int32_t)(sym_value - patch_addr - 4);
                    uint16_t* instr = (uint16_t*)patch_addr;
                    
                    uint32_t s = (offset >> 24) & 1;
                    uint32_t i1 = (offset >> 23) & 1;
                    uint32_t i2 = (offset >> 22) & 1;
                    uint32_t imm10 = (offset >> 12) & 0x3FF;
                    uint32_t imm11 = (offset >> 1) & 0x7FF;
                    
                    uint32_t j1 = ((~i1) ^ s) & 1;
                    uint32_t j2 = ((~i2) ^ s) & 1;
                    
                    instr[0] = 0xF000 | (s << 10) | imm10;
                    instr[1] = 0xD000 | (j1 << 13) | (j2 << 11) | imm11;
                    break;
                }
            }
        }
        
        if (symtab) mimic_kfree(symtab);
    }
    
    mimic_fclose(fd);
    
    // Set entry point
    task->entry = (void*)(addr + task->mem.text_start + hdr.entry_offset);
    
    // Set up initial stack pointer
    task->sp = addr + task->mem.stack_top;
    
    // Copy program name
    strncpy(task->name, hdr.name, sizeof(task->name) - 1);
    
    kernel.programs_loaded++;
    
    return MIMIC_OK;
}

// Load and spawn a task from file
int mimic_task_load(const char* path, uint8_t priority) {
    MimicTCB* task = task_alloc();
    if (!task) return MIMIC_ERR_NOMEM;
    
    int err = mimic_load_binary(path, task);
    if (err != MIMIC_OK) {
        task->state = TASK_STATE_FREE;
        kernel.task_count--;
        return err;
    }
    
    task->priority = priority;
    task->start_time = time_us_64() / 1000;
    task->state = TASK_STATE_READY;
    
    return (int)task->id;
}

// ============================================================================
// SCHEDULER
// ============================================================================

static void scheduler_tick(void) {
    if (!kernel.running) return;
    
    uint64_t now_us = time_us_64();
    kernel.tick_count++;
    
    critical_section_enter_blocking(&kernel.sched_cs);
    
    // Update sleeping tasks
    uint64_t now_ms = now_us / 1000;
    for (uint8_t i = 1; i < MIMIC_MAX_TASKS; i++) {
        if (kernel.tasks[i].state == TASK_STATE_SLEEPING) {
            if (now_ms >= kernel.tasks[i].wake_time) {
                kernel.tasks[i].state = TASK_STATE_READY;
            }
        }
    }
    
    // Simple priority scheduler
    MimicTCB* next = &kernel.tasks[0];  // Default to idle
    uint8_t best_prio = 255;
    
    for (uint8_t i = 1; i < MIMIC_MAX_TASKS; i++) {
        if (kernel.tasks[i].state == TASK_STATE_READY) {
            if (kernel.tasks[i].priority < best_prio) {
                best_prio = kernel.tasks[i].priority;
                next = &kernel.tasks[i];
            }
        }
    }
    
    if (next->id != kernel.current_task) {
        // Context switch
        kernel.tasks[kernel.current_task].state = TASK_STATE_READY;
        kernel.current_task = next->id;
        next->state = TASK_STATE_RUNNING;
        kernel.context_switches++;
    }
    
    kernel.last_schedule_us = now_us;
    
    critical_section_exit(&kernel.sched_cs);
}

// ============================================================================
// SYSCALL HANDLERS
// ============================================================================

// Simple syscall dispatcher - called via SVC or function table
int32_t mimic_syscall(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    kernel.syscalls_handled++;
    uint32_t task_id = kernel.current_task;
    
    switch (num) {
        // Process control
        case MIMIC_SYS_EXIT:
            mimic_task_kill(task_id);
            return 0;
            
        case MIMIC_SYS_YIELD:
            mimic_task_yield();
            return 0;
            
        case MIMIC_SYS_SLEEP:
            mimic_task_sleep(a0);
            return 0;
            
        case MIMIC_SYS_TIME:
            return (time_us_64() - kernel.boot_time_us) / 1000;
            
        // Memory
        case MIMIC_SYS_MALLOC:
            return (uint32_t)mimic_umalloc(task_id, a0);
            
        case MIMIC_SYS_FREE:
            mimic_ufree(task_id, (void*)a0);
            return 0;
            
        // Console
        case MIMIC_SYS_PUTCHAR:
            putchar(a0);
            return a0;
            
        case MIMIC_SYS_GETCHAR:
            return getchar();
            
        case MIMIC_SYS_PUTS:
            printf("%s", (const char*)a0);
            return 0;
            
        // GPIO
        case MIMIC_SYS_GPIO_INIT:
            gpio_init(a0);
            return 0;
            
        case MIMIC_SYS_GPIO_DIR:
            gpio_set_dir(a0, a1);
            return 0;
            
        case MIMIC_SYS_GPIO_PUT:
            gpio_put(a0, a1);
            return 0;
            
        case MIMIC_SYS_GPIO_GET:
            return gpio_get(a0);
            
        case MIMIC_SYS_GPIO_PULL:
            if (a1 == 1) gpio_pull_up(a0);
            else if (a1 == 2) gpio_pull_down(a0);
            else gpio_disable_pulls(a0);
            return 0;
            
        // File I/O
        case MIMIC_SYS_OPEN:
            return mimic_fopen((const char*)a0, a1);
            
        case MIMIC_SYS_CLOSE:
            return mimic_fclose(a0);
            
        case MIMIC_SYS_READ:
            return mimic_fread(a0, (void*)a1, a2);
            
        case MIMIC_SYS_WRITE:
            return mimic_fwrite(a0, (const void*)a1, a2);
            
        case MIMIC_SYS_SEEK:
            return mimic_fseek(a0, a1, a2);
            
        default:
            return MIMIC_ERR_NOSYS;
    }
}

void mimic_task_yield(void) {
    kernel.preempt_pending = true;
    scheduler_tick();
}

void mimic_task_sleep(uint32_t ms) {
    if (kernel.current_task == 0) return;  // Kernel can't sleep
    
    MimicTCB* task = &kernel.tasks[kernel.current_task];
    task->wake_time = (time_us_64() / 1000) + ms;
    task->state = TASK_STATE_SLEEPING;
    
    scheduler_tick();
}

// ============================================================================
// PUBLIC API
// ============================================================================

uint32_t mimic_get_free_memory(void) {
    return kernel.user_free;
}

uint32_t mimic_get_task_count(void) {
    return kernel.task_count;
}

uint32_t mimic_get_uptime_ms(void) {
    return (time_us_64() - kernel.boot_time_us) / 1000;
}

float mimic_get_cpu_usage(void) {
    // TODO: proper calculation
    return 0.0f;
}

void mimic_dump_tasks(void) {
    printf("\n=== MIMIC TASKS ===\n");
    printf("ID  NAME            STATE    PRI  MEM\n");
    for (uint8_t i = 0; i < MIMIC_MAX_TASKS; i++) {
        MimicTCB* t = &kernel.tasks[i];
        if (t->state != TASK_STATE_FREE) {
            const char* state_str[] = {"FREE", "READY", "RUN", "BLOCK", "SLEEP", "ZOMB"};
            printf("%2d  %-15s %-7s  %3d  %lu\n",
                   t->id, t->name, state_str[t->state], 
                   t->priority, t->mem.total_size);
        }
    }
}

void mimic_dump_memory(void) {
    printf("\n=== MIMIC MEMORY ===\n");
    printf("Kernel: %lu / %d bytes free\n", kernel.kernel_free, MIMIC_KERNEL_HEAP);
    printf("User:   %lu / %d bytes free\n", kernel.user_free, MIMIC_USER_HEAP);
    printf("Allocs: %lu  Frees: %lu  Failed: %lu\n",
           kernel.total_allocs, kernel.total_frees, kernel.failed_allocs);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void mimic_kernel_init(void) {
    kernel.boot_time_us = time_us_64();
    kernel.running = false;
    
    mem_init();
    task_init();
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════╗\n");
    printf("║  MimiC Kernel v1.0.0 - " MIMIC_CHIP_NAME "                  ║\n");
    printf("╚═══════════════════════════════════════════════╝\n");
    printf("Kernel heap: %d KB\n", MIMIC_KERNEL_HEAP / 1024);
    printf("User heap:   %d KB\n", MIMIC_USER_HEAP / 1024);
    printf("Max tasks:   %d\n\n", MIMIC_MAX_TASKS);
}

void mimic_kernel_run(void) {
    // Mount filesystem
    int err = mimic_fat32_mount();
    if (err == MIMIC_OK) {
        kernel.fs_mounted = true;
        printf("[FS] SD card mounted\n");
    } else {
        printf("[FS] Mount failed: %d\n", err);
    }
    
    kernel.running = true;
    
    printf("[KERNEL] Running...\n\n");
    
    // Main kernel loop
    while (kernel.running) {
        scheduler_tick();
        
        // Execute current task tick (cooperative)
        MimicTCB* current = &kernel.tasks[kernel.current_task];
        if (current->id != 0 && current->state == TASK_STATE_RUNNING) {
            // In a real implementation, this would execute the task
            // For now, we're cooperative - task runs until yield/sleep/exit
        }
        
        // Idle if nothing to do
        if (kernel.current_task == 0) {
            __wfi();  // Wait for interrupt
        }
    }
}
