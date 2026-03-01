/**
 * Picomimi Task Management
 * 
 * Task creation, lifecycle, and dual-core support.
 */

#include "picomimi.h"

// ============================================================================
// GLOBALS
// ============================================================================

static task_t task_pool[PICOMIMI_MAX_TASKS];
static u32 task_count = 0;
static pid_t next_pid = 1;
static spinlock_t task_lock = SPINLOCK_INIT;

extern task_t *current_task[NUM_CORES];
extern sched_domain_t *current_domain[NUM_CORES];

// ============================================================================
// TASK CREATION
// ============================================================================

static void task_wrapper(void) {
    task_t *self = task_current();
    
    // Get entry point and arg from stack
    void (*entry)(void *) = (void (*)(void *))self->stack[PICOMIMI_STACK_SIZE/4 - 2];
    void *arg = (void *)self->stack[PICOMIMI_STACK_SIZE/4 - 1];
    
    // Run task
    entry(arg);
    
    // Task returned - clean up
    task_exit();
}

task_t *task_create(const char *name, void (*entry)(void *), void *arg, u8 priority) {
    spin_lock(&task_lock);
    
    if (task_count >= PICOMIMI_MAX_TASKS) {
        spin_unlock(&task_lock);
        return NULL;
    }
    
    task_t *task = &task_pool[task_count++];
    
    // Initialize
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->priority = priority;
    task->core_affinity = 0;  // Any core
    task->current_core = 0;
    task->wake_tick = 0;
    task->deadline = 0;
    task->period = 0;
    task->time_slice = 10;
    task->total_ticks = 0;
    task->switches = 0;
    task->se = NULL;
    task->domain = NULL;
    task->next = NULL;
    task->prev = NULL;
    
    // Copy name
    for (int i = 0; i < 15 && name[i]; i++) {
        task->name[i] = name[i];
    }
    task->name[15] = '\0';
    
    // Setup stack
    // ARM Cortex-M exception frame: xPSR, PC, LR, R12, R3, R2, R1, R0
    // Plus we save R4-R11 manually
    u32 *sp = &task->stack[PICOMIMI_STACK_SIZE / 4];
    
    // Store entry and arg at top for wrapper
    *(--sp) = (u32)arg;
    *(--sp) = (u32)entry;
    
    // Exception frame (auto-pushed on exception entry)
    *(--sp) = 0x01000000;           // xPSR (Thumb bit set)
    *(--sp) = (u32)task_wrapper;    // PC
    *(--sp) = 0xFFFFFFFD;           // LR (return to thread mode, PSP)
    *(--sp) = 0;                    // R12
    *(--sp) = 0;                    // R3
    *(--sp) = 0;                    // R2
    *(--sp) = 0;                    // R1
    *(--sp) = (u32)task;            // R0 (first arg)
    
    // Manually saved registers
    *(--sp) = 0;    // R11
    *(--sp) = 0;    // R10
    *(--sp) = 0;    // R9
    *(--sp) = 0;    // R8
    *(--sp) = 0;    // R7
    *(--sp) = 0;    // R6
    *(--sp) = 0;    // R5
    *(--sp) = 0;    // R4
    
    task->sp = sp;
    
    spin_unlock(&task_lock);
    return task;
}

// ============================================================================
// TASK LIFECYCLE
// ============================================================================

task_t *task_current(void) {
    return current_task[core_id()];
}

void task_yield(void) {
    // Trigger PendSV for context switch
    *(volatile u32 *)0xE000ED04 = (1 << 28);  // SCB->ICSR |= PENDSVSET
    __asm__ volatile("dsb\nisb");
}

void task_sleep(u32 ms) {
    task_t *self = task_current();
    
    spin_lock(&task_lock);
    self->state = TASK_BLOCKED;
    self->wake_tick = timer_ticks() + (ms * PICOMIMI_TICK_HZ / 1000);
    spin_unlock(&task_lock);
    
    task_yield();
}

void task_suspend(task_t *task) {
    spin_lock(&task_lock);
    task->state = TASK_SUSPENDED;
    spin_unlock(&task_lock);
    
    if (task == task_current()) {
        task_yield();
    }
}

void task_resume(task_t *task) {
    spin_lock(&task_lock);
    if (task->state == TASK_SUSPENDED) {
        task->state = TASK_READY;
    }
    spin_unlock(&task_lock);
}

void task_exit(void) {
    task_t *self = task_current();
    
    spin_lock(&task_lock);
    self->state = TASK_TERMINATED;
    
    // Remove from domain
    if (self->domain && self->se) {
        self->domain->sclass->dequeue(self->domain, self->se);
    }
    spin_unlock(&task_lock);
    
    // Never return - yield to other tasks
    while (1) {
        task_yield();
    }
}

// ============================================================================
// SLEEP WAKEUP (called from tick handler)
// ============================================================================

void task_check_wakeups(tick_t now) {
    for (u32 i = 0; i < task_count; i++) {
        task_t *task = &task_pool[i];
        if (task->state == TASK_BLOCKED && task->wake_tick && now >= task->wake_tick) {
            task->state = TASK_READY;
            task->wake_tick = 0;
        }
    }
}

// ============================================================================
// TASK STATS
// ============================================================================

void task_list(void) {
    shell_puts("\nPID  NAME            STATE    PRIO  CORE  SWITCHES\n");
    shell_puts("---  ----            -----    ----  ----  --------\n");
    
    for (u32 i = 0; i < task_count; i++) {
        task_t *t = &task_pool[i];
        
        const char *state_str[] = {"READY", "RUN  ", "BLOCK", "SUSP ", "DEAD "};
        
        char buf[64];
        // Manual sprintf since we might not have it
        shell_puts("  ");
        // Print PID
        char pid_buf[8];
        int n = t->pid;
        int idx = 0;
        do { pid_buf[idx++] = '0' + (n % 10); n /= 10; } while (n);
        while (idx < 3) pid_buf[idx++] = ' ';
        while (idx--) shell_putc(pid_buf[idx]);
        shell_puts("  ");
        
        // Print name
        shell_puts(t->name);
        for (int j = 16 - strlen(t->name); j > 0; j--) shell_putc(' ');
        
        // Print state
        shell_puts(state_str[t->state]);
        shell_puts("     ");
        
        // Print priority
        shell_putc('0' + t->priority);
        shell_puts("     ");
        
        // Print core
        shell_putc('0' + t->current_core);
        shell_puts("     ");
        
        // Print switches (simplified)
        shell_puts("...\n");
    }
    shell_puts("\n");
}

// ============================================================================
// DUAL-CORE SUPPORT
// ============================================================================

static void (*core1_entry_func)(void) = NULL;

static void core1_main(void) {
    // Initialize core1
    irq_init();
    
    if (core1_entry_func) {
        core1_entry_func();
    }
    
    // Idle loop
    while (1) {
        __asm__ volatile("wfi");
    }
}

void core1_launch(void (*entry)(void)) {
    core1_entry_func = entry;
    
    // RP2040 core1 launch sequence
    // Push entry point and stack to FIFO, then SEV
    volatile u32 *fifo = (volatile u32 *)0xD0000050;  // SIO FIFO
    volatile u32 *cpuid = (volatile u32 *)0xD0000000;
    
    // Get stack for core1
    extern u32 __core1_stack_top;
    
    // Launch sequence
    u32 seq[] = {0, 0, 1, (u32)&__core1_stack_top, (u32)core1_main};
    
    for (int i = 0; i < 5; i++) {
        // Drain FIFO
        while (*fifo & (1 << 0)) {
            (void)*fifo;
        }
        
        // Send command
        *fifo = seq[i];
        __asm__ volatile("sev");
        
        // Wait for response (simplified)
        while (!(*fifo & (1 << 0)));
        (void)*fifo;
    }
}

u32 core_id(void) {
    return *(volatile u32 *)0xD0000000;  // SIO CPUID
}

void core_fifo_push(u32 data) {
    volatile u32 *fifo = (volatile u32 *)0xD0000054;  // FIFO_WR
    while (!(*((volatile u32 *)0xD0000050) & (1 << 1)));  // Wait for space
    *fifo = data;
    __asm__ volatile("sev");
}

u32 core_fifo_pop(void) {
    volatile u32 *fifo = (volatile u32 *)0xD0000058;  // FIFO_RD
    while (!(*((volatile u32 *)0xD0000050) & (1 << 0)));  // Wait for data
    return *fifo;
}

// ============================================================================
// CONTEXT SWITCH (Assembly)
// ============================================================================

void __attribute__((naked)) context_switch(task_t *prev, task_t *next) {
    __asm__ volatile(
        // Save prev context
        "push {r4-r11, lr}\n"
        "cmp r0, #0\n"
        "beq 1f\n"
        "str sp, [r0]\n"        // prev->sp = sp
        
        "1:\n"
        // Load next context
        "ldr sp, [r1]\n"        // sp = next->sp
        "pop {r4-r11, pc}\n"
    );
}
