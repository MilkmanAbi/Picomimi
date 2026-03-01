/**
 * Picomimi Scheduler Hypervisor
 * 
 * Hierarchical scheduler with pluggable policies.
 * Main scheduler picks domains, domain scheduler picks tasks.
 */

#include "picomimi.h"

// ============================================================================
// GLOBALS
// ============================================================================

static sched_domain_t *domains[PICOMIMI_MAX_DOMAINS];
static u32 num_domains = 0;
static sched_domain_t *current_domain[NUM_CORES];
static task_t *current_task[NUM_CORES];
static sched_class_t sched_classes[SCHED_CLASS_MAX];

static spinlock_t sched_lock = SPINLOCK_INIT;
static tick_t system_ticks = 0;

// ============================================================================
// COOPERATIVE SCHEDULER CLASS
// ============================================================================

static void coop_init(sched_domain_t *domain) {
    domain->runqueue = NULL;
    domain->nr_running = 0;
}

static void coop_enqueue(sched_domain_t *domain, sched_entity_t *se) {
    se->next = domain->runqueue;
    se->prev = NULL;
    if (domain->runqueue) domain->runqueue->prev = se;
    domain->runqueue = se;
    domain->nr_running++;
}

static void coop_dequeue(sched_domain_t *domain, sched_entity_t *se) {
    if (se->prev) se->prev->next = se->next;
    else domain->runqueue = se->next;
    if (se->next) se->next->prev = se->prev;
    domain->nr_running--;
}

static sched_entity_t *coop_pick_next(sched_domain_t *domain) {
    return domain->runqueue;  // FIFO - just pick first
}

static void coop_tick(sched_domain_t *domain) {
    // No preemption in coop - do nothing
    (void)domain;
}

static bool coop_should_preempt(sched_domain_t *domain, sched_entity_t *curr, sched_entity_t *new) {
    (void)domain; (void)curr; (void)new;
    return false;  // Never preempt - cooperative!
}

static void coop_yield(sched_domain_t *domain, sched_entity_t *se) {
    // Move to back of queue
    coop_dequeue(domain, se);
    
    // Find tail
    sched_entity_t *tail = domain->runqueue;
    if (tail) {
        while (tail->next) tail = tail->next;
        tail->next = se;
        se->prev = tail;
        se->next = NULL;
    } else {
        domain->runqueue = se;
        se->prev = NULL;
        se->next = NULL;
    }
    domain->nr_running++;
}

// ============================================================================
// REALTIME (EDF) SCHEDULER CLASS
// ============================================================================

static void rt_init(sched_domain_t *domain) {
    domain->runqueue = NULL;
    domain->nr_running = 0;
}

static void rt_enqueue(sched_domain_t *domain, sched_entity_t *se) {
    // Insert sorted by deadline (earliest first)
    sched_entity_t **pp = &domain->runqueue;
    while (*pp && (*pp)->deadline <= se->deadline) {
        pp = &(*pp)->next;
    }
    se->next = *pp;
    se->prev = NULL;  // Simplified - not maintaining prev for RT
    *pp = se;
    domain->nr_running++;
}

static void rt_dequeue(sched_domain_t *domain, sched_entity_t *se) {
    sched_entity_t **pp = &domain->runqueue;
    while (*pp && *pp != se) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        *pp = se->next;
        domain->nr_running--;
    }
}

static sched_entity_t *rt_pick_next(sched_domain_t *domain) {
    // EDF: pick earliest deadline
    return domain->runqueue;
}

static void rt_tick(sched_domain_t *domain) {
    // Check for deadline misses
    sched_entity_t *se = domain->runqueue;
    while (se) {
        if (se->deadline && system_ticks > se->deadline) {
            // Deadline miss! Could panic or handle gracefully
            // For now just reset deadline to next period
            se->deadline = system_ticks + se->period;
        }
        se = se->next;
    }
}

static bool rt_should_preempt(sched_domain_t *domain, sched_entity_t *curr, sched_entity_t *new) {
    (void)domain;
    // Preempt if new task has earlier deadline
    return new->deadline < curr->deadline;
}

static void rt_yield(sched_domain_t *domain, sched_entity_t *se) {
    rt_dequeue(domain, se);
    se->deadline = system_ticks + se->period;  // Reset deadline
    rt_enqueue(domain, se);
}

// ============================================================================
// FAIR (CFS-like) SCHEDULER CLASS
// ============================================================================

#define FAIR_GRANULARITY    10  // Minimum timeslice in ticks
#define FAIR_WEIGHT_DEFAULT 1024

static void fair_init(sched_domain_t *domain) {
    domain->runqueue = NULL;
    domain->nr_running = 0;
}

static void fair_enqueue(sched_domain_t *domain, sched_entity_t *se) {
    // Insert sorted by vruntime (lowest first)
    sched_entity_t **pp = &domain->runqueue;
    while (*pp && (*pp)->vruntime <= se->vruntime) {
        pp = &(*pp)->next;
    }
    se->next = *pp;
    *pp = se;
    domain->nr_running++;
}

static void fair_dequeue(sched_domain_t *domain, sched_entity_t *se) {
    sched_entity_t **pp = &domain->runqueue;
    while (*pp && *pp != se) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        *pp = se->next;
        domain->nr_running--;
    }
}

static sched_entity_t *fair_pick_next(sched_domain_t *domain) {
    // Pick lowest vruntime
    return domain->runqueue;
}

static void fair_tick(sched_domain_t *domain) {
    sched_entity_t *curr = domain->runqueue;
    if (!curr) return;
    
    // Update vruntime based on weight
    u32 delta = (FAIR_WEIGHT_DEFAULT * 1) / curr->weight;
    curr->vruntime += delta;
    curr->total_runtime++;
    
    // Check if we need to rebalance
    if (curr->next && curr->vruntime > curr->next->vruntime + FAIR_GRANULARITY) {
        // Need to reschedule
        fair_dequeue(domain, curr);
        fair_enqueue(domain, curr);
    }
}

static bool fair_should_preempt(sched_domain_t *domain, sched_entity_t *curr, sched_entity_t *new) {
    (void)domain;
    return new->vruntime + FAIR_GRANULARITY < curr->vruntime;
}

static void fair_yield(sched_domain_t *domain, sched_entity_t *se) {
    // Penalize yielding task slightly
    se->vruntime += FAIR_GRANULARITY / 2;
    fair_dequeue(domain, se);
    fair_enqueue(domain, se);
}

// ============================================================================
// BATCH SCHEDULER CLASS
// ============================================================================

#define BATCH_TIMESLICE     100  // Long timeslices for throughput

static void batch_init(sched_domain_t *domain) {
    fair_init(domain);
    domain->timeslice = BATCH_TIMESLICE;
}

static void batch_tick(sched_domain_t *domain) {
    domain->remaining--;
    if (domain->remaining == 0) {
        domain->remaining = domain->timeslice;
        fair_tick(domain);  // Reuse fair tick logic
    }
}

// ============================================================================
// IDLE SCHEDULER CLASS
// ============================================================================

static sched_entity_t idle_entity[NUM_CORES];
static task_t idle_task[NUM_CORES];

static void idle_init(sched_domain_t *domain) {
    domain->runqueue = NULL;
    domain->nr_running = 0;
}

static sched_entity_t *idle_pick_next(sched_domain_t *domain) {
    (void)domain;
    u32 core = core_id();
    return &idle_entity[core];
}

static bool idle_should_preempt(sched_domain_t *domain, sched_entity_t *curr, sched_entity_t *new) {
    (void)domain; (void)curr; (void)new;
    return true;  // Always preemptible
}

// ============================================================================
// SCHEDULER CLASS REGISTRATION
// ============================================================================

static void register_sched_classes(void) {
    // Cooperative
    sched_classes[SCHED_CLASS_COOP] = (sched_class_t){
        .name = "cooperative",
        .type = SCHED_CLASS_COOP,
        .init = coop_init,
        .enqueue = coop_enqueue,
        .dequeue = coop_dequeue,
        .pick_next = coop_pick_next,
        .tick = coop_tick,
        .should_preempt = coop_should_preempt,
        .yield = coop_yield
    };
    
    // Realtime (EDF)
    sched_classes[SCHED_CLASS_REALTIME] = (sched_class_t){
        .name = "realtime",
        .type = SCHED_CLASS_REALTIME,
        .init = rt_init,
        .enqueue = rt_enqueue,
        .dequeue = rt_dequeue,
        .pick_next = rt_pick_next,
        .tick = rt_tick,
        .should_preempt = rt_should_preempt,
        .yield = rt_yield
    };
    
    // Fair (CFS-like)
    sched_classes[SCHED_CLASS_FAIR] = (sched_class_t){
        .name = "fair",
        .type = SCHED_CLASS_FAIR,
        .init = fair_init,
        .enqueue = fair_enqueue,
        .dequeue = fair_dequeue,
        .pick_next = fair_pick_next,
        .tick = fair_tick,
        .should_preempt = fair_should_preempt,
        .yield = fair_yield
    };
    
    // Batch
    sched_classes[SCHED_CLASS_BATCH] = (sched_class_t){
        .name = "batch",
        .type = SCHED_CLASS_BATCH,
        .init = batch_init,
        .enqueue = fair_enqueue,
        .dequeue = fair_dequeue,
        .pick_next = fair_pick_next,
        .tick = batch_tick,
        .should_preempt = fair_should_preempt,
        .yield = fair_yield
    };
    
    // Idle
    sched_classes[SCHED_CLASS_IDLE] = (sched_class_t){
        .name = "idle",
        .type = SCHED_CLASS_IDLE,
        .init = idle_init,
        .enqueue = coop_enqueue,
        .dequeue = coop_dequeue,
        .pick_next = idle_pick_next,
        .tick = coop_tick,
        .should_preempt = idle_should_preempt,
        .yield = coop_yield
    };
}

// ============================================================================
// SCHEDULER HYPERVISOR API
// ============================================================================

void sched_hypervisor_init(void) {
    register_sched_classes();
    
    // Initialize idle tasks
    for (int i = 0; i < NUM_CORES; i++) {
        idle_task[i].pid = 0;
        idle_task[i].state = TASK_READY;
        idle_task[i].priority = 255;
        idle_entity[i].task = &idle_task[i];
        idle_entity[i].vruntime = 0xFFFFFFFF;
        current_task[i] = &idle_task[i];
    }
    
    // Create default domains
    sched_domain_create("realtime", SCHED_CLASS_REALTIME, 0);
    sched_domain_create("normal", SCHED_CLASS_FAIR, 10);
    sched_domain_create("batch", SCHED_CLASS_BATCH, 20);
    sched_domain_create("idle", SCHED_CLASS_IDLE, 255);
}

sched_domain_t *sched_domain_create(const char *name, sched_class_type_t type, u8 priority) {
    if (num_domains >= PICOMIMI_MAX_DOMAINS) return NULL;
    
    static sched_domain_t domain_pool[PICOMIMI_MAX_DOMAINS];
    sched_domain_t *domain = &domain_pool[num_domains];
    
    // Copy name
    for (int i = 0; i < 15 && name[i]; i++) {
        domain->name[i] = name[i];
    }
    domain->name[15] = '\0';
    
    domain->id = num_domains;
    domain->priority = priority;
    domain->sclass = &sched_classes[type];
    domain->timeslice = 10;
    domain->remaining = 10;
    
    // Initialize via class
    domain->sclass->init(domain);
    
    // Insert sorted by priority
    sched_domain_t **pp = &domains[0];
    int idx = 0;
    while (idx < (int)num_domains && domains[idx] && domains[idx]->priority < priority) {
        idx++;
    }
    // Shift and insert
    for (int i = num_domains; i > idx; i--) {
        domains[i] = domains[i-1];
    }
    domains[idx] = domain;
    num_domains++;
    
    return domain;
}

void sched_domain_add_task(sched_domain_t *domain, task_t *task) {
    static sched_entity_t entity_pool[PICOMIMI_MAX_TASKS];
    static u32 entity_idx = 0;
    
    sched_entity_t *se = &entity_pool[entity_idx++];
    se->task = task;
    se->vruntime = 0;
    se->weight = FAIR_WEIGHT_DEFAULT;
    se->deadline = 0;
    se->period = 0;
    
    task->se = se;
    task->domain = domain;
    
    spin_lock(&sched_lock);
    domain->sclass->enqueue(domain, se);
    spin_unlock(&sched_lock);
}

// ============================================================================
// MAIN SCHEDULER ENTRY POINTS
// ============================================================================

void sched_tick(void) {
    system_ticks++;
    
    u32 core = core_id();
    sched_domain_t *domain = current_domain[core];
    
    if (domain && domain->sclass->tick) {
        domain->sclass->tick(domain);
    }
    
    // Check if domain timeslice expired
    if (domain) {
        domain->remaining--;
        if (domain->remaining == 0) {
            domain->remaining = domain->timeslice;
            sched_schedule();
        }
    }
}

void sched_schedule(void) {
    spin_lock(&sched_lock);
    
    u32 core = core_id();
    sched_entity_t *next = NULL;
    sched_domain_t *next_domain = NULL;
    
    // Find highest priority domain with runnable tasks
    for (u32 i = 0; i < num_domains; i++) {
        sched_domain_t *d = domains[i];
        if (d && d->nr_running > 0) {
            sched_entity_t *se = d->sclass->pick_next(d);
            if (se && se->task->state == TASK_READY) {
                // Check core affinity
                if (se->task->core_affinity == 0 || 
                    se->task->core_affinity == core + 1) {
                    next = se;
                    next_domain = d;
                    break;
                }
            }
        }
    }
    
    // Fall back to idle
    if (!next) {
        next = &idle_entity[core];
        next_domain = domains[num_domains - 1];  // Idle domain
    }
    
    task_t *prev = current_task[core];
    task_t *next_task = next->task;
    
    if (prev != next_task) {
        current_task[core] = next_task;
        current_domain[core] = next_domain;
        next_task->state = TASK_RUNNING;
        next_task->current_core = core;
        next_task->switches++;
        
        if (prev && prev->state == TASK_RUNNING) {
            prev->state = TASK_READY;
        }
        
        spin_unlock(&sched_lock);
        
        // Context switch
        context_switch(prev, next_task);
    } else {
        spin_unlock(&sched_lock);
    }
}

void task_yield(void) {
    u32 core = core_id();
    task_t *curr = current_task[core];
    sched_domain_t *domain = curr->domain;
    
    if (domain && domain->sclass->yield) {
        spin_lock(&sched_lock);
        domain->sclass->yield(domain, curr->se);
        spin_unlock(&sched_lock);
    }
    
    sched_schedule();
}

// ============================================================================
// CONTEXT SWITCH (ARM Cortex-M)
// ============================================================================

// Implemented in assembly - saves/restores r4-r11, lr, sp
extern void context_switch(task_t *prev, task_t *next);

// PendSV handler for deferred context switch
void __attribute__((naked)) PendSV_Handler(void) {
    __asm__ volatile(
        // Save current context
        "mrs r0, psp\n"
        "stmdb r0!, {r4-r11, lr}\n"
        
        // Save SP to current task
        "ldr r1, =current_task\n"
        "ldr r2, [r1]\n"
        "str r0, [r2]\n"
        
        // Call scheduler
        "bl sched_schedule\n"
        
        // Load new task SP
        "ldr r1, =current_task\n"
        "ldr r2, [r1]\n"
        "ldr r0, [r2]\n"
        
        // Restore context
        "ldmia r0!, {r4-r11, lr}\n"
        "msr psp, r0\n"
        "bx lr\n"
    );
}

// ============================================================================
// O(1) BITMAP SCHEDULER (Alternative fast path)
// ============================================================================

#define BITMAP_BITS     32

static u32 priority_bitmap = 0;
static task_t *priority_queues[BITMAP_BITS];

static inline int find_first_bit(u32 bitmap) {
    return __builtin_ctz(bitmap);  // Count trailing zeros
}

void bitmap_sched_enqueue(task_t *task) {
    u8 prio = task->priority;
    if (prio >= BITMAP_BITS) prio = BITMAP_BITS - 1;
    
    task->next = priority_queues[prio];
    priority_queues[prio] = task;
    priority_bitmap |= (1 << prio);
}

void bitmap_sched_dequeue(task_t *task) {
    u8 prio = task->priority;
    if (prio >= BITMAP_BITS) prio = BITMAP_BITS - 1;
    
    task_t **pp = &priority_queues[prio];
    while (*pp && *pp != task) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        *pp = task->next;
    }
    
    // Clear bitmap if queue empty
    if (!priority_queues[prio]) {
        priority_bitmap &= ~(1 << prio);
    }
}

task_t *bitmap_sched_pick(void) {
    if (!priority_bitmap) return NULL;
    
    int prio = find_first_bit(priority_bitmap);
    return priority_queues[prio];
}
