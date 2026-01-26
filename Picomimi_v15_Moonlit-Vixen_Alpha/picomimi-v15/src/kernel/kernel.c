/**
 * PICOMIMI-AXISOS v15.0.0-Alpha Kernel Core
 * Ported from v14.3.1 "Quiet Otter" to pure Pico SDK
 */
#include "api/picomimi_kernel.h"
#include "kernel/scheduler.h"
#include "power/governor.h"
#include "shell/shell.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

pm_kernel_state_t g_kernel;
pm_core_scheduler_t g_core0_sched;
static pm_panic_info_t g_panic_info;
static uint32_t heap_offset = 0;

// USB activity tracking (v14.1.1 fix)
static uint32_t usb_last_activity_ms = 0;

// ============================================================================
// COMPILER HINTS
// ============================================================================

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// ============================================================================
// TIME HELPERS
// ============================================================================

static inline uint64_t get_time_us(void) { return time_us_64(); }
static inline uint32_t get_time_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static inline void precise_sleep_us(uint32_t us) { if (us > 0) sleep_us(us); }

// USB activity tracking
static void usb_record_activity(void) { usb_last_activity_ms = get_time_ms(); }
static bool usb_is_active(void) { return (get_time_ms() - usb_last_activity_ms) < 5000; }

// ============================================================================
// TEMPERATURE READING
// ============================================================================

float pm_read_temperature(void) {
    adc_select_input(4);
    uint16_t raw = adc_read();
    float voltage = raw * 3.3f / 4096.0f;
    return 27.0f - (voltage - 0.706f) / 0.001721f;
}

// ============================================================================
// SYSTEM TASKS
// ============================================================================

static void idle_task(void* arg) {
    (void)arg;
    if (!usb_is_active()) __wfi();
}

static void reaper_task(void* arg) {
    (void)arg;
    for (int i = 2; i < PICOMIMI_MAX_TASKS; i++) {
        pm_tcb_t* task = &g_kernel.tasks[i];
        if (task->state == TASK_STATE_ZOMBIE) {
            if (task->callbacks && task->callbacks->deinit) task->callbacks->deinit();
            task->id = PM_INVALID_TASK;
            task->state = TASK_STATE_TERMINATED;
            task->entry = NULL;
            task->callbacks = NULL;
            if (g_kernel.task_count > 0) g_kernel.task_count--;
        }
    }
    pm_task_sleep(1000);
}

static void temp_monitor_task(void* arg) {
    (void)arg;
    g_kernel.governor.temperature = pm_read_temperature();
    if (g_kernel.governor.temperature > g_kernel.governor.temperature_peak)
        g_kernel.governor.temperature_peak = g_kernel.governor.temperature;
    if (g_kernel.governor.temperature > PICOMIMI_THERMAL_LIMIT)
        pm_klog(PICOMIMI_LOG_LEVEL_WARN, "High temp: %.1f C", g_kernel.governor.temperature);
    pm_task_sleep(2000);
}

static void cpu_monitor_task(void* arg) {
    (void)arg;
    pm_task_sleep(1000);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

static void kernel_init_memory(void) {
    memset(g_kernel.heap, 0, PICOMIMI_HEAP_SIZE);
    heap_offset = 0;
    g_kernel.free_memory = PICOMIMI_HEAP_SIZE;
    g_kernel.used_memory = 0;
}

static void kernel_init_tasks(void) {
    for (int i = 0; i < PICOMIMI_MAX_TASKS; i++) {
        g_kernel.tasks[i].id = PM_INVALID_TASK;
        g_kernel.tasks[i].state = TASK_STATE_TERMINATED;
        g_kernel.tasks[i].entry = NULL;
        g_kernel.tasks[i].callbacks = NULL;
        memset(g_kernel.tasks[i].name, 0, PICOMIMI_TASK_NAME_LEN);
    }
    g_kernel.task_count = 0;
    g_kernel.current_task = 0;
}

static void kernel_init_logging(void) {
    g_kernel.log_head = 0;
    g_kernel.log_count = 0;
    g_kernel.log_level = PICOMIMI_LOG_LEVEL_INFO;
}

static void print_boot_banner(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  PICOMIMI-AXISOS v%s                       ║\n", PICOMIMI_VERSION_STRING);
    printf("║  \"%s\" - Resource-Owning Kernel                 ║\n", PICOMIMI_CODENAME);
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Target: %s %s                            ║\n", PICOMIMI_CHIP_NAME, PICOMIMI_CORE_ARCH);
    printf("║  RAM: %luKB | Heap: %luKB | Apps: ~%luKB               ║\n",
           (unsigned long)(PICOMIMI_TOTAL_SRAM / 1024),
           (unsigned long)(PICOMIMI_HEAP_SIZE / 1024),
           (unsigned long)((PICOMIMI_TOTAL_SRAM - PICOMIMI_HEAP_SIZE) / 1024));
    printf("╚═══════════════════════════════════════════════════════════╝\n");
}

// ============================================================================
// KERNEL MAIN API
// ============================================================================

pm_result_t pm_kernel_init(void) {
    memset(&g_kernel, 0, sizeof(g_kernel));
    memset(&g_core0_sched, 0, sizeof(g_core0_sched));
    
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB
    
    adc_init();
    adc_set_temp_sensor_enabled(true);
    
    g_kernel.boot_time_us = get_time_us();
    
    print_boot_banner();
    printf("Initializing AxisOS subsystems...\n");
    
    kernel_init_memory();
    printf("[OK] Memory manager (%luKB heap)\n", (unsigned long)(PICOMIMI_HEAP_SIZE / 1024));
    
    kernel_init_tasks();
    printf("[OK] Task scheduler\n");
    
    kernel_init_logging();
    printf("[OK] Logging system\n");
    
    pm_governor_init(&g_kernel.governor);
    printf("[OK] CPU Governor (%s @ %lu MHz)\n",
           pm_governor_profile_name(g_kernel.governor.current_profile),
           (unsigned long)(g_kernel.governor.current_freq_khz / 1000));
    
    g_kernel.governor.temperature = pm_read_temperature();
    printf("[OK] Temperature (%.1f C)\n", g_kernel.governor.temperature);
    
    usb_record_activity();
    printf("[OK] USB Serial stability\n");
    
    printf("\n=== Loading System Tasks ===\n");
    
    pm_task_id_t id;
    
    id = pm_task_create("idle", idle_task, NULL, 15, CORE_AFFINITY_CORE0);
    if (id != PM_INVALID_TASK) {
        g_kernel.tasks[id].flags |= PICOMIMI_TASK_FLAG_PROTECTED;
        g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_KERNEL;
        printf("[OK] idle (Pri 15)\n");
    }
    
    id = pm_task_create("reaper", reaper_task, NULL, 14, CORE_AFFINITY_CORE0);
    if (id != PM_INVALID_TASK) {
        g_kernel.tasks[id].flags |= PICOMIMI_TASK_FLAG_PROTECTED;
        g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_KERNEL;
        printf("[OK] reaper (Pri 14)\n");
    }
    
    id = pm_task_create_callback("shell", pm_shell_get_callbacks(), NULL, 2, CORE_AFFINITY_CORE0);
    if (id != PM_INVALID_TASK) {
        g_kernel.tasks[id].flags |= PICOMIMI_TASK_FLAG_PROTECTED;
        g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_SERVICE;
        printf("[OK] shell (Pri 2)\n");
    }
    
    id = pm_task_create("tempmon", temp_monitor_task, NULL, 12, CORE_AFFINITY_CORE0);
    if (id != PM_INVALID_TASK) {
        g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_SERVICE;
        printf("[OK] tempmon (Pri 12)\n");
    }
    
    id = pm_task_create("cpumon", cpu_monitor_task, NULL, 12, CORE_AFFINITY_CORE0);
    if (id != PM_INVALID_TASK) {
        g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_SERVICE;
        printf("[OK] cpumon (Pri 12)\n");
    }
    
    g_kernel.initialized = true;
    return PM_OK;
}

pm_result_t pm_kernel_start(void) {
    if (!g_kernel.initialized) return PM_ERROR_INVALID;
    
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  AXISOS BOOT COMPLETE                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    printf("CPU: %s @ %lu MHz (%s)\n", PICOMIMI_CHIP_NAME,
           (unsigned long)(g_kernel.governor.current_freq_khz / 1000), PICOMIMI_CORE_ARCH);
    printf("RAM: %luKB | Heap: %luKB | Free: %luKB\n",
           (unsigned long)(PICOMIMI_TOTAL_SRAM / 1024),
           (unsigned long)(PICOMIMI_HEAP_SIZE / 1024),
           (unsigned long)(g_kernel.free_memory / 1024));
    printf("Tasks: %lu active\n", (unsigned long)g_kernel.task_count);
    printf("Governor: %s (AUTO)\n", pm_governor_profile_name(g_kernel.governor.current_profile));
    printf("Thermal Limit: %d C\n\n", PICOMIMI_THERMAL_LIMIT);
    printf("Type 'help' for commands.\n\n");
    
    pm_scheduler_init();
    pm_scheduler_start();
    pm_shell_prompt();
    
    g_kernel.current_task = 0;
    g_kernel.tasks[0].scheduled_at_us = get_time_us();
    g_kernel.running = true;
    
    return PM_OK;
}

void pm_kernel_tick(void) {
    if (unlikely(!g_kernel.running || g_kernel.panic_mode)) return;
    if (unlikely(g_kernel.current_task >= PICOMIMI_MAX_TASKS || g_kernel.task_count == 0)) {
        pm_kernel_panic("Kernel tick fault");
        return;
    }
    
    uint64_t loop_start = get_time_us();
    g_kernel.uptime_ms = (uint32_t)((loop_start - g_kernel.boot_time_us) / 1000);
    
    pm_tcb_t* task = &g_kernel.tasks[g_kernel.current_task];
    
    if (unlikely(task->id == PM_INVALID_TASK || task->state == TASK_STATE_TERMINATED || 
                 task->state == TASK_STATE_ZOMBIE || task->mem_blocked)) {
        pm_task_yield();
        return;
    }
    
    if (task->wake_time_ms > 0 && g_kernel.uptime_ms < task->wake_time_ms) {
        pm_task_yield();
        return;
    }
    task->wake_time_ms = 0;
    
    if (likely(task->state == TASK_STATE_READY || task->state == TASK_STATE_RUNNING)) {
        if (likely(task->entry || (task->callbacks && task->callbacks->tick))) {
            task->state = TASK_STATE_RUNNING;
            task->scheduled_at_us = loop_start;
            
            uint64_t task_start = get_time_us();
            if (likely(task->callbacks && task->callbacks->tick)) {
                task->callbacks->tick(task->arg);
            } else if (task->entry) {
                task->entry(task->arg);
            }
            
            uint64_t task_duration = get_time_us() - task_start;
            task->cpu_time_ms += (uint32_t)(task_duration / 1000);
            task->total_cpu_time_ms += task_duration / 1000;
            
            if (likely(task->state == TASK_STATE_RUNNING)) task->state = TASK_STATE_READY;
        }
    }
    
    pm_task_yield();
    pm_governor_tick(&g_kernel.governor);
    
    static uint32_t wd_cnt = 0;
    if (++wd_cnt >= 100) { watchdog_update(); wd_cnt = 0; }
    
    uint64_t elapsed = get_time_us() - loop_start;
    if (elapsed < PICOMIMI_SCHED_TICK_US) precise_sleep_us(PICOMIMI_SCHED_TICK_US - elapsed);
}

bool pm_kernel_is_running(void) { return g_kernel.running && !g_kernel.panic_mode; }

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

pm_task_id_t pm_task_create(const char* name, pm_task_entry_t entry, void* arg,
                            uint8_t priority, pm_core_affinity_t affinity) {
    pm_task_id_t slot = PM_INVALID_TASK;
    for (int i = 0; i < PICOMIMI_MAX_TASKS; i++) {
        if (g_kernel.tasks[i].id == PM_INVALID_TASK) { slot = i; break; }
    }
    if (slot == PM_INVALID_TASK) return PM_INVALID_TASK;
    
    pm_tcb_t* task = &g_kernel.tasks[slot];
    task->id = slot;
    task->entry = entry;
    task->callbacks = NULL;
    task->arg = arg;
    task->flags = 0;
    task->mem_used = 0;
    task->mem_peak = 0;
    task->cpu_time_ms = 0;
    task->total_cpu_time_ms = 0;
    task->wake_time_ms = 0;
    task->start_time_ms = g_kernel.uptime_ms;
    task->state = TASK_STATE_READY;
    task->priority = priority;
    task->original_priority = priority;
    task->task_type = PICOMIMI_TASK_TYPE_APPLICATION;
    task->affinity = affinity;
    task->mem_blocked = false;
    task->sched_info.quantum_us = PICOMIMI_SCHED_BASE_QUANTUM_US;
    task->sched_info.base_priority = priority;
    task->sched_info.effective_priority = priority;
    
    if (name) {
        strncpy(task->name, name, PICOMIMI_TASK_NAME_LEN - 1);
        task->name[PICOMIMI_TASK_NAME_LEN - 1] = '\0';
    } else {
        snprintf(task->name, PICOMIMI_TASK_NAME_LEN, "task%lu", (unsigned long)slot);
    }
    
    g_kernel.task_count++;
    return slot;
}

pm_task_id_t pm_task_create_callback(const char* name, pm_module_callbacks_t* callbacks,
                                      void* arg, uint8_t priority, pm_core_affinity_t affinity) {
    pm_task_id_t id = pm_task_create(name, NULL, arg, priority, affinity);
    if (id != PM_INVALID_TASK) {
        g_kernel.tasks[id].callbacks = callbacks;
        if (callbacks && callbacks->init) callbacks->init(id);
    }
    return id;
}

pm_result_t pm_task_terminate(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    if (task->flags & PICOMIMI_TASK_FLAG_PROTECTED) return PM_ERROR_DENIED;
    task->state = TASK_STATE_ZOMBIE;
    return PM_OK;
}

pm_result_t pm_task_suspend(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    task->state = TASK_STATE_SUSPENDED;
    if (task->callbacks && task->callbacks->suspend) task->callbacks->suspend();
    return PM_OK;
}

pm_result_t pm_task_resume(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    if (task->state != TASK_STATE_SUSPENDED) return PM_ERROR_INVALID;
    task->state = TASK_STATE_READY;
    if (task->callbacks && task->callbacks->resume) task->callbacks->resume();
    return PM_OK;
}

pm_result_t pm_task_sleep(uint32_t ms) {
    if (g_kernel.current_task >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    g_kernel.tasks[g_kernel.current_task].wake_time_ms = g_kernel.uptime_ms + ms;
    return PM_OK;
}

void pm_task_yield(void) {
    uint32_t best_task = 0;
    uint8_t best_priority = 255;
    
    for (uint32_t i = 0; i < PICOMIMI_MAX_TASKS; i++) {
        pm_tcb_t* task = &g_kernel.tasks[i];
        if (task->id == PM_INVALID_TASK) continue;
        if (task->state != TASK_STATE_READY && task->state != TASK_STATE_RUNNING) continue;
        if (task->wake_time_ms > 0 && g_kernel.uptime_ms < task->wake_time_ms) continue;
        if (task->priority < best_priority) {
            best_priority = task->priority;
            best_task = i;
        }
    }
    
    if (best_task != g_kernel.current_task) g_core0_sched.level_mask++;
    g_kernel.current_task = best_task;
}

void pm_task_exit(void) { pm_task_terminate(g_kernel.current_task); pm_task_yield(); }
pm_task_id_t pm_task_get_current(void) { return g_kernel.current_task; }

pm_tcb_t* pm_task_get(pm_task_id_t task_id) {
    if (task_id >= PICOMIMI_MAX_TASKS) return NULL;
    if (g_kernel.tasks[task_id].id == PM_INVALID_TASK) return NULL;
    return &g_kernel.tasks[task_id];
}

pm_result_t pm_task_set_priority(pm_task_id_t task_id, uint8_t priority) {
    if (task_id >= PICOMIMI_MAX_TASKS) return PM_ERROR_INVALID;
    pm_tcb_t* task = &g_kernel.tasks[task_id];
    if (task->id == PM_INVALID_TASK) return PM_ERROR_NOTFOUND;
    task->priority = priority;
    task->sched_info.effective_priority = priority;
    return PM_OK;
}

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

void* pm_kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + PICOMIMI_MEM_ALIGNMENT - 1) & ~(PICOMIMI_MEM_ALIGNMENT - 1);
    if (heap_offset + size > PICOMIMI_HEAP_SIZE) return NULL;
    void* ptr = &g_kernel.heap[heap_offset];
    heap_offset += size;
    g_kernel.used_memory += size;
    g_kernel.free_memory = PICOMIMI_HEAP_SIZE - heap_offset;
    g_kernel.mem_stats.total_allocs++;
    if (g_kernel.current_task < PICOMIMI_MAX_TASKS) {
        g_kernel.tasks[g_kernel.current_task].mem_used += size;
    }
    if (g_kernel.used_memory > g_kernel.mem_stats.peak_usage)
        g_kernel.mem_stats.peak_usage = g_kernel.used_memory;
    return ptr;
}

void* pm_kcalloc(size_t count, size_t size) {
    void* ptr = pm_kmalloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}

void pm_kfree(void* ptr) { (void)ptr; g_kernel.mem_stats.total_frees++; }

void* pm_krealloc(void* ptr, size_t new_size) {
    if (!ptr) return pm_kmalloc(new_size);
    void* new_ptr = pm_kmalloc(new_size);
    if (new_ptr) memcpy(new_ptr, ptr, new_size);
    return new_ptr;
}

uint32_t pm_get_free_memory(void) { return g_kernel.free_memory; }
void pm_get_memory_stats(pm_mem_stats_t* stats) { if (stats) *stats = g_kernel.mem_stats; }

pm_mem_pressure_t pm_get_memory_pressure(void) {
    uint32_t free = g_kernel.free_memory;
    if (free < PICOMIMI_MEM_CRITICAL_THRESHOLD) return MEM_PRESSURE_CRITICAL;
    if (free < PICOMIMI_MEM_WARNING_THRESHOLD) return MEM_PRESSURE_HIGH;
    if (free < PICOMIMI_HEAP_SIZE / 4) return MEM_PRESSURE_MODERATE;
    if (free < PICOMIMI_HEAP_SIZE / 2) return MEM_PRESSURE_LOW;
    return MEM_PRESSURE_NONE;
}

// ============================================================================
// LOGGING
// ============================================================================

void pm_klog(uint8_t level, const char* fmt, ...) {
    if (level < g_kernel.log_level) return;
    static const char* names[] = {"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
    printf("[%lu] [%s] ", (unsigned long)g_kernel.uptime_ms, names[level < 6 ? level : 5]);
    va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args);
    printf("\n");
}

void pm_kprint(const char* str) { printf("%s", str); }
void pm_kprintf(const char* fmt, ...) { va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); }

// ============================================================================
// TIME API
// ============================================================================

uint64_t pm_get_time_us(void) { return time_us_64(); }
uint32_t pm_get_time_ms(void) { return to_ms_since_boot(get_absolute_time()); }
void pm_delay_us(uint32_t us) { sleep_us(us); }
void pm_delay_ms(uint32_t ms) { sleep_ms(ms); }

// ============================================================================
// PANIC
// ============================================================================

void pm_kernel_panic(const char* reason) {
    PICOMIMI_DISABLE_IRQ();
    g_kernel.panic_mode = true;
    g_kernel.running = false;
    g_panic_info.reason = reason;
    g_panic_info.timestamp_ms = g_kernel.uptime_ms;
    g_panic_info.task_id = g_kernel.current_task;
    g_panic_info.is_core1 = (get_core_num() == 1);
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      KERNEL PANIC                          ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║ Reason: %-50s║\n", reason);
    printf("║ Task: %-52lu║\n", (unsigned long)g_panic_info.task_id);
    printf("║ Core: %-52d║\n", g_panic_info.is_core1 ? 1 : 0);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    while (1) __wfi();
}

pm_panic_info_t* pm_get_panic_info(void) { return &g_panic_info; }
