/**
 * PICOMIMI-AXISOS v15.0.0-Alpha
 * Main Public API Header - Include this in your apps!
 * 
 * Usage:
 *   #include "picomimi.h"
 *   
 *   void my_app(void* arg) {
 *     int led = Pico.ClaimGPIO(25);
 *     if (led >= 0) {
 *       gpio_init(led);
 *       gpio_set_dir(led, GPIO_OUT);
 *       while (1) {
 *         gpio_put(led, 1);
 *         Pico.Sleep(500);
 *         gpio_put(led, 0);
 *         Pico.Sleep(500);
 *       }
 *     }
 *   }
 */
#ifndef PICOMIMI_H
#define PICOMIMI_H

#include "picomimi_kernel.h"
#include "picomimi_types.h"
#include "ipc/ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// PICOMIMI API - The main interface for applications
// ============================================================================

typedef struct {
    // === TASK MANAGEMENT ===
    pm_task_id_t (*GetTaskId)(void);
    const char* (*GetTaskName)(pm_task_id_t id);
    void (*Sleep)(uint32_t ms);
    void (*Yield)(void);
    void (*Exit)(void);
    
    // === MEMORY ===
    void* (*Alloc)(size_t size);
    void (*Free)(void* ptr);
    size_t (*GetFreeMemory)(void);
    size_t (*GetMyMemory)(void);
    bool (*IsMemoryLow)(void);
    bool (*IsMemoryCritical)(void);
    
    // === IPC ===
    bool (*SendMessage)(pm_task_id_t target, pm_ipc_msg_type_t type, void* data, size_t size);
    bool (*ReceiveMessage)(pm_ipc_message_t* msg);
    bool (*Broadcast)(pm_ipc_msg_type_t type, void* data, size_t size);
    
    // === SYNCHRONIZATION ===
    pm_result_t (*MutexLock)(pm_kmutex_t* mtx, uint32_t timeout_ms);
    pm_result_t (*MutexUnlock)(pm_kmutex_t* mtx);
    pm_result_t (*SemWait)(pm_ksemaphore_t* sem, uint32_t timeout_ms);
    pm_result_t (*SemPost)(pm_ksemaphore_t* sem);
    
    // === SYSTEM INFO ===
    uint64_t (*GetUptime)(void);
    float (*GetTemperature)(void);
    uint32_t (*GetTaskCount)(void);
    uint32_t (*GetCPUFreqMHz)(void);
    
    // === CPU GOVERNOR ===
    void (*SetCPUProfile)(pm_cpu_profile_t profile);
    pm_cpu_profile_t (*GetCPUProfile)(void);
    void (*RequestTurbo)(uint32_t duration_ms);
    bool (*IsThermalThrottled)(void);
    
    // === RESOURCE CLAIMING ===
    int8_t (*ClaimGPIO)(uint8_t pin);
    void (*ReleaseGPIO)(uint8_t pin);
    int8_t (*ClaimADC)(uint8_t channel);
    void (*ReleaseADC)(uint8_t channel);
    int8_t (*ClaimSPI)(uint8_t bus, uint8_t cs_pin);
    void (*ReleaseSPI)(uint8_t bus);
    int8_t (*ClaimI2C)(uint8_t bus, uint8_t addr);
    void (*ReleaseI2C)(uint8_t bus, uint8_t addr);
    int8_t (*ClaimPWM)(uint8_t slice, uint8_t channel);
    void (*ReleasePWM)(uint8_t slice, uint8_t channel);
    
    // === LOGGING ===
    void (*Log)(const char* message);
    void (*LogWarn)(const char* message);
    void (*LogError)(const char* message);
    
    // === VERSION ===
    const char* (*GetVersion)(void);
} PicomimiAPI_t;

// Global API instance - use this in your apps!
extern PicomimiAPI_t Pico;

// ============================================================================
// APP/DRIVER/SERVICE REGISTRATION
// ============================================================================

// Register an application (call before setup())
#define Picomimi_RegisterApp(name, spawn_fn) \
    __attribute__((constructor)) static void _reg_app_##spawn_fn(void) { \
        pm_register_app(name, spawn_fn); \
    }

// Register a driver
#define Picomimi_RegisterDriver(name, init_fn, tick_fn, deinit_fn, priority, auto_start) \
    __attribute__((constructor)) static void _reg_drv_##init_fn(void) { \
        pm_register_driver(name, init_fn, tick_fn, deinit_fn, priority, auto_start); \
    }

// Register a service
#define Picomimi_RegisterService(name, init_fn, tick_fn, deinit_fn, priority, mem_kb, auto_start) \
    __attribute__((constructor)) static void _reg_svc_##init_fn(void) { \
        pm_register_service(name, init_fn, tick_fn, deinit_fn, priority, mem_kb, auto_start); \
    }

// Registration functions (called by macros)
void pm_register_app(const char* name, pm_task_entry_t spawn_fn);
void pm_register_driver(const char* name, void (*init)(pm_task_id_t), void (*tick)(void*), 
                        void (*deinit)(void), uint8_t priority, bool auto_start);
void pm_register_service(const char* name, void (*init)(pm_task_id_t), void (*tick)(void*),
                         void (*deinit)(void), uint8_t priority, uint32_t mem_kb, bool auto_start);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_H
