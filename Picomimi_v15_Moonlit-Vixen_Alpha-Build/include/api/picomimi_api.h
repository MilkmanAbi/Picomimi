/**
 * PICOMIMI SDK Application API (PicomimiAPI)
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * This provides a clean interface for apps to access kernel services.
 * In C, functions are prefixed with Pico_ (replaces Pico. from C++ version)
 */
#ifndef PICOMIMI_API_H
#define PICOMIMI_API_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

// Get current task ID
pm_task_id_t Pico_GetTaskId(void);

// Get task name
const char* Pico_GetTaskName(pm_task_id_t task_id);

// Sleep current task
void Pico_Sleep(uint32_t ms);

// Yield to other tasks
void Pico_Yield(void);

// Exit current task cleanly
void Pico_Exit(void);

// Spawn a subtask on Core 1 (for compute offload)
pm_task_id_t Pico_SpawnOnCore1(const char* name, pm_task_entry_t entry, void* arg, uint8_t priority);

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

// Allocate memory (returns NULL on failure)
void* Pico_Alloc(size_t size);

// Allocate and zero memory
void* Pico_AllocZero(size_t size);

// Free memory
void Pico_Free(void* ptr);

// Reallocate memory
void* Pico_Realloc(void* ptr, size_t new_size);

// Get free memory
size_t Pico_GetFreeMemory(void);

// Get memory used by current task
size_t Pico_GetMyMemory(void);

// Get memory used by a specific task
size_t Pico_GetTaskMemory(pm_task_id_t task_id);

// Check if memory is low
bool Pico_IsMemoryLow(void);

// Check if memory is critical
bool Pico_IsMemoryCritical(void);

// Get memory pressure level
pm_mem_pressure_t Pico_GetMemoryPressure(void);

// Register OOM handler (called when system needs memory)
void Pico_OnOOM(pm_oom_callback_t handler);

// Report that OOM cleanup is done
void Pico_OOMDone(uint32_t bytes_freed);

// Hint memory pressure to kernel
void Pico_HintMemoryPressure(void);

// ============================================================================
// ALIGNED MEMORY ALLOCATION
// ============================================================================

// Allocate cache-line aligned memory
void* Pico_AllocAligned(size_t size, size_t alignment);

// Allocate DMA-safe memory
void* Pico_AllocDMA(size_t size);

// ============================================================================
// IPC (INTER-PROCESS COMMUNICATION)
// ============================================================================

// Send message to another task
bool Pico_SendMessage(pm_task_id_t target_id, pm_ipc_msg_type_t type, void* data, size_t size);

// Send high-priority message
bool Pico_SendUrgent(pm_task_id_t target_id, pm_ipc_msg_type_t type, void* data, size_t size);

// Receive message (non-blocking, returns false if no message)
bool Pico_ReceiveMessage(pm_ipc_message_t* msg);

// Broadcast message to all tasks
bool Pico_Broadcast(pm_ipc_msg_type_t type, void* data, size_t size);

// ============================================================================
// SYNCHRONIZATION PRIMITIVES
// ============================================================================

// Initialize a mutex
bool Pico_MutexInit(uint32_t id);

// Lock a mutex (blocking, with timeout)
bool Pico_MutexLock(uint32_t id);

// Try to lock a mutex (non-blocking)
bool Pico_MutexTryLock(uint32_t id);

// Unlock a mutex
void Pico_MutexUnlock(uint32_t id);

// Initialize a semaphore
bool Pico_SemInit(uint32_t id, int32_t initial_count, uint32_t max_count);

// Wait on semaphore (with optional timeout)
bool Pico_SemWait(uint32_t id, uint32_t timeout_ms);

// Signal semaphore
void Pico_SemPost(uint32_t id);

// Initialize event flags
bool Pico_EventInit(uint32_t id);

// Wait for event flags
uint32_t Pico_EventWait(uint32_t id, uint32_t flags, bool wait_all, uint32_t timeout_ms);

// Set event flags
void Pico_EventSet(uint32_t id, uint32_t flags);

// Clear event flags
void Pico_EventClear(uint32_t id, uint32_t flags);

// ============================================================================
// SYSTEM INFO
// ============================================================================

// Get uptime in milliseconds
uint64_t Pico_GetUptime(void);

// Get CPU usage (Core 0)
float Pico_GetCPU0Usage(void);

// Get CPU usage (Core 1)
float Pico_GetCPU1Usage(void);

// Get CPU temperature
float Pico_GetTemperature(void);

// Get task count
uint32_t Pico_GetTaskCount(void);

// Get kernel version
const char* Pico_GetVersion(void);

// Get current core number
uint8_t Pico_GetCoreNum(void);

// Get total RAM
uint32_t Pico_GetTotalRAM(void);

// Get boot time in ms
uint32_t Pico_GetBootTime(void);

// ============================================================================
// CPU GOVERNOR CONTROL
// ============================================================================

// Set CPU profile (ULTRA_LOW, POWERSAVE, BALANCED, PERFORMANCE, TURBO)
void Pico_SetCPUProfile(pm_cpu_profile_t profile);

// Get current CPU profile
pm_cpu_profile_t Pico_GetCPUProfile(void);

// Get current CPU frequency in KHz
uint32_t Pico_GetCPUFreqKHz(void);

// Get current CPU frequency in MHz
uint32_t Pico_GetCPUFreqMHz(void);

// Request turbo mode for a specific duration
void Pico_RequestTurbo(uint32_t duration_ms);

// Request instant turbo (no time limit)
void Pico_RequestInstantTurbo(void);

// Check if thermal throttling is active
bool Pico_IsThermalThrottled(void);

// Hint that a heavy task is starting
void Pico_HintHeavyTaskStart(void);

// Hint that a heavy task has ended
void Pico_HintHeavyTaskEnd(void);

// ============================================================================
// GUI FOCUS
// ============================================================================

// Request GUI focus for current task
bool Pico_RequestFocus(void);

// Release GUI focus
void Pico_ReleaseFocus(void);

// Check if we have focus
bool Pico_HasFocus(void);

// Register stdout handler (for when we have focus)
void Pico_SetStdout(void (*write_char)(char));

// ============================================================================
// DISPLAY SHORTCUTS (if display driver registered)
// ============================================================================

// Get display width (0 if no display)
uint16_t Pico_DisplayWidth(void);

// Get display height (0 if no display)
uint16_t Pico_DisplayHeight(void);

// Clear display
void Pico_DisplayClear(uint16_t color);

// Draw text
void Pico_DisplayText(int16_t x, int16_t y, const char* text, uint16_t color);

// Fill rectangle
void Pico_DisplayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// ============================================================================
// INPUT SHORTCUTS (if input driver registered)
// ============================================================================

// Check if button pressed
bool Pico_ButtonPressed(uint8_t id);

// Poll for input event
bool Pico_PollInput(void* event);

// ============================================================================
// FILESYSTEM SHORTCUTS
// ============================================================================

// Check if filesystem is available
bool Pico_FSAvailable(void);

// Open file
int Pico_FileOpen(const char* path, bool write_mode);

// Close file
void Pico_FileClose(int fd);

// Read from file
int Pico_FileRead(int fd, uint8_t* buffer, uint32_t size);

// Write to file
int Pico_FileWrite(int fd, const uint8_t* data, uint32_t size);

// ============================================================================
// LOGGING
// ============================================================================

// Log a message
void Pico_Log(const char* message);

// Log warning
void Pico_LogWarn(const char* message);

// Log error
void Pico_LogError(const char* message);

// Log formatted message
void Pico_LogF(const char* fmt, ...);

// ============================================================================
// RESOURCE MANAGEMENT (v14.3.1 Resource-Owning Kernel)
// ============================================================================
// ZERO-OVERHEAD DESIGN:
//   1. ClaimXXX() - register ownership (one-time cost, ~1µs)
//   2. Use direct hardware access - ZERO overhead
//   3. ReleaseXXX() or automatic cleanup on task death

// === GPIO RESOURCES ===

// Claim a GPIO pin. Returns pin number on success, -1 on failure.
int8_t Pico_ClaimGPIO(uint8_t pin, pm_resource_mode_t mode);

// Release a GPIO pin
void Pico_ReleaseGPIO(uint8_t pin);

// Notify kernel of GPIO direction (optional - helps cleanup)
void Pico_NotifyGPIODirection(uint8_t pin, pm_gpio_dir_t dir);

// Notify kernel of GPIO state (optional - helps cleanup)
void Pico_NotifyGPIOState(uint8_t pin, bool state);

// === SPI RESOURCES ===

// Claim an SPI bus. Returns bus number on success, -1 on failure.
int8_t Pico_ClaimSPI(uint8_t bus, uint8_t cs_pin, pm_resource_mode_t mode);

// Release SPI bus
void Pico_ReleaseSPI(uint8_t bus);

// === I2C RESOURCES ===

// Claim an I2C bus with device address. Returns bus number on success, -1 on failure.
int8_t Pico_ClaimI2C(uint8_t bus, uint8_t device_addr, pm_resource_mode_t mode);

// Release I2C bus
void Pico_ReleaseI2C(uint8_t bus, uint8_t device_addr);

// === ADC RESOURCES ===

// Claim an ADC channel. Returns channel number on success, -1 on failure.
int8_t Pico_ClaimADC(uint8_t channel, pm_resource_mode_t mode);

// Release ADC channel
void Pico_ReleaseADC(uint8_t channel);

// === PWM RESOURCES ===

// Claim a PWM slice+channel. Returns index on success, -1 on failure.
int8_t Pico_ClaimPWM(uint8_t slice, uint8_t channel, pm_resource_mode_t mode);

// Release PWM
void Pico_ReleasePWM(uint8_t slice, uint8_t channel);

// === PIO RESOURCES ===

// Claim a PIO state machine. Returns SM number on success, -1 on failure.
int8_t Pico_ClaimPIO(uint8_t pio_num, uint8_t sm_num, pm_resource_mode_t mode);

// Release PIO state machine
void Pico_ReleasePIO(uint8_t pio_num, uint8_t sm_num);

// === UART RESOURCES ===

// Claim a UART. Returns UART number on success, -1 on failure.
int8_t Pico_ClaimUART(uint8_t uart_num, pm_resource_mode_t mode);

// Release UART
void Pico_ReleaseUART(uint8_t uart_num);

// === DMA RESOURCES ===

// Claim a DMA channel. Returns channel on success, -1 on failure.
int8_t Pico_ClaimDMA(uint8_t channel, pm_resource_mode_t mode);

// Release DMA channel
void Pico_ReleaseDMA(uint8_t channel);

// === TIMER RESOURCES ===

// Claim a hardware timer alarm. Returns alarm number on success, -1 on failure.
int8_t Pico_ClaimTimer(uint8_t alarm_num, pm_resource_mode_t mode);

// Release timer alarm
void Pico_ReleaseTimer(uint8_t alarm_num);

// ============================================================================
// RESOURCE QUERY
// ============================================================================

// Check if a GPIO is owned by current task
bool Pico_OwnsGPIO(uint8_t pin);

// Get owner of a GPIO (returns PM_INVALID_TASK if not owned)
pm_task_id_t Pico_GetGPIOOwner(uint8_t pin);

// Get count of resources owned by current task
uint32_t Pico_GetResourceCount(void);

// ============================================================================
// SDK REGISTRATION FUNCTIONS (called before kernel start)
// ============================================================================

// Register an application
void Picomimi_RegisterApp(const char* name, void (*spawn_func)(void));

// Register a hardware driver
void Picomimi_RegisterDriver(const char* name, 
                              void (*init_fn)(pm_task_id_t),
                              void (*tick_fn)(void*), 
                              void (*deinit_fn)(void),
                              uint8_t priority, 
                              bool auto_start);

// Register a system service
void Picomimi_RegisterService(const char* name,
                               void (*init_fn)(pm_task_id_t),
                               void (*tick_fn)(void*),
                               void (*deinit_fn)(void),
                               uint8_t priority,
                               uint32_t mem_limit_kb,
                               bool auto_start);

// Start all auto-start drivers
void Picomimi_StartDrivers(void);

// Start all auto-start services
void Picomimi_StartServices(void);

// Start a specific driver by name
pm_task_id_t Picomimi_StartDriver(const char* name);

// Start a specific service by name
pm_task_id_t Picomimi_StartService(const char* name);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_API_H
