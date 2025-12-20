# Picomimi MicroOS v13.0 — Complete Kernel Documentation

**A Real-Time Dual-Core Microkernel for RP2040/RP2350 Microcontrollers**

*Made with determination ฅ(•ㅅ•❀)ฅ and love ˗ˋˏ ♡ ˎˊ˗*

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [What's New in v13](#2-whats-new-in-v13)
3. [System Architecture](#3-system-architecture)
4. [Hardware Requirements](#4-hardware-requirements)
5. [Memory Architecture](#5-memory-architecture)
6. [Task Management System](#6-task-management-system)
7. [Scheduler Architecture](#7-scheduler-architecture)
8. [Memory Manager](#8-memory-manager)
9. [Out-of-Memory (OOM) Killer](#9-out-of-memory-oom-killer)
10. [CPU Protection System](#10-cpu-protection-system)
11. [Inter-Process Communication (IPC)](#11-inter-process-communication-ipc)
12. [RTOS Primitives](#12-rtos-primitives)
13. [PMFS — Picomimi Filesystem](#13-pmfs--picomimi-filesystem)
14. [Dual-Core Architecture](#14-dual-core-architecture)
15. [Kernel Services & System Tasks](#15-kernel-services--system-tasks)
16. [UISocket API](#16-uisocket-api)
17. [Application Development](#17-application-development)
18. [Shell Interface & Commands](#18-shell-interface--commands)
19. [Toolchain (MEOW)](#19-toolchain-meow)
20. [Configuration Reference](#20-configuration-reference)
21. [System Limits](#21-system-limits)
22. [Kernel Panic & Recovery](#22-kernel-panic--recovery)
23. [API Reference](#23-api-reference)
24. [Best Practices](#24-best-practices)
25. [Troubleshooting](#25-troubleshooting)

---

## 1. Executive Summary

### What is Picomimi?

Picomimi MicroOS v13.0 is a **real-time, dual-core microkernel** designed specifically for the RP2040 and RP2350 microcontrollers. It provides a complete operating system environment for resource-constrained embedded devices with features typically found in much larger systems.

### Core Philosophy

Picomimi v13 follows a **microcontroller-first** design philosophy:

- **Instant resource reclamation** — No deferred cleanup, no stagnation
- **Deterministic behavior** — Predictable timing for real-time applications
- **Memory efficiency** — Optimized for constrained environments (120KB heap)
- **Protection without overhead** — Security through resource limits, not sandboxing

### Key Capabilities

| Feature | Description |
|---------|-------------|
| **Dual-Core SMP** | Both ARM Cortex-M0+ cores with task affinity |
| **O(1) Scheduler** | Constant-time task selection via priority bitmap |
| **Best-Fit Memory** | Immediate coalescing, corruption detection |
| **Instant OOM** | Microsecond-precision out-of-memory handling |
| **Priority IPC** | 32-level priority message passing |
| **PMFS Filesystem** | Journaling FS with A/B OTA and tmpfs |
| **RTOS Primitives** | Mutexes, semaphores, event flags |
| **CPU Protection** | Abuse detection and automatic throttling |

---

## 2. What's New in v13

### Major Architectural Changes

#### ACE (App Check Environment) — REMOVED

The v10-v12 sandbox system has been **completely eliminated**. It was:
- Overly complex for microcontroller constraints
- Maintenance burden without proportional benefit
- Causing resource stagnation issues

**Replacement:** Direct resource limits per task with immediate enforcement.

#### Instant OOM Killing

The OOM killer now operates with **microsecond precision**:
- No grace periods causing latency spikes
- Immediate victim selection and termination
- Velocity-based abusive allocator detection

#### Immediate Block Coalescing

Memory blocks are merged **on kfree()**, not during a reaper pass:
```
Before v13: Free → Mark → Wait → Reaper → Coalesce
v13:        Free → Coalesce → Done (instant)
```

#### PMFS Integration

Full filesystem integrated into kernel:
- **Transactional journaling** — Crash-safe operations
- **Write caching** — 8KB write buffer
- **Dual system banks** — A/B partitioning for OTA updates
- **tmpfs RAM disk** — 4KB volatile storage
- **File locking** — Exclusive/shared locks

### Summary of Changes from v12

| Component | v12 | v13 |
|-----------|-----|-----|
| Sandboxing | ACE environment | Removed |
| OOM Response | Deferred, with grace period | Instant |
| Memory Coalescing | Reaper-driven | On-free |
| Filesystem | Basic SD wrapper | Full PMFS |
| Heap Size | 180KB | 120KB (tmpfs allocation) |
| Task Limit | 32 | 24 (temporary) |

### Known Issues in v13.0

| Issue | Impact | Workaround |
|-------|--------|------------|
| Shell commands not wired | `top`, `mem`, `memmap`, `compact`, `listapps` listed in help but not callable | Use `ps`, `oomstat`, `schedstat` instead |
| Core 1 task kill from shell | `kill` rejects task IDs ≥1000 | Kill from code using `brutal_task_kill()` |
| MEM_PRESSURE_HIGH unreachable | Threshold ordering bug — CRITICAL (30KB) shadows HIGH (25KB) | Transitions directly to CRITICAL |
| Alignment inconsistency | `KMEM_ALIGNMENT=8` but code uses 4-byte alignment | No impact on operation |

> These issues will be fixed in v13.1.

---

## 3. System Architecture

### Layered Design

```
┌─────────────────────────────────────────────────────────────┐
│                    User Applications                         │
│               (Registered via Application_Register)          │
├─────────────────────────────────────────────────────────────┤
│                      UISocket API                            │
│     (Focus, IPC, Memory, RTOS primitives, Core1 spawn)      │
├─────────────────────────────────────────────────────────────┤
│   Services    │    Drivers    │    Modules                  │
│  (shell, fs,  │  (input,      │  (user-defined              │
│   monitors)   │   display)    │   extensions)               │
├─────────────────────────────────────────────────────────────┤
│                    Kernel Core                               │
│  ┌──────────┬──────────┬──────────┬──────────┬──────────┐  │
│  │ Task Mgr │ Scheduler│ Mem Mgr  │   IPC    │   PMFS   │  │
│  └──────────┴──────────┴──────────┴──────────┴──────────┘  │
├─────────────────────────────────────────────────────────────┤
│                    Hardware Abstraction                      │
│  ┌──────────────────────┬──────────────────────────────┐   │
│  │      Core 0          │          Core 1              │   │
│  │   (Main Execution)   │    (Offload/Compute)         │   │
│  └──────────────────────┴──────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│              RP2040 / RP2350 Hardware                        │
└─────────────────────────────────────────────────────────────┘
```

### Task Type Hierarchy

Tasks are classified by privilege level:

| Type | Value | Description | OOM Killable |
|------|-------|-------------|--------------|
| `TASK_TYPE_KERNEL` | 0x01 | Core kernel tasks | Never |
| `TASK_TYPE_DRIVER` | 0x02 | Hardware drivers | Never |
| `TASK_TYPE_SERVICE` | 0x04 | System services | Never |
| `TASK_TYPE_MODULE` | 0x08 | Optional modules | Configurable |
| `TASK_TYPE_APPLICATION` | 0x10 | User applications | Yes |

### Component Interaction

```
            ┌────────────────┐
            │  Application   │
            └───────┬────────┘
                    │ UISocket API
            ┌───────▼────────┐
            │   Task Manager │◄────── Scheduler
            └───────┬────────┘           │
                    │                    │
        ┌───────────┼───────────┐       │
        ▼           ▼           ▼       │
   ┌────────┐ ┌──────────┐ ┌────────┐   │
   │  IPC   │ │ Mem Mgr  │ │  PMFS  │   │
   └────────┘ └────┬─────┘ └────────┘   │
                   │                     │
                   ▼                     │
              ┌─────────┐               │
              │   OOM   │───────────────┘
              │ Killer  │  (kills tasks to free memory)
              └─────────┘
```

---

## 4. Hardware Requirements

### Supported Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Raspberry Pi Pico (RP2040) | ✅ Primary | Full support |
| RP2040-based boards | ✅ Supported | Pin mapping may vary |
| RP2350 | ✅ Supported | Enhanced performance |

### Resource Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| Flash | ~200KB | 256KB+ |
| SRAM | 264KB total | 264KB |
| Clock Speed | 125 MHz | 225 MHz |
| SD Card | Optional | FAT16/FAT32 for PMFS |

### Pin Configuration (Default)

```cpp
#define SD_CS       5    // SD Card Chip Select
#define SD_MOSI     19   // SD Card MOSI
#define SD_MISO     16   // SD Card MISO
#define SD_SCK      18   // SD Card Clock
#define BTN_ONOFF   9    // Focus cycle button
```

### Arduino IDE Settings

| Setting | Value |
|---------|-------|
| Board | Raspberry Pi Pico |
| CPU Speed | 225 MHz (recommended) |
| Optimize | `-O3` (Optimize More) |
| USB Stack | Pico SDK |
| Debug Level | None |

---

## 5. Memory Architecture

### Memory Map

```
┌─────────────────────────────────────────┐ 0x20042000
│                                         │
│              Stack Space                │ ~20KB
│           (Both Cores)                  │
│                                         │
├─────────────────────────────────────────┤
│                                         │
│           Kernel Structures             │ ~64KB
│     (TCBs, IPC Pool, Schedulers)        │
│                                         │
├─────────────────────────────────────────┤
│                                         │
│        Kernel Heap (120KB)              │
│   ┌─────────────────────────────────┐   │
│   │      Application Space          │   │ 110KB
│   │    (Dynamic Allocations)        │   │
│   ├─────────────────────────────────┤   │
│   │      Kernel Reserve             │   │ 10KB
│   │   (Emergency allocations)       │   │
│   └─────────────────────────────────┘   │
│                                         │
├─────────────────────────────────────────┤
│         PMFS tmpfs Pool (4KB)           │
├─────────────────────────────────────────┤
│        Static Kernel Data               │
└─────────────────────────────────────────┘ 0x20000000
```

### Memory Constants

```cpp
#define HEAP_SIZE               (120 * 1024)   // 120KB total heap
#define KERNEL_RESERVE          (10 * 1024)    // 10KB kernel-only
#define MAX_MEMORY_BLOCKS       128            // Block tracking limit
#define KMEM_ALIGNMENT          8              // Defined as 8-byte
#define MEM_MIN_SPLIT_SIZE      64             // Minimum block split
#define MEM_COALESCE_THRESHOLD  16             // Coalesce trigger
```

> **Note:** `KMEM_ALIGNMENT` is defined as 8, but `kmalloc()` actually aligns to 4 bytes (`(size + 3) & ~3`). This inconsistency should be resolved in v13.1.

### Memory Pressure Levels

| Level | Enum Value | Threshold | Kernel Action |
|-------|------------|-----------|---------------|
| None | `MEM_PRESSURE_NONE` | Used ≤ 50% | Normal operation |
| Low | `MEM_PRESSURE_LOW` | 50% < Used ≤ 70% | Warning logged |
| Moderate | `MEM_PRESSURE_MODERATE` | Used > 70% | Compact memory |
| High | `MEM_PRESSURE_HIGH` | Free < 25KB | ⚠️ Unreachable (see note) |
| Critical | `MEM_PRESSURE_CRITICAL` | Free < 30KB | OOM killer active |
| Emergency | `MEM_PRESSURE_EMERGENCY` | Free < 15KB | Aggressive killing |

> **⚠️ Implementation Note:** Due to threshold ordering, `MEM_PRESSURE_HIGH` is unreachable — `CRITICAL` (< 30KB) triggers before `HIGH` (< 25KB). This should be fixed in v13.1.

---

## 6. Task Management System

### Task Control Block (TCB)

Each task is represented by a TCB containing:

```cpp
struct TCB {
    // Identity
    uint32_t id;                    // Unique task ID
    char name[24];                  // Human-readable name
    const char* description;        // Purpose description
    
    // State
    TaskState state;                // Current execution state
    uint8_t task_type;              // KERNEL/DRIVER/SERVICE/MODULE/APP
    uint32_t flags;                 // Behavioral flags
    
    // Scheduling
    uint8_t priority;               // Current priority (0-31)
    CoreAffinity affinity;          // Core binding
    TaskSchedInfo sched_info;       // Detailed scheduling data
    
    // Execution
    void (*entry)(void*);           // Entry function pointer
    void* arg;                      // User argument
    ModuleCallbacks* callbacks;     // Optional init/tick/deinit
    
    // Timing
    uint64_t wake_time;             // Wake time (if sleeping)
    uint64_t start_time;            // Task creation time
    uint64_t max_runtime;           // Timeout (0 = unlimited)
    
    // Memory Management
    uint32_t mem_used;              // Current memory usage
    uint32_t mem_peak;              // Peak memory usage
    uint32_t mem_limit;             // Hard limit (bytes)
    uint32_t mem_request_bytes;     // Requested allocation
    bool mem_blocked;               // Blocked from allocation
    uint32_t alloc_velocity;        // Allocations per second
    
    // OOM
    uint8_t oom_priority;           // Kill priority (0=never, 4=first)
    uint32_t oom_bytes_requested;   // Pending OOM request
    
    // IPC
    TaskIPCQueue ipc;               // Per-task message queue
    
    // Statistics
    uint32_t cpu_time;              // Accumulated CPU time (ms)
    uint64_t total_cpu_time_us;     // High-precision CPU time
    uint32_t context_switches;      // Switch count
    uint32_t page_faults;           // Memory allocation failures
    bool is_cpu_abuser;             // Flagged for CPU abuse
};
```

### Task States

```
       ┌──────────────────────────────────────────┐
       │                                          │
       ▼                                          │
┌─────────────┐    schedule    ┌─────────────┐   │
│   READY     │ ─────────────► │   RUNNING   │   │
└─────────────┘                └──────┬──────┘   │
       ▲                              │          │
       │              ┌───────────────┼──────────┤
       │              │               │          │
       │         sleep/block      yield      kill/exit
       │              │               │          │
       │              ▼               │          │
       │       ┌─────────────┐       │          │
       └────── │   WAITING   │ ◄─────┘          │
       wake    └─────────────┘                  │
                                                │
                      ┌─────────────────────────┘
                      │
                      ▼
              ┌─────────────┐    reaper    ┌─────────────┐
              │ TERMINATED  │ ───────────► │   ZOMBIE    │
              └─────────────┘              └─────────────┘
                    │                            │
                    │ (if RESPAWN flag)          │ (cleanup)
                    ▼                            ▼
              ┌─────────────┐              ┌─────────────┐
              │   READY     │              │  (removed)  │
              └─────────────┘              └─────────────┘
```

| State | Value | Description |
|-------|-------|-------------|
| `TASK_READY` | 0 | Waiting for CPU time |
| `TASK_RUNNING` | 1 | Currently executing |
| `TASK_WAITING` | 2 | Sleeping or blocked on resource |
| `TASK_SUSPENDED` | 3 | Manually suspended |
| `TASK_TERMINATED` | 4 | Killed or exited cleanly |
| `TASK_ZOMBIE` | 5 | Dead, awaiting cleanup |

### Task Flags

| Flag | Value | Description |
|------|-------|-------------|
| `TASK_FLAG_PROTECTED` | 0x01 | Cannot be killed normally |
| `TASK_FLAG_CRITICAL` | 0x02 | Kernel panic if killed |
| `TASK_FLAG_RESPAWN` | 0x04 | Auto-restart after death |
| `TASK_FLAG_ONESHOT` | 0x08 | Only one instance allowed |
| `TASK_FLAG_PERSISTENT` | 0x10 | Survives soft resets |
| `TASK_FLAG_OOM_CLEANUP_REQUESTED` | 0x20 | OOM handler pending |

### Task Creation API

```cpp
uint32_t task_create(
    const char* name,           // Task name (max 24 chars)
    void (*entry)(void*),       // Entry function (or NULL for callbacks)
    void* arg,                  // User argument
    uint8_t priority,           // Priority level (0-31)
    uint8_t task_type,          // TASK_TYPE_* constant
    uint32_t flags,             // TASK_FLAG_* combinations
    uint64_t max_runtime_ms,    // Timeout in ms (0 = unlimited)
    uint8_t oom_priority,       // OOM kill priority (0-4)
    uint32_t mem_limit,         // Memory limit in bytes
    uint32_t mem_request,       // Initial memory request
    ModuleCallbacks* callbacks, // Optional callbacks struct
    const char* description,    // Human-readable description
    CoreAffinity affinity       // Core binding (CORE_ANY/0/1)
);
```

### Module Callbacks

For stateless, periodic tasks:

```cpp
struct ModuleCallbacks {
    void (*init)(uint32_t id);    // Called once at creation
    void (*tick)(void* arg);      // Called each scheduler tick
    void (*deinit)();             // Called at termination
};
```

### Core Affinity

| Value | Meaning |
|-------|---------|
| `CORE_ANY` (0) | Run on either core |
| `CORE_0` (1) | Pin to Core 0 only |
| `CORE_1` (2) | Pin to Core 1 only |

---

## 7. Scheduler Architecture

### O(1) Priority Bitmap Scheduler

The scheduler uses a **two-level bitmap** for constant-time task selection:

```
Level Mask (32 bits):
┌─────────────────────────────────────────────────────────────────┐
│ 31 │ 30 │ 29 │ ... │ 24 │ 23 │ ... │ 1 │ 0 │
│ RT │ RT │ RT │     │ RT │    │     │   │   │  (1 = priority has tasks)
└─────────────────────────────────────────────────────────────────┘
        │
        ▼ Find highest set bit
┌─────────────────────────────────────────────────────────────────┐
│ Task Mask for Priority 29:                                      │
│ ┌───┬───┬───┬───┬───┬───┐                                      │
│ │ T5│ T4│ T3│ T2│ T1│ T0│  (1 = task is runnable)              │
│ └───┴───┴───┴───┴───┴───┘                                      │
└─────────────────────────────────────────────────────────────────┘
        │
        ▼ Find first set bit = Next task to run
```

### Scheduler Constants

```cpp
#define SCHEDULER_TICK_US           1000      // 1ms tick interval
#define SCHED_NUM_PRIORITY_LEVELS   32        // Priority levels 0-31
#define SCHED_RT_THRESHOLD          24        // Real-time threshold
#define SCHED_BASE_QUANTUM_US       5000      // 5ms base quantum
#define SCHED_MAX_QUANTUM_US        80000     // 80ms max quantum
#define SCHED_AGING_INTERVAL_MS     500       // Priority aging check
#define SCHED_IDLE_INJECTION_THRESHOLD 85     // CPU% for idle injection
```

### Priority Levels

| Range | Type | Quantum | Aging | Use Case |
|-------|------|---------|-------|----------|
| 0-7 | Background | 80ms | Yes | Idle, cleanup tasks |
| 8-15 | Normal | 40-60ms | Yes | Standard applications |
| 16-23 | Elevated | 20-40ms | Yes | Interactive tasks |
| 24-31 | Real-Time | Fixed 5ms | No | Audio, sensors, critical |

### Priority Aging

Non-real-time tasks that wait too long get temporary priority boosts:

```cpp
// Aging algorithm (every 500ms)
for each task:
    if task.state == READY and task.priority < RT_THRESHOLD:
        wait_time = now - task.last_run
        if wait_time > 1000ms:
            task.priority++  // Boost priority
            update_bitmap()
```

### Idle Injection

When CPU load exceeds 85%, the scheduler periodically forces idle time:

```cpp
if (cpu_load > 85%) {
    every 10 ticks:
        return idle_task;  // Force idle
        idle_injections++;
}
```

### Scheduler Data Structures

```cpp
struct PriorityBitmap {
    uint32_t level_mask;                          // Which priorities have tasks
    uint32_t task_masks[SCHED_NUM_PRIORITY_LEVELS]; // Tasks per priority
};

struct CoreScheduler {
    PriorityBitmap runnable;      // Ready tasks
    PriorityBitmap waiting;       // Blocked tasks
    uint32_t current_task;        // Currently running
    uint32_t idle_task;           // Idle task ID
    uint8_t current_priority;     // Current priority level
    uint64_t last_switch;         // Last context switch time
    float cpu_load;               // Smoothed CPU usage
    float cpu_load_instant;       // Instant CPU usage
    uint32_t switches;            // Context switch count
    uint32_t preemptions;         // Preemption count
    uint32_t idle_injections;     // Forced idle count
    mutex_t lock;                 // Scheduler lock
};
```

### Preemption

Higher-priority tasks preempt lower-priority tasks immediately:

```cpp
void sched_check_preemption() {
    uint32_t task_id;
    int highest_prio = find_highest_ready(&task_id);
    
    if (highest_prio > current_task.priority) {
        kernel.preemption_pending = true;
    }
}
```

---

## 8. Memory Manager

### Design Philosophy

The v13 memory manager is designed for **immediate action**:

1. **Best-Fit Allocation** — Minimize fragmentation
2. **Instant Coalescing** — Merge free blocks on `kfree()`
3. **Corruption Detection** — Magic numbers and canaries
4. **Per-Task Tracking** — Memory ownership and limits

### Memory Block Structure

```cpp
struct MemBlock {
    void* addr;              // Block address
    uint32_t size;           // Block size
    uint32_t owner_id;       // Owning task ID
    uint32_t alloc_seq;      // Allocation sequence number
    uint64_t alloc_time_us;  // Allocation timestamp
    uint64_t last_access_us; // Last access timestamp
    
    // Integrity checking
    uint32_t magic;          // 0xDEADBEEF (alloc) / 0xFEEDFACE (free)
    uint32_t canary_front;   // 0xCAFEBABE (overflow detection)
    uint32_t canary_back;    // 0xCAFEBABE (overflow detection)
    
    // Status
    uint16_t access_count;   // Usage tracking
    uint8_t pressure_level;  // Pressure at allocation
    bool free;               // Is block free?
    bool paged_out;          // Reserved for future paging
    bool pinned;             // Cannot be paged
};
```

### Allocation Algorithm (Best-Fit)

```cpp
void* kmalloc(size_t size, uint32_t task_id) {
    // 1. Align size to 8 bytes
    size = ALIGN_UP(size, KMEM_ALIGNMENT);
    
    // 2. Check memory pressure
    if (free_memory < CRITICAL_THRESHOLD) {
        if (!oom_prevent(size)) {
            oom_killer(size);
        }
    }
    
    // 3. Check task limits (applications only)
    if (task_type == APPLICATION) {
        if (current_usage + size > mem_limit) {
            kill_task();
            return NULL;
        }
        check_velocity_throttle();
    }
    
    // 4. Find best-fit block
    uint32_t best_idx = INVALID;
    uint32_t best_size = MAX_UINT32;
    
    for each block:
        if (block.free && block.size >= size) {
            if (block.size == size) {
                best_idx = i;
                break;  // Exact match
            }
            if (block.size < best_size) {
                best_idx = i;
                best_size = block.size;
            }
        }
    
    // 5. Split if needed
    if (block.size > size + 32) {
        create_new_block(block.addr + size, block.size - size);
        block.size = size;
    }
    
    // 6. Mark allocated
    block.free = false;
    block.owner_id = task_id;
    block.magic = MEM_MAGIC_ALLOCATED;
    
    return block.addr;
}
```

### Deallocation with Immediate Coalescing

```cpp
void kfree(void* ptr) {
    // 1. Find block
    MemBlock* block = find_block(ptr);
    if (!block || block->free) {
        log_error("Invalid/double free");
        return;
    }
    
    // 2. Mark free
    block->free = true;
    block->magic = MEM_MAGIC_FREE;
    free_memory += block->size;
    
    // 3. Merge with next adjacent free block
    for each other_block:
        if (other_block.free && 
            block.addr + block.size == other_block.addr) {
            block.size += other_block.size;
            remove_block(other_block);
            break;
        }
    
    // 4. Merge with previous adjacent free block
    for each other_block:
        if (other_block.free &&
            other_block.addr + other_block.size == block.addr) {
            other_block.size += block.size;
            remove_block(block);
            break;
        }
    
    // 5. Update task statistics
    update_task_memory(owner_id);
}
```

### Memory Integrity Verification

```cpp
bool mem_verify_block_integrity(MemBlock* block) {
    // Check magic number
    uint32_t expected = block->free ? MEM_MAGIC_FREE : MEM_MAGIC_ALLOCATED;
    if (block->magic != expected) {
        panic("Memory corruption: bad magic");
    }
    
    // Check canaries (buffer overflow detection)
    if (block->canary_front != MEM_CANARY_VALUE ||
        block->canary_back != MEM_CANARY_VALUE) {
        panic("Memory corruption: canary violation");
    }
    
    // Sanity check size
    if (block->size == 0 || block->size > HEAP_SIZE) {
        return false;
    }
    
    return true;
}
```

### Velocity Throttling

Applications allocating too quickly are throttled:

```cpp
// If app allocates >4KB in <80ms, throttle
if (current_usage - last_check > VELOCITY_CHECK_CHUNK) {
    delta_time = now - last_check_time;
    
    if (delta_time < VELOCITY_TIME_THRESHOLD_US) {
        log("Velocity throttle triggered");
        task_sleep(250);  // Force slowdown
    }
}
```

### Memory Constants

```cpp
#define MEM_MAGIC_ALLOCATED     0xDEADBEEF
#define MEM_MAGIC_FREE          0xFEEDFACE
#define MEM_CANARY_VALUE        0xCAFEBABE
#define KMEM_ALIGNMENT          8
#define MEM_MIN_SPLIT_SIZE      64
#define MEM_CRITICAL_THRESHOLD  (15 * 1024)
#define MEM_WARNING_THRESHOLD   (25 * 1024)
#define MEM_FRAGMENTATION_CRITICAL 75
```

---

## 9. Out-of-Memory (OOM) Killer

### Design Goals

1. **Instant response** — No waiting, no grace periods
2. **Fair victim selection** — Score-based algorithm
3. **Abusive allocator detection** — Kill memory hogs first
4. **Voluntary cleanup support** — Apps can register handlers

### OOM Trigger Conditions

The OOM killer activates when:
- `kmalloc()` cannot find a suitable block
- Free memory drops below critical threshold
- Memory compaction fails to recover space

### Victim Selection Algorithm

```cpp
int32_t oom_calculate_victim_score(TCB* task, uint32_t mem_used) {
    int32_t score = 0;
    
    // Base score: memory usage (1 point per KB)
    score += mem_used / 1024;
    
    // OOM priority multiplier (0-4)
    score += task->oom_priority * 100;
    
    // Idle time bonus (prefer killing idle tasks)
    uint64_t idle_time = now - task->last_run;
    if (idle_time > 5000ms) score += 200;
    else if (idle_time > 1000ms) score += 50;
    
    // Handler bonus (prefer tasks without handlers)
    if (has_oom_handler(task)) score -= 50;
    
    // CPU abuser penalty
    if (task->is_cpu_abuser) score += 150;
    
    // Protected tasks: never kill
    if (task->flags & TASK_FLAG_CRITICAL) score = -10000;
    if (task->task_type != TASK_TYPE_APPLICATION) score = -10000;
    
    return score;
}
```

### OOM Priority Levels

| Level | Constant | Score Impact | Description |
|-------|----------|--------------|-------------|
| 0 | `OOM_PRIORITY_NEVER` | -10000 | Never killed |
| 1 | `OOM_PRIORITY_CRITICAL` | +100 | Kill only if desperate |
| 2 | `OOM_PRIORITY_HIGH` | +200 | Reluctant to kill |
| 3 | `OOM_PRIORITY_NORMAL` | +300 | Standard priority |
| 4 | `OOM_PRIORITY_LOW` | +400 | Kill first |

### Abusive Allocator Detection

Tasks that allocate aggressively are killed immediately:

```cpp
#define OOM_ABUSIVE_ALLOC_VELOCITY  80      // 80 allocs/second
#define OOM_ABUSIVE_ALLOC_SIZE      (60 * 1024)  // 60KB single alloc

if (task->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY ||
    requested_size > OOM_ABUSIVE_ALLOC_SIZE) {
    if (task->task_type == TASK_TYPE_APPLICATION) {
        log("ABUSIVE ALLOCATOR DETECTED!");
        brutal_task_kill(task->id);
        oom_stats.abusive_kills++;
    }
}
```

### OOM Handler Registration

Applications can register cleanup handlers:

```cpp
void k_register_oom_handler(uint32_t task_id, oom_callback_t callback);
void k_unregister_oom_handler(uint32_t task_id);

// Handler signature
typedef void (*oom_callback_t)(uint32_t bytes_requested);

// Example handler
void my_oom_handler(uint32_t bytes_requested) {
    // Free internal caches
    clear_texture_cache();
    clear_audio_buffer();
    
    // Signal completion
    ui.oom_cleanup_done(my_task_id, freed_bytes);
}
```

### OOM Flow

```
┌──────────────────┐
│ kmalloc() fails  │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐     Yes    ┌──────────────────┐
│ Abusive alloc?   │ ──────────►│ Kill allocator   │
└────────┬─────────┘            └──────────────────┘
         │ No
         ▼
┌──────────────────┐     Yes    ┌──────────────────┐
│ oom_prevent()    │ ──────────►│ Return (success) │
│ (compact memory) │            └──────────────────┘
└────────┬─────────┘
         │ No
         ▼
┌──────────────────┐
│ Select victim    │
│ (highest score)  │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐     Yes    ┌──────────────────┐
│ Has handler?     │ ──────────►│ Request cleanup  │
└────────┬─────────┘            │ (wait for ack)   │
         │ No                   └────────┬─────────┘
         ▼                               │ Timeout?
┌──────────────────┐                     │
│ brutal_task_kill │ ◄───────────────────┘
└──────────────────┘
```

### OOM Statistics

```cpp
struct OOMStats {
    uint32_t requests_sent;        // Handler invocations
    uint32_t voluntary_releases;   // Successful cleanups
    uint32_t forced_kills;         // Brutal kills
    uint32_t total_bytes_reclaimed;
    uint32_t prevention_count;     // Prevented via compaction
    uint32_t abusive_kills;        // Velocity-triggered kills
};
```

---

## 10. CPU Protection System

### CPU Monitoring

The kernel tracks CPU usage per task with **sliding window averaging**:

```cpp
void update_task_cpu_usage(TCB* task, uint64_t cpu_time_us) {
    // Sliding window of 5 samples
    task->sched_info.cpu_samples[task->sched_info.cpu_sample_index] = 
        (float)cpu_time_us / SCHEDULER_TICK_US * 100.0f;
    
    task->sched_info.cpu_sample_index = 
        (task->sched_info.cpu_sample_index + 1) % CPU_ABUSE_SAMPLE_COUNT;
    
    // Calculate average
    float sum = 0;
    for (int i = 0; i < CPU_ABUSE_SAMPLE_COUNT; i++) {
        sum += task->sched_info.cpu_samples[i];
    }
    task->sched_info.cpu_usage_percent = sum / CPU_ABUSE_SAMPLE_COUNT;
}
```

### CPU Thresholds

```cpp
#define CPU_OVERLOAD_THRESHOLD      92.0f   // System overloaded
#define CPU_CRITICAL_THRESHOLD      97.0f   // Emergency action
#define CPU_TASK_ABUSE_THRESHOLD    80.0f   // Single task abuse
#define CPU_ABUSE_SAMPLE_COUNT      5       // Samples for averaging
```

### Overload Handling

When system CPU exceeds critical threshold:

```cpp
void handle_cpu_overload() {
    log("CPU CRITICAL! Looking for abuser...");
    
    TCB* worst_abuser = NULL;
    float worst_usage = 0;
    
    // Find worst offender among applications
    for each application task:
        if (task->sched_info.cpu_usage_percent > worst_usage &&
            task->sched_info.cpu_usage_percent > CPU_TASK_ABUSE_THRESHOLD) {
            worst_abuser = task;
            worst_usage = task->sched_info.cpu_usage_percent;
        }
    
    if (worst_abuser) {
        log("Killing CPU abuser: %s (%.1f%%)", 
            worst_abuser->name, worst_usage);
        worst_abuser->is_cpu_abuser = true;
        brutal_task_kill(worst_abuser->id);
    } else {
        log("No obvious abuser, throttling system");
        task_sleep(100);  // Force breathing room
    }
}
```

---

## 11. Inter-Process Communication (IPC)

### Architecture

Picomimi uses a **priority-aware, pool-based** IPC system:

```
                    ┌─────────────────────────┐
                    │   Global Message Pool   │
                    │    (48 messages max)    │
                    └───────────┬─────────────┘
                                │
            ┌───────────────────┼───────────────────┐
            ▼                   ▼                   ▼
    ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
    │ Task A Queue  │   │ Task B Queue  │   │ Task C Queue  │
    │ Pri 31: msg→  │   │ Pri 31: ─     │   │ Pri 31: msg→  │
    │ Pri 30: ─     │   │ Pri 30: msg→  │   │ Pri 30: ─     │
    │ ...           │   │ ...           │   │ ...           │
    │ Pri 0:  msg→  │   │ Pri 0:  ─     │   │ Pri 0:  ─     │
    └───────────────┘   └───────────────┘   └───────────────┘
```

### Message Structure

```cpp
struct IPCMessage {
    uint32_t sender_id;           // Source task ID
    uint32_t target_id;           // Destination task ID
    IPCMessageType type;          // Message type enum
    uint8_t priority;             // Priority level (0-31)
    uint64_t timestamp;           // Send timestamp (µs)
    uint16_t sequence;            // Sequence number
    uint8_t data[64];             // Payload (64 bytes max)
    uint16_t next;                // Next message in queue
    bool in_use;                  // Pool slot in use
};
```

### Message Types

| Type | Value | Purpose |
|------|-------|---------|
| `IPC_NONE` | 0 | Invalid/empty |
| `IPC_RENDER_FRAME` | 1 | Display update request |
| `IPC_PROCESS_INPUT` | 2 | Input event |
| `IPC_COMPUTE_DATA` | 3 | Computation result |
| `IPC_AUDIO_SAMPLE` | 4 | Audio data |
| `IPC_USER_DEFINED` | 5+ | Application-defined |

### Sending Messages

```cpp
// Via UISocket API
bool ui.send_message_api(
    uint32_t target_id,      // Destination task (or IPC_TARGET_BROADCAST)
    IPCMessageType type,     // Message type
    void* data,              // Payload pointer
    size_t size,             // Payload size (max 64)
    uint8_t priority         // Priority (0-31, higher = processed first)
);

// Broadcast to all tasks
ui.send_message_api(IPC_TARGET_BROADCAST, IPC_USER_DEFINED, &data, sizeof(data), 16);
```

### Receiving Messages

```cpp
IPCMessage msg;
if (ui.receive_message_api(&msg)) {
    switch (msg.type) {
        case IPC_RENDER_FRAME:
            handle_render((RenderRequest*)msg.data);
            break;
        case IPC_USER_DEFINED:
            handle_custom(&msg);
            break;
    }
}
```

### IPC Constants

```cpp
#define MAX_IPC_MESSAGES    48          // Global pool size
#define IPC_MSG_SIZE        64          // Payload size
#define IPC_NULL_MSG        0xFFFF      // Invalid message marker
#define IPC_TARGET_BROADCAST 0xFFFFFFFF // Broadcast target
```

### IPC Statistics

```cpp
struct IPCStats {
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t messages_dropped_pool_full;
    uint32_t messages_dropped_task_full;
    uint32_t broadcasts_sent;
    float avg_queue_depth_global;
    uint32_t max_queue_depth_global;
};
```

---

## 12. RTOS Primitives

### Mutexes with Priority Inheritance

```cpp
struct KMutex {
    bool locked;                    // Lock state
    uint32_t owner_id;              // Current owner task
    uint8_t original_priority;      // Owner's original priority
    TaskWaitNode* wait_list_head;   // Waiting tasks
};
```

**Priority Inheritance:** When a high-priority task waits on a mutex held by a low-priority task, the holder's priority is temporarily boosted.

```cpp
// Lock a mutex
bool k_mutex_lock(uint32_t mutex_id);

// Unlock a mutex
void k_mutex_unlock(uint32_t mutex_id);

// Example
if (k_mutex_lock(MUTEX_DISPLAY)) {
    update_display();
    k_mutex_unlock(MUTEX_DISPLAY);
}
```

### Counting Semaphores

```cpp
struct KSemaphore {
    int32_t count;                  // Current count
    uint32_t max_count;             // Maximum count
    TaskWaitNode* wait_list_head;   // Waiting tasks
};
```

```cpp
// Initialize semaphore
bool k_sem_init(uint32_t sem_id, int32_t initial, uint32_t max);

// Wait (decrement, block if zero)
bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms);

// Post (increment, wake waiters)
void k_sem_post(uint32_t sem_id);

// Example: Producer-Consumer
// Producer
k_sem_wait(SEM_EMPTY, 0);      // Wait for empty slot
produce_item();
k_sem_post(SEM_FULL);          // Signal item available

// Consumer
k_sem_wait(SEM_FULL, 0);       // Wait for item
consume_item();
k_sem_post(SEM_EMPTY);         // Signal slot freed
```

### Event Flags

```cpp
struct KEvent {
    uint32_t flags;                 // 32 event bits
    TaskWaitNode* wait_list_head;   // Waiting tasks
};
```

```cpp
// Wait modes
#define K_EVENT_WAIT_ANY  0   // Wake on any flag set
#define K_EVENT_WAIT_ALL  1   // Wake only when all flags set

// Wait for events
uint32_t k_event_wait(
    uint32_t event_id,      // Event group ID
    uint32_t flags,         // Flags to wait for
    uint8_t mode,           // WAIT_ANY or WAIT_ALL
    bool clear,             // Clear flags on exit
    uint32_t timeout_ms     // Timeout (0 = forever)
);

// Set event flags
void k_event_set(uint32_t event_id, uint32_t flags);

// Example
#define EVT_DATA_READY   (1 << 0)
#define EVT_DISPLAY_DONE (1 << 1)

// Sender
k_event_set(EVENT_APP, EVT_DATA_READY);

// Receiver
uint32_t flags = k_event_wait(EVENT_APP, EVT_DATA_READY, K_EVENT_WAIT_ANY, true, 1000);
if (flags & EVT_DATA_READY) {
    process_data();
}
```

### Resource Limits

```cpp
#define MAX_KERNEL_MUTEXES   16
#define MAX_SEMAPHORES       16
#define MAX_EVENT_FLAGS      16
```

---

## 13. PMFS — Picomimi Filesystem

### Overview

PMFS (Picomimi FileSystem) is a **transactional, journaling filesystem** integrated into the v13 kernel:

- **Journaling** — Crash-safe operations
- **Write caching** — 8KB write buffer
- **Dual banks** — A/B system partitions for OTA
- **tmpfs** — 4KB RAM disk
- **File locking** — Shared/exclusive locks

### Directory Structure

```
/PMFS/                     # Root directory
├── system_a/              # System bank A
├── system_b/              # System bank B (backup/OTA)
├── tmpfs/                 # RAM disk mount point
├── logs/
│   ├── system/            # System logs
│   └── user/              # Application logs
├── data/                  # Persistent data
├── config/                # Configuration files
├── .cache/                # Write cache
├── .journal/              # Journal files
│   └── journal.dat
├── .metadata              # Filesystem metadata
└── .boot                  # Boot flags
```

### PMFS Configuration

```cpp
#define PMFS_VERSION              "3.0.0"
#define PMFS_MAGIC                0x504D4653  // "PMFS"

// Feature flags
#define PMFS_ENABLE_JOURNALING    true
#define PMFS_ENABLE_WRITE_CACHE   true
#define PMFS_ENABLE_COMPRESSION   false  // Future
#define PMFS_ENABLE_ENCRYPTION    false  // Future

// Limits
#define PMFS_MAX_OPEN_FILES       16
#define PMFS_MAX_PATH_LENGTH      256
#define PMFS_MAX_FILENAME         64
#define PMFS_WRITE_CACHE_SIZE     (8 * 1024)   // 8KB
#define PMFS_JOURNAL_ENTRIES      64
#define PMFS_TMPFS_SIZE           (4 * 1024)   // 4KB
#define PMFS_MAX_TMPFS_ENTRIES    32
#define PMFS_MAX_LOCKS            32
```

### File Operations

```cpp
// Open file (returns file descriptor)
int pmfs.open(const char* path, uint32_t flags, uint32_t task_id);

// Flags
#define PMFS_MODE_READ     0x01
#define PMFS_MODE_WRITE    0x02
#define PMFS_MODE_APPEND   0x04
#define PMFS_MODE_CREATE   0x08
#define PMFS_MODE_TRUNCATE 0x10

// Read/Write
int pmfs.read(int fd, uint8_t* buffer, uint32_t size);
int pmfs.write(int fd, const uint8_t* data, uint32_t size);

// Positioning
PMFSStatus pmfs.seek(int fd, uint32_t position);
uint32_t pmfs.tell(int fd);
uint32_t pmfs.size(int fd);
bool pmfs.eof(int fd);

// Close
PMFSStatus pmfs.close(int fd);

// Directory operations
PMFSStatus pmfs.mkdir(const char* path);
PMFSStatus pmfs.rmdir(const char* path, bool recursive);
PMFSStatus pmfs.remove(const char* path);
PMFSStatus pmfs.rename(const char* old_path, const char* new_path);

// Queries
bool pmfs.exists(const char* path);
bool pmfs.is_file(const char* path);
bool pmfs.is_dir(const char* path);
uint32_t pmfs.file_size(const char* path);
```

### tmpfs (RAM Disk)

```cpp
// Create entry with size reservation
PMFSStatus pmfs.tmpfs_create(const char* name, uint32_t size);

// Write data
PMFSStatus pmfs.tmpfs_write(const char* name, const uint8_t* data, uint32_t size);

// Read data
int pmfs.tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);

// Delete entry
PMFSStatus pmfs.tmpfs_delete(const char* name);

// Queries
bool pmfs.tmpfs_exists(const char* name);
uint32_t pmfs.tmpfs_available();

// Maintenance
void pmfs.tmpfs_clear();
void pmfs.tmpfs_compact();  // Defragment
```

### System Banks (A/B OTA)

```cpp
// Get current banks
PMFSSystemBank pmfs.get_active_bank();   // PMFS_BANK_A or PMFS_BANK_B
PMFSSystemBank pmfs.get_backup_bank();

// Switch active bank
PMFSStatus pmfs.set_active_bank(PMFSSystemBank bank);

// Copy bank contents
PMFSStatus pmfs.copy_bank(PMFSSystemBank src, PMFSSystemBank dst);

// Clear bank
PMFSStatus pmfs.clear_bank(PMFSSystemBank bank);

// Verify bank integrity
PMFSStatus pmfs.verify_bank(PMFSSystemBank bank);
```

### Journaling

Every file operation is journaled before execution:

```cpp
enum PMFSJournalOp {
    JOURNAL_OP_CREATE,   // File creation
    JOURNAL_OP_DELETE,   // File deletion
    JOURNAL_OP_WRITE,    // Write operation
    JOURNAL_OP_RENAME,   // Rename/move
    JOURNAL_OP_MKDIR     // Directory creation
};

// On crash, journal is replayed to recover consistent state
PMFSStatus pmfs.replay_journal();
```

### File Locking

```cpp
// Acquire lock (exclusive or shared)
bool pmfs.acquire_lock(const char* path, uint32_t task_id, bool exclusive);

// Release lock
void pmfs.release_lock(const char* path);

// Check lock status
bool pmfs.is_locked(const char* path, uint32_t task_id);
```

### Maintenance Operations

```cpp
// Filesystem check (with optional auto-repair)
PMFSStatus pmfs.fsck(bool auto_repair);

// Defragmentation
PMFSStatus pmfs.defragment();

// Garbage collection
PMFSStatus pmfs.garbage_collect();

// Verify all files
PMFSStatus pmfs.verify_all_files();

// Repair corruption
PMFSStatus pmfs.repair_corruption();
```

### Error Codes

| Code | Meaning |
|------|---------|
| `PMFS_OK` | Success |
| `PMFS_ERROR_NOT_INITIALIZED` | PMFS not initialized |
| `PMFS_ERROR_SD_NOT_FOUND` | SD card not detected |
| `PMFS_ERROR_NO_ROOT` | Root structure missing |
| `PMFS_ERROR_CORRUPT` | Filesystem corruption |
| `PMFS_ERROR_NO_SPACE` | Insufficient space |
| `PMFS_ERROR_FILE_NOT_FOUND` | File doesn't exist |
| `PMFS_ERROR_ACCESS_DENIED` | Permission denied |
| `PMFS_ERROR_FILE_LOCKED` | File is locked |
| `PMFS_ERROR_INVALID_PARAM` | Invalid parameter |
| `PMFS_ERROR_IO_FAILURE` | I/O error |
| `PMFS_ERROR_JOURNAL_FULL` | Journal overflow |
| `PMFS_ERROR_NOT_MOUNTED` | FS not mounted |
| `PMFS_ERROR_ALREADY_EXISTS` | File already exists |

---

## 14. Dual-Core Architecture

### Core Roles

| Core | Role | Tasks | Notes |
|------|------|-------|-------|
| Core 0 | Main execution | 24 max | Runs kernel, services, apps |
| Core 1 | Offload/compute | 8 max | Parallel processing |

### Core 1 Task Spawning

```cpp
// Spawn task on Core 1
uint32_t task_id = ui.spawn_core1_task(
    "renderer",       // Task name
    render_loop,      // Entry function
    &render_data,     // User argument
    20                // Priority
);

// Core 1 task IDs are 1000+
// e.g., first Core 1 task is ID 1000
```

### Core 1 Lifecycle

```cpp
void core1_main() {
    scheduler_init_core1();
    
    while (kernel.core1.running) {
        if (kernel.core1.task_count == 0) {
            sleep_us(1000);  // No tasks, idle
            continue;
        }
        
        // Wake waiting tasks
        for each task:
            if (task.state == WAITING && now >= task.wake_time) {
                task_wake(task);
            }
        
        // Select and run task
        uint32_t task_id = sched_select_next_core1();
        if (task_id != INVALID) {
            execute_task(task_id);
        }
        
        // Update statistics
        update_cpu_stats();
    }
}
```

### Cross-Core Communication

Tasks on different cores communicate via IPC:

```cpp
// Core 0 task sends to Core 1 task
ui.send_message_api(1000, IPC_COMPUTE_DATA, &request, sizeof(request), 20);

// Core 1 task receives
IPCMessage msg;
if (ui.receive_message_api(&msg)) {
    process_request((ComputeRequest*)msg.data);
}
```

### Core 1 Statistics

```cpp
struct Core1State {
    TCB tasks[MAX_CORE1_TASKS];     // Task array
    uint32_t task_count;            // Active tasks
    uint32_t current_task;          // Running task
    bool running;                   // Core active
    uint64_t uptime_us;             // Core uptime
    float cpu_usage;                // CPU usage %
    uint32_t context_switches;      // Switch count
};
```

---

## 15. Kernel Services & System Tasks

### Boot Sequence

```
1. Serial initialization (115200 baud)
2. Mutex initialization
3. SPI/SD card setup
4. Watchdog enable (8s timeout)
5. Input GPIO setup
6. Temperature sensor init
7. Memory manager init
8. Task scheduler init
9. Logging system init
10. IPC manager init
11. RTOS primitives init
12. Core 1 launch
13. PMFS initialization
14. System tasks creation
15. Boot complete
```

### System Tasks

| Task | Priority | Type | Description |
|------|----------|------|-------------|
| `idle` | 0 | KERNEL | CPU idle loop |
| `k_reaper` | 1 | KERNEL | Zombie cleanup |
| `cpumon` | 2 | SERVICE | CPU monitoring |
| `tempmon` | 2 | SERVICE | Temperature monitoring |
| `fs` | 8 | SERVICE | Filesystem maintenance |
| `shell` | 10 | SERVICE | Command interpreter |
| `input_cycle` | 28 | DRIVER | Focus cycle input |

### Idle Task

Runs when no other tasks are ready:

```cpp
void idle_task(void* arg) {
    task_sleep(100);  // Release CPU for 100ms
}
```

### Reaper Task

Cleans up zombie tasks:

```cpp
void k_reaper_task(void* arg) {
    task_sleep(REAPER_INTERVAL_MS);  // Every 2 seconds
    
    if (kernel.zombie_tasks == 0) return;
    
    task_sleep(REAPER_GRACE_PERIOD_MS);  // 500ms grace
    
    for each zombie task:
        // Free memory blocks
        for each block owned by task:
            mark_free(block);
        
        // Update statistics
        reclaimed_bytes += task.mem_used;
        zombies_cleaned++;
}
```

### CPU Monitor

```cpp
void cpu_monitor_task(void* arg) {
    task_sleep(5000);  // Every 5 seconds
    
    if (kernel.cpu_usage > 95.0f) {
        log("High CPU Load: %.1f%%", kernel.cpu_usage);
    }
    
    if (kernel.core1.cpu_usage > 95.0f) {
        log("High CPU Load (Core 1): %.1f%%", kernel.core1.cpu_usage);
    }
}
```

### Temperature Monitor

```cpp
void temp_monitor_task(void* arg) {
    kernel.temperature = read_temperature();
    
    if (kernel.temperature > 70.0f) {
        log("High Temperature: %.1f C", kernel.temperature);
    }
    
    task_sleep(2000);  // Every 2 seconds
}
```

### FS Service

```cpp
void fs_task(void* arg) {
    task_sleep(30000);  // Every 30 seconds
    
    // Flush open write handles
    for each open file:
        if (file.write_mode) {
            file.handle.flush();
            flushed_files++;
        }
}
```

---

## 16. UISocket API

The UISocket provides the application interface to kernel services:

```cpp
struct UISocket {
    // Focus management
    bool (*request_focus)(uint32_t task_id);
    void (*release_focus)(uint32_t task_id);
    void (*register_stdout)(void (*write_char_fn)(char));
    
    // IPC
    bool (*send_message_api)(uint32_t target_id, IPCMessageType type, 
                             void* data, size_t size, uint8_t priority);
    bool (*receive_message_api)(IPCMessage* msg_out);
    
    // Core 1
    uint32_t (*spawn_core1_task)(const char* name, void (*entry)(void*), 
                                  void* arg, uint8_t priority);
    
    // Task control
    void (*task_exit)();
    
    // RTOS primitives
    bool (*mutex_lock)(uint32_t mutex_id);
    void (*mutex_unlock)(uint32_t mutex_id);
    bool (*sem_wait)(uint32_t sem_id, uint32_t timeout_ms);
    void (*sem_post)(uint32_t sem_id);
    uint32_t (*event_wait)(uint32_t event_id, uint32_t flags, 
                           uint8_t mode, bool clear, uint32_t timeout_ms);
    void (*event_set)(uint32_t event_id, uint32_t flags);
    
    // Monitoring
    float (*get_core0_usage)();
    float (*get_core1_usage)();
    uint32_t (*get_task_memory)(uint32_t task_id);
    
    // OOM
    void (*register_oom_handler)(uint32_t task_id, 
                                  void (*handler)(uint32_t bytes));
    void (*oom_cleanup_done)(uint32_t task_id, uint32_t bytes_freed);
    void (*hint_memory_pressure)(uint32_t task_id);
};
```

### Registration

```cpp
void k_register_gui_app(UISocket* socket) {
    socket->request_focus = k_request_gui_focus;
    socket->release_focus = k_release_gui_focus;
    socket->register_stdout = k_register_stdout_target;
    socket->send_message_api = ipc_send_api;
    socket->receive_message_api = ipc_receive_api;
    socket->spawn_core1_task = k_spawn_core1_task;
    socket->task_exit = k_task_exit_api;
    socket->mutex_lock = k_mutex_lock;
    socket->mutex_unlock = k_mutex_unlock;
    socket->sem_wait = k_sem_wait;
    socket->sem_post = k_sem_post;
    socket->event_wait = k_event_wait;
    socket->event_set = k_event_set;
    socket->get_core0_usage = k_get_core0_usage;
    socket->get_core1_usage = k_get_core1_usage;
    socket->get_task_memory = k_get_task_memory_api;
    socket->register_oom_handler = k_register_oom_handler;
    socket->oom_cleanup_done = k_oom_cleanup_done;
    socket->hint_memory_pressure = k_hint_memory_pressure;
}
```

---

## 17. Application Development

### Application Template

```cpp
// MyApp.ino
// Add to kernel project via Arduino IDE: Sketch -> Add File...

static UISocket ui;
static uint32_t my_task_id;

// Main application loop
void spawn_myapp() {
    k_register_gui_app(&ui);
    my_task_id = kernel.current_task;
    
    kout.println("[MyApp] Started!");
    
    // Optional: Register OOM handler
    ui.register_oom_handler(my_task_id, my_oom_handler);
    
    while (1) {
        // Check for messages
        IPCMessage msg;
        if (ui.receive_message_api(&msg)) {
            handle_message(&msg);
        }
        
        // Main logic
        do_work();
        
        // Yield CPU
        task_sleep(100);
    }
}

// OOM cleanup handler
void my_oom_handler(uint32_t bytes_requested) {
    // Free caches, buffers, etc.
    clear_cache();
    ui.oom_cleanup_done(my_task_id, freed_bytes);
}

// Message handler
void handle_message(IPCMessage* msg) {
    switch (msg->type) {
        case IPC_USER_DEFINED:
            // Handle custom message
            break;
    }
}

// Global registration (runs at startup)
struct MyAppReg {
    MyAppReg() {
        Application_Register("myapp", spawn_myapp);
    }
} _myapp_reg;
```

### Memory-Safe Application

```cpp
void spawn_safe_app() {
    k_register_gui_app(&ui);
    
    // Allocate with tracking
    void* buffer = kmalloc(4096, kernel.current_task);
    if (!buffer) {
        kout.println("[SafeApp] Allocation failed!");
        ui.task_exit();
        return;
    }
    
    // Register OOM handler
    ui.register_oom_handler(kernel.current_task, safe_oom_handler);
    
    while (1) {
        // Use buffer safely
        process_data(buffer);
        task_sleep(100);
    }
}

void safe_oom_handler(uint32_t bytes_needed) {
    // Free non-essential memory
    kfree(optional_cache);
    ui.oom_cleanup_done(kernel.current_task, cache_size);
}
```

### Real-Time Application

```cpp
// Audio processing example
void spawn_audio_rt() {
    k_register_gui_app(&ui);
    
    // Spawned with:
    // task_create("audio_rt", spawn_audio_rt, NULL, 28,
    //             TASK_TYPE_APPLICATION, TASK_FLAG_PROTECTED,
    //             0, OOM_PRIORITY_CRITICAL, 8*1024, 8*1024,
    //             NULL, "Real-time audio", CORE_0);
    
    while (1) {
        // Process audio at 48kHz
        process_audio_samples();
        
        // Minimal sleep (1ms)
        task_sleep(1);
    }
}
```

### Multi-Core Application

```cpp
static uint32_t renderer_task_id;

void spawn_multicore_app() {
    k_register_gui_app(&ui);
    
    // Spawn renderer on Core 1
    renderer_task_id = ui.spawn_core1_task(
        "renderer", render_loop, NULL, 20
    );
    
    kout.print("[App] Renderer on Core 1: ");
    kout.println(renderer_task_id);
    
    while (1) {
        // Main logic on Core 0
        update_game_state();
        
        // Send render request to Core 1
        RenderRequest req = { .frame = current_frame };
        ui.send_message_api(renderer_task_id, IPC_RENDER_FRAME,
                           &req, sizeof(req), 20);
        
        task_sleep(16);  // ~60 FPS
    }
}

void render_loop(void* arg) {
    while (1) {
        IPCMessage msg;
        if (ui.receive_message_api(&msg)) {
            if (msg.type == IPC_RENDER_FRAME) {
                RenderRequest* req = (RenderRequest*)msg.data;
                render_frame(req->frame);
            }
        }
        task_sleep(1);
    }
}
```

---

## 18. Shell Interface & Commands

### Shell Access

Connect via Serial at 115200 baud. Prompt format:

```
Picomimi:/~>           # Normal mode
Picomimi:/# #          # Root mode
```

### Command Reference

> **⚠️ Known Issue (v13.0):** Several commands are listed in `help` but not wired up in the shell dispatcher. These are marked with ⚠️ below. The handler functions exist (`cmd_top()`, `cmd_mem()`, etc.) but are not connected to shell input. This will be fixed in a future patch.

#### System Information

| Command | Description | Status |
|---------|-------------|--------|
| `help` | Show all commands | ✓ |
| `ps` | List all tasks | ✓ |
| `taskinfo <id>` | Detailed task info | ✓ |
| `top` | CPU usage by task | ⚠️ Not wired |
| `uptime` | System uptime | ✓ |
| `temp` | Current temperature | ✓ |
| `dmesg` | Kernel message log | ✓ |
| `listapps` | List registered apps | ⚠️ Not wired |

#### Memory

| Command | Description | Status |
|---------|-------------|--------|
| `mem` | Memory overview | ⚠️ Not wired |
| `memmap` | Memory block map | ⚠️ Not wired |
| `compact` | Compact memory | ⚠️ Not wired |
| `oomstat` | OOM statistics | ✓ |

#### Scheduling

| Command | Description |
|---------|-------------|
| `schedstat` | Scheduler statistics |
| `ipcstat` | IPC statistics |
| `rtos_stat` | RTOS primitive status |

#### Filesystem

| Command | Description |
|---------|-------------|
| `ls [path]` | List directory |
| `cd <path>` | Change directory |
| `cat <file>` | Display file contents |
| `touch <file>` | Create empty file |
| `mkdir <dir>` | Create directory |
| `rm <path>` | Remove file/directory |
| `write <file> <text>` | Write to file |
| `stat` | Filesystem statistics |
| `tree` | Directory tree view |
| `logtail [n]` | Show last n log lines |
| `logls` | List log files |

#### PMFS Specific

| Command | Description |
|---------|-------------|
| `pmfs` | PMFS statistics |
| `tmpfs` | tmpfs status |
| `bank` | System bank info |
| `fsck` | Filesystem check |
| `defrag` | Defragment FS |
| `format` | Format PMFS |

#### Applications

| Command | Description |
|---------|-------------|
| `listapps` | Show registered apps |
| `<appname>` | Launch application |
| `kill <id>` | Terminate task |
| `app block list` | List blocked apps |
| `app block unlock <name>` | Unblock app |

#### System Control

| Command | Description |
|---------|-------------|
| `root` | Toggle root mode |
| `reboot` | Reboot system |

### Example Session

```
================================================
 Picomimi Kernel v13 Mach 1
 Advanced Instant OOM
================================================
Initializing...
[OK] Input system
[OK] Temperature (34.2C)
[OK] Memory manager (Best-Fit + Merge-on-Free)
[OK] Task scheduler (Preemptive O(1))
[OK] Logging system
[IPC] O(1) Pool Manager initialized
[PMFS] Initializing PicoMimi FileSystem v3.0.0
[PMFS] FILESYSTEM MOUNTED
========================================
Kernel boot complete!
========================================
Heap: 110 KB (App) + 10 KB (Sys)
Core0 Tasks: 7
Core1: Ready for offload
Apps: 2 registered
SD Card: Available

Type 'help' for commands
Picomimi:/~> ps
=== Task List ===
ID Name                 Type      State    CPU%  Mem   Pri
-- -------------------- --------- -------- ----- ----- ---
0  idle                 KERNEL    READY    0.0%  0KB   0
1  k_reaper             KERNEL    WAITING  0.1%  1KB   1
2  input_cycle          DRIVER    READY    0.0%  1KB   28
3  shell                SERVICE   RUNNING  0.3%  4KB   10
4  cpumon               SERVICE   WAITING  0.0%  2KB   2
5  tempmon              SERVICE   WAITING  0.0%  2KB   2
6  fs                   SERVICE   WAITING  0.0%  4KB   8

Picomimi:/~> mem
=== Memory Status ===
Total:       120 KB
Free:        102 KB
Used:        18 KB
App Reserve: 110 KB
Sys Reserve: 10 KB
Blocks:      12
Fragmentation: 2.3%
Pressure: NONE

Picomimi:/~> listapps
=== Registered Applications ===
1. myapp
2. game

Picomimi:/~> myapp
[MyApp] Started!
```

---

## 19. Toolchain (MEOW)

### MEOW — MicroOS Engineering Orchestration Workbench

The MEOW toolchain manages modular development:

| Tool | Name | Purpose |
|------|------|---------|
| **MRRP.py** | Monolithic Repartition & Refactor Program | Split kernel into modules |
| **MIAU.py** | Monolithic INO Aggregator Utility | Assemble modules into kernel |
| **NYAA.py** | Normalize Your Architecture Automatically | Surgical code edits via JSON |
| **MROW.py** | Mend & Review Our Weirdness | Verification and validation |

### Workflow

```
┌─────────────────┐
│ Picomimi_v13.ino│  (Monolithic kernel)
└────────┬────────┘
         │ MRRP.py (split)
         ▼
┌─────────────────┐
│    /modules/    │
│  ├── core.ino   │
│  ├── memory.ino │
│  ├── sched.ino  │
│  ├── ipc.ino    │
│  ├── pmfs.ino   │
│  └── shell.ino  │
└────────┬────────┘
         │ Edit modules
         ▼
┌─────────────────┐
│  NYAA.py (fix)  │  Apply JSON patches
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  MROW.py (check)│  Verify syntax/structure
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  MIAU.py (build)│  Reassemble kernel
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Picomimi_v13.ino│  (Updated monolithic)
└─────────────────┘
```

---

## 20. Configuration Reference

### System Limits

```cpp
// Task configuration
#define MAX_TASKS                24    // Core 0 task limit
#define MAX_CORE1_TASKS          8     // Core 1 task limit
#define TASK_NAME_LEN            24    // Max task name length

// Memory configuration
#define HEAP_SIZE                (120 * 1024)
#define KERNEL_RESERVE           (10 * 1024)
#define MAX_MEMORY_BLOCKS        128

// IPC configuration
#define MAX_IPC_MESSAGES         48
#define IPC_MSG_SIZE             64

// Scheduler configuration
#define SCHEDULER_TICK_US        1000
#define SCHED_NUM_PRIORITY_LEVELS 32
#define SCHED_RT_THRESHOLD       24
#define SCHED_BASE_QUANTUM_US    5000
#define SCHED_MAX_QUANTUM_US     80000

// RTOS primitives
#define MAX_KERNEL_MUTEXES       16
#define MAX_SEMAPHORES           16
#define MAX_EVENT_FLAGS          16

// OOM configuration
#define OOM_REQUEST_TIMEOUT_MS   1500
#define MAX_OOM_HANDLERS         16
#define OOM_ABUSIVE_ALLOC_VELOCITY 80
#define OOM_ABUSIVE_ALLOC_SIZE   (60 * 1024)
#define MAX_APP_MEM_REQUEST_GLOBAL (75 * 1024)

// Protection thresholds
#define MEM_CRITICAL_THRESHOLD   (15 * 1024)
#define MEM_WARNING_THRESHOLD    (25 * 1024)
#define CPU_OVERLOAD_THRESHOLD   92.0f
#define CPU_CRITICAL_THRESHOLD   97.0f
#define CPU_TASK_ABUSE_THRESHOLD 80.0f

// Watchdog
#define WATCHDOG_TIMEOUT_MS      8000

// Filesystem
#define FS_MAX_FILENAME          32
#define FS_MAX_OPEN_FILES        8
#define FS_BUFFER_SIZE           512
#define MAX_APPS                 16
#define MAX_GUI_APPS             8
#define MAX_LOG_ENTRIES          40
```

### Pin Definitions

```cpp
#define SD_CS    5     // SD Chip Select
#define SD_MOSI  19    // SD MOSI
#define SD_MISO  16    // SD MISO
#define SD_SCK   18    // SD Clock
#define BTN_ONOFF 9    // Focus button
```

### PMFS Configuration

```cpp
#define PMFS_WRITE_CACHE_SIZE    (8 * 1024)
#define PMFS_TMPFS_SIZE          (4 * 1024)
#define PMFS_JOURNAL_ENTRIES     64
#define PMFS_MAX_OPEN_FILES      16
#define PMFS_MAX_PATH_LENGTH     256
#define PMFS_MAX_FILENAME        64
#define PMFS_MAX_LOCKS           32
```

---

## 21. System Limits

### Hard Limits

| Resource | Limit | Notes |
|----------|-------|-------|
| Core 0 Tasks | 24 | Reduced from 32 in v12 |
| Core 1 Tasks | 8 | Offload tasks |
| Memory Blocks | 128 | Tracking entries |
| Heap Size | 120 KB | Down from 180KB |
| Kernel Reserve | 10 KB | Emergency allocations |
| App Memory Limit | 75 KB | Per-application |
| IPC Messages | 48 | Global pool |
| IPC Payload | 64 bytes | Per message |
| Priority Levels | 32 | 0-31 |
| Mutexes | 16 | |
| Semaphores | 16 | |
| Event Flags | 16 | |
| OOM Handlers | 16 | |
| Open Files | 8 | Kernel FS |
| PMFS Open Files | 16 | PMFS FS |
| Log Entries | 40 | Ring buffer |
| Registered Apps | 16 | |
| GUI Apps | 8 | Focus-capable |

### Recommended Operating Ranges

| Metric | Safe | Warning | Critical |
|--------|------|---------|----------|
| Task Count | < 16 | 16-20 | > 20 |
| Memory Usage | < 70% | 70-85% | > 85% |
| CPU Usage | < 75% | 75-90% | > 90% |
| Fragmentation | < 30% | 30-50% | > 50% |
| Temperature | < 50°C | 50-70°C | > 70°C |

---

## 22. Kernel Panic & Recovery

### Panic Triggers

- Idle task death
- Critical task killed
- Memory corruption detected
- Watchdog timeout
- Invalid kernel state

### Panic Display

```
╔═══════════════════════════════════════╗
║          *** KERNEL PANIC ***         ║
╚═══════════════════════════════════════╝

Reason: IDLE TASK DEAD
Core: 0
Task: idle (ID=0)
Uptime: 1234 s

--- System State ---
Tasks: 12
Memory: 45/120 KB
CPU Usage: 34.5%

System halted. Watchdog will reset in 8s...
```

### Panic Handler

```cpp
void kernel_panic(const char* reason) {
    if (in_panic) {
        // Double panic - just halt
        while(1) { watchdog_update(); }
    }
    
    in_panic = true;
    disable_all_interrupts();
    
    // Record panic info
    last_panic.reason = reason;
    last_panic.task_id = kernel.current_task;
    last_panic.timestamp = get_time_ms();
    last_panic.is_core1 = (get_core_num() == 1);
    
    // Display panic screen
    print_panic_info();
    
    // Log to PMFS
    if (kernel.fs_mounted) {
        pmfs.log_system(panic_msg);
        pmfs.emergency_unmount();
    }
    
    // Wait for watchdog reset
    while(1) {
        watchdog_update();
        delay(100);
    }
}
```

### Watchdog Integration

The hardware watchdog resets the system if the kernel stops responding:

```cpp
// Watchdog configuration
#define WATCHDOG_TIMEOUT_MS  8000  // 8 second timeout

void watchdog_init() {
    if (watchdog_caused_reboot()) {
        log("!!! REBOOT BY WATCHDOG !!!");
        watchdog_state.triggers++;
    }
    
    watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
}

void watchdog_feed() {
    if (!in_panic) {
        watchdog_update();
    }
}
```

### Recovery After Panic

1. Watchdog triggers reset after 8 seconds
2. Kernel boots normally
3. PMFS runs fsck if `needs_fsck` flag set
4. Panic count visible in `dmesg`
5. Check `/PMFS/logs/system/` for panic logs

---

## 23. API Reference

### Memory Management

```cpp
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);
size_t get_free_memory();
size_t get_used_memory();
size_t get_task_memory(uint32_t task_id);
void mem_compact();
bool is_memory_critical();
bool is_memory_warning();
```

### Task Management

```cpp
uint32_t task_create(const char* name, void (*entry)(void*), void* arg,
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, uint32_t mem_request,
                     ModuleCallbacks* callbacks, const char* description,
                     CoreAffinity affinity);
void brutal_task_kill(uint32_t id);
void task_sleep(uint32_t ms);
void task_wake(uint32_t task_id);
void task_yield();
void k_task_exit_api();
```

### IPC

```cpp
bool ipc_send_api(uint32_t target_id, IPCMessageType type,
                  void* data, size_t size, uint8_t priority);
bool ipc_receive_api(IPCMessage* msg_out);
```

### RTOS Primitives

```cpp
bool k_mutex_lock(uint32_t mutex_id);
void k_mutex_unlock(uint32_t mutex_id);
bool k_sem_init(uint32_t sem_id, int32_t initial, uint32_t max);
bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms);
void k_sem_post(uint32_t sem_id);
bool k_event_init(uint32_t event_id);
uint32_t k_event_wait(uint32_t event_id, uint32_t flags,
                      uint8_t mode, bool clear, uint32_t timeout_ms);
void k_event_set(uint32_t event_id, uint32_t flags);
```

### OOM

```cpp
void k_register_oom_handler(uint32_t task_id, oom_callback_t callback);
void k_unregister_oom_handler(uint32_t task_id);
void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed);
void k_hint_memory_pressure(uint32_t task_id);
```

### Filesystem

```cpp
void fs_init();
bool fs_mount();
void fs_unmount();
void fs_list(const char* path);
void fs_cat(const char* path);
bool fs_write_new(const char* path, const char* content, bool append);
bool fs_mkdir(const char* path);
bool fs_remove(const char* path);
int fs_open(const char* path, bool write_mode);
void fs_close(int fd);
```

### Utility

```cpp
void klog(uint8_t level, const char* msg);
float read_temperature();
void Application_Register(const char* name, void (*spawn_func)());
```

---

## 24. Best Practices

### Memory Management

1. **Always check allocation results**
   ```cpp
   void* ptr = kmalloc(size, task_id);
   if (!ptr) {
       handle_allocation_failure();
       return;
   }
   ```

2. **Register OOM handlers for large allocations**
   ```cpp
   ui.register_oom_handler(my_task_id, my_oom_handler);
   ```

3. **Free memory promptly**
   ```cpp
   // Don't: Hold memory longer than needed
   // Do: Free as soon as possible
   kfree(temp_buffer);
   ```

4. **Respect memory limits**
   ```cpp
   // Request reasonable amounts
   task_create(..., mem_limit=8*1024, ...);
   ```

### Task Design

1. **Yield frequently**
   ```cpp
   while (1) {
       do_work();
       task_sleep(10);  // Don't hog CPU
   }
   ```

2. **Use appropriate priorities**
   - 24-31: Real-time only
   - 16-23: Interactive
   - 8-15: Background
   - 0-7: Idle/cleanup

3. **Handle messages promptly**
   ```cpp
   while (1) {
       // Check messages first
       IPCMessage msg;
       while (ui.receive_message_api(&msg)) {
           handle_message(&msg);
       }
       do_work();
       task_sleep(10);
   }
   ```

### IPC Best Practices

1. **Use appropriate priorities**
   - High priority for time-critical messages
   - Low priority for bulk data

2. **Check send results**
   ```cpp
   if (!ui.send_message_api(...)) {
       // Queue full or target dead
       handle_send_failure();
   }
   ```

3. **Don't block on receive**
   ```cpp
   // Check and continue if no message
   if (ui.receive_message_api(&msg)) {
       handle_message(&msg);
   }
   ```

### Filesystem

1. **Close files promptly**
   ```cpp
   int fd = fs_open(path, false);
   // ... use file ...
   fs_close(fd);  // Don't forget!
   ```

2. **Use tmpfs for temporary data**
   ```cpp
   pmfs.tmpfs_create("temp", 256);
   pmfs.tmpfs_write("temp", data, size);
   // ... use ...
   pmfs.tmpfs_delete("temp");
   ```

3. **Check error codes**
   ```cpp
   if (pmfs.open(path, flags, task_id) < 0) {
       // Handle error
   }
   ```

---

## 25. Troubleshooting

### Common Issues

#### Application Won't Start

```
MEM_PROTECT: Launch rejected, app X is blocked.
```
**Cause:** App previously exceeded memory limit
**Fix:** `app block unlock <appname>`

#### Out of Memory

```
!!! OUT OF MEMORY !!!
Need: XX KB
```
**Cause:** Heap exhausted
**Fix:** 
- Check for memory leaks
- Reduce task memory limits
- Add OOM handlers

#### High CPU Usage

```
CPU CRITICAL! Looking for abuser...
```
**Cause:** Task consuming excessive CPU
**Fix:**
- Add `task_sleep()` calls
- Reduce work per tick
- Lower priority

#### Watchdog Reset

```
!!! REBOOT BY WATCHDOG !!!
```
**Cause:** Kernel stopped responding
**Fix:**
- Check for infinite loops
- Ensure watchdog is fed
- Check for deadlocks

#### Filesystem Issues

```
[PMFS] ERROR: Filesystem corruption
```
**Fix:** Run `fsck` command

### Debug Commands

```bash
# Memory status (⚠️ mem/memmap not wired - use oomstat for now)
oomstat              # Works - shows OOM statistics

# CPU status (⚠️ top not wired - use schedstat)
schedstat            # Works - scheduler statistics

# Task status
ps                   # Works - list all tasks
taskinfo <id>        # Works - detailed task info

# System log
dmesg                # Works - kernel messages

# Filesystem
stat                 # Works - FS statistics
fsck                 # Works - filesystem check
```

> **Workaround:** Until `mem`, `memmap`, and `top` are wired, use `ps` and `oomstat` for memory info, and `schedstat` for CPU info.

### Debug Code

```cpp
// Add logging
klog(0, "Reached checkpoint A");

// Check memory
kout.print("Free memory: ");
kout.println(get_free_memory());

// Monitor CPU
kout.print("Core0 CPU: ");
kout.println(ui.get_core0_usage());

// Track allocations
void* ptr = kmalloc(size, task_id);
kout.print("Allocated at: 0x");
kout.println((uint32_t)ptr, HEX);
```

---

## License

**MIT License**

Copyright (c) 2024-2025 Picomimi Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.

---

*Picomimi MicroOS v13.0 — Built for microcontrollers, designed for reliability*

*Documentation generated from source analysis of `Picomimi_v13_0_Foxxo-Base.ino` (7773 lines)*
