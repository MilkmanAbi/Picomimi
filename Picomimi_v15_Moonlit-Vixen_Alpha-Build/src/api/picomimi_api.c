/**
 * PICOMIMI SDK Application API Implementation
 * Ported from v14.3.1 "Quiet Otter"
 */
#include "api/picomimi_api.h"
#include "api/picomimi_kernel.h"
#include "power/governor.h"
#include "resource/resource.h"
#include "ipc/ipc.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern pm_kernel_state_t g_kernel;

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

pm_task_id_t Pico_GetTaskId(void) {
    return g_kernel.current_task;
}

const char* Pico_GetTaskName(pm_task_id_t task_id) {
    if (task_id < PICOMIMI_MAX_TASKS) {
        return g_kernel.tasks[task_id].name;
    }
    return "unknown";
}

void Pico_Sleep(uint32_t ms) {
    pm_task_sleep(ms);
}

void Pico_Yield(void) {
    pm_task_yield();
}

void Pico_Exit(void) {
    pm_task_exit();
}

pm_task_id_t Pico_SpawnOnCore1(const char* name, pm_task_entry_t entry, void* arg, uint8_t priority) {
    // TODO: Implement Core 1 task spawning
    (void)name; (void)entry; (void)arg; (void)priority;
    return PM_INVALID_TASK;
}

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

void* Pico_Alloc(size_t size) {
    return pm_kmalloc(size);
}

void* Pico_AllocZero(size_t size) {
    return pm_kcalloc(1, size);
}

void Pico_Free(void* ptr) {
    pm_kfree(ptr);
}

void* Pico_Realloc(void* ptr, size_t new_size) {
    return pm_krealloc(ptr, new_size);
}

size_t Pico_GetFreeMemory(void) {
    return pm_get_free_memory();
}

size_t Pico_GetMyMemory(void) {
    pm_task_id_t id = Pico_GetTaskId();
    if (id < PICOMIMI_MAX_TASKS) {
        return g_kernel.tasks[id].mem_used;
    }
    return 0;
}

size_t Pico_GetTaskMemory(pm_task_id_t task_id) {
    if (task_id < PICOMIMI_MAX_TASKS) {
        return g_kernel.tasks[task_id].mem_used;
    }
    return 0;
}

bool Pico_IsMemoryLow(void) {
    return pm_get_memory_pressure() >= MEM_PRESSURE_MODERATE;
}

bool Pico_IsMemoryCritical(void) {
    return pm_get_memory_pressure() >= MEM_PRESSURE_CRITICAL;
}

pm_mem_pressure_t Pico_GetMemoryPressure(void) {
    return pm_get_memory_pressure();
}

void Pico_OnOOM(pm_oom_callback_t handler) {
    // TODO: Register OOM handler for current task
    (void)handler;
}

void Pico_OOMDone(uint32_t bytes_freed) {
    // TODO: Notify kernel that OOM cleanup is done
    (void)bytes_freed;
}

void Pico_HintMemoryPressure(void) {
    pm_klog(PICOMIMI_LOG_LEVEL_WARN, "Task %d hints memory pressure", Pico_GetTaskId());
}

void* Pico_AllocAligned(size_t size, size_t alignment) {
    // For now, just return regular allocation
    // TODO: Implement proper aligned allocation
    (void)alignment;
    return pm_kmalloc(size);
}

void* Pico_AllocDMA(size_t size) {
    // DMA-safe memory on RP2040/RP2350 just needs to be word-aligned
    return Pico_AllocAligned(size, 4);
}

// ============================================================================
// IPC
// ============================================================================

bool Pico_SendMessage(pm_task_id_t target_id, pm_ipc_msg_type_t type, void* data, size_t size) {
    return pm_ipc_send(target_id, type, data, size, 0);
}

bool Pico_SendUrgent(pm_task_id_t target_id, pm_ipc_msg_type_t type, void* data, size_t size) {
    return pm_ipc_send(target_id, type, data, size, 15);
}

bool Pico_ReceiveMessage(pm_ipc_message_t* msg) {
    return pm_ipc_receive(msg);
}

bool Pico_Broadcast(pm_ipc_msg_type_t type, void* data, size_t size) {
    // TODO: Implement broadcast
    (void)type; (void)data; (void)size;
    return false;
}

// ============================================================================
// SYNCHRONIZATION
// ============================================================================

bool Pico_MutexInit(uint32_t id) {
    return pm_mutex_init(id);
}

bool Pico_MutexLock(uint32_t id) {
    return pm_mutex_lock(id, PICOMIMI_MUTEX_TIMEOUT_DEFAULT);
}

bool Pico_MutexTryLock(uint32_t id) {
    return pm_mutex_try_lock(id);
}

void Pico_MutexUnlock(uint32_t id) {
    pm_mutex_unlock(id);
}

bool Pico_SemInit(uint32_t id, int32_t initial_count, uint32_t max_count) {
    return pm_sem_init(id, initial_count, max_count);
}

bool Pico_SemWait(uint32_t id, uint32_t timeout_ms) {
    return pm_sem_wait(id, timeout_ms);
}

void Pico_SemPost(uint32_t id) {
    pm_sem_post(id);
}

bool Pico_EventInit(uint32_t id) {
    return pm_event_init(id);
}

uint32_t Pico_EventWait(uint32_t id, uint32_t flags, bool wait_all, uint32_t timeout_ms) {
    return pm_event_wait(id, flags, wait_all, timeout_ms);
}

void Pico_EventSet(uint32_t id, uint32_t flags) {
    pm_event_set(id, flags);
}

void Pico_EventClear(uint32_t id, uint32_t flags) {
    pm_event_clear(id, flags);
}

// ============================================================================
// SYSTEM INFO
// ============================================================================

uint64_t Pico_GetUptime(void) {
    return g_kernel.uptime_ms;
}

float Pico_GetCPU0Usage(void) {
    return g_kernel.cpu_usage;
}

float Pico_GetCPU1Usage(void) {
    return g_kernel.core1.cpu_usage;
}

float Pico_GetTemperature(void) {
    return g_kernel.governor.temperature;
}

uint32_t Pico_GetTaskCount(void) {
    return g_kernel.task_count;
}

const char* Pico_GetVersion(void) {
    return "AxisOS v" PICOMIMI_VERSION_STRING " (" PICOMIMI_CODENAME ")";
}

uint8_t Pico_GetCoreNum(void) {
    return get_core_num();
}

uint32_t Pico_GetTotalRAM(void) {
    return PICOMIMI_TOTAL_SRAM;
}

uint32_t Pico_GetBootTime(void) {
    return (uint32_t)(g_kernel.boot_time_us / 1000);
}

// ============================================================================
// CPU GOVERNOR
// ============================================================================

void Pico_SetCPUProfile(pm_cpu_profile_t profile) {
    pm_governor_set_profile(&g_kernel.governor, profile);
}

pm_cpu_profile_t Pico_GetCPUProfile(void) {
    return g_kernel.governor.current_profile;
}

uint32_t Pico_GetCPUFreqKHz(void) {
    return g_kernel.governor.current_freq_khz;
}

uint32_t Pico_GetCPUFreqMHz(void) {
    return g_kernel.governor.current_freq_khz / 1000;
}

void Pico_RequestTurbo(uint32_t duration_ms) {
    pm_governor_request_turbo(&g_kernel.governor, duration_ms);
}

void Pico_RequestInstantTurbo(void) {
    pm_governor_request_instant_turbo(&g_kernel.governor);
}

bool Pico_IsThermalThrottled(void) {
    return g_kernel.governor.thermal_throttled;
}

void Pico_HintHeavyTaskStart(void) {
    pm_governor_request_instant_turbo(&g_kernel.governor);
}

void Pico_HintHeavyTaskEnd(void) {
    // Governor will automatically scale down based on load
}

// ============================================================================
// GUI FOCUS
// ============================================================================

bool Pico_RequestFocus(void) {
    pm_task_id_t id = Pico_GetTaskId();
    g_kernel.gui_focus_task_id = id;
    return true;
}

void Pico_ReleaseFocus(void) {
    pm_task_id_t id = Pico_GetTaskId();
    if (g_kernel.gui_focus_task_id == (int32_t)id) {
        g_kernel.gui_focus_task_id = -1;
    }
}

bool Pico_HasFocus(void) {
    return g_kernel.gui_focus_task_id == (int32_t)Pico_GetTaskId();
}

void Pico_SetStdout(void (*write_char)(char)) {
    g_kernel.app_write_char = write_char;
}

// ============================================================================
// DISPLAY SHORTCUTS
// ============================================================================

uint16_t Pico_DisplayWidth(void) {
    // TODO: Get from display driver if registered
    return 0;
}

uint16_t Pico_DisplayHeight(void) {
    return 0;
}

void Pico_DisplayClear(uint16_t color) {
    (void)color;
}

void Pico_DisplayText(int16_t x, int16_t y, const char* text, uint16_t color) {
    (void)x; (void)y; (void)text; (void)color;
}

void Pico_DisplayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}

// ============================================================================
// INPUT SHORTCUTS
// ============================================================================

bool Pico_ButtonPressed(uint8_t id) {
    (void)id;
    return false;
}

bool Pico_PollInput(void* event) {
    (void)event;
    return false;
}

// ============================================================================
// FILESYSTEM SHORTCUTS
// ============================================================================

bool Pico_FSAvailable(void) {
    return g_kernel.fs_mounted;
}

int Pico_FileOpen(const char* path, bool write_mode) {
    // TODO: Use PMFS
    (void)path; (void)write_mode;
    return -1;
}

void Pico_FileClose(int fd) {
    (void)fd;
}

int Pico_FileRead(int fd, uint8_t* buffer, uint32_t size) {
    (void)fd; (void)buffer; (void)size;
    return -1;
}

int Pico_FileWrite(int fd, const uint8_t* data, uint32_t size) {
    (void)fd; (void)data; (void)size;
    return -1;
}

// ============================================================================
// LOGGING
// ============================================================================

void Pico_Log(const char* message) {
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "%s", message);
}

void Pico_LogWarn(const char* message) {
    pm_klog(PICOMIMI_LOG_LEVEL_WARN, "%s", message);
}

void Pico_LogError(const char* message) {
    pm_klog(PICOMIMI_LOG_LEVEL_ERROR, "%s", message);
}

void Pico_LogF(const char* fmt, ...) {
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "%s", buffer);
}

// ============================================================================
// RESOURCE MANAGEMENT
// ============================================================================

int8_t Pico_ClaimGPIO(uint8_t pin, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_gpio(pin, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)pin;
}

void Pico_ReleaseGPIO(uint8_t pin) {
    pm_res_release_gpio(pin, Pico_GetTaskId());
}

void Pico_NotifyGPIODirection(uint8_t pin, pm_gpio_dir_t dir) {
    pm_res_gpio_notify_direction(pin, dir, Pico_GetTaskId());
}

void Pico_NotifyGPIOState(uint8_t pin, bool state) {
    pm_res_gpio_notify_state(pin, state, Pico_GetTaskId());
}

int8_t Pico_ClaimSPI(uint8_t bus, uint8_t cs_pin, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_spi(bus, cs_pin, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)bus;
}

void Pico_ReleaseSPI(uint8_t bus) {
    pm_res_release_spi(bus, Pico_GetTaskId());
}

int8_t Pico_ClaimI2C(uint8_t bus, uint8_t device_addr, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_i2c(bus, device_addr, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)bus;
}

void Pico_ReleaseI2C(uint8_t bus, uint8_t device_addr) {
    pm_res_release_i2c(bus, device_addr, Pico_GetTaskId());
}

int8_t Pico_ClaimADC(uint8_t channel, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_adc(channel, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)channel;
}

void Pico_ReleaseADC(uint8_t channel) {
    pm_res_release_adc(channel, Pico_GetTaskId());
}

int8_t Pico_ClaimPWM(uint8_t slice, uint8_t channel, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_pwm(slice, channel, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)(slice * 2 + channel);
}

void Pico_ReleasePWM(uint8_t slice, uint8_t channel) {
    pm_res_release_pwm(slice, channel, Pico_GetTaskId());
}

int8_t Pico_ClaimPIO(uint8_t pio_num, uint8_t sm_num, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_pio(pio_num, sm_num, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)(pio_num * 4 + sm_num);
}

void Pico_ReleasePIO(uint8_t pio_num, uint8_t sm_num) {
    pm_res_release_pio(pio_num, sm_num, Pico_GetTaskId());
}

int8_t Pico_ClaimUART(uint8_t uart_num, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_uart(uart_num, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)uart_num;
}

void Pico_ReleaseUART(uint8_t uart_num) {
    pm_res_release_uart(uart_num, Pico_GetTaskId());
}

int8_t Pico_ClaimDMA(uint8_t channel, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_dma(channel, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)channel;
}

void Pico_ReleaseDMA(uint8_t channel) {
    pm_res_release_dma(channel, Pico_GetTaskId());
}

int8_t Pico_ClaimTimer(uint8_t alarm_num, pm_resource_mode_t mode) {
    pm_res_handle_t h = pm_res_claim_timer(alarm_num, mode, Pico_GetTaskId());
    if (h == PM_INVALID_HANDLE) return -1;
    return (int8_t)alarm_num;
}

void Pico_ReleaseTimer(uint8_t alarm_num) {
    pm_res_release_timer(alarm_num, Pico_GetTaskId());
}

// ============================================================================
// RESOURCE QUERY
// ============================================================================

bool Pico_OwnsGPIO(uint8_t pin) {
    return pm_res_is_owned_by(RES_TYPE_GPIO, pin, Pico_GetTaskId());
}

pm_task_id_t Pico_GetGPIOOwner(uint8_t pin) {
    return pm_res_get_owner(RES_TYPE_GPIO, pin);
}

uint32_t Pico_GetResourceCount(void) {
    return pm_res_count_owned_by(Pico_GetTaskId());
}

// ============================================================================
// SDK REGISTRATION (App Registry)
// ============================================================================

#define MAX_REGISTERED_APPS 16
#define MAX_REGISTERED_DRIVERS 8
#define MAX_REGISTERED_SERVICES 8

typedef struct {
    char name[PICOMIMI_TASK_NAME_LEN];
    void (*spawn_func)(void);
    bool registered;
} app_registry_entry_t;

typedef struct {
    char name[PICOMIMI_TASK_NAME_LEN];
    pm_module_callbacks_t callbacks;
    uint8_t priority;
    uint32_t mem_limit;
    bool auto_start;
    bool registered;
} driver_registry_entry_t;

typedef driver_registry_entry_t service_registry_entry_t;

static app_registry_entry_t g_app_registry[MAX_REGISTERED_APPS];
static uint32_t g_app_count = 0;

static driver_registry_entry_t g_driver_registry[MAX_REGISTERED_DRIVERS];
static uint32_t g_driver_count = 0;

static service_registry_entry_t g_service_registry[MAX_REGISTERED_SERVICES];
static uint32_t g_service_count = 0;

void Picomimi_RegisterApp(const char* name, void (*spawn_func)(void)) {
    if (g_app_count >= MAX_REGISTERED_APPS) {
        printf("[SDK] App registry full, '%s' not registered\n", name);
        return;
    }
    
    app_registry_entry_t* entry = &g_app_registry[g_app_count++];
    strncpy(entry->name, name, PICOMIMI_TASK_NAME_LEN - 1);
    entry->name[PICOMIMI_TASK_NAME_LEN - 1] = '\0';
    entry->spawn_func = spawn_func;
    entry->registered = true;
    
    printf("[SDK] Registered app '%s'\n", name);
}

void Picomimi_RegisterDriver(const char* name, 
                              void (*init_fn)(pm_task_id_t),
                              void (*tick_fn)(void*), 
                              void (*deinit_fn)(void),
                              uint8_t priority, 
                              bool auto_start) {
    if (g_driver_count >= MAX_REGISTERED_DRIVERS) {
        printf("[SDK] Driver registry full, '%s' not registered\n", name);
        return;
    }
    
    driver_registry_entry_t* entry = &g_driver_registry[g_driver_count++];
    strncpy(entry->name, name, PICOMIMI_TASK_NAME_LEN - 1);
    entry->name[PICOMIMI_TASK_NAME_LEN - 1] = '\0';
    entry->callbacks.init = init_fn;
    entry->callbacks.tick = tick_fn;
    entry->callbacks.deinit = deinit_fn;
    entry->priority = (priority > 15) ? 15 : priority;
    entry->auto_start = auto_start;
    entry->registered = true;
    
    printf("[SDK] Registered driver '%s' (pri=%d)\n", name, entry->priority);
}

void Picomimi_RegisterService(const char* name,
                               void (*init_fn)(pm_task_id_t),
                               void (*tick_fn)(void*),
                               void (*deinit_fn)(void),
                               uint8_t priority,
                               uint32_t mem_limit_kb,
                               bool auto_start) {
    if (g_service_count >= MAX_REGISTERED_SERVICES) {
        printf("[SDK] Service registry full, '%s' not registered\n", name);
        return;
    }
    
    service_registry_entry_t* entry = &g_service_registry[g_service_count++];
    strncpy(entry->name, name, PICOMIMI_TASK_NAME_LEN - 1);
    entry->name[PICOMIMI_TASK_NAME_LEN - 1] = '\0';
    entry->callbacks.init = init_fn;
    entry->callbacks.tick = tick_fn;
    entry->callbacks.deinit = deinit_fn;
    entry->priority = (priority > 15) ? 15 : priority;
    entry->mem_limit = mem_limit_kb * 1024;
    entry->auto_start = auto_start;
    entry->registered = true;
    
    printf("[SDK] Registered service '%s' (pri=%d, mem=%luKB)\n", 
           name, entry->priority, (unsigned long)mem_limit_kb);
}

pm_task_id_t Picomimi_StartDriver(const char* name) {
    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (g_driver_registry[i].registered && 
            strcmp(g_driver_registry[i].name, name) == 0) {
            
            driver_registry_entry_t* drv = &g_driver_registry[i];
            pm_task_id_t id = pm_task_create_callback(
                drv->name, &drv->callbacks, NULL, 
                drv->priority, CORE_AFFINITY_CORE0);
            
            if (id != PM_INVALID_TASK) {
                g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_DRIVER;
                g_kernel.tasks[id].flags |= PICOMIMI_TASK_FLAG_PROTECTED;
                printf("[SDK] Started driver '%s' (id=%d)\n", name, id);
            }
            return id;
        }
    }
    
    printf("[SDK] Driver '%s' not found\n", name);
    return PM_INVALID_TASK;
}

pm_task_id_t Picomimi_StartService(const char* name) {
    for (uint32_t i = 0; i < g_service_count; i++) {
        if (g_service_registry[i].registered && 
            strcmp(g_service_registry[i].name, name) == 0) {
            
            service_registry_entry_t* svc = &g_service_registry[i];
            pm_task_id_t id = pm_task_create_callback(
                svc->name, &svc->callbacks, NULL,
                svc->priority, CORE_AFFINITY_CORE0);
            
            if (id != PM_INVALID_TASK) {
                g_kernel.tasks[id].task_type = PICOMIMI_TASK_TYPE_SERVICE;
                g_kernel.tasks[id].flags |= PICOMIMI_TASK_FLAG_PROTECTED;
                g_kernel.tasks[id].mem_limit = svc->mem_limit;
                printf("[SDK] Started service '%s' (id=%d)\n", name, id);
            }
            return id;
        }
    }
    
    printf("[SDK] Service '%s' not found\n", name);
    return PM_INVALID_TASK;
}

void Picomimi_StartDrivers(void) {
    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (g_driver_registry[i].registered && g_driver_registry[i].auto_start) {
            Picomimi_StartDriver(g_driver_registry[i].name);
        }
    }
}

void Picomimi_StartServices(void) {
    for (uint32_t i = 0; i < g_service_count; i++) {
        if (g_service_registry[i].registered && g_service_registry[i].auto_start) {
            Picomimi_StartService(g_service_registry[i].name);
        }
    }
}

// ============================================================================
// APP LISTING (for shell)
// ============================================================================

uint32_t Picomimi_GetAppCount(void) {
    return g_app_count;
}

const char* Picomimi_GetAppName(uint32_t index) {
    if (index < g_app_count) {
        return g_app_registry[index].name;
    }
    return NULL;
}

bool Picomimi_SpawnApp(const char* name) {
    for (uint32_t i = 0; i < g_app_count; i++) {
        if (g_app_registry[i].registered && 
            strcmp(g_app_registry[i].name, name) == 0) {
            if (g_app_registry[i].spawn_func) {
                g_app_registry[i].spawn_func();
                return true;
            }
        }
    }
    return false;
}
