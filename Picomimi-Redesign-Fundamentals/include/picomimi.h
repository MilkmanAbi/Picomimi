/**
 * Picomimi - Advanced Microkernel for RP2040/RP2350
 * 
 * Features:
 * - Scheduler Hypervisor with pluggable policies
 * - O(1) Bitmap Scheduler
 * - Dual-core support
 * - Memory pools
 * - Cooperative fibers
 * - Real-time EDF scheduling
 * - Built-in shell
 */

#ifndef PICOMIMI_H
#define PICOMIMI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define PICOMIMI_VERSION        "2.0.0"
#define PICOMIMI_MAX_TASKS      32
#define PICOMIMI_MAX_PRIORITY   8
#define PICOMIMI_STACK_SIZE     1024
#define PICOMIMI_TICK_HZ        1000
#define PICOMIMI_MAX_DOMAINS    8
#define PICOMIMI_MAX_FIBERS     16

// RP2040/RP2350 specific
#define NUM_CORES               2
#define CORE0                   0
#define CORE1                   1

// ============================================================================
// BASIC TYPES
// ============================================================================

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef u32 tick_t;
typedef u32 pid_t;

// ============================================================================
// TASK STATES
// ============================================================================

typedef enum {
    TASK_READY      = 0,
    TASK_RUNNING    = 1,
    TASK_BLOCKED    = 2,
    TASK_SUSPENDED  = 3,
    TASK_TERMINATED = 4
} task_state_t;

// ============================================================================
// SCHEDULER CLASSES
// ============================================================================

typedef enum {
    SCHED_CLASS_COOP     = 0,   // Cooperative (fibers)
    SCHED_CLASS_REALTIME = 1,   // EDF deadline
    SCHED_CLASS_FAIR     = 2,   // CFS-like
    SCHED_CLASS_BATCH    = 3,   // Throughput
    SCHED_CLASS_IDLE     = 4,   // Background
    SCHED_CLASS_MAX
} sched_class_type_t;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

typedef struct task task_t;
typedef struct sched_entity sched_entity_t;
typedef struct sched_domain sched_domain_t;
typedef struct sched_class sched_class_t;
typedef struct fiber fiber_t;

// ============================================================================
// TASK CONTROL BLOCK
// ============================================================================

struct task {
    // Context (saved registers)
    u32 *sp;                    // Stack pointer
    u32 stack[PICOMIMI_STACK_SIZE / 4];
    
    // Identity
    pid_t pid;
    char name[16];
    
    // State
    task_state_t state;
    u8 priority;
    u8 core_affinity;           // 0=any, 1=core0, 2=core1
    u8 current_core;
    
    // Scheduling
    sched_entity_t *se;         // Scheduler entity
    sched_domain_t *domain;     // Current domain
    
    // Timing
    tick_t wake_tick;           // For sleep
    tick_t deadline;            // For realtime
    tick_t period;              // For periodic tasks
    u32 time_slice;             // Remaining time slice
    
    // Statistics
    u32 total_ticks;
    u32 switches;
    
    // Links
    task_t *next;
    task_t *prev;
};

// ============================================================================
// SCHEDULER ENTITY (wraps task for domain scheduling)
// ============================================================================

struct sched_entity {
    task_t *task;
    
    // For CFS-like fair scheduling
    u64 vruntime;
    u32 weight;
    
    // For EDF realtime
    tick_t deadline;
    tick_t period;
    tick_t exec_start;
    
    // For cooperative
    u32 *fiber_sp;
    u32 fiber_stack[256];
    
    // Domain linkage
    sched_entity_t *next;
    sched_entity_t *prev;
    
    // Stats
    u32 total_runtime;
    u32 invocations;
};

// ============================================================================
// SCHEDULER CLASS (pluggable policy)
// ============================================================================

struct sched_class {
    const char *name;
    sched_class_type_t type;
    
    // Operations
    void (*init)(sched_domain_t *domain);
    void (*enqueue)(sched_domain_t *domain, sched_entity_t *se);
    void (*dequeue)(sched_domain_t *domain, sched_entity_t *se);
    sched_entity_t *(*pick_next)(sched_domain_t *domain);
    void (*tick)(sched_domain_t *domain);
    bool (*should_preempt)(sched_domain_t *domain, sched_entity_t *curr, sched_entity_t *new);
    void (*yield)(sched_domain_t *domain, sched_entity_t *se);
};

// ============================================================================
// SCHEDULER DOMAIN
// ============================================================================

struct sched_domain {
    char name[16];
    u8 id;
    u8 priority;                // Domain priority (lower = higher)
    
    // Class
    sched_class_t *sclass;
    
    // Run queue
    sched_entity_t *runqueue;
    u32 nr_running;
    
    // For O(1) bitmap
    u32 bitmap;                 // Priority bitmap
    sched_entity_t *queues[PICOMIMI_MAX_PRIORITY];
    
    // Timing
    u32 timeslice;
    u32 remaining;
    
    // Stats
    u32 total_switches;
    u32 total_ticks;
    
    // Links
    sched_domain_t *next;
};

// ============================================================================
// FIBER (lightweight cooperative thread)
// ============================================================================

struct fiber {
    u32 *sp;
    u32 stack[256];
    void (*entry)(void *arg);
    void *arg;
    bool active;
    fiber_t *next;
};

// ============================================================================
// SPINLOCK (for dual-core)
// ============================================================================

typedef struct {
    volatile u32 lock;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spin_lock(spinlock_t *lock) {
    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE));
}

static inline void spin_unlock(spinlock_t *lock) {
    __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
}

static inline bool spin_trylock(spinlock_t *lock) {
    return !__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE);
}

// ============================================================================
// MEMORY POOL
// ============================================================================

typedef struct {
    u8 *base;
    u32 block_size;
    u32 num_blocks;
    u32 bitmap[(PICOMIMI_MAX_TASKS + 31) / 32];
    spinlock_t lock;
} mempool_t;

// ============================================================================
// API DECLARATIONS
// ============================================================================

// Core
void picomimi_init(void);
void picomimi_start(void);
void picomimi_panic(const char *msg);

// Tasks
task_t *task_create(const char *name, void (*entry)(void *), void *arg, u8 priority);
void task_yield(void);
void task_sleep(u32 ms);
void task_suspend(task_t *task);
void task_resume(task_t *task);
void task_exit(void);
task_t *task_current(void);

// Scheduler Hypervisor
void sched_hypervisor_init(void);
sched_domain_t *sched_domain_create(const char *name, sched_class_type_t type, u8 priority);
void sched_domain_add_task(sched_domain_t *domain, task_t *task);
void sched_tick(void);
void sched_schedule(void);

// Fibers
fiber_t *fiber_create(void (*entry)(void *), void *arg);
void fiber_yield(void);
void fiber_exit(void);

// Memory
void *pmimi_alloc(u32 size);
void pmimi_free(void *ptr);
void mempool_init(mempool_t *pool, void *base, u32 block_size, u32 num_blocks);
void *mempool_alloc(mempool_t *pool);
void mempool_free(mempool_t *pool, void *ptr);

// Shell
void shell_init(void);
void shell_run(void);
void shell_putc(char c);
void shell_puts(const char *s);
int shell_getc(void);

// Drivers
void uart_init(u32 baud);
void uart_putc(char c);
void uart_puts(const char *s);
int uart_getc(void);
void gpio_init(u32 pin, bool output);
void gpio_set(u32 pin, bool value);
bool gpio_get(u32 pin);

// Interrupts
void irq_init(void);
void irq_enable(u32 irq);
void irq_disable(u32 irq);
void irq_set_handler(u32 irq, void (*handler)(void));

// Timer
void timer_init(u32 hz);
tick_t timer_ticks(void);
void timer_delay_ms(u32 ms);

// Multicore
void core1_launch(void (*entry)(void));
u32 core_id(void);
void core_fifo_push(u32 data);
u32 core_fifo_pop(void);

#endif // PICOMIMI_H
