/**
 * Picomimi Synchronization Primitives
 * 
 * Mutex, Semaphore, Message Queue for RTOS
 */

#include "picomimi.h"

// ============================================================================
// MUTEX
// ============================================================================

typedef struct {
    spinlock_t lock;
    task_t *owner;
    u32 count;          // Recursive count
    task_t *waiters;    // Waiting tasks
} mutex_t;

#define MUTEX_INIT { SPINLOCK_INIT, NULL, 0, NULL }

void mutex_init(mutex_t *m) {
    m->lock = (spinlock_t)SPINLOCK_INIT;
    m->owner = NULL;
    m->count = 0;
    m->waiters = NULL;
}

bool mutex_lock(mutex_t *m, u32 timeout_ms) {
    task_t *self = task_current();
    tick_t deadline = timer_ticks() + (timeout_ms * PICOMIMI_TICK_HZ / 1000);
    
    spin_lock(&m->lock);
    
    // Check if we already own it (recursive)
    if (m->owner == self) {
        m->count++;
        spin_unlock(&m->lock);
        return true;
    }
    
    // Try to acquire
    while (m->owner != NULL) {
        // Add to waiters
        self->next = m->waiters;
        m->waiters = self;
        self->state = TASK_BLOCKED;
        
        spin_unlock(&m->lock);
        
        // Check timeout
        if (timeout_ms > 0 && timer_ticks() >= deadline) {
            // Timeout - remove from waiters
            spin_lock(&m->lock);
            task_t **pp = &m->waiters;
            while (*pp && *pp != self) pp = &(*pp)->next;
            if (*pp) *pp = self->next;
            spin_unlock(&m->lock);
            return false;
        }
        
        task_yield();
        spin_lock(&m->lock);
    }
    
    // Acquired
    m->owner = self;
    m->count = 1;
    spin_unlock(&m->lock);
    return true;
}

void mutex_unlock(mutex_t *m) {
    spin_lock(&m->lock);
    
    if (m->owner != task_current()) {
        spin_unlock(&m->lock);
        return;  // Not owner!
    }
    
    m->count--;
    if (m->count == 0) {
        m->owner = NULL;
        
        // Wake first waiter
        if (m->waiters) {
            task_t *waiter = m->waiters;
            m->waiters = waiter->next;
            waiter->state = TASK_READY;
        }
    }
    
    spin_unlock(&m->lock);
}

bool mutex_trylock(mutex_t *m) {
    task_t *self = task_current();
    
    spin_lock(&m->lock);
    
    if (m->owner == NULL || m->owner == self) {
        m->owner = self;
        m->count++;
        spin_unlock(&m->lock);
        return true;
    }
    
    spin_unlock(&m->lock);
    return false;
}

// ============================================================================
// SEMAPHORE
// ============================================================================

typedef struct {
    spinlock_t lock;
    s32 count;
    s32 max_count;
    task_t *waiters;
} semaphore_t;

void sem_init(semaphore_t *s, u32 initial, u32 max) {
    s->lock = (spinlock_t)SPINLOCK_INIT;
    s->count = initial;
    s->max_count = max;
    s->waiters = NULL;
}

bool sem_wait(semaphore_t *s, u32 timeout_ms) {
    task_t *self = task_current();
    tick_t deadline = timer_ticks() + (timeout_ms * PICOMIMI_TICK_HZ / 1000);
    
    spin_lock(&s->lock);
    
    while (s->count <= 0) {
        // Add to waiters
        self->next = s->waiters;
        s->waiters = self;
        self->state = TASK_BLOCKED;
        
        spin_unlock(&s->lock);
        
        if (timeout_ms > 0 && timer_ticks() >= deadline) {
            // Remove from waiters
            spin_lock(&s->lock);
            task_t **pp = &s->waiters;
            while (*pp && *pp != self) pp = &(*pp)->next;
            if (*pp) *pp = self->next;
            spin_unlock(&s->lock);
            return false;
        }
        
        task_yield();
        spin_lock(&s->lock);
    }
    
    s->count--;
    spin_unlock(&s->lock);
    return true;
}

void sem_signal(semaphore_t *s) {
    spin_lock(&s->lock);
    
    if (s->count < s->max_count) {
        s->count++;
    }
    
    // Wake first waiter
    if (s->waiters) {
        task_t *waiter = s->waiters;
        s->waiters = waiter->next;
        waiter->state = TASK_READY;
    }
    
    spin_unlock(&s->lock);
}

bool sem_trywait(semaphore_t *s) {
    spin_lock(&s->lock);
    
    if (s->count > 0) {
        s->count--;
        spin_unlock(&s->lock);
        return true;
    }
    
    spin_unlock(&s->lock);
    return false;
}

// ============================================================================
// MESSAGE QUEUE
// ============================================================================

#define QUEUE_MAX_SIZE  16
#define QUEUE_MSG_SIZE  32

typedef struct {
    spinlock_t lock;
    u8 buffer[QUEUE_MAX_SIZE][QUEUE_MSG_SIZE];
    u32 head;
    u32 tail;
    u32 count;
    u32 capacity;
    u32 msg_size;
    task_t *send_waiters;
    task_t *recv_waiters;
} queue_t;

void queue_init(queue_t *q, u32 capacity, u32 msg_size) {
    q->lock = (spinlock_t)SPINLOCK_INIT;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->capacity = capacity < QUEUE_MAX_SIZE ? capacity : QUEUE_MAX_SIZE;
    q->msg_size = msg_size < QUEUE_MSG_SIZE ? msg_size : QUEUE_MSG_SIZE;
    q->send_waiters = NULL;
    q->recv_waiters = NULL;
}

bool queue_send(queue_t *q, const void *msg, u32 timeout_ms) {
    task_t *self = task_current();
    tick_t deadline = timer_ticks() + (timeout_ms * PICOMIMI_TICK_HZ / 1000);
    
    spin_lock(&q->lock);
    
    while (q->count >= q->capacity) {
        self->next = q->send_waiters;
        q->send_waiters = self;
        self->state = TASK_BLOCKED;
        
        spin_unlock(&q->lock);
        
        if (timeout_ms > 0 && timer_ticks() >= deadline) {
            return false;
        }
        
        task_yield();
        spin_lock(&q->lock);
    }
    
    // Copy message
    u8 *dst = q->buffer[q->tail];
    const u8 *src = (const u8 *)msg;
    for (u32 i = 0; i < q->msg_size; i++) {
        dst[i] = src[i];
    }
    
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    
    // Wake receiver
    if (q->recv_waiters) {
        task_t *waiter = q->recv_waiters;
        q->recv_waiters = waiter->next;
        waiter->state = TASK_READY;
    }
    
    spin_unlock(&q->lock);
    return true;
}

bool queue_recv(queue_t *q, void *msg, u32 timeout_ms) {
    task_t *self = task_current();
    tick_t deadline = timer_ticks() + (timeout_ms * PICOMIMI_TICK_HZ / 1000);
    
    spin_lock(&q->lock);
    
    while (q->count == 0) {
        self->next = q->recv_waiters;
        q->recv_waiters = self;
        self->state = TASK_BLOCKED;
        
        spin_unlock(&q->lock);
        
        if (timeout_ms > 0 && timer_ticks() >= deadline) {
            return false;
        }
        
        task_yield();
        spin_lock(&q->lock);
    }
    
    // Copy message
    u8 *src = q->buffer[q->head];
    u8 *dst = (u8 *)msg;
    for (u32 i = 0; i < q->msg_size; i++) {
        dst[i] = src[i];
    }
    
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    
    // Wake sender
    if (q->send_waiters) {
        task_t *waiter = q->send_waiters;
        q->send_waiters = waiter->next;
        waiter->state = TASK_READY;
    }
    
    spin_unlock(&q->lock);
    return true;
}

// ============================================================================
// EVENT FLAGS
// ============================================================================

typedef struct {
    spinlock_t lock;
    u32 flags;
    task_t *waiters;
} event_t;

void event_init(event_t *e) {
    e->lock = (spinlock_t)SPINLOCK_INIT;
    e->flags = 0;
    e->waiters = NULL;
}

void event_set(event_t *e, u32 bits) {
    spin_lock(&e->lock);
    e->flags |= bits;
    
    // Wake all waiters (they'll check their conditions)
    task_t *waiter = e->waiters;
    while (waiter) {
        waiter->state = TASK_READY;
        waiter = waiter->next;
    }
    e->waiters = NULL;
    
    spin_unlock(&e->lock);
}

void event_clear(event_t *e, u32 bits) {
    spin_lock(&e->lock);
    e->flags &= ~bits;
    spin_unlock(&e->lock);
}

u32 event_wait(event_t *e, u32 bits, bool all, bool clear, u32 timeout_ms) {
    task_t *self = task_current();
    tick_t deadline = timer_ticks() + (timeout_ms * PICOMIMI_TICK_HZ / 1000);
    
    spin_lock(&e->lock);
    
    while (1) {
        u32 match = e->flags & bits;
        bool satisfied = all ? (match == bits) : (match != 0);
        
        if (satisfied) {
            if (clear) {
                e->flags &= ~match;
            }
            spin_unlock(&e->lock);
            return match;
        }
        
        if (timeout_ms > 0 && timer_ticks() >= deadline) {
            spin_unlock(&e->lock);
            return 0;
        }
        
        self->next = e->waiters;
        e->waiters = self;
        self->state = TASK_BLOCKED;
        
        spin_unlock(&e->lock);
        task_yield();
        spin_lock(&e->lock);
    }
}
