/**
 * Picomimi Fibers
 * 
 * Lightweight cooperative threads (stackful coroutines)
 */

#include "picomimi.h"

// ============================================================================
// FIBER POOL
// ============================================================================

static fiber_t fiber_pool[PICOMIMI_MAX_FIBERS];
static u32 fiber_count = 0;
static fiber_t *current_fiber = NULL;
static fiber_t *fiber_list = NULL;

static spinlock_t fiber_lock = SPINLOCK_INIT;

// ============================================================================
// FIBER CREATION
// ============================================================================

static void fiber_wrapper(void) {
    fiber_t *self = current_fiber;
    
    // Run fiber entry
    self->entry(self->arg);
    
    // Fiber returned - mark inactive and yield
    self->active = false;
    fiber_yield();
    
    // Should never reach here
    while (1);
}

fiber_t *fiber_create(void (*entry)(void *), void *arg) {
    spin_lock(&fiber_lock);
    
    if (fiber_count >= PICOMIMI_MAX_FIBERS) {
        spin_unlock(&fiber_lock);
        return NULL;
    }
    
    fiber_t *fiber = &fiber_pool[fiber_count++];
    
    fiber->entry = entry;
    fiber->arg = arg;
    fiber->active = true;
    
    // Setup stack
    u32 *sp = &fiber->stack[255];  // Stack grows down
    
    // Push initial context
    *(--sp) = 0x01000000;           // xPSR (Thumb)
    *(--sp) = (u32)fiber_wrapper;   // PC
    *(--sp) = 0;                    // LR
    *(--sp) = 0;                    // R12
    *(--sp) = 0;                    // R3
    *(--sp) = 0;                    // R2
    *(--sp) = 0;                    // R1
    *(--sp) = (u32)fiber;           // R0
    *(--sp) = 0;                    // R11
    *(--sp) = 0;                    // R10
    *(--sp) = 0;                    // R9
    *(--sp) = 0;                    // R8
    *(--sp) = 0;                    // R7
    *(--sp) = 0;                    // R6
    *(--sp) = 0;                    // R5
    *(--sp) = 0;                    // R4
    
    fiber->sp = sp;
    
    // Add to list
    fiber->next = fiber_list;
    fiber_list = fiber;
    
    spin_unlock(&fiber_lock);
    return fiber;
}

// ============================================================================
// FIBER SCHEDULING
// ============================================================================

void fiber_yield(void) {
    if (!current_fiber) return;
    
    fiber_t *prev = current_fiber;
    fiber_t *next = prev->next;
    
    // Find next active fiber (round-robin)
    while (next && !next->active) {
        next = next->next;
    }
    if (!next) {
        next = fiber_list;
        while (next && next != prev && !next->active) {
            next = next->next;
        }
    }
    
    if (next && next != prev && next->active) {
        current_fiber = next;
        fiber_switch(prev, next);
    }
}

void fiber_exit(void) {
    if (current_fiber) {
        current_fiber->active = false;
    }
    fiber_yield();
}

// ============================================================================
// FIBER CONTEXT SWITCH
// ============================================================================

void __attribute__((naked)) fiber_switch(fiber_t *from, fiber_t *to) {
    __asm__ volatile(
        // Save context to 'from'
        "push {r4-r11, lr}\n"
        "str sp, [r0]\n"        // from->sp = sp
        
        // Load context from 'to'
        "ldr sp, [r1]\n"        // sp = to->sp
        "pop {r4-r11, pc}\n"
    );
}

// ============================================================================
// FIBER SCHEDULER (runs in cooperative domain)
// ============================================================================

void fiber_scheduler_run(void) {
    // Pick first active fiber
    current_fiber = fiber_list;
    while (current_fiber && !current_fiber->active) {
        current_fiber = current_fiber->next;
    }
    
    if (current_fiber) {
        // Jump to fiber
        __asm__ volatile(
            "ldr sp, [%0]\n"
            "pop {r4-r11, pc}\n"
            :
            : "r" (&current_fiber->sp)
        );
    }
}

// ============================================================================
// FIBER UTILITIES
// ============================================================================

fiber_t *fiber_current(void) {
    return current_fiber;
}

u32 fiber_count_active(void) {
    u32 count = 0;
    fiber_t *f = fiber_list;
    while (f) {
        if (f->active) count++;
        f = f->next;
    }
    return count;
}

void fiber_list_all(void) {
    shell_puts("\nFibers:\n");
    shell_puts("  ID  Active  Entry\n");
    shell_puts("  --  ------  -----\n");
    
    fiber_t *f = fiber_list;
    int id = 0;
    while (f) {
        shell_puts("  ");
        shell_putc('0' + id);
        shell_puts("   ");
        shell_puts(f->active ? "yes" : "no ");
        shell_puts("     0x");
        // Print address (simplified)
        shell_puts("...\n");
        f = f->next;
        id++;
    }
    shell_puts("\n");
}
