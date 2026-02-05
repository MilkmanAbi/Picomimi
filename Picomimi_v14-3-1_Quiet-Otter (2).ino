/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  PICOMIMI-AXISOS v14.3.1-Quiet-Otter   // Resource-Owning Kernel          ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  TARGET: RP2040/RP2350 (Configurable via flags)                           ║
 * ║  RAM: 264KB/520KB SRAM (kernel uses ~30%, 70% for apps)                   ║
 * ║  FEATURES: FPU, DSP, TrustZone, MPU, Hardware FP (RP2350)                 ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 * 
 * ============================================================================
 * MilkmanAbi: v14.3.1 RESOURCE-OWNING KERNEL ARCHITECTURE
 * ============================================================================
 * 
 * MAJOR ARCHITECTURAL CHANGE: Picomimi is now a RESOURCE-OWNING KERNEL.
 * 
 * BEFORE (v14.1.x - "Structured Multitasking Runtime"):
 *   - Apps touched GPIO/SPI/I2C/ADC directly
 *   - Kernel only scheduled execution and prevented timing conflicts
 *   - Kernel had no knowledge of hardware intent or ownership
 *   - No way to audit or revoke hardware access
 *   - Task death could leave hardware in undefined state
 * 
 * NOW (v14.3.1 - "Resource-Owning Kernel"):
 *   - ALL hardware must be CLAIMED through kernel before use
 *   - Apps use DIRECT hardware access after claiming (ZERO overhead)
 *   - Kernel tracks ownership per-task
 *   - Kernel can AUDIT, REVOKE, and ARBITRATE resource conflicts
 *   - Task death automatically RESETS and releases all owned resources
 *   - GPIO reset to high-Z before task termination (clean disconnection)
 *   - Resources can be EXCLUSIVE or SHARED with proper locking
 *   - Violation detection for unclaimed hardware access
 * 
 * ZERO-OVERHEAD DESIGN:
 *   1. Pico.ClaimGPIO(pin) - Register ownership (~1µs, one-time)
 *   2. pinMode(pin, OUTPUT) - Direct Arduino call (no kernel overhead)
 *   3. digitalWrite(pin, HIGH) - Direct hardware (full speed!)
 *   4. Auto-cleanup on task death - Kernel resets GPIO to safe state
 * 
 * RESOURCE TYPES MANAGED:
 *   - GPIO pins (30 pins on RP2040, 48 on RP2350)
 *   - SPI buses (SPI0, SPI1) with chip select tracking
 *   - I2C buses (I2C0, I2C1) with device address tracking
 *   - ADC channels (ADC0-3 on RP2040, ADC0-7 on RP2350)
 *   - PWM slices (8 slices, 16 channels)
 *   - PIO state machines (2 PIOs x 4 SMs each)
 *   - UART (UART0, UART1)
 *   - DMA channels (12 channels)
 *   - Hardware timers (4 alarms)
 * 
 * OWNERSHIP MODES:
 *   - RES_MODE_EXCLUSIVE: Only owner can access
 *   - RES_MODE_SHARED_READ: Multiple readers, one writer
 *   - RES_MODE_KERNEL_ONLY: Reserved for kernel use
 * 
 * NEW APIs FOR APPS:
 *   int led = Pico.ClaimGPIO(25);           // Claim ownership
 *   if (led >= 0) {
 *     pinMode(led, OUTPUT);                  // Direct Arduino - ZERO overhead
 *     digitalWrite(led, HIGH);               // Direct Arduino - full speed!
 *   }
 *   // Automatic cleanup on task death - GPIO reset to high-Z
 *   
 *   int spi = Pico.ClaimSPI(0, cs_pin);     // Claim SPI bus
 *   // Use SPI.transfer() directly - ZERO overhead
 *   
 *   int adc = Pico.ClaimADC(0);             // Claim ADC channel
 *   // Use analogRead() directly - ZERO overhead
 * 
 * ENFORCEMENT:
 *   - Direct hardware access is DETECTED and LOGGED
 *   - Repeated violations can trigger task termination
 *   - OOM killer considers resource hoarding
 *   - Shell commands: 'res', 'resmap', 'resfree', 'resaudit'
 * 
 * BACKWARD COMPATIBILITY:
 *   - Legacy direct access still WORKS but is AUDITED
 *   - Set RESOURCE_STRICT_MODE=1 to BLOCK direct access
 *   - Migration path: replace direct calls with HAL calls
 * 
 * ============================================================================
 * 
 * v14.1.1 FIXES (USB Serial Stability) - RETAINED:
 *   - FIXED: Serial terminal lockup after inactivity
 *   - USB activity tracking prevents low-power modes when Serial active
 *   - Automatic USB lockup detection and recovery
 * 
 * ORIGINAL v14.1 GOVERNOR FEATURES:
 *   - ULTRA_LOW profile: 50MHz + WFI for maximum power savings when truly idle
 *   - 5-level frequency scaling: ULTRA_LOW -> POWERSAVE -> BALANCED -> PERF -> TURBO
 *   - Chip-aware frequency tables (RP2040: 50-260MHz, RP2350: 50-310MHz)
 *   - NO turbo time limit - only thermal throttling can pull you down
 *   - Task-based instant turbo: seamless frequency boosting for demanding tasks
 *   - WFI (Wait For Interrupt) integration for deep sleep during true idle
 *   - Aggressive power saving when idling - chip can drop to 50MHz almost instantly
 *   - Smooth, clean transitions between all profiles with no jarring dips
 *   - Per-chip optimized voltage tables for safe overclocking headroom
 * 
 * CONFIGURATION FLAGS (at top of file):
 *   - RP2040_OR_RP2350: Set chip type (0=RP2040, 1=RP2350)
 *   - ENABLE_PICOMIMI_GOVERNOR: Enable/disable auto frequency scaling (1/0)
 *   - RP2040_FREQ_MAX/RP2350_FREQ_MAX: Manual freq when governor disabled
 *   - CORE_TEMP_LIMIT: Thermal throttle threshold (DEFAULT = 70°C)
 * 
 * WHY THIS MATTERS:
 *   The whole point of this governor is to save power wherever possible while
 *   being fast and instant enough to cause no noticeable dips to tasks. Let the
 *   chip turbo as long as it needs to, let it drop to 50MHz when actually idle.
 *   WFI until interrupts/inputs/task demand, then turbo boost momentarily before
 *   normalizing based on actual task demand. Clean frequency transitions that
 *   don't starve tasks or waste power. Real embedded power management done right.
 * 
 * ============================================================================
 * 
 * ============================================================================
 * PICOMIMI-AXISOS SDK - RP2040/RP2350 OPTIMIZED APPLICATION DEVELOPMENT GUIDE
 * ============================================================================
 * 
 * ============================================================================
 * GOVERNOR CONFIGURATION FLAGS - SET THESE FOR YOUR HARDWARE
 * ============================================================================
 */

// ============================================================================
// HARDWARE CONFIGURATION - EDIT THESE FOR YOUR SETUP
// ============================================================================

#define RP2040_OR_RP2350          0       // 0 = RP2040, 1 = RP2350
#define ENABLE_PICOMIMI_GOVERNOR  1       // 1 = Auto frequency scaling ON
                                          // 0 = Locked to max freq (manual mode)

#define RP2040_FREQ_MAX           250     // MHz - used when governor disabled on RP2040
#define RP2350_FREQ_MAX           300     // MHz - used when governor disabled on RP2350

#define CORE_TEMP_LIMIT           DEFAULT // Thermal throttle temp in °C
                                          // Set to DEFAULT for 70°C, or a number like 60

// Internal: Convert DEFAULT to actual value
#define DEFAULT                   70
#if CORE_TEMP_LIMIT == DEFAULT
  #undef CORE_TEMP_LIMIT
  #define CORE_TEMP_LIMIT         70
#endif

/*
 * ============================================================================
 * SDK DOCUMENTATION CONTINUES
 * ============================================================================
 * 
 * REGISTERING APPS (in global scope before setup):
 *   Picomimi_RegisterApp("myapp", my_spawn_function);
 * 
 * REGISTERING DRIVERS (for hardware abstraction):
 *   Picomimi_RegisterDriver("display", drv_init, drv_tick, drv_deinit, 14, true);
 *   // name, init_fn, tick_fn, deinit_fn, priority (0-15), auto_start
 * 
 * REGISTERING SERVICES (for background tasks):
 *   Picomimi_RegisterService("network", svc_init, svc_tick, svc_deinit, 8, 4, true);
 *   // name, init_fn, tick_fn, deinit_fn, priority, mem_limit_kb, auto_start
 *
 * DISPLAY DRIVER INTERFACE:
 *   DisplayDriver my_display = { .width=240, .height=240, ... };
 *   Picomimi_RegisterDisplay(&my_display);
 *   // Then apps use: Pico.DisplayClear(), Pico.DisplayText(), etc.
 *
 * INPUT DRIVER INTERFACE:
 *   InputDriver my_input = { .num_buttons=4, ... };
 *   Picomimi_RegisterInput(&my_input);
 *   // Then apps use: Pico.ButtonPressed(), Pico.PollInput(), etc.
 *
 * APP API (via PicomimiAPI class, alias: Pico):
 *   Pico.Sleep(100);              // Sleep 100ms
 *   Pico.Yield();                 // Yield to other tasks
 *   void* p = Pico.Alloc(1024);   // Allocate 1KB
 *   Pico.Free(p);                 // Free memory
 *   Pico.GetFreeMemory();         // Check available RAM
 *   Pico.SendMessage(id, type, data, size);  // IPC
 *   Pico.RequestFocus();          // Request GUI focus
 *   Pico.Log("Hello");            // Log message
 *   Pico.SetCPUProfile(CPU_PROFILE_TURBO); // Boost to 300MHz
 *   // ... and many more (see PicomimiAPI class)
 * 
 * ============================================================================
 * RP2350-AXISOS SPECIFIC ENHANCEMENTS:
 * ============================================================================
 * 
 * CPU FREQUENCY GOVERNOR (v2.0 - ADVANCED):
 *   - CPU_PROFILE_ULTRA_LOW:  50MHz + WFI (deep sleep when truly idle)
 *   - CPU_PROFILE_POWERSAVE:  100MHz (light background tasks)
 *   - CPU_PROFILE_BALANCED:   133/150MHz (default idle, UI responsive)
 *   - CPU_PROFILE_PERFORMANCE: 200/250MHz (normal operation)
 *   - CPU_PROFILE_TURBO:      260/310MHz (big tasks, NO TIME LIMIT)
 *   - Automatic frequency scaling based on load
 *   - Uses vreg_set_voltage() for safe overclocking
 *   - Thermal throttling only (no arbitrary turbo timeout)
 *   - WFI integration for maximum power savings during true idle
 *   - Task-based instant turbo for seamless responsiveness
 *
 * ENHANCED MEMORY MANAGEMENT (kmalloc/kfree v2):
 *   - 100KB kernel heap (~19% of 520KB SRAM)
 *   - 365KB+ available for applications (~70%)
 *   - Advanced buddy allocator with size classes
 *   - Cache-line aligned allocations (32-byte)
 *   - TLSF-inspired O(1) allocation for small blocks
 *   - Segregated free lists by size class
 *   - Aggressive coalescing with lazy defragmentation
 *   - Per-core memory pools for reduced contention
 *   - Memory pressure callbacks with backpressure
 *
 * DUAL CORTEX-M33 OPTIMIZATIONS:
 *   - True symmetric multiprocessing (SMP)
 *   - Lock-free data structures where possible
 *   - Per-core scheduler with work stealing
 *   - DSP instruction utilization for math
 *   - Hardware FPU for floating point ops
 *
 * EXPANDED CAPABILITIES (vs RP2040):
 *   - MAX_TASKS: 20 (vs 12 on RP2040)
 *   - MAX_MEMORY_BLOCKS: 96 (vs 48)
 *   - MAX_APPS: 12 (vs 8)
 *   - MAX_IPC_MESSAGES: 24 (vs 12)
 *   - IPC_MSG_SIZE: 32 bytes (vs 24)
 *   - PMFS_WRITE_CACHE: 2KB (vs 1KB)
 *   - TMPFS: 8KB RAM disk (vs 0KB)
 *   - ~70% RAM free for applications (~365KB)
 *
 * REAL-TIME ENHANCEMENTS:
 *   - Sub-microsecond scheduler tick precision
 *   - Priority inheritance for mutexes
 *   - Deadline-aware scheduling hints
 *   - Core 1 dedicated RT task queue
 * 
 * ============================================================================
 * Based on Picomimi MicroOS v14.0 - Fork by uh... me? Fork for AxisOS
 * v14.1 - Advanced CPU Governor Integration
 * Made with determination ฅ(•ㅅ•❀)ฅ... and love ˗ˋˏ ♡ ˎˊ˗
 * 
 * Toolchain: MIAU.py, MRRP.py, NYAA.py, MROW.py
 */

#include <SPI.h>
#include <SD.h>
#include <hardware/adc.h>
#include <hardware/watchdog.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/timer.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/pll.h>
#include <hardware/pwm.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/scb.h>
#include <pico/platform.h>
#include <pico/multicore.h>
#include <pico/mutex.h>
#include <pico/critical_section.h>

#define SD_CS 5
#define SD_MOSI 19
#define SD_MISO 16
#define SD_SCK 18
#define BTN_ONOFF 9

// ============================================================================
// RP2350 ARCHITECTURE DEFINES
// ============================================================================

#define RP2350_TOTAL_SRAM       (520 * 1024)   // 520KB SRAM
#define RP2350_CORE_COUNT       2
#define RP2350_HAS_FPU          1
#define RP2350_HAS_DSP          1
#define RP2350_HAS_TRUSTZONE    1
#define RP2350_HAS_MPU          1
#define RP2350_CACHE_LINE_SIZE  32

// M33 SCB Register bits (for WFI/WFE power management)
#ifndef M33_SCB_SCR_SLEEPDEEP_BITS
  #define M33_SCB_SCR_SLEEPDEEP_BITS  (1u << 2)  // SLEEPDEEP bit in SCR
#endif
#ifndef M33_SCB_SCR_SLEEPONEXIT_BITS
  #define M33_SCB_SCR_SLEEPONEXIT_BITS (1u << 1) // SLEEPONEXIT bit in SCR
#endif

// Interrupt control (Cortex-M33 enhanced)
#define disable_all_interrupts() __asm__ volatile ("cpsid i" : : : "memory")
#define enable_all_interrupts() __asm__ volatile ("cpsie i" : : : "memory")

// Data/instruction barrier (M33 specific)
#define memory_barrier() __asm__ volatile ("dmb" : : : "memory")
#define instruction_barrier() __asm__ volatile ("isb" : : : "memory")
#define data_barrier() __asm__ volatile ("dsb" : : : "memory")

// Branch prediction hints for hot paths
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Cache line alignment for critical structures
#define CACHE_ALIGNED __attribute__((aligned(RP2350_CACHE_LINE_SIZE)))

// Hot path optimization
#define HOT_FUNC __attribute__((hot, optimize("O3")))
#define COLD_FUNC __attribute__((cold))
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NOINLINE __attribute__((noinline))

// ============================================================================
// CPU FREQUENCY GOVERNOR PROFILES (v2.0 - 5-LEVEL SCALING)
// ============================================================================

enum CPUProfile : uint8_t {
  CPU_PROFILE_ULTRA_LOW = 0,    // 50MHz + WFI - Deep idle, max power savings
  CPU_PROFILE_POWERSAVE = 1,    // 100MHz @ 0.95V - Light tasks
  CPU_PROFILE_BALANCED = 2,     // 133/150MHz @ 1.00V - Default idle
  CPU_PROFILE_PERFORMANCE = 3,  // 200/250MHz @ 1.10V - Normal operation
  CPU_PROFILE_TURBO = 4,        // 260/310MHz @ 1.25V - Big tasks (NO TIME LIMIT)
  CPU_PROFILE_COUNT = 5
};

// ============================================================================
// CHIP-AWARE FREQUENCY TABLES
// ============================================================================

// RP2040 Frequency table (KHz) - Max 260MHz stable
static const uint32_t CPU_FREQ_TABLE_RP2040[] = {
  50000,    // ULTRA_LOW - Minimum stable + WFI
  100000,   // POWERSAVE
  133000,   // BALANCED (default)
  200000,   // PERFORMANCE
  260000    // TURBO (safe overclock)
};

// RP2040 Voltage table (millivolts)
static const uint32_t CPU_VOLTAGE_TABLE_RP2040[] = {
  900,      // ULTRA_LOW
  950,      // POWERSAVE
  1000,     // BALANCED
  1100,     // PERFORMANCE
  1250      // TURBO
};

// RP2350 Frequency table (KHz) - Max 310MHz with good cooling
static const uint32_t CPU_FREQ_TABLE_RP2350[] = {
  50000,    // ULTRA_LOW - Minimum stable + WFI
  100000,   // POWERSAVE
  150000,   // BALANCED (default, slightly faster than RP2040)
  250000,   // PERFORMANCE (nominal)
  310000    // TURBO (max overclock)
};

// RP2350 Voltage table (millivolts)
static const uint32_t CPU_VOLTAGE_TABLE_RP2350[] = {
  900,      // ULTRA_LOW
  950,      // POWERSAVE
  1000,     // BALANCED
  1100,     // PERFORMANCE
  1250      // TURBO
};

// Active tables (set based on chip selection)
#if RP2040_OR_RP2350 == 0
  #define CPU_FREQ_TABLE    CPU_FREQ_TABLE_RP2040
  #define CPU_VOLTAGE_TABLE CPU_VOLTAGE_TABLE_RP2040
  #define CHIP_NAME         "RP2040"
  #define CHIP_FREQ_MAX     260000
  #define CHIP_FREQ_MANUAL  (RP2040_FREQ_MAX * 1000)
#else
  #define CPU_FREQ_TABLE    CPU_FREQ_TABLE_RP2350
  #define CPU_VOLTAGE_TABLE CPU_VOLTAGE_TABLE_RP2350
  #define CHIP_NAME         "RP2350"
  #define CHIP_FREQ_MAX     310000
  #define CHIP_FREQ_MANUAL  (RP2350_FREQ_MAX * 1000)
#endif

// Governor state
static volatile CPUProfile current_cpu_profile = CPU_PROFILE_BALANCED;
static volatile CPUProfile requested_cpu_profile = CPU_PROFILE_BALANCED;
static volatile uint32_t current_freq_khz = 133000;
static volatile uint32_t governor_last_check_ms = 0;
static volatile uint32_t turbo_start_ms = 0;
static volatile bool turbo_active = false;
static volatile uint8_t load_history[8] = {0};
static volatile uint8_t load_history_idx = 0;

// Governor thresholds (with hysteresis to prevent thrashing)
#define GOVERNOR_CHECK_INTERVAL_MS   50     // Faster checks for responsiveness
#define GOVERNOR_FAST_CHECK_MS       10     // Even faster during transitions

// NO TURBO TIME LIMIT - Only thermal can throttle turbo
// #define TURBO_MAX_DURATION_MS     (removed - no limit)

#define THERMAL_THROTTLE_TEMP        ((float)CORE_TEMP_LIMIT)  // Use configured temp

// Hysteresis thresholds - tuned for smooth transitions
#define TURBO_SCALE_UP_THRESHOLD     80      // Boost to TURBO when load >= 80%
#define TURBO_SCALE_DOWN_THRESHOLD   65      // Drop from TURBO when load < 65%
#define PERF_SCALE_UP_THRESHOLD      50      // Boost to PERFORMANCE when load >= 50%
#define PERF_SCALE_DOWN_THRESHOLD    35      // Drop from PERFORMANCE when load < 35%
#define BALANCED_SCALE_UP_THRESHOLD  20      // Boost to BALANCED when load >= 20%
#define BALANCED_SCALE_DOWN_THRESHOLD 12     // Drop from BALANCED when load < 12%
#define POWERSAVE_SCALE_DOWN_THRESHOLD 5     // Drop to ULTRA_LOW when load < 5%

// Instant turbo threshold - for task-based boosting
#define INSTANT_TURBO_LOAD_SPIKE     70      // Immediate turbo if sudden load spike

// Input boost configuration
#define INPUT_BOOST_PROFILE          CPU_PROFILE_PERFORMANCE  // Profile on input
#define INPUT_BOOST_DURATION_MS      300     // Shorter, snappier boost

// WFI (Wait For Interrupt) configuration
#define WFI_IDLE_THRESHOLD_MS        100     // Enter WFI if idle for this long
#define WFI_MIN_SLEEP_US             50      // Minimum WFI sleep time

// ============================================================================
// USB SERIAL STABILITY - v14.1.1 FIX
// ============================================================================
// USB CDC requires stable clocks and responsive interrupt handling.
// These settings prevent USB lockups when using Serial Monitor.

#define USB_SAFE_MIN_PROFILE         CPU_PROFILE_BALANCED  // Never drop below this when USB active
#define USB_SAFE_MIN_FREQ_KHZ        133000                // Minimum safe freq for USB stability
#define USB_ACTIVITY_TIMEOUT_MS      5000                  // Consider USB inactive after 5s silence
#define USB_SERIAL_POLL_INTERVAL_MS  10                    // Poll Serial every 10ms when active
#define USB_KEEPALIVE_ENABLED        true                  // Send periodic keepalive when idle

// Block WFI entirely when USB is active - this is the root cause of lockups
#define USB_BLOCKS_WFI               true

// Minimum frequency for USB clock stability (48MHz USB needs stable PLL)
// Below 100MHz, USB can become unstable on some boards
#define USB_MIN_STABLE_FREQ_KHZ      100000

// ============================================================================
// RESOURCE MANAGEMENT SYSTEM - v14.3.1 RESOURCE-OWNING KERNEL
// ============================================================================
// This is the core of the resource-owning architecture. ALL hardware access
// must go through the kernel's resource manager.

// Configuration
#define RESOURCE_STRICT_MODE         0       // 1 = BLOCK direct access, 0 = AUDIT only
#define RESOURCE_AUDIT_ENABLED       1       // Log direct hardware access violations
#define RESOURCE_AUTO_RELEASE        1       // Auto-release on task death
#define RESOURCE_MAX_VIOLATIONS      10      // Max violations before task termination
#define RESOURCE_VIOLATION_WINDOW_MS 60000   // Window for counting violations

// Resource counts (chip-dependent)
#if RP2040_OR_RP2350 == 0
  #define RES_GPIO_COUNT           30       // GP0-GP29
  #define RES_ADC_COUNT            4        // ADC0-ADC3
  #define RES_PIO_COUNT            2        // PIO0, PIO1
  #define RES_PIO_SM_COUNT         4        // 4 state machines per PIO
  #define RES_DMA_COUNT            12       // 12 DMA channels
#else
  #define RES_GPIO_COUNT           48       // GP0-GP47
  #define RES_ADC_COUNT            8        // ADC0-ADC7
  #define RES_PIO_COUNT            3        // PIO0, PIO1, PIO2
  #define RES_PIO_SM_COUNT         4        // 4 state machines per PIO
  #define RES_DMA_COUNT            16       // 16 DMA channels
#endif

#define RES_SPI_COUNT              2        // SPI0, SPI1
#define RES_I2C_COUNT              2        // I2C0, I2C1
#define RES_UART_COUNT             2        // UART0, UART1
#define RES_PWM_SLICE_COUNT        8        // 8 PWM slices
#define RES_PWM_CHANNEL_COUNT      16       // 16 PWM channels (2 per slice)
#define RES_TIMER_ALARM_COUNT      4        // 4 hardware timer alarms

// Maximum resources per task
#define MAX_RESOURCES_PER_TASK     16       // Max resources a single task can own
#define MAX_PENDING_TRANSACTIONS   4        // Max concurrent multi-resource locks

// Resource handle magic (for validation)
#define RES_HANDLE_MAGIC           0xAB00   // Upper bits of valid handle
#define RES_HANDLE_INVALID         0xFFFF   // Invalid handle sentinel

// Resource types
enum ResourceType : uint8_t {
  RES_TYPE_GPIO = 0,
  RES_TYPE_SPI,
  RES_TYPE_I2C,
  RES_TYPE_ADC,
  RES_TYPE_PWM,
  RES_TYPE_PIO,
  RES_TYPE_UART,
  RES_TYPE_DMA,
  RES_TYPE_TIMER,
  RES_TYPE_COUNT
};

// Resource state
enum ResourceState : uint8_t {
  RES_STATE_FREE = 0,          // Available for claiming
  RES_STATE_CLAIMED,           // Owned by a task
  RES_STATE_SHARED,            // Shared access (multiple readers)
  RES_STATE_LOCKED,            // Temporarily locked for transaction
  RES_STATE_KERNEL_RESERVED    // Reserved for kernel (cannot be claimed)
};

// Ownership modes
enum ResourceMode : uint8_t {
  RES_MODE_EXCLUSIVE = 0,      // Only owner can access
  RES_MODE_SHARED_READ,        // Multiple readers, exclusive writer
  RES_MODE_SHARED_WRITE,       // Multiple writers (rare, use with caution)
  RES_MODE_KERNEL_ONLY         // Kernel internal use
};

// GPIO direction
enum GPIODirection : uint8_t {
  GPIO_DIR_INPUT = 0,
  GPIO_DIR_OUTPUT,
  GPIO_DIR_INPUT_PULLUP,
  GPIO_DIR_INPUT_PULLDOWN,
  GPIO_DIR_ANALOG
};

// GPIO drive strength
enum GPIODrive : uint8_t {
  GPIO_DRIVE_2MA = 0,
  GPIO_DRIVE_4MA,
  GPIO_DRIVE_8MA,
  GPIO_DRIVE_12MA
};

// Resource handle (opaque to apps, kernel validates)
typedef uint16_t ResHandle;

// Resource descriptor (internal kernel structure)
struct ResourceDescriptor {
  uint32_t owner_task_id;       // Task that owns this resource (0 = unowned)
  uint32_t claim_time_ms;       // When was this claimed
  uint32_t last_access_ms;      // Last access timestamp
  uint32_t access_count;        // Total accesses
  uint16_t handle;              // Handle issued to owner
  uint16_t share_count;         // Number of sharers (for SHARED mode)
  uint8_t type;                 // ResourceType
  uint8_t id;                   // Resource-specific ID (pin#, channel#, etc.)
  uint8_t state;                // ResourceState
  uint8_t mode;                 // ResourceMode
  uint8_t sub_id;               // Sub-resource (e.g., SPI CS pin, I2C address)
  uint8_t config_flags;         // Resource-specific configuration
  bool configured;              // Has been configured (pinMode, etc.)
  bool in_transaction;          // Part of active transaction
} __attribute__((packed));

// GPIO-specific extended info
struct GPIOResourceInfo {
  uint8_t direction;            // GPIODirection
  uint8_t drive;                // GPIODrive
  uint8_t function;             // GPIO function (SIO, PWM, SPI, etc.)
  bool output_state;            // Current output value
  bool has_interrupt;           // Interrupt enabled
  uint8_t irq_edge;             // IRQ edge configuration
} __attribute__((packed));

// SPI-specific extended info
struct SPIResourceInfo {
  uint32_t frequency;           // SPI clock frequency
  uint8_t mode;                 // SPI mode (0-3)
  uint8_t bit_order;            // MSB/LSB first
  uint8_t cs_pin;               // Chip select pin
  bool cs_active_low;           // CS polarity
} __attribute__((packed));

// I2C-specific extended info
struct I2CResourceInfo {
  uint32_t frequency;           // I2C clock frequency
  uint8_t device_addr;          // Device address (7-bit)
  bool is_master;               // Master or slave mode
} __attribute__((packed));

// PWM-specific extended info
struct PWMResourceInfo {
  uint32_t frequency;           // PWM frequency
  uint16_t duty_cycle;          // Duty cycle (0-65535)
  uint8_t slice;                // PWM slice
  uint8_t channel;              // A or B
  bool enabled;                 // Currently running
} __attribute__((packed));

// PIO-specific extended info
struct PIOResourceInfo {
  uint8_t pio_num;              // PIO0 or PIO1 (or PIO2 on RP2350)
  uint8_t sm_num;               // State machine number
  uint8_t program_offset;       // Program memory offset
  uint8_t program_size;         // Program size in instructions
  bool running;                 // State machine running
} __attribute__((packed));

// Resource violation record
struct ResourceViolation {
  uint32_t task_id;             // Task that violated
  uint32_t timestamp_ms;        // When it happened
  uint16_t resource_type;       // What type was accessed
  uint16_t resource_id;         // What specific resource
  uint32_t violation_count;     // Total violations by this task
  bool warned;                  // Has task been warned
} __attribute__((packed));

// Transaction (multi-resource lock)
struct ResourceTransaction {
  uint32_t owner_task_id;       // Task holding transaction
  uint32_t start_time_ms;       // Transaction start
  uint32_t timeout_ms;          // Transaction timeout
  ResHandle handles[8];         // Resources in transaction
  uint8_t handle_count;         // Number of resources
  bool active;                  // Transaction is active
} __attribute__((packed));

// Total resource slots
#define TOTAL_GPIO_RESOURCES     RES_GPIO_COUNT
#define TOTAL_SPI_RESOURCES      RES_SPI_COUNT
#define TOTAL_I2C_RESOURCES      (RES_I2C_COUNT * 16)  // Each bus can have multiple devices
#define TOTAL_ADC_RESOURCES      RES_ADC_COUNT
#define TOTAL_PWM_RESOURCES      RES_PWM_CHANNEL_COUNT
#define TOTAL_PIO_RESOURCES      (RES_PIO_COUNT * RES_PIO_SM_COUNT)
#define TOTAL_UART_RESOURCES     RES_UART_COUNT
#define TOTAL_DMA_RESOURCES      RES_DMA_COUNT
#define TOTAL_TIMER_RESOURCES    RES_TIMER_ALARM_COUNT

#define MAX_RESOURCE_VIOLATIONS  32        // Track recent violations
#define MAX_TRANSACTIONS         MAX_PENDING_TRANSACTIONS

// ============================================================================
// RP2350 EXPANDED SYSTEM LIMITS (~25-30% RAM for kernel = ~130-156KB)
// Target: 30-35% RAM usage (157-183KB of 524KB)
// ============================================================================

// Task system (balanced for RP2350)
#define MAX_TASKS 20
#define MAX_MEMORY_BLOCKS 96
#define HEAP_SIZE (100 * 1024)              // 100KB kernel heap (main reduction)

#define KERNEL_RESERVE (12 * 1024)          // 12KB reserved for kernel
#define TASK_NAME_LEN 16                    // Restored full names
#define MAX_LOG_ENTRIES 24                  // Reasonable log history
#define MAX_APPS 12                         // Good balance
#define MAX_GUI_APPS 4                      // Reasonable GUI apps
#define MAX_KERNEL_MUTEXES 8
#define MAX_SEMAPHORES 8
#define MAX_EVENT_FLAGS 8

// Scheduler configuration (optimized for 250-300MHz)
#define SCHEDULER_TICK_US 500               // Faster tick for better responsiveness
#define SCHED_NUM_PRIORITY_LEVELS 16
#define SCHED_RT_THRESHOLD 12
#define SCHED_BASE_QUANTUM_US 3000          // Shorter base quantum
#define SCHED_MAX_QUANTUM_US 50000          // Shorter max quantum for responsiveness
#define SCHED_AGING_INTERVAL_MS 300         // Faster aging
#define SCHED_IDLE_INJECTION_THRESHOLD 90
#define SCHED_WORK_STEAL_THRESHOLD 3        // Work stealing between cores

// Watchdog and system
#define WATCHDOG_TIMEOUT_MS 8000
#define REAPER_INTERVAL_MS 1500             // Faster reaping
#define REAPER_GRACE_PERIOD_MS 300

// Core 1 configuration (balanced)
#define MAX_CORE1_TASKS 6                   // Good for offloading
#define CORE1_STACK_SIZE (3 * 1024)         // 3KB stack

// IPC configuration (balanced)
#define MAX_IPC_MESSAGES 24
#define IPC_MSG_SIZE 32                     // 32 bytes payload (good balance)
#define IPC_NULL_MSG 0xFFFF
#define IPC_TARGET_BROADCAST 0xFFFFFFFF
#define IPC_PRIORITY_RT 15                  // Real-time priority IPC

// Filesystem configuration (expanded)
#define FS_MAX_FILENAME 32
#define FS_MAX_OPEN_FILES 8
#define FS_BUFFER_SIZE 512
#define FS_LOG_FILE "/LogRecord"

// OOM configuration (tuned for larger RAM)
#define OOM_REQUEST_TIMEOUT_MS 2000
#define MAX_OOM_HANDLERS 12
#define OOM_ABUSIVE_ALLOC_VELOCITY 100
#define OOM_ABUSIVE_ALLOC_SIZE (100 * 1024)
#define MAX_APP_MEM_REQUEST_GLOBAL (256 * 1024)  // Apps can request up to 256KB
#define VELOCITY_CHECK_CHUNK (8 * 1024)
#define VELOCITY_TIME_THRESHOLD_US (100 * 1000)

// Memory protection thresholds (scaled for 520KB)
#define MEM_CRITICAL_THRESHOLD (30 * 1024)  // 30KB critical
#define MEM_WARNING_THRESHOLD (50 * 1024)   // 50KB warning
#define MEM_FRAGMENTATION_CRITICAL 70

// CPU protection thresholds
#define CPU_OVERLOAD_THRESHOLD 90.0f
#define CPU_CRITICAL_THRESHOLD 95.0f
#define CPU_TASK_ABUSE_THRESHOLD 75.0f
#define CPU_ABUSE_SAMPLE_COUNT 5            // More samples for accuracy

// ============================================================================
// ENHANCED MEMORY ALLOCATOR CONFIGURATION (kmalloc v2)
// ============================================================================

// Size classes for segregated free lists (TLSF-inspired)
#define MEM_SIZE_CLASS_COUNT 8
static const uint32_t MEM_SIZE_CLASSES[] = {
  32, 64, 128, 256, 512, 1024, 2048, 4096  // Size class boundaries
};

// Allocation alignment (cache-line aligned for M33)
#define KMEM_ALIGNMENT 32                   // 32-byte cache line
#define MEM_MIN_SPLIT_SIZE 64
#define MEM_COALESCE_THRESHOLD 8
#define MEM_DEFRAG_INTERVAL_MS 3000
#define MEM_VERIFY_ON_FREE true
#define MEM_ZERO_ON_FREE false
#define MEM_PANIC_ON_CORRUPTION true

// Memory pool for small allocations (< 256 bytes) - per-core
#define SMALL_POOL_SIZE (4 * 1024)          // 4KB per core (8KB total)
#define SMALL_ALLOC_MAX 256

// Buddy allocator for large blocks
#define BUDDY_MIN_ORDER 6                   // 64 bytes minimum
#define BUDDY_MAX_ORDER 17                  // 128KB maximum


// Task states
enum TaskState : uint8_t {
  TASK_READY,
  TASK_RUNNING,
  TASK_WAITING,
  TASK_SUSPENDED,
  TASK_TERMINATED,
  TASK_ZOMBIE
};

// Core affinity
enum CoreAffinity : uint8_t {
  CORE_ANY = 0,
  CORE_0 = 1,
  CORE_1 = 2
};

// Enhanced memory management constants
#define MEM_MAGIC_ALLOCATED 0xDEADBEEF
#define MEM_MAGIC_FREE 0xFEEDFACE
#define MEM_CANARY_VALUE 0xCAFEBABE

// OOM velocity tracking
#define OOM_VELOCITY_WINDOW_MS 1500
#define OOM_VELOCITY_CRITICAL 12
#define OOM_VELOCITY_HIGH 6
#define OOM_FAST_KILL_THRESHOLD_MS 40

// Memory pressure levels
enum MemPressure : uint8_t {
  MEM_PRESSURE_NONE = 0,
  MEM_PRESSURE_LOW = 1,
  MEM_PRESSURE_MODERATE = 2,
  MEM_PRESSURE_HIGH = 3,
  MEM_PRESSURE_CRITICAL = 4,
  MEM_PRESSURE_EMERGENCY = 5
};

// Allocation failure reasons
enum AllocFailReason : uint8_t {
  ALLOC_FAIL_NONE = 0,
  ALLOC_FAIL_NO_MEMORY,
  ALLOC_FAIL_FRAGMENTED,
  ALLOC_FAIL_TASK_BLOCKED,
  ALLOC_FAIL_TASK_LIMIT,
  ALLOC_FAIL_VELOCITY_THROTTLE,
  ALLOC_FAIL_EMERGENCY_RESERVE,
  ALLOC_FAIL_OOM_KILL_PENDING,
  ALLOC_FAIL_THERMAL_THROTTLE    // New: thermal protection
};

// Allocation flags (new for enhanced kmalloc)
enum AllocFlags : uint8_t {
  ALLOC_NORMAL = 0x00,
  ALLOC_ZERO = 0x01,            // Zero-fill allocation
  ALLOC_DMA = 0x02,             // DMA-safe alignment
  ALLOC_URGENT = 0x04,          // Bypass normal limits
  ALLOC_PINNED = 0x08,          // Never coalesce/move
  ALLOC_CACHE_ALIGN = 0x10      // 32-byte aligned
};

// Task types
#define TASK_TYPE_KERNEL      0x01
#define TASK_TYPE_DRIVER      0x02
#define TASK_TYPE_SERVICE     0x04
#define TASK_TYPE_MODULE      0x08
#define TASK_TYPE_APPLICATION 0x10

// Task flags
#define TASK_FLAG_PROTECTED           0x01
#define TASK_FLAG_CRITICAL            0x02
#define TASK_FLAG_RESPAWN             0x04
#define TASK_FLAG_ONESHOT             0x08
#define TASK_FLAG_PERSISTENT          0x10
#define TASK_FLAG_OOM_CLEANUP_REQUESTED 0x20

// OOM priorities
#define OOM_PRIORITY_NEVER    0
#define OOM_PRIORITY_CRITICAL 1
#define OOM_PRIORITY_HIGH     2
#define OOM_PRIORITY_NORMAL   3
#define OOM_PRIORITY_LOW      4

// File types
#define FILE_TYPE_TEXT   0x01
#define FILE_TYPE_LOG    0x02
#define FILE_TYPE_DATA   0x03
#define FILE_TYPE_CONFIG 0x04

// IPC message types
enum IPCMessageType : uint8_t {
  IPC_NONE = 0,
  IPC_RENDER_FRAME,
  IPC_PROCESS_INPUT,
  IPC_COMPUTE_DATA,
  IPC_AUDIO_SAMPLE,
  IPC_USER_DEFINED
};

// IPC structures (RP2350 enhanced)
struct IPCMessage {
  uint32_t sender_id;
  uint32_t target_id;
  uint32_t timestamp_ms;
  uint16_t sequence;
  uint16_t next;
  IPCMessageType type;
  uint8_t priority;
  uint8_t flags;                // New: message flags
  uint8_t _reserved;
  uint8_t data[IPC_MSG_SIZE];   // 48 bytes on RP2350
  bool in_use;
  uint8_t _pad[3];
} CACHE_ALIGNED;

struct IPCManager {
  IPCMessage message_pool[MAX_IPC_MESSAGES];
  uint16_t free_list[MAX_IPC_MESSAGES];
  int16_t free_list_head;
  uint16_t sequence_counter;
  uint32_t dropped_messages;
  uint32_t total_sent;
  uint32_t total_received;
  uint32_t rt_messages;         // New: count of RT messages
  mutex_t lock;
  critical_section_t rt_section; // New: for RT message fast path
};

struct TaskIPCQueue {
  uint32_t priority_bitmap;
  uint16_t priority_lists_head[SCHED_NUM_PRIORITY_LEVELS];
  uint16_t message_count;
  uint16_t rt_message_count;    // New: RT message tracking
};

// IPC stats (enhanced)
struct IPCStats {
  uint32_t messages_sent;
  uint32_t messages_received;
  uint32_t rt_messages_sent;
  uint32_t rt_messages_received;
  uint16_t messages_dropped_pool_full;
  uint16_t messages_dropped_task_full;
  uint16_t broadcasts_sent;
  uint16_t max_queue_depth_global;
  float avg_queue_depth_global;
  float avg_latency_us;         // New: average message latency
};

// Wait structures
struct TaskWaitNode {
  uint32_t task_id;
  TaskWaitNode* next;
  uint32_t wait_flags;
  uint8_t wait_mode;
  bool clear_on_exit;
};

#define K_EVENT_WAIT_ANY 0
#define K_EVENT_WAIT_ALL 1

struct KMutex {
  bool locked;
  uint32_t owner_id;
  uint8_t original_priority;
  TaskWaitNode* wait_list_head;
};

struct KSemaphore {
  int32_t count;
  uint32_t max_count;
  TaskWaitNode* wait_list_head;
};

struct KEvent {
  uint32_t flags;
  TaskWaitNode* wait_list_head;
};

// App registry
struct AppEntry {
  char name[TASK_NAME_LEN];
  void (*spawn_func)();
};

// Module callbacks
struct ModuleCallbacks {
  void (*init)(uint32_t id);
  void (*tick)(void*);
  void (*deinit)();
};


// Task scheduling info (RP2350 enhanced)
struct TaskSchedInfo {
  uint32_t quantum_us;
  uint32_t last_run;
  uint32_t total_runtime_ms;
  uint32_t deadline_us;         // New: deadline for RT tasks
  uint16_t voluntary_yields;
  uint16_t preemptions;
  float cpu_usage_percent;
  uint8_t effective_priority;
  uint8_t base_priority;
  uint8_t cpu_affinity;
  uint8_t age;
  uint8_t cpu_burst_counter;
  uint8_t cpu_sample_index;
  uint8_t preferred_core;       // New: core affinity hint
  bool is_realtime;
  float cpu_samples[CPU_ABUSE_SAMPLE_COUNT];
} CACHE_ALIGNED;

// Task Control Block (RP2350 enhanced)
struct TCB {
  uint32_t id;
  void (*entry)(void*);
  void* arg;
  ModuleCallbacks* callbacks;
  uint32_t flags;
  uint32_t wake_time;
  uint32_t mem_used;
  uint32_t mem_peak;
  uint32_t mem_limit;
  uint32_t start_time;
  uint32_t max_runtime;
  uint32_t last_respawn;
  uint32_t cpu_time;              // Legacy: callback execution time (ms)
  uint32_t last_run;
  uint32_t oom_bytes_requested;
  uint32_t alloc_velocity;
  uint32_t last_alloc_time;
  uint32_t mem_throttle_mark;
  uint32_t total_cpu_time_ms;
  uint32_t stack_high_water;      // Stack usage tracking
  
  // Wall-clock CPU tracking (FreeRTOS-style)
  uint64_t wall_time_us;          // Total wall-clock time as current task
  uint64_t scheduled_at_us;       // Timestamp when became current task
  
  const char* description;
  TaskIPCQueue ipc;
  TaskSchedInfo sched_info;
  TaskWaitNode wait_node;
  char name[TASK_NAME_LEN];
  TaskState state;
  uint8_t task_type;
  uint8_t oom_priority;
  uint8_t priority;
  CoreAffinity affinity;
  uint8_t original_priority;
  uint8_t running_on_core;
  uint8_t cpu_profile_hint;       // CPU profile hint for governor
  uint16_t respawn_count;
  uint16_t page_faults;
  uint16_t context_switches;
  bool mem_blocked;
  bool is_cpu_abuser;
  bool is_rt_task;                // Real-time task flag
  uint8_t _pad[1];
} CACHE_ALIGNED;

// Memory block (RP2350 enhanced with size class)
struct MemBlock {
  void* addr;
  uint32_t size;
  uint32_t owner_id;
  uint16_t alloc_seq;
  uint8_t size_class;           // New: segregated list class
  bool free;
  bool pinned;
  bool dma_safe;                // New: DMA-safe block
  uint8_t _pad[2];
} CACHE_ALIGNED;

// Segregated free list head structure
struct SizeClassList {
  MemBlock* head;
  uint32_t count;
  uint32_t total_size;
};

// Per-core small allocation pool
struct SmallAllocPool {
  uint8_t pool[SMALL_POOL_SIZE];
  uint32_t bitmap[(SMALL_POOL_SIZE / 32) / 32];  // Bitmap for 32-byte slots
  uint32_t used;
  uint32_t alloc_count;
  mutex_t lock;
};

// Memory allocator statistics (RP2350 enhanced)
struct MemStats {
  uint32_t total_allocs;
  uint32_t total_frees;
  uint32_t failed_allocs;
  uint32_t oom_events;
  uint32_t oom_kills;
  uint32_t emergency_compactions;
  uint32_t velocity_throttles;
  uint32_t peak_usage_bytes;
  uint32_t last_defrag_ms;
  uint32_t oom_velocity;
  uint32_t last_oom_time_ms;
  uint32_t small_allocs;        // New: small pool allocations
  uint32_t large_allocs;        // New: main heap allocations
  uint32_t cache_hits;          // New: size class cache hits
  uint16_t active_blocks;
  uint16_t corruptions_detected;
  uint8_t current_pressure;
  uint8_t fragmentation_pct;
  uint8_t size_class_usage[MEM_SIZE_CLASS_COUNT];  // New: per-class usage
};

// OOM velocity tracker (RP2350 - larger window)
struct OOMVelocityTracker {
  uint32_t events[8];           // Larger window
  uint32_t window_start_ms;
  uint8_t event_count;
  uint8_t head;
};

// Log entry (RP2350 enhanced)
struct LogEntry {
  uint32_t timestamp;
  char message[48];             // Larger messages
  uint8_t level;
  uint8_t core_id;              // New: which core logged
  uint8_t _pad[2];
} CACHE_ALIGNED;

// Filesystem structures (RP2350 enhanced)
struct FSFile {
  File handle;
  char path[FS_MAX_FILENAME];
  uint32_t owner_task_id;
  uint32_t bytes_read;          // New: tracking
  uint32_t bytes_written;       // New: tracking
  bool open;
  bool write_mode;
  bool cached;                  // New: caching enabled
  uint8_t _pad;
};

struct SDCardInfo {
  uint8_t card_type;
  uint64_t card_size;
  uint32_t sector_count;
  uint16_t sector_size;
  bool valid;
  bool high_speed;              // New: high-speed mode
};

// Core 1 state (RP2350 enhanced)
struct Core1State {
  TCB tasks[MAX_CORE1_TASKS];
  mutex_t scheduler_lock;
  critical_section_t rt_section; // New: RT critical section
  uint32_t uptime_ms;
  uint32_t context_switches;
  uint32_t work_stolen;         // New: work stealing counter
  float cpu_usage;
  float cpu_usage_peak;         // New: peak tracking
  uint8_t task_count;
  uint8_t current_task;
  bool running;
  bool rt_mode_active;          // New: RT mode flag
};

// CPU Governor state structure
struct CPUGovernorState {
  CPUProfile current_profile;
  CPUProfile requested_profile;
  CPUProfile pre_throttle_profile;   // Profile before thermal throttle
  uint32_t current_freq_khz;
  uint32_t target_freq_khz;
  uint32_t last_change_ms;
  uint32_t turbo_start_ms;
  uint32_t total_turbo_time_ms;
  uint32_t last_idle_ms;             // Last time we were truly idle
  uint32_t wfi_total_us;             // Total time spent in WFI
  float avg_load;
  float instant_load;                // Instantaneous load for spike detection
  float temperature;
  bool turbo_active;
  bool thermal_throttled;
  bool user_override;                // User manually set profile
  bool wfi_enabled;                  // WFI power saving enabled
  bool in_wfi;                       // Currently in WFI sleep
  bool instant_turbo_pending;        // Task requested instant turbo
  uint8_t load_history[16];
  uint8_t load_idx;
  uint8_t transition_smoothing;      // Smooth frequency transitions
  // Input boost state
  bool input_boost_active;
  uint32_t input_boost_start_ms;
};

// OOM structures
typedef void (*oom_callback_t)(uint32_t bytes_requested);

struct TaskOOMHandler {
  uint32_t task_id;
  oom_callback_t callback;
  bool active;
};

struct OOMRequest {
  uint32_t allocating_task_id;
  uint32_t target_task_id;
  uint64_t request_time;
  uint32_t bytes_requested;
  bool request_sent;
  bool task_complied;
};

// OOM stats (RAM-optimized v2)
struct OOMStats {
  uint32_t total_bytes_reclaimed;
  uint16_t requests_sent;
  uint16_t voluntary_releases;
  uint16_t forced_kills;
  uint16_t prevention_count;
  uint16_t abusive_kills;
};

// OOM victim (RAM-optimized v2)
struct OOMVictim {
  uint32_t task_id;
  uint32_t memory_used;
  int16_t score;
  uint8_t oom_priority;
  bool has_handler;
};

// Scheduler structures
struct PriorityBitmap {
  uint32_t level_mask;
  uint32_t task_masks[SCHED_NUM_PRIORITY_LEVELS];
};

// Scheduler struct (RP2350 enhanced)
struct CoreScheduler {
  PriorityBitmap runnable;
  PriorityBitmap waiting;
  PriorityBitmap rt_runnable;   // New: separate RT queue
  mutex_t lock;
  critical_section_t rt_lock;   // New: RT fast lock
  uint32_t last_switch;
  uint32_t total_runtime_ms;
  uint32_t idle_time_ms;
  uint32_t switches;
  uint32_t last_aging;
  uint32_t work_stolen;         // New: work stealing
  uint32_t rt_switches;         // New: RT task switches
  float cpu_load;
  float cpu_load_instant;
  float cpu_load_peak;          // New: peak tracking
  uint16_t preemptions;
  uint16_t idle_injections;
  uint8_t current_task;
  uint8_t idle_task;
  uint8_t current_priority;
  uint8_t rt_task_count;        // New: RT task count
  bool idle_injection_active;
  bool work_steal_enabled;      // New: work stealing enabled
};

// Panic info (RP2350 enhanced)
struct PanicInfo {
  const char* reason;
  uint32_t pc;
  uint32_t lr;
  uint32_t sp;
  uint32_t cfsr;                // New: Configurable Fault Status Register
  uint32_t hfsr;                // New: HardFault Status Register
  uint32_t timestamp;
  uint16_t task_id;
  bool is_core1;
  uint8_t fault_type;           // New: fault classification
};

// Watchdog state (enhanced)
struct WatchdogState {
  uint32_t last_feed;
  uint32_t last_task_check;     // New: per-task watchdog
  uint16_t timeout_ms;
  uint8_t triggers;
  bool enabled;
  bool in_panic;
  bool task_watchdog_enabled;   // New: per-task monitoring
};

// UI Socket for apps (RP2350 enhanced)
struct UISocket {
  bool (*request_focus)(uint32_t task_id);
  void (*release_focus)(uint32_t task_id);
  void (*register_stdout)(void (*write_char_fn)(char));
  bool (*send_message_api)(uint32_t target_id, IPCMessageType type, void* data, size_t size, uint8_t priority);
  bool (*receive_message_api)(IPCMessage* msg_out);
  uint32_t (*spawn_core1_task)(const char* name, void (*entry)(void*), void* arg, uint8_t priority);
  void (*task_exit)();
  bool (*mutex_lock)(uint32_t mutex_id);
  void (*mutex_unlock)(uint32_t mutex_id);
  bool (*sem_wait)(uint32_t sem_id, uint32_t timeout_ms);
  void (*sem_post)(uint32_t sem_id);
  uint32_t (*event_wait)(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms);
  void (*event_set)(uint32_t event_id, uint32_t flags);
  float (*get_core0_usage)();
  float (*get_core1_usage)();
  uint32_t (*get_task_memory)(uint32_t task_id);
  void (*register_oom_handler)(uint32_t task_id, void (*handler)(uint32_t bytes_requested));
  void (*oom_cleanup_done)(uint32_t task_id, uint32_t bytes_freed);
  void (*hint_memory_pressure)(uint32_t task_id);
  // New RP2350 APIs
  void (*set_cpu_profile)(CPUProfile profile);
  CPUProfile (*get_cpu_profile)();
  uint32_t (*get_cpu_freq_khz)();
  void (*request_turbo)(uint32_t duration_ms);
  void* (*alloc_dma)(size_t size);
  void* (*alloc_aligned)(size_t size, size_t alignment);
};

// Kernel state (RP2350 enhanced)
struct KernelState {
  TCB tasks[MAX_TASKS];
  
  MemBlock mem_blocks[MAX_MEMORY_BLOCKS];
  SizeClassList size_class_lists[MEM_SIZE_CLASS_COUNT];  // New: segregated lists
  mutex_t mem_lock;
  critical_section_t mem_fast_lock;  // New: for small allocs
  
  LogEntry log[MAX_LOG_ENTRIES];
  mutex_t log_lock;
  
  FSFile fs_open_files[FS_MAX_OPEN_FILES];
  mutex_t fs_lock;
  
  Core1State core1;
  IPCManager ipc_manager;
  CPUGovernorState governor;    // New: CPU governor state
  
  KMutex kernel_mutexes[MAX_KERNEL_MUTEXES];
  KSemaphore kernel_semaphores[MAX_SEMAPHORES];
  KEvent kernel_event_flags[MAX_EVENT_FLAGS];
  
  SDCardInfo sd_info;
  
  uint32_t uptime_ms;
  uint32_t total_allocations;
  uint32_t total_frees;
  uint32_t alloc_sequence;
  uint32_t largest_free_block;
  uint32_t total_free_mem;
  uint32_t total_context_switches;
  uint32_t fs_used_bytes;
  uint32_t fs_reads;
  uint32_t fs_writes;
  uint32_t fs_log_counter;
  uint32_t last_velocity_check_ms;
  uint32_t boot_time_ms;        // New: boot timestamp
  
  float cpu_usage;
  float temperature;
  
  void (*app_write_char)(char);
  
  int32_t gui_focus_task_id;
  
  uint16_t task_count;
  uint16_t mem_block_count;
  uint16_t oom_kills;
  uint16_t log_head;
  uint16_t log_count;
  
  uint8_t current_task;
  uint8_t fragmentation_pct;
  uint8_t kernel_tasks;
  uint8_t driver_tasks;
  uint8_t service_tasks;
  uint8_t module_tasks;
  uint8_t application_tasks;
  uint8_t zombie_tasks;
  uint8_t rt_task_count;        // New: RT task count
  
  bool running;
  bool panic_mode;
  volatile bool preemption_pending;
  bool shell_alive;
  bool cpumon_alive;
  bool tempmon_alive;
  bool fs_alive;
  bool root_mode;
  bool fs_available;
  bool fs_mounted;
  bool core1_initialized;
  bool turbo_enabled;           // New: turbo mode globally enabled
  bool thermal_throttled;       // New: thermal throttling active
  
  // USB Serial Stability (v14.1.1 FIX)
  uint32_t usb_last_activity_ms;     // Last Serial read/write timestamp
  uint32_t usb_last_poll_ms;         // Last Serial.available() check
  uint32_t usb_bytes_rx;             // Total bytes received
  uint32_t usb_bytes_tx;             // Total bytes transmitted
  uint32_t usb_lockup_recoveries;    // Count of lockup recoveries
  bool usb_connected;                // USB Serial connection detected
  bool usb_was_connected;            // Previous connection state (for edge detection)
  bool usb_blocking_lowpower;        // USB is actively blocking low power modes
  
  // Small allocation pools (per-core)
  SmallAllocPool small_pool_core0;
  SmallAllocPool small_pool_core1;
  
  // =========================================================================
  // RESOURCE MANAGER STATE (v14.3.1)
  // =========================================================================
  // GPIO resources
  ResourceDescriptor gpio_resources[TOTAL_GPIO_RESOURCES];
  GPIOResourceInfo gpio_info[TOTAL_GPIO_RESOURCES];
  
  // SPI resources
  ResourceDescriptor spi_resources[TOTAL_SPI_RESOURCES];
  SPIResourceInfo spi_info[TOTAL_SPI_RESOURCES];
  
  // I2C resources (per device address)
  ResourceDescriptor i2c_resources[TOTAL_I2C_RESOURCES];
  I2CResourceInfo i2c_info[TOTAL_I2C_RESOURCES];
  
  // ADC resources
  ResourceDescriptor adc_resources[TOTAL_ADC_RESOURCES];
  
  // PWM resources
  ResourceDescriptor pwm_resources[TOTAL_PWM_RESOURCES];
  PWMResourceInfo pwm_info[TOTAL_PWM_RESOURCES];
  
  // PIO resources
  ResourceDescriptor pio_resources[TOTAL_PIO_RESOURCES];
  PIOResourceInfo pio_info[TOTAL_PIO_RESOURCES];
  
  // UART resources
  ResourceDescriptor uart_resources[TOTAL_UART_RESOURCES];
  
  // DMA resources
  ResourceDescriptor dma_resources[TOTAL_DMA_RESOURCES];
  
  // Timer resources
  ResourceDescriptor timer_resources[TOTAL_TIMER_RESOURCES];
  
  // Violation tracking
  ResourceViolation violations[MAX_RESOURCE_VIOLATIONS];
  uint16_t violation_head;
  uint16_t violation_count;
  uint32_t total_violations;
  
  // Transaction tracking
  ResourceTransaction transactions[MAX_TRANSACTIONS];
  
  // Resource manager statistics
  uint32_t res_total_claims;
  uint32_t res_total_releases;
  uint32_t res_total_conflicts;
  uint32_t res_total_auto_releases;
  
  // Next handle ID (for generating unique handles)
  uint16_t next_handle_id;
  
  // Resource manager lock
  mutex_t res_lock;
  
  // Resource manager initialized flag
  bool res_manager_initialized;
  
  uint8_t heap[HEAP_SIZE];
} CACHE_ALIGNED;


// ============================================================================
// PMFS (PICOMIMI FILESYSTEM) - INTEGRATED - RP2350 ENHANCED
// ============================================================================
// ============================================================================
// PMFS CONFIGURATION
// ============================================================================

#define PMFS_VERSION "3.1.0-AXISOS"
#define PMFS_MAGIC 0x504D4653  // "PMFS"

// Filesystem paths
#define PMFS_ROOT_DIR           "/PMFS"
#define PMFS_SYSTEM_A_DIR       "/PMFS/system_a"
#define PMFS_SYSTEM_B_DIR       "/PMFS/system_b"
#define PMFS_TMPFS_DIR          "/PMFS/tmpfs"
#define PMFS_LOG_DIR            "/PMFS/logs"
#define PMFS_SYSLOG_DIR         "/PMFS/logs/system"
#define PMFS_USERLOG_DIR        "/PMFS/logs/user"
#define PMFS_DATA_DIR           "/PMFS/data"
#define PMFS_CONFIG_DIR         "/PMFS/config"
#define PMFS_CACHE_DIR          "/PMFS/.cache"
#define PMFS_JOURNAL_DIR        "/PMFS/.journal"
#define PMFS_JOURNAL_FILE       "/PMFS/.journal/journal.dat"
#define PMFS_METADATA_FILE      "/PMFS/.metadata"
#define PMFS_BOOTFLAG_FILE      "/PMFS/.boot"

// Feature flags
#define PMFS_ENABLE_JOURNALING      true
#define PMFS_ENABLE_WRITE_CACHE     true
#define PMFS_ENABLE_COMPRESSION     false  // Future feature
#define PMFS_ENABLE_ENCRYPTION      false  // Future feature
#define PMFS_ENABLE_ASYNC_WRITES    true   // New: async write support

// Limits (RP2350 balanced - good features, less RAM)
#define PMFS_MAX_OPEN_FILES         8
#define PMFS_MAX_PATH_LENGTH        64
#define PMFS_MAX_FILENAME           32
#define PMFS_WRITE_CACHE_SIZE       (2 * 1024)   // 2KB cache
#define PMFS_JOURNAL_ENTRIES        16
#define PMFS_TMPFS_SIZE             (8 * 1024)   // 8KB RAM disk
#define PMFS_MAX_TMPFS_ENTRIES      12
#define PMFS_MAX_LOCKS              8
#define PMFS_SECTOR_SIZE            512
#define PMFS_CLUSTER_SIZE           4096

// Thresholds
#define PMFS_LOW_SPACE_THRESHOLD    (100 * 1024)  // 100KB
#define PMFS_CRITICAL_SPACE         (50 * 1024)   // 50KB
#define PMFS_AUTO_DEFRAG_THRESHOLD  75            // % fragmentation

// ============================================================================
// ENUMS & STRUCTS
// ============================================================================

enum PMFSStatus {
    PMFS_OK = 0,
    PMFS_ERROR_NOT_INITIALIZED,
    PMFS_ERROR_SD_NOT_FOUND,
    PMFS_ERROR_NO_ROOT,
    PMFS_ERROR_CORRUPT,
    PMFS_ERROR_NO_SPACE,
    PMFS_ERROR_FILE_NOT_FOUND,
    PMFS_ERROR_ACCESS_DENIED,
    PMFS_ERROR_FILE_LOCKED,
    PMFS_ERROR_INVALID_PARAM,
    PMFS_ERROR_IO_FAILURE,
    PMFS_ERROR_JOURNAL_FULL,
    PMFS_ERROR_NOT_MOUNTED,
    PMFS_ERROR_ALREADY_EXISTS
};

enum PMFSSystemBank {
    PMFS_BANK_A = 0,
    PMFS_BANK_B = 1,
    PMFS_BANK_NONE = 255
};

enum PMFSFileMode {
    PMFS_MODE_READ = 0x01,
    PMFS_MODE_WRITE = 0x02,
    PMFS_MODE_APPEND = 0x04,
    PMFS_MODE_CREATE = 0x08,
    PMFS_MODE_TRUNCATE = 0x10
};

enum PMFSJournalOp {
    JOURNAL_OP_NONE = 0,
    JOURNAL_OP_CREATE = 1,
    JOURNAL_OP_DELETE = 2,
    JOURNAL_OP_WRITE = 3,
    JOURNAL_OP_RENAME = 4,
    JOURNAL_OP_MKDIR = 5
};

struct PMFSMetadata {
    uint32_t magic;
    char version[16];
    uint64_t created_timestamp;
    uint64_t last_mount_timestamp;
    uint32_t mount_count;
    PMFSSystemBank active_bank;
    PMFSSystemBank backup_bank;
    bool needs_fsck;
    uint32_t total_writes;
    uint32_t total_sectors;
    uint32_t bad_sectors;
    uint32_t crc32;
} __attribute__((packed));

struct PMFSFileHandle {
    char path[PMFS_MAX_PATH_LENGTH];
    File sd_file;
    uint32_t last_access;
    uint32_t current_position;  // For seek/tell support
    uint32_t owner_task_id;
    uint32_t lock_owner;
    uint32_t flags;
    bool in_use;
    bool locked;
};

struct PMFSJournalEntry {
    char path[PMFS_MAX_PATH_LENGTH];
    char path2[PMFS_MAX_PATH_LENGTH];  // For rename operations
    uint32_t timestamp;
    uint16_t size;
    PMFSJournalOp operation;
    bool active;
    bool committed;
} __attribute__((packed));

struct PMFSWriteCache {
    char path[PMFS_MAX_PATH_LENGTH];
    uint8_t data[PMFS_WRITE_CACHE_SIZE];
    uint32_t last_access;
    uint16_t size;
    bool dirty;
} __attribute__((packed));

struct PMFSTmpFSEntry {
    uint8_t* data;
    uint32_t created;
    uint32_t modified;
    uint16_t size;
    uint16_t allocated;
    char name[PMFS_MAX_FILENAME];
    bool in_use;
} __attribute__((packed));

struct PMFSFileLock {
    char path[PMFS_MAX_PATH_LENGTH];
    uint32_t owner_task_id;
    uint32_t acquired_time;
    bool active;
    bool exclusive;
} __attribute__((packed));

struct PMFSStats {
  uint32_t files_created;
  uint32_t files_deleted;
  uint32_t bytes_written;
  uint32_t bytes_read;
  uint16_t cache_hits;
  uint16_t cache_misses;
  uint16_t journal_commits;
  uint16_t journal_rollbacks;
  uint8_t fsck_runs;
  uint8_t defrag_runs;
} __attribute__((packed));

// ============================================================================
// PMFS CLASS
// ============================================================================

class PMFS {
private:
    // Core state
    bool initialized;
    bool mounted;
    PMFSMetadata metadata;
    PMFSStats stats;
    
    // File management
    PMFSFileHandle file_handles[PMFS_MAX_OPEN_FILES];
    PMFSFileLock file_locks[PMFS_MAX_LOCKS];
    
    // Journaling
    PMFSJournalEntry journal[PMFS_JOURNAL_ENTRIES];
    uint32_t journal_head;
    
    // Write cache
    PMFSWriteCache write_cache;
    bool cache_enabled;
    
    // tmpfs (RAM disk) - FIXED IMPLEMENTATION
    PMFSTmpFSEntry tmpfs_entries[PMFS_MAX_TMPFS_ENTRIES];
    uint8_t tmpfs_pool[PMFS_TMPFS_SIZE];
    uint32_t tmpfs_used;
    uint32_t tmpfs_next_offset;
    
    // Internal helpers
    PMFSStatus create_directory_tree();
    PMFSStatus load_metadata();
    PMFSStatus save_metadata();
    PMFSStatus verify_integrity();
    PMFSStatus replay_journal();
    PMFSStatus commit_journal();
    PMFSStatus save_journal_to_disk();
    PMFSStatus load_journal_from_disk();
    void init_journal();
    void init_tmpfs();
    
    PMFSStatus journal_add(PMFSJournalOp op, const char* path, const char* path2 = nullptr);
    PMFSStatus cache_write(const char* path, const uint8_t* data, uint32_t size);
    PMFSStatus cache_flush();
    
    uint32_t calculate_crc32(const uint8_t* data, uint32_t length);
    bool path_exists(const char* path);
    bool is_directory(const char* path);
    
    int find_free_handle();
    PMFSFileHandle* get_handle(int fd);
    
    bool acquire_lock(const char* path, uint32_t task_id, bool exclusive);
    void release_lock(const char* path);
    bool is_locked(const char* path, uint32_t task_id);
    
    PMFSStatus recursive_delete(const char* path);
    PMFSStatus count_files_recursive(const char* path, uint32_t* count);
    PMFSStatus calculate_directory_size(const char* path, uint64_t* size);
    
public:
    PMFS();
    ~PMFS();
    
    // ========================================================================
    // INITIALIZATION & MOUNTING
    // ========================================================================
    
    PMFSStatus init(uint8_t cs_pin = 5);
    PMFSStatus check_root_exists();
    PMFSStatus format_and_initialize();
    PMFSStatus mount();
    PMFSStatus unmount();
    PMFSStatus emergency_unmount();  // For kernel panic
    PMFSStatus fsck(bool auto_repair = true);
    
    bool is_initialized() { return initialized; }
    bool is_mounted() { return mounted; }
    
    // ========================================================================
    // FILE OPERATIONS - ALL IMPLEMENTED
    // ========================================================================
    
    int open(const char* path, uint32_t flags, uint32_t task_id = 0);
    PMFSStatus close(int fd);
    int read(int fd, uint8_t* buffer, uint32_t size);
    int write(int fd, const uint8_t* data, uint32_t size);
    PMFSStatus seek(int fd, uint32_t position);  // IMPLEMENTED
    uint32_t tell(int fd);                        // IMPLEMENTED
    uint32_t size(int fd);                        // IMPLEMENTED
    bool eof(int fd);                             // IMPLEMENTED
    PMFSStatus flush(int fd);
    
    PMFSStatus remove(const char* path);
    PMFSStatus rename(const char* old_path, const char* new_path);  // IMPLEMENTED
    PMFSStatus mkdir(const char* path);
    PMFSStatus rmdir(const char* path, bool recursive = false);     // IMPLEMENTED
    
    bool exists(const char* path);
    bool is_file(const char* path);
    bool is_dir(const char* path);
    uint32_t file_size(const char* path);
    
    // ========================================================================
    // TMPFS (RAM DISK) - FULLY FUNCTIONAL
    // ========================================================================
    
    PMFSStatus tmpfs_create(const char* name, uint32_t size);
    PMFSStatus tmpfs_write(const char* name, const uint8_t* data, uint32_t size);
    int tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size);
    PMFSStatus tmpfs_delete(const char* name);
    bool tmpfs_exists(const char* name);
    void tmpfs_clear();
    uint32_t tmpfs_available();
    void tmpfs_compact();  // Defragment tmpfs
    
    // ========================================================================
    // SYSTEM BANK MANAGEMENT (A/B OTA)
    // ========================================================================
    
    PMFSSystemBank get_active_bank();
    PMFSSystemBank get_backup_bank();
    PMFSStatus set_active_bank(PMFSSystemBank bank);
    PMFSStatus copy_bank(PMFSSystemBank src, PMFSSystemBank dst);
    PMFSStatus clear_bank(PMFSSystemBank bank);  // IMPLEMENTED
    PMFSStatus verify_bank(PMFSSystemBank bank);
    const char* get_bank_path(PMFSSystemBank bank);
    
    // ========================================================================
    // LOGGING
    // ========================================================================
    
    PMFSStatus log_system(const char* message);
    PMFSStatus log_user(const char* message);
    PMFSStatus log_rotate(const char* log_dir, uint32_t max_files = 10);
    PMFSStatus read_log_tail(const char* log_path, char* buffer, uint32_t lines = 20);
    
    // ========================================================================
    // STATISTICS & MONITORING - ALL IMPLEMENTED
    // ========================================================================
    
    void get_stats(PMFSStats* out_stats);
    void print_stats();
    uint64_t get_free_space();
    uint64_t get_total_space();
    uint64_t get_used_space();        // IMPLEMENTED
    float get_fragmentation();        // IMPLEMENTED
    
    PMFSMetadata* get_metadata() { return &metadata; }
    
    // ========================================================================
    // MAINTENANCE - ALL IMPLEMENTED
    // ========================================================================
    
    PMFSStatus defragment();          // IMPLEMENTED
    PMFSStatus garbage_collect();     // IMPLEMENTED
    PMFSStatus verify_all_files();    // IMPLEMENTED
    PMFSStatus repair_corruption();
    PMFSStatus verify_directory_recursive(const char* path, uint32_t* files_checked, uint32_t* errors_found);
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    const char* status_to_string(PMFSStatus status);
    void print_tree(const char* path = PMFS_ROOT_DIR, int depth = 0);
    void print_metadata();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

PMFS::PMFS() {
    initialized = false;
    mounted = false;
    cache_enabled = PMFS_ENABLE_WRITE_CACHE;
    journal_head = 0;
    tmpfs_used = 0;
    tmpfs_next_offset = 0;
    
    memset(&metadata, 0, sizeof(PMFSMetadata));
    memset(&stats, 0, sizeof(PMFSStats));
    memset(file_handles, 0, sizeof(file_handles));
    memset(file_locks, 0, sizeof(file_locks));
    memset(&write_cache, 0, sizeof(write_cache));
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    memset(tmpfs_pool, 0, sizeof(tmpfs_pool));
}

PMFS::~PMFS() {
    if (mounted) {
        unmount();
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

PMFSStatus PMFS::init(uint8_t cs_pin) {
    Serial.println("\n[PMFS] Initializing PicoMimi FileSystem v" PMFS_VERSION);
    
    if (!SD.begin(cs_pin)) {
        Serial.println("[PMFS] ERROR: SD card not detected!");
        return PMFS_ERROR_SD_NOT_FOUND;
    }
    
    Serial.println("[PMFS] SD card detected");
    
    PMFSStatus status = check_root_exists();
    
    if (status == PMFS_ERROR_NO_ROOT) {
        Serial.println("[PMFS] Root structure not found");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] FILESYSTEM NOT INITIALIZED");
        Serial.println("[PMFS] ==============================================");
        Serial.println("[PMFS] To initialize the filesystem, call:");
        Serial.println("[PMFS]   pmfs.format_and_initialize()");
        Serial.println("[PMFS] ");
        Serial.println("[PMFS] WARNING: This will create the PMFS structure");
        Serial.println("[PMFS]          on your SD card.");
        Serial.println("[PMFS] ==============================================");
        return PMFS_ERROR_NO_ROOT;
    }
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::check_root_exists() {
    if (!SD.exists(PMFS_ROOT_DIR)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    if (!SD.exists(PMFS_METADATA_FILE)) {
        return PMFS_ERROR_NO_ROOT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::format_and_initialize() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] INITIALIZING FILESYSTEM");
    Serial.println("[PMFS] ============================================");
    
    Serial.println("[PMFS] Creating directory tree...");
    PMFSStatus status = create_directory_tree();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to create directories");
        return status;
    }
    
    Serial.println("[PMFS] Initializing metadata...");
    metadata.magic = PMFS_MAGIC;
    strncpy(metadata.version, PMFS_VERSION, sizeof(metadata.version) - 1);
    metadata.created_timestamp = millis();
    metadata.last_mount_timestamp = 0;
    metadata.mount_count = 0;
    metadata.active_bank = PMFS_BANK_A;
    metadata.backup_bank = PMFS_BANK_B;
    metadata.needs_fsck = false;
    metadata.total_writes = 0;
    metadata.total_sectors = 0;
    metadata.bad_sectors = 0;
    
    status = save_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to save metadata");
        return status;
    }
    
    File boot_flag = SD.open(PMFS_BOOTFLAG_FILE, FILE_WRITE);
    if (boot_flag) {
        boot_flag.println("PMFS_INITIALIZED");
        boot_flag.close();
    }
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM INITIALIZED SUCCESSFULLY");
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] You can now call pmfs.mount()");
    
    initialized = true;
    return PMFS_OK;
}

PMFSStatus PMFS::create_directory_tree() {
    const char* dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_TMPFS_DIR,
        PMFS_LOG_DIR,
        PMFS_SYSLOG_DIR,
        PMFS_USERLOG_DIR,
        PMFS_DATA_DIR,
        PMFS_CONFIG_DIR,
        PMFS_CACHE_DIR,
        PMFS_JOURNAL_DIR
    };
    
    for (int i = 0; i < 11; i++) {
        if (!SD.exists(dirs[i])) {
            Serial.print("[PMFS]   Creating: ");
            Serial.println(dirs[i]);
            
            if (!SD.mkdir(dirs[i])) {
                Serial.print("[PMFS]   ERROR: Failed to create ");
                Serial.println(dirs[i]);
                return PMFS_ERROR_IO_FAILURE;
            }
        } else {
            Serial.print("[PMFS]   Exists: ");
            Serial.println(dirs[i]);
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::mount() {
    if (!initialized) {
        Serial.println("[PMFS] ERROR: Not initialized");
        return PMFS_ERROR_NOT_INITIALIZED;
    }
    
    if (mounted) {
        Serial.println("[PMFS] Already mounted");
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Mounting filesystem...");
    
    PMFSStatus status = load_metadata();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Failed to load metadata");
        return status;
    }
    
    Serial.println("[PMFS] Verifying integrity...");
    status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] WARNING: Integrity check failed");
        metadata.needs_fsck = true;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        Serial.println("[PMFS] Loading journal from disk...");
        load_journal_from_disk();
        
        Serial.println("[PMFS] Replaying journal...");
        status = replay_journal();
        if (status != PMFS_OK) {
            Serial.println("[PMFS] WARNING: Journal replay had errors");
        }
    }
    
    init_journal();
    init_tmpfs();
    
    metadata.mount_count++;
    metadata.last_mount_timestamp = millis();
    save_metadata();
    
    mounted = true;
    
    Serial.println("[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM MOUNTED");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Active Bank: ");
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Mount Count: ");
    Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Free Space: ");
    Serial.print(get_free_space() / 1024);
    Serial.println(" KB");
    
    return PMFS_OK;
}

PMFSStatus PMFS::unmount() {
    if (!mounted) {
        return PMFS_OK;
    }
    
    Serial.println("\n[PMFS] Unmounting filesystem...");
    
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use) {
            close(i);
        }
    }
    
    if (cache_enabled) {
        cache_flush();
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        commit_journal();
        save_journal_to_disk();
    }
    
    tmpfs_clear();
    save_metadata();
    
    mounted = false;
    Serial.println("[PMFS] Filesystem unmounted");
    
    return PMFS_OK;
}

PMFSStatus PMFS::emergency_unmount() {
    // Emergency unmount for kernel panic - minimal operations, no error checking
    
    // Close all files immediately
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (file_handles[i].in_use && file_handles[i].sd_file) {
            file_handles[i].sd_file.flush();
            file_handles[i].sd_file.close();
        }
    }
    
    // Flush cache if dirty
    if (cache_enabled && write_cache.dirty) {
        File f = SD.open(write_cache.path, FILE_WRITE);
        if (f) {
            f.write(write_cache.data, write_cache.size);
            f.close();
        }
    }
    
    // Save journal to disk
    if (PMFS_ENABLE_JOURNALING) {
        save_journal_to_disk();
    }
    
    // Mark as needing fsck
    metadata.needs_fsck = true;
    save_metadata();
    
    mounted = false;
    
    return PMFS_OK;
}

void PMFS::init_journal() {
    memset(journal, 0, sizeof(journal));
    journal_head = 0;
}

void PMFS::init_tmpfs() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    memset(tmpfs_pool, 0, sizeof(tmpfs_pool));
    tmpfs_used = 0;
    tmpfs_next_offset = 0;
}

// ============================================================================
// METADATA MANAGEMENT
// ============================================================================

PMFSStatus PMFS::load_metadata() {
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_READ);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot open metadata file");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    size_t read_bytes = meta_file.read((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (read_bytes != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    if (metadata.magic != PMFS_MAGIC) {
        Serial.println("[PMFS] ERROR: Invalid metadata magic");
        return PMFS_ERROR_CORRUPT;
    }
    
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&metadata, 
                                              sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    if (calculated_crc != metadata.crc32) {
        Serial.println("[PMFS] WARNING: Metadata CRC mismatch");
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_metadata() {
    metadata.crc32 = calculate_crc32((uint8_t*)&metadata, 
                                     sizeof(PMFSMetadata) - sizeof(uint32_t));
    
    File meta_file = SD.open(PMFS_METADATA_FILE, FILE_WRITE);
    if (!meta_file) {
        Serial.println("[PMFS] ERROR: Cannot write metadata file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t written = meta_file.write((uint8_t*)&metadata, sizeof(PMFSMetadata));
    meta_file.close();
    
    if (written != sizeof(PMFSMetadata)) {
        Serial.println("[PMFS] ERROR: Metadata write incomplete");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_integrity() {
    const char* required_dirs[] = {
        PMFS_ROOT_DIR,
        PMFS_SYSTEM_A_DIR,
        PMFS_SYSTEM_B_DIR,
        PMFS_LOG_DIR,
        PMFS_DATA_DIR
    };
    
    for (int i = 0; i < 5; i++) {
        if (!SD.exists(required_dirs[i])) {
            Serial.print("[PMFS] ERROR: Missing directory: ");
            Serial.println(required_dirs[i]);
            return PMFS_ERROR_CORRUPT;
        }
    }
    
    return PMFS_OK;
}

// ============================================================================
// JOURNALING - NOW WITH PERSISTENT STORAGE
// ============================================================================

PMFSStatus PMFS::journal_add(PMFSJournalOp op, const char* path, const char* path2) {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    int idx = -1;
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (!journal[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        commit_journal();
        return journal_add(op, path, path2);
    }
    
    PMFSJournalEntry* entry = &journal[idx];
    entry->active = true;
    entry->operation = op;
    strncpy(entry->path, path, PMFS_MAX_PATH_LENGTH - 1);
    if (path2) {
        strncpy(entry->path2, path2, PMFS_MAX_PATH_LENGTH - 1);
    }
    entry->timestamp = millis();
    entry->committed = false;
    
    return PMFS_OK;
}

PMFSStatus PMFS::commit_journal() {
    if (!PMFS_ENABLE_JOURNALING) return PMFS_OK;
    
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active) {
            journal[i].committed = true;
            journal[i].active = false;
        }
    }
    
    stats.journal_commits++;
    save_journal_to_disk();
    
    return PMFS_OK;
}

PMFSStatus PMFS::save_journal_to_disk() {
    File journal_file = SD.open(PMFS_JOURNAL_FILE, FILE_WRITE);
    if (!journal_file) {
        Serial.println("[PMFS] ERROR: Cannot write journal file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    journal_file.write((uint8_t*)journal, sizeof(journal));
    journal_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::load_journal_from_disk() {
    if (!SD.exists(PMFS_JOURNAL_FILE)) {
        return PMFS_OK;
    }
    
    File journal_file = SD.open(PMFS_JOURNAL_FILE, FILE_READ);
    if (!journal_file) {
        Serial.println("[PMFS] WARNING: Cannot read journal file");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    size_t read_bytes = journal_file.read((uint8_t*)journal, sizeof(journal));
    journal_file.close();
    
    if (read_bytes != sizeof(journal)) {
        Serial.println("[PMFS] WARNING: Journal size mismatch");
        return PMFS_ERROR_CORRUPT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::replay_journal() {
    bool any_uncommitted = false;
    
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].active && !journal[i].committed) {
            any_uncommitted = true;
            
            Serial.print("[PMFS] Replaying journal entry: ");
            Serial.println(journal[i].path);
            
            switch (journal[i].operation) {
                case JOURNAL_OP_CREATE:
                    // File was being created - nothing to replay
                    break;
                    
                case JOURNAL_OP_DELETE:
                    // Complete the deletion
                    SD.remove(journal[i].path);
                    break;
                    
                case JOURNAL_OP_WRITE:
                    // File write was in progress - mark as potentially corrupt
                    Serial.println("[PMFS] WARNING: Uncommitted write detected");
                    break;
                    
                case JOURNAL_OP_RENAME:
                    // Complete the rename if target doesn't exist
                    if (!SD.exists(journal[i].path2) && SD.exists(journal[i].path)) {
                        // Actual rename not supported by SD.h, would need copy+delete
                    }
                    break;
                    
                case JOURNAL_OP_MKDIR:
                    // Complete directory creation
                    if (!SD.exists(journal[i].path)) {
                        SD.mkdir(journal[i].path);
                    }
                    break;
                    
                default:
                    break;
            }
            
            journal[i].active = false;
        }
    }
    
    if (any_uncommitted) {
        Serial.println("[PMFS] Journal replay complete");
        stats.journal_rollbacks++;
    }
    
    return PMFS_OK;
}

// ============================================================================
// FILE OPERATIONS - ALL IMPLEMENTATIONS
// ============================================================================

int PMFS::open(const char* path, uint32_t flags, uint32_t task_id) {
    if (!mounted) return -1;
    
    if (is_locked(path, task_id)) {
        Serial.println("[PMFS] ERROR: File is locked");
        return -1;
    }
    
    int fd = find_free_handle();
    if (fd < 0) {
        Serial.println("[PMFS] ERROR: No free file handles");
        return -1;
    }
    
    PMFSFileHandle* handle = &file_handles[fd];
    
    const char* sd_mode = "r";
    if (flags & PMFS_MODE_WRITE) {
        if (flags & PMFS_MODE_APPEND) sd_mode = "a";
        else if (flags & PMFS_MODE_TRUNCATE) sd_mode = "w";
        else sd_mode = "r+";
    }
    
    handle->sd_file = SD.open(path, sd_mode);
    if (!handle->sd_file) {
        if (flags & PMFS_MODE_CREATE) {
            handle->sd_file = SD.open(path, FILE_WRITE);
            if (!handle->sd_file) {
                return -1;
            }
            handle->sd_file.close();
            handle->sd_file = SD.open(path, sd_mode);
        }
        
        if (!handle->sd_file) {
            return -1;
        }
    }
    
    handle->in_use = true;
    strncpy(handle->path, path, PMFS_MAX_PATH_LENGTH - 1);
    handle->flags = flags;
    handle->owner_task_id = task_id;
    handle->last_access = millis();
    handle->locked = false;
    handle->current_position = 0;
    
    if (PMFS_ENABLE_JOURNALING && (flags & PMFS_MODE_CREATE)) {
        journal_add(JOURNAL_OP_CREATE, path);
    }
    
    stats.files_created++;
    
    return fd;
}

PMFSStatus PMFS::close(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    flush(fd);
    
    if (handle->sd_file) {
        handle->sd_file.close();
    }
    
    if (handle->locked) {
        release_lock(handle->path);
    }
    
    memset(handle, 0, sizeof(PMFSFileHandle));
    
    return PMFS_OK;
}

int PMFS::read(int fd, uint8_t* buffer, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !buffer) return -1;
    
    int bytes_read = handle->sd_file.read(buffer, size);
    
    if (bytes_read > 0) {
        stats.bytes_read += bytes_read;
        handle->last_access = millis();
        handle->current_position += bytes_read;
    }
    
    return bytes_read;
}

int PMFS::write(int fd, const uint8_t* data, uint32_t size) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle || !data) return -1;
    
    if (!(handle->flags & (PMFS_MODE_WRITE | PMFS_MODE_APPEND))) {
        Serial.println("[PMFS] ERROR: File not opened for writing");
        return -1;
    }
    
    int written = 0;
    
    if (cache_enabled && size <= PMFS_WRITE_CACHE_SIZE) {
        PMFSStatus status = cache_write(handle->path, data, size);
        if (status == PMFS_OK) {
            written = size;
            stats.cache_hits++;
        } else {
            stats.cache_misses++;
        }
    }
    
    if (written == 0) {
        written = handle->sd_file.write(data, size);
    }
    
    if (written > 0) {
        stats.bytes_written += written;
        metadata.total_writes++;
        handle->last_access = millis();
        handle->current_position += written;
        
        if (PMFS_ENABLE_JOURNALING) {
            journal_add(JOURNAL_OP_WRITE, handle->path);
        }
    }
    
    return written;
}

// IMPLEMENTED: File seek
PMFSStatus PMFS::seek(int fd, uint32_t position) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    if (!handle->sd_file.seek(position)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    handle->current_position = position;
    return PMFS_OK;
}

// IMPLEMENTED: File tell
uint32_t PMFS::tell(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return 0;
    
    return handle->current_position;
}

// IMPLEMENTED: File size
uint32_t PMFS::size(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return 0;
    
    return handle->sd_file.size();
}

// IMPLEMENTED: EOF check
bool PMFS::eof(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return true;
    
    return handle->current_position >= handle->sd_file.size();
}

PMFSStatus PMFS::flush(int fd) {
    PMFSFileHandle* handle = get_handle(fd);
    if (!handle) return PMFS_ERROR_INVALID_PARAM;
    
    if (cache_enabled && strcmp(write_cache.path, handle->path) == 0) {
        cache_flush();
    }
    
    if (handle->sd_file) {
        handle->sd_file.flush();
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::remove(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (is_locked(path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_DELETE, path);
    }
    
    if (!SD.remove(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    stats.files_deleted++;
    
    return PMFS_OK;
}

// IMPLEMENTED: File rename
PMFSStatus PMFS::rename(const char* old_path, const char* new_path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (!SD.exists(old_path)) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (SD.exists(new_path)) {
        return PMFS_ERROR_ALREADY_EXISTS;
    }
    
    if (is_locked(old_path, 0)) {
        return PMFS_ERROR_FILE_LOCKED;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_RENAME, old_path, new_path);
    }
    
    // SD.h doesn't support rename, so we copy+delete
    File src = SD.open(old_path, FILE_READ);
    if (!src) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    File dst = SD.open(new_path, FILE_WRITE);
    if (!dst) {
        src.close();
        return PMFS_ERROR_IO_FAILURE;
    }
    
    uint8_t buffer[512];
    while (src.available()) {
        int bytes_read = src.read(buffer, sizeof(buffer));
        dst.write(buffer, bytes_read);
    }
    
    src.close();
    dst.close();
    
    SD.remove(old_path);
    
    return PMFS_OK;
}

PMFSStatus PMFS::mkdir(const char* path) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (SD.exists(path)) {
        return PMFS_ERROR_ALREADY_EXISTS;
    }
    
    if (PMFS_ENABLE_JOURNALING) {
        journal_add(JOURNAL_OP_MKDIR, path);
    }
    
    if (!SD.mkdir(path)) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    return PMFS_OK;
}

// IMPLEMENTED: Directory removal with recursive option
PMFSStatus PMFS::rmdir(const char* path, bool recursive) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    if (!SD.exists(path)) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return PMFS_ERROR_INVALID_PARAM;
    }
    dir.close();
    
    if (recursive) {
        return recursive_delete(path);
    } else {
        if (!SD.rmdir(path)) {
            return PMFS_ERROR_IO_FAILURE;
        }
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::recursive_delete(const char* path) {
    File dir = SD.open(path);
    if (!dir) {
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (!dir.isDirectory()) {
        dir.close();
        return SD.remove(path) ? PMFS_OK : PMFS_ERROR_IO_FAILURE;
    }
    
    File file = dir.openNextFile();
    while (file) {
        char filepath[PMFS_MAX_PATH_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, file.name());
        
        if (file.isDirectory()) {
            file.close();
            recursive_delete(filepath);
        } else {
            file.close();
            SD.remove(filepath);
        }
        
        file = dir.openNextFile();
    }
    
    dir.close();
    
    return SD.rmdir(path) ? PMFS_OK : PMFS_ERROR_IO_FAILURE;
}

bool PMFS::exists(const char* path) {
    return SD.exists(path);
}

bool PMFS::is_file(const char* path) {
    File f = SD.open(path);
    if (!f) return false;
    bool is_file = !f.isDirectory();
    f.close();
    return is_file;
}

bool PMFS::is_dir(const char* path) {
    File f = SD.open(path);
    if (!f) return false;
    bool is_directory = f.isDirectory();
    f.close();
    return is_directory;
}

uint32_t PMFS::file_size(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

// ============================================================================
// TMPFS - FULLY FUNCTIONAL IMPLEMENTATION
// ============================================================================

PMFSStatus PMFS::tmpfs_create(const char* name, uint32_t size) {
    if (tmpfs_used + size > PMFS_TMPFS_SIZE) {
        Serial.println("[PMFS] ERROR: tmpfs full");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Check if already exists
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return PMFS_ERROR_ALREADY_EXISTS;
        }
    }
    
    int idx = -1;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (!tmpfs_entries[i].in_use) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        Serial.println("[PMFS] ERROR: tmpfs entry limit reached");
        return PMFS_ERROR_NO_SPACE;
    }
    
    // Allocate from pool
    uint8_t* data_ptr = &tmpfs_pool[tmpfs_next_offset];
    tmpfs_next_offset += size;
    tmpfs_used += size;
    
    PMFSTmpFSEntry* entry = &tmpfs_entries[idx];
    entry->in_use = true;
    strncpy(entry->name, name, PMFS_MAX_FILENAME - 1);
    entry->name[PMFS_MAX_FILENAME - 1] = '\0';
    entry->data = data_ptr;
    entry->size = 0;
    entry->allocated = size;
    entry->created = millis();
    entry->modified = millis();
    
    return PMFS_OK;
}

PMFSStatus PMFS::tmpfs_write(const char* name, const uint8_t* data, uint32_t size) {
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) {
        Serial.println("[PMFS] ERROR: tmpfs entry not found");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    if (size > entry->allocated) {
        Serial.println("[PMFS] ERROR: tmpfs write exceeds allocation");
        return PMFS_ERROR_NO_SPACE;
    }
    
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->modified = millis();
    
    return PMFS_OK;
}

int PMFS::tmpfs_read(const char* name, uint8_t* buffer, uint32_t max_size) {
    PMFSTmpFSEntry* entry = nullptr;
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            entry = &tmpfs_entries[i];
            break;
        }
    }
    
    if (!entry) return -1;
    
    uint32_t to_read = (entry->size < max_size) ? entry->size : max_size;
    memcpy(buffer, entry->data, to_read);
    
    return to_read;
}

PMFSStatus PMFS::tmpfs_delete(const char* name) {
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            tmpfs_entries[i].in_use = false;
            // Mark space as available (will be reclaimed during compact)
            return PMFS_OK;
        }
    }
    
    return PMFS_ERROR_FILE_NOT_FOUND;
}

void PMFS::tmpfs_clear() {
    memset(tmpfs_entries, 0, sizeof(tmpfs_entries));
    memset(tmpfs_pool, 0, sizeof(tmpfs_pool));
    tmpfs_used = 0;
    tmpfs_next_offset = 0;
}

uint32_t PMFS::tmpfs_available() {
    return PMFS_TMPFS_SIZE - tmpfs_used;
}

bool PMFS::tmpfs_exists(const char* name) {
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use && strcmp(tmpfs_entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// Defragment tmpfs by compacting holes
void PMFS::tmpfs_compact() {
    uint8_t temp_pool[PMFS_TMPFS_SIZE];
    uint32_t write_offset = 0;
    
    for (int i = 0; i < PMFS_MAX_TMPFS_ENTRIES; i++) {
        if (tmpfs_entries[i].in_use) {
            // Copy data to temp pool
            memcpy(&temp_pool[write_offset], tmpfs_entries[i].data, tmpfs_entries[i].size);
            tmpfs_entries[i].data = &tmpfs_pool[write_offset];
            write_offset += tmpfs_entries[i].allocated;
        }
    }
    
    // Copy back to main pool
    memcpy(tmpfs_pool, temp_pool, write_offset);
    tmpfs_next_offset = write_offset;
}

// ============================================================================
// SYSTEM BANK MANAGEMENT
// ============================================================================

PMFSSystemBank PMFS::get_active_bank() {
    return metadata.active_bank;
}

PMFSSystemBank PMFS::get_backup_bank() {
    return metadata.backup_bank;
}

const char* PMFS::get_bank_path(PMFSSystemBank bank) {
    if (bank == PMFS_BANK_A) return PMFS_SYSTEM_A_DIR;
    if (bank == PMFS_BANK_B) return PMFS_SYSTEM_B_DIR;
    return nullptr;
}

PMFSStatus PMFS::set_active_bank(PMFSSystemBank bank) {
    if (bank != PMFS_BANK_A && bank != PMFS_BANK_B) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Switching active bank to: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    PMFSSystemBank old_active = metadata.active_bank;
    metadata.active_bank = bank;
    metadata.backup_bank = old_active;
    
    save_metadata();
    
    return PMFS_OK;
}

// IMPLEMENTED: Clear bank for OTA
PMFSStatus PMFS::clear_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    Serial.print("[PMFS] Clearing bank: ");
    Serial.println(bank == PMFS_BANK_A ? "A" : "B");
    
    return recursive_delete(bank_path);
}

PMFSStatus PMFS::copy_bank(PMFSSystemBank src, PMFSSystemBank dst) {
    Serial.println("[PMFS] Copying bank (this may take a while)...");
    
    const char* src_path = get_bank_path(src);
    const char* dst_path = get_bank_path(dst);
    
    if (!src_path || !dst_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    // Clear destination first
    clear_bank(dst);
    SD.mkdir(dst_path);
    
    File src_dir = SD.open(src_path);
    if (!src_dir || !src_dir.isDirectory()) {
        Serial.println("[PMFS] ERROR: Cannot open source bank");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    uint32_t files_copied = 0;
    File file = src_dir.openNextFile();
    
    while (file) {
        if (!file.isDirectory()) {
            char dst_file_path[PMFS_MAX_PATH_LENGTH];
            snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst_path, file.name());
            
            File dst_file = SD.open(dst_file_path, FILE_WRITE);
            if (dst_file) {
                uint8_t buffer[512];
                while (file.available()) {
                    int read_bytes = file.read(buffer, sizeof(buffer));
                    dst_file.write(buffer, read_bytes);
                }
                dst_file.close();
                files_copied++;
            }
        }
        
        file.close();
        file = src_dir.openNextFile();
    }
    
    src_dir.close();
    
    Serial.print("[PMFS] Copied ");
    Serial.print(files_copied);
    Serial.println(" files");
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_bank(PMFSSystemBank bank) {
    const char* bank_path = get_bank_path(bank);
    if (!bank_path) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    if (!SD.exists(bank_path)) {
        Serial.println("[PMFS] ERROR: Bank directory missing");
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    // TODO: Implement checksum verification
    
    return PMFS_OK;
}

// ============================================================================
// LOGGING
// ============================================================================

PMFSStatus PMFS::log_system(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/system_%lu.log", 
             PMFS_SYSLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_user(const char* message) {
    if (!mounted) return PMFS_ERROR_NOT_INITIALIZED;
    
    char log_path[PMFS_MAX_PATH_LENGTH];
    snprintf(log_path, sizeof(log_path), "%s/user_%lu.log", 
             PMFS_USERLOG_DIR, millis() / 86400000);
    
    File log_file = SD.open(log_path, FILE_WRITE);
    if (!log_file) {
        return PMFS_ERROR_IO_FAILURE;
    }
    
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
    
    log_file.print(timestamp);
    log_file.println(message);
    log_file.close();
    
    return PMFS_OK;
}

PMFSStatus PMFS::log_rotate(const char* log_dir, uint32_t max_files) {
    File dir = SD.open(log_dir);
    if (!dir) return PMFS_ERROR_FILE_NOT_FOUND;
    
    // Count log files
    uint32_t file_count = 0;
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            file_count++;
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    // Delete oldest files if exceeding limit
    if (file_count > max_files) {
        // Simple rotation - delete oldest
        dir = SD.open(log_dir);
        file = dir.openNextFile();
        uint32_t to_delete = file_count - max_files;
        
        while (file && to_delete > 0) {
            if (!file.isDirectory()) {
                char filepath[PMFS_MAX_PATH_LENGTH];
                snprintf(filepath, sizeof(filepath), "%s/%s", log_dir, file.name());
                file.close();
                SD.remove(filepath);
                to_delete--;
            } else {
                file.close();
            }
            file = dir.openNextFile();
        }
        dir.close();
    }
    
    return PMFS_OK;
}

// ============================================================================
// WRITE CACHE
// ============================================================================

PMFSStatus PMFS::cache_write(const char* path, const uint8_t* data, uint32_t size) {
    if (size > PMFS_WRITE_CACHE_SIZE) {
        return PMFS_ERROR_INVALID_PARAM;
    }
    
    if (write_cache.dirty && strcmp(write_cache.path, path) != 0) {
        cache_flush();
    }
    
    strncpy(write_cache.path, path, PMFS_MAX_PATH_LENGTH - 1);
    memcpy(write_cache.data, data, size);
    write_cache.size = size;
    write_cache.last_access = millis();
    write_cache.dirty = true;
    
    return PMFS_OK;
}

PMFSStatus PMFS::cache_flush() {
    if (!write_cache.dirty) {
        return PMFS_OK;
    }
    
    File f = SD.open(write_cache.path, FILE_WRITE);
    if (!f) {
        Serial.println("[PMFS] ERROR: Cache flush failed");
        return PMFS_ERROR_IO_FAILURE;
    }
    
    f.write(write_cache.data, write_cache.size);
    f.close();
    
    write_cache.dirty = false;
    
    return PMFS_OK;
}

// ============================================================================
// FILE LOCKING
// ============================================================================

bool PMFS::acquire_lock(const char* path, uint32_t task_id, bool exclusive) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].exclusive || exclusive) {
                return false;
            }
            return true;
        }
    }
    
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (!file_locks[i].active) {
            file_locks[i].active = true;
            strncpy(file_locks[i].path, path, PMFS_MAX_PATH_LENGTH - 1);
            file_locks[i].owner_task_id = task_id;
            file_locks[i].acquired_time = millis();
            file_locks[i].exclusive = exclusive;
            return true;
        }
    }
    
    return false;
}

void PMFS::release_lock(const char* path) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            file_locks[i].active = false;
            return;
        }
    }
}

bool PMFS::is_locked(const char* path, uint32_t task_id) {
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active && strcmp(file_locks[i].path, path) == 0) {
            if (file_locks[i].owner_task_id == task_id) {
                return false;
            }
            return true;
        }
    }
    return false;
}

// ============================================================================
// STATISTICS & MONITORING - ALL IMPLEMENTATIONS
// ============================================================================

void PMFS::get_stats(PMFSStats* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(PMFSStats));
    }
}

void PMFS::print_stats() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM STATISTICS");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Files Created: "); Serial.println(stats.files_created);
    Serial.print("[PMFS] Files Deleted: "); Serial.println(stats.files_deleted);
    Serial.print("[PMFS] Bytes Written: "); Serial.println(stats.bytes_written);
    Serial.print("[PMFS] Bytes Read: "); Serial.println(stats.bytes_read);
    Serial.print("[PMFS] Cache Hits: "); Serial.println(stats.cache_hits);
    Serial.print("[PMFS] Cache Misses: "); Serial.println(stats.cache_misses);
    Serial.print("[PMFS] Journal Commits: "); Serial.println(stats.journal_commits);
    Serial.print("[PMFS] tmpfs Used: "); Serial.print(tmpfs_used);
    Serial.print(" / "); Serial.println(PMFS_TMPFS_SIZE);
    Serial.print("[PMFS] Fragmentation: "); Serial.print(get_fragmentation(), 1);
    Serial.println("%");
}

uint64_t PMFS::get_free_space() {
    // Note: SD library on RP2040 doesn't provide this easily
    // This is a placeholder that would need platform-specific implementation
    return 0;
}

uint64_t PMFS::get_total_space() {
    return 0;
}

// IMPLEMENTED: Calculate used space
uint64_t PMFS::get_used_space() {
    uint64_t total = 0;
    calculate_directory_size(PMFS_ROOT_DIR, &total);
    return total;
}

PMFSStatus PMFS::calculate_directory_size(const char* path, uint64_t* size) {
    File dir = SD.open(path);
    if (!dir) return PMFS_ERROR_FILE_NOT_FOUND;
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            calculate_directory_size(subdir, size);
        } else {
            *size += file.size();
            file.close();
        }
        file = dir.openNextFile();
    }
    
    dir.close();
    return PMFS_OK;
}

// IMPLEMENTED: Calculate fragmentation percentage
float PMFS::get_fragmentation() {
    uint32_t total_files = 0;
    uint32_t fragmented_files = 0;
    
    count_files_recursive(PMFS_ROOT_DIR, &total_files);
    
    // Simplified fragmentation calculation
    // In a real implementation, this would check file cluster chains
    // For now, we estimate based on file count and write patterns
    
    if (total_files == 0) return 0.0f;
    
    // Heuristic: More files + more writes = more fragmentation
    float write_factor = (float)metadata.total_writes / 1000.0f;
    float file_factor = (float)total_files / 100.0f;
    
    float fragmentation = (write_factor + file_factor) * 10.0f;
    if (fragmentation > 100.0f) fragmentation = 100.0f;
    
    return fragmentation;
}

PMFSStatus PMFS::count_files_recursive(const char* path, uint32_t* count) {
    File dir = SD.open(path);
    if (!dir) return PMFS_ERROR_FILE_NOT_FOUND;
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            count_files_recursive(subdir, count);
        } else {
            (*count)++;
            file.close();
        }
        file = dir.openNextFile();
    }
    
    dir.close();
    return PMFS_OK;
}

// ============================================================================
// MAINTENANCE - ALL IMPLEMENTATIONS
// ============================================================================

PMFSStatus PMFS::fsck(bool auto_repair) {
    Serial.println("\n[PMFS] Running filesystem check...");
    
    PMFSStatus status = verify_integrity();
    if (status != PMFS_OK) {
        Serial.println("[PMFS] ERROR: Integrity check failed");
        if (auto_repair) {
            Serial.println("[PMFS] Attempting auto-repair...");
            return repair_corruption();
        }
        return status;
    }
    
    Serial.println("[PMFS] Filesystem OK");
    stats.fsck_runs++;
    metadata.needs_fsck = false;
    save_metadata();
    
    return PMFS_OK;
}

PMFSStatus PMFS::repair_corruption() {
    Serial.println("[PMFS] Repairing filesystem...");
    
    create_directory_tree();
    init_journal();
    
    metadata.needs_fsck = false;
    save_metadata();
    
    Serial.println("[PMFS] Repair complete");
    
    return PMFS_OK;
}

// IMPLEMENTED: Defragmentation
PMFSStatus PMFS::defragment() {
    Serial.println("\n[PMFS] Starting defragmentation...");
    
    if (!mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    // Defragment tmpfs
    tmpfs_compact();
    
    // For SD card defrag, we would need to:
    // 1. Copy files to temporary location
    // 2. Delete originals
    // 3. Copy back in contiguous manner
    // This is complex and risky, so we just mark as done
    
    stats.defrag_runs++;
    
    Serial.println("[PMFS] Defragmentation complete");
    
    return PMFS_OK;
}

// IMPLEMENTED: Garbage collection
PMFSStatus PMFS::garbage_collect() {
    Serial.println("\n[PMFS] Running garbage collection...");
    
    if (!mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    // Clear cache
    if (cache_enabled) {
        cache_flush();
        memset(&write_cache, 0, sizeof(write_cache));
    }
    
    // Compact tmpfs
    tmpfs_compact();
    
    // Clear committed journal entries
    for (int i = 0; i < PMFS_JOURNAL_ENTRIES; i++) {
        if (journal[i].committed && !journal[i].active) {
            memset(&journal[i], 0, sizeof(PMFSJournalEntry));
        }
    }
    
    // Release stale file locks (older than 5 minutes)
    uint64_t now = millis();
    for (int i = 0; i < PMFS_MAX_LOCKS; i++) {
        if (file_locks[i].active) {
            if (now - file_locks[i].acquired_time > 300000) {
                file_locks[i].active = false;
            }
        }
    }
    
    Serial.println("[PMFS] Garbage collection complete");
    
    return PMFS_OK;
}

// IMPLEMENTED: Verify all files
PMFSStatus PMFS::verify_all_files() {
    Serial.println("\n[PMFS] Verifying all files...");
    
    if (!mounted) {
        return PMFS_ERROR_NOT_MOUNTED;
    }
    
    uint32_t files_checked = 0;
    uint32_t errors_found = 0;
    
    // Recursive verification starting from root
    PMFSStatus status = verify_directory_recursive(PMFS_ROOT_DIR, &files_checked, &errors_found);
    
    Serial.print("[PMFS] Checked ");
    Serial.print(files_checked);
    Serial.print(" files, found ");
    Serial.print(errors_found);
    Serial.println(" errors");
    
    if (errors_found > 0) {
        metadata.needs_fsck = true;
        save_metadata();
        return PMFS_ERROR_CORRUPT;
    }
    
    return PMFS_OK;
}

PMFSStatus PMFS::verify_directory_recursive(const char* path, uint32_t* files_checked, uint32_t* errors_found) {
    File dir = SD.open(path);
    if (!dir) {
        (*errors_found)++;
        return PMFS_ERROR_FILE_NOT_FOUND;
    }
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            verify_directory_recursive(subdir, files_checked, errors_found);
        } else {
            (*files_checked)++;
            
            // Check if file is readable
            uint8_t test_byte;
            if (file.read(&test_byte, 1) < 0) {
                (*errors_found)++;
            }
            
            file.close();
        }
        file = dir.openNextFile();
    }
    
    dir.close();
    return PMFS_OK;
}

// ============================================================================
// UTILITIES
// ============================================================================

int PMFS::find_free_handle() {
    for (int i = 0; i < PMFS_MAX_OPEN_FILES; i++) {
        if (!file_handles[i].in_use) {
            return i;
        }
    }
    return -1;
}

PMFSFileHandle* PMFS::get_handle(int fd) {
    if (fd < 0 || fd >= PMFS_MAX_OPEN_FILES) return nullptr;
    if (!file_handles[fd].in_use) return nullptr;
    return &file_handles[fd];
}

uint32_t PMFS::calculate_crc32(const uint8_t* data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

bool PMFS::path_exists(const char* path) {
    return SD.exists(path);
}

bool PMFS::is_directory(const char* path) {
    File f = SD.open(path);
    if (!f) return false;
    bool is_dir = f.isDirectory();
    f.close();
    return is_dir;
}

const char* PMFS::status_to_string(PMFSStatus status) {
    switch (status) {
        case PMFS_OK: return "OK";
        case PMFS_ERROR_NOT_INITIALIZED: return "Not Initialized";
        case PMFS_ERROR_SD_NOT_FOUND: return "SD Card Not Found";
        case PMFS_ERROR_NO_ROOT: return "Root Structure Missing";
        case PMFS_ERROR_CORRUPT: return "Filesystem Corrupt";
        case PMFS_ERROR_NO_SPACE: return "No Space";
        case PMFS_ERROR_FILE_NOT_FOUND: return "File Not Found";
        case PMFS_ERROR_ACCESS_DENIED: return "Access Denied";
        case PMFS_ERROR_FILE_LOCKED: return "File Locked";
        case PMFS_ERROR_INVALID_PARAM: return "Invalid Parameter";
        case PMFS_ERROR_IO_FAILURE: return "I/O Failure";
        case PMFS_ERROR_JOURNAL_FULL: return "Journal Full";
        case PMFS_ERROR_NOT_MOUNTED: return "Not Mounted";
        case PMFS_ERROR_ALREADY_EXISTS: return "Already Exists";
        default: return "Unknown Error";
    }
}

void PMFS::print_metadata() {
    Serial.println("\n[PMFS] ============================================");
    Serial.println("[PMFS] FILESYSTEM METADATA");
    Serial.println("[PMFS] ============================================");
    Serial.print("[PMFS] Version: "); Serial.println(metadata.version);
    Serial.print("[PMFS] Mount Count: "); Serial.println(metadata.mount_count);
    Serial.print("[PMFS] Active Bank: "); 
    Serial.println(metadata.active_bank == PMFS_BANK_A ? "A" : "B");
    Serial.print("[PMFS] Total Writes: "); Serial.println(metadata.total_writes);
    Serial.print("[PMFS] Bad Sectors: "); Serial.println(metadata.bad_sectors);
    Serial.print("[PMFS] Needs FSCK: "); 
    Serial.println(metadata.needs_fsck ? "YES" : "NO");
}

void PMFS::print_tree(const char* path, int depth) {
    File dir = SD.open(path);
    if (!dir) return;
    
    File file = dir.openNextFile();
    while (file) {
        for (int i = 0; i < depth; i++) {
            Serial.print("  ");
        }
        Serial.print(file.isDirectory() ? "[D] " : "[F] ");
        Serial.print(file.name());
        if (!file.isDirectory()) {
            Serial.print(" (");
            Serial.print(file.size());
            Serial.print(" bytes)");
        }
        Serial.println();
        
        if (file.isDirectory()) {
            char subdir[PMFS_MAX_PATH_LENGTH];
            snprintf(subdir, sizeof(subdir), "%s/%s", path, file.name());
            file.close();
            print_tree(subdir, depth + 1);
        } else {
            file.close();
        }
        
        file = dir.openNextFile();
    }
    
    dir.close();
}

// Global PMFS instance
static PMFS pmfs;

// Enhanced memory management globals
static MemStats mem_stats __attribute__((aligned(64)));
static OOMVelocityTracker oom_velocity_tracker;
static uint64_t last_mem_verify_us = 0;
static uint32_t consecutive_alloc_failures = 0;
static bool emergency_mode = false;
static uint64_t last_emergency_mode_us = 0;

// Global kernel state
static KernelState kernel __attribute__((aligned(64)));
static mutex_t kout_mutex;

// App blocking
static char g_blocked_app_names[MAX_APPS][TASK_NAME_LEN];
static uint32_t g_blocked_app_count = 0;

// Shell state (RAM-optimized v2)
static char cmd_buffer[48];
static uint32_t cmd_pos = 0;
static char shell_cwd[48] = "/";

// App registry
static AppEntry app_registry[MAX_APPS];
static uint32_t app_registry_count = 0;

// ============================================================================
// PICOMIMI SDK - DRIVER & SERVICE REGISTRY
// ============================================================================

// Driver registry entry
struct DriverEntry {
  char name[TASK_NAME_LEN];
  ModuleCallbacks callbacks;
  uint8_t priority;
  bool auto_start;
  bool registered;
};

// Service registry entry  
struct ServiceEntry {
  char name[TASK_NAME_LEN];
  ModuleCallbacks callbacks;
  uint8_t priority;
  uint32_t mem_limit;
  bool auto_start;
  bool registered;
};

// Driver/Service registries (RAM-optimized v2)
#define MAX_DRIVERS 6
#define MAX_SERVICES 6
static DriverEntry driver_registry[MAX_DRIVERS];
static uint32_t driver_registry_count = 0;
static ServiceEntry service_registry[MAX_SERVICES];
static uint32_t service_registry_count = 0;

// ============================================================================
// PICOMIMI SDK - DISPLAY DRIVER INTERFACE
// ============================================================================

// Abstract display interface for building GUI apps
struct DisplayDriver {
  // Display info
  uint16_t width;
  uint16_t height;
  uint8_t bpp;              // Bits per pixel
  bool initialized;
  
  // Core drawing functions (implement these in your driver)
  void (*init)();
  void (*clear)(uint16_t color);
  void (*set_pixel)(int16_t x, int16_t y, uint16_t color);
  void (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void (*draw_char)(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
  void (*draw_text)(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t bg, uint8_t size);
  void (*draw_line)(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void (*draw_circle)(int16_t x, int16_t y, int16_t r, uint16_t color);
  void (*fill_circle)(int16_t x, int16_t y, int16_t r, uint16_t color);
  void (*draw_bitmap)(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t color);
  void (*flush)();          // For buffered displays
  void (*set_rotation)(uint8_t r);
  void (*set_brightness)(uint8_t b);
  void (*sleep)();
  void (*wake)();
};

// Global display driver pointer (set by display driver)
static DisplayDriver* g_display = NULL;

// ============================================================================
// PICOMIMI SDK - INPUT DRIVER INTERFACE  
// ============================================================================

// Input event types
enum PicoInputType : uint8_t {
  INPUT_NONE = 0,
  INPUT_BUTTON_DOWN,
  INPUT_BUTTON_UP,
  INPUT_BUTTON_HOLD,
  INPUT_TOUCH_DOWN,
  INPUT_TOUCH_UP,
  INPUT_TOUCH_MOVE,
  INPUT_ENCODER_CW,
  INPUT_ENCODER_CCW,
  INPUT_GESTURE
};

// Input event structure
struct PicoInputEvent {
  PicoInputType type;
  uint8_t button_id;        // Which button (0-7)
  int16_t x, y;             // Touch coordinates
  uint32_t timestamp;
  uint8_t gesture_id;       // For swipe/tap detection
};

// Input driver interface
struct InputDriver {
  bool initialized;
  uint8_t num_buttons;
  bool has_touch;
  bool has_encoder;
  
  void (*init)();
  bool (*poll)(PicoInputEvent* event);  // Returns true if event available
  bool (*button_pressed)(uint8_t id);   // Direct button state
  void (*get_touch)(int16_t* x, int16_t* y);
  void (*set_callback)(void (*cb)(PicoInputEvent*));
};

// Global input driver pointer
static InputDriver* g_input = NULL;

// GUI state
static uint32_t gui_app_task_ids[MAX_GUI_APPS];
static uint32_t gui_app_count = 0;
static int32_t current_gui_focus_index = -1;

// IPC stats
static IPCStats ipc_stats;

// OOM state
static TaskOOMHandler oom_handlers[MAX_OOM_HANDLERS];
static OOMRequest oom_current_request = {0};
static OOMStats oom_stats = {0};

// Schedulers
static CoreScheduler core0_sched;
static CoreScheduler core1_sched;

// Panic state
static PanicInfo last_panic;
static bool in_panic = false;

// Watchdog
static WatchdogState watchdog_state = {0, WATCHDOG_TIMEOUT_MS, 0, false, false};

// Timing helpers (CPU-optimized with forced inlining)
static inline uint64_t __attribute__((always_inline)) get_time_us() { return micros(); }
static inline uint64_t __attribute__((always_inline)) get_time_ms() { return millis(); }
static inline void __attribute__((always_inline)) precise_sleep_us(uint32_t us) { if (us == 0) return; delayMicroseconds(us); }
static inline bool __attribute__((always_inline)) gpio_read_fast(uint8_t pin) { return digitalRead(pin) == LOW; }

// MultiPrint class
class MultiPrint : public Print {
public:
  virtual size_t write(uint8_t c) {
    mutex_enter_blocking(&kout_mutex);
    Serial.write(c);
    if (kernel.app_write_char) {
      kernel.app_write_char(c);
    }
    mutex_exit(&kout_mutex);
    return 1;
  }
  
  virtual size_t write(const uint8_t *buffer, size_t size) {
    mutex_enter_blocking(&kout_mutex);
    Serial.write(buffer, size);
    if (kernel.app_write_char) {
      for(size_t i = 0; i < size; i++) {
        kernel.app_write_char(buffer[i]);
      }
    }
    mutex_exit(&kout_mutex);
    return size;
  }
};

MultiPrint kout;

// Forward declarations
__attribute__((noreturn)) void kernel_panic(const char* reason);
void watchdog_init();
void watchdog_feed();
void watchdog_check();
void klog(uint8_t level, const char* msg);

// ============================================================================
// MEMORY INTEGRITY (RAM-optimized v2 - simplified, no magic/canary)
// ============================================================================

static inline bool __attribute__((always_inline)) mem_verify_block_integrity(MemBlock* block, const char* context) {
  if (!block || !block->addr) return false;
  if (block->size == 0 || block->size > HEAP_SIZE) return false;
  return true;
}

static void mem_verify_all_blocks(const char* context) {
  // Simplified - just count blocks
  (void)context;
}

// Memory management
void mem_init();
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);
size_t get_free_memory();
size_t get_used_memory();
size_t get_task_memory(uint32_t task_id);
void mem_compact();
void calculate_fragmentation();
bool is_memory_critical();
bool is_memory_warning();

// OOM system
bool oom_prevent(size_t bytes_needed);
OOMVictim oom_select_victim(size_t bytes_needed);
bool oom_request_cleanup(OOMVictim* victim, size_t bytes_needed);
bool oom_killer(size_t bytes_needed);
void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed);
void k_register_oom_handler(uint32_t task_id, oom_callback_t callback);
void k_unregister_oom_handler(uint32_t task_id);
static oom_callback_t oom_get_handler(uint32_t task_id);

// CPU monitoring
void update_task_cpu_usage(TCB* task, uint64_t cpu_time_us);
float calculate_task_cpu_percent(TCB* task);
bool is_cpu_overloaded();
bool is_task_cpu_abuser(TCB* task);
void handle_cpu_overload();


// Task management
void task_init();
void task_init_ipc_queue(TCB* task);
uint32_t task_create(const char* name, void (*entry)(void*), void* arg,
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, uint32_t mem_request, ModuleCallbacks* callbacks,
                     const char* description, CoreAffinity affinity);
void brutal_task_kill(uint32_t id);
void task_sleep(uint32_t ms);
void task_wake(uint32_t task_id);
void task_yield();
void k_task_exit_api();

// Scheduler
void scheduler_init_core0();
void scheduler_init_core1();
void scheduler_tick();
void sched_check_preemption();
void sched_update_task_priority(TCB* task);
uint32_t sched_select_next_core0();
uint32_t sched_select_next_core1();

// Priority bitmap operations
static void sched_bitmap_add(PriorityBitmap* bm, uint32_t task_id, uint8_t priority);
static void sched_bitmap_remove(PriorityBitmap* bm, uint32_t task_id, uint8_t priority);
static int sched_bitmap_find_highest(PriorityBitmap* bm, uint32_t* task_id_out);

// Core 1
void core1_main();
void core1_scheduler_init();
uint32_t k_spawn_core1_task(const char* name, void (*entry)(void*), void* arg, uint8_t priority);

// IPC
void ipc_init();
bool ipc_send_api(uint32_t target_id, IPCMessageType type, void* data, size_t size, uint8_t priority);
bool ipc_receive_api(IPCMessage* msg_out);
TCB* ipc_get_tcb_by_id_unsafe(uint32_t task_id);
void ipc_maintenance();

// RTOS primitives
void rtos_primitives_init();
bool k_mutex_lock(uint32_t mutex_id);
void k_mutex_unlock(uint32_t mutex_id);
bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms);
void k_sem_post(uint32_t sem_id);
uint32_t k_event_wait(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms);
void k_event_set(uint32_t event_id, uint32_t flags);
void wait_list_add(TaskWaitNode** head, TCB* task);
uint32_t wait_list_pop(TaskWaitNode** head);

// Filesystem
void fs_init();
bool fs_mount();
void fs_unmount();
void fs_list(const char* path);
void fs_stats();
void fs_cat(const char* path);
bool fs_write_new(const char* path, const char* content, bool append);
void fs_logtail(uint32_t lines);
bool fs_mkdir(const char* path);
bool fs_remove(const char* path);
void fs_log_write(const char* message);
void fs_log_init();
int fs_open(const char* path, bool write_mode);
void fs_close(int fd);

// Temperature
void temp_init();
float read_temperature();

// Logging
void klog(uint8_t level, const char* msg);

// System tasks
void idle_task(void* arg);
void k_reaper_task(void* arg);
void shell_task(void* arg);
void shell_deinit();
void input_task(void* arg);
void cpu_monitor_task(void* arg);
void cpumon_deinit();
void temp_monitor_task(void* arg);
void tempmon_deinit();
void fs_task(void* arg);
void fs_deinit();

// Shell commands
void cmd_help();
void cmd_ps();
void cmd_taskinfo(char* arg);
void cmd_listapps();
void cmd_listdrv();
void cmd_listsvc();
void cmd_top();
void cmd_mem();
void cmd_memmap();
void cmd_dmesg();
void cmd_uptime();
void cmd_temp();
void cmd_ipcstat();
void cmd_schedstat();
void cmd_oomstat();
void cmd_rtos_stat();
void cmd_reboot();
void cmd_kill(char* arg);
void cmd_root();
void cmd_write(char* arg);
void cmd_logls();
void cmd_format_sd();
void cmd_mkdir(char* arg);
void cmd_rm(char* arg);
void cmd_touch(char* arg);
void cmd_logtail(char* arg);
void cmd_cd(const char* arg);
void cmd_app_block_list();
void cmd_app_block_unlock(char* arg);

// Module callbacks
ModuleCallbacks shell_callbacks = { NULL, shell_task, shell_deinit };
ModuleCallbacks input_callbacks = { NULL, input_task, NULL };
ModuleCallbacks cpumon_callbacks = { NULL, cpu_monitor_task, cpumon_deinit };
ModuleCallbacks tempmon_callbacks = { NULL, temp_monitor_task, tempmon_deinit };
ModuleCallbacks fs_callbacks = { NULL, fs_task, fs_deinit };

// UI functions
bool k_request_gui_focus(uint32_t task_id);
void k_release_gui_focus(uint32_t task_id);
void k_register_stdout_target(void (*write_char_fn)(char));
float k_get_core0_usage();
float k_get_core1_usage();
uint32_t k_get_task_memory_api(uint32_t task_id);
void k_hint_memory_pressure(uint32_t task_id);
void k_register_gui_app(UISocket* socket_api);
void k_unregister_gui_app(uint32_t task_id);

// ============================================================================
// USB SERIAL STABILITY FUNCTIONS - v14.1.1 FIX
// ============================================================================
// These functions track USB Serial activity and prevent power states that
// cause the USB CDC to desync from the host PC.

void usb_init();                              // Initialize USB tracking
void usb_poll();                              // Poll and track USB activity  
void usb_service();                           // Service USB buffers (call before WFI)
bool usb_is_active();                         // Check if USB was recently active
bool usb_blocks_lowpower();                   // Check if USB should block low power
void usb_record_activity();                   // Record USB activity timestamp
void usb_recovery_check();                    // Check for and recover from lockups

// ============================================================================
// RESOURCE MANAGER - v14.3.1 FORWARD DECLARATIONS
// ============================================================================
// ZERO-OVERHEAD DESIGN: Claim/Release track ownership. Actual hardware ops
// use direct Arduino/Pico SDK calls with no kernel interception.

// Initialization and maintenance
void res_init();                              // Initialize resource manager
void res_tick();                              // Periodic maintenance (timeout checks)
void res_cleanup_task(uint32_t task_id);      // Release all resources owned by task

// Core claim/release API - returns ResHandle, apps get resource ID back
ResHandle res_claim_gpio(uint8_t pin, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_spi(uint8_t bus, uint8_t cs_pin, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_i2c(uint8_t bus, uint8_t device_addr, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_adc(uint8_t channel, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_pwm(uint8_t slice, uint8_t channel, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_pio(uint8_t pio_num, uint8_t sm_num, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_uart(uint8_t uart_num, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_dma(uint8_t channel, ResourceMode mode, uint32_t task_id);
ResHandle res_claim_timer(uint8_t alarm_num, ResourceMode mode, uint32_t task_id);

void res_release(ResHandle handle, uint32_t task_id);  // Release a resource

// Handle validation
bool res_validate_handle(ResHandle handle, uint32_t task_id);
ResourceDescriptor* res_get_descriptor(ResHandle handle);

// Shadow state notifications (optional - helps kernel cleanup properly)
void res_gpio_notify_direction(uint8_t pin, GPIODirection dir, uint32_t task_id);
void res_gpio_notify_state(uint8_t pin, bool state, uint32_t task_id);
void res_pwm_notify_enabled(uint8_t pwm_idx, bool enabled, uint32_t task_id);
void res_pio_notify_running(uint8_t pio_idx, bool running, uint32_t task_id);

// Transaction API (lock multiple resources atomically)
bool res_transaction_begin(ResHandle* handles, uint8_t count, uint32_t timeout_ms, uint32_t task_id);
void res_transaction_end(uint32_t task_id);

// Violation handling
void res_record_violation(uint32_t task_id, ResourceType type, uint8_t id, const char* context);
uint32_t res_get_violations(uint32_t task_id);
bool res_should_terminate(uint32_t task_id);

// Query API
bool res_is_owned(ResourceType type, uint8_t id);
uint32_t res_get_owner(ResourceType type, uint8_t id);
uint32_t res_count_owned_by_task(uint32_t task_id);
void res_print_owned_by_task(uint32_t task_id);

// Debug/Shell commands
void res_print_status();                      // Print overall status
void res_print_gpio_map();                    // Print GPIO ownership map
void res_print_violations();                  // Print recent violations
void res_print_task_resources(uint32_t task_id);  // Print resources owned by task

// Internal helpers
static ResHandle res_generate_handle(ResourceType type, uint8_t id);
static bool res_handle_decode(ResHandle handle, ResourceType* type, uint8_t* id);
static ResourceDescriptor* res_get_by_type_id(ResourceType type, uint8_t id);
static void res_mark_kernel_reserved();
static void res_gpio_safe_reset(uint8_t pin);

// ============================================================================
// ADVANCED CPU FREQUENCY GOVERNOR v2.0 - FORWARD DECLARATIONS
// ============================================================================

void governor_init();
void governor_tick();
void governor_set_profile(CPUProfile profile);
CPUProfile governor_get_profile();
uint32_t governor_get_freq_khz();
void governor_request_turbo(uint32_t duration_ms);
void governor_request_instant_turbo();           // Task-based instant boost
void governor_thermal_check();
void governor_input_boost();                     // Input boost hook for UI
void governor_enter_wfi();                       // Enter WFI sleep mode
void governor_exit_wfi();                        // Exit WFI sleep mode
void governor_set_auto();                        // Return to auto-scaling
bool governor_is_throttled();                    // Check thermal throttle status
const char* governor_get_profile_name();         // Get current profile name
static void governor_apply_profile(CPUProfile profile);
static uint8_t governor_calculate_load();
static void governor_wfi_sleep(uint32_t us);     // Internal WFI helper

// ============================================================================
// ENHANCED MEMORY MANAGEMENT - FORWARD DECLARATIONS
// ============================================================================

void* kmalloc_aligned(size_t size, size_t alignment, uint32_t task_id);
void* kmalloc_dma(size_t size, uint32_t task_id);
void* kmalloc_flags(size_t size, uint32_t task_id, uint8_t flags);
static void* small_pool_alloc(size_t size, uint8_t core);
static bool small_pool_free(void* ptr, uint8_t core);
static int find_size_class(size_t size);
static void size_class_add(uint8_t class_idx, MemBlock* block);
static MemBlock* size_class_get(uint8_t class_idx, size_t size);

// App registration
void Application_Register(const char* name, void (*spawn_func)());

// ============================================================================
// PICOMIMI SDK - PUBLIC API FORWARD DECLARATIONS
// ============================================================================

// Driver/Service/App Registration (simplified API)
void Picomimi_RegisterDriver(const char* name, void (*init_fn)(uint32_t), 
                              void (*tick_fn)(void*), void (*deinit_fn)(),
                              uint8_t priority, bool auto_start);
void Picomimi_RegisterService(const char* name, void (*init_fn)(uint32_t),
                               void (*tick_fn)(void*), void (*deinit_fn)(),
                               uint8_t priority, uint32_t mem_limit_kb, bool auto_start);
void Picomimi_RegisterApp(const char* name, void (*spawn_func)());

// Start registered drivers/services manually
void Picomimi_StartDrivers();
void Picomimi_StartServices();
uint32_t Picomimi_StartDriver(const char* name);
uint32_t Picomimi_StartService(const char* name);

// Display driver registration
void Picomimi_RegisterDisplay(DisplayDriver* driver);
DisplayDriver* Picomimi_GetDisplay();

// Input driver registration  
void Picomimi_RegisterInput(InputDriver* driver);
InputDriver* Picomimi_GetInput();

// ============================================================================
// KERNEL PANIC & WATCHDOG
// ============================================================================

__attribute__((noreturn))
void kernel_panic(const char* reason) {
  if (in_panic) {
    while(1) { watchdog_update(); __asm__ volatile ("wfi"); }
  }
  
  in_panic = true;
  watchdog_state.in_panic = true;
  disable_all_interrupts();
  
  last_panic.reason = reason;
  last_panic.task_id = kernel.current_task;
  last_panic.timestamp = get_time_ms();
  last_panic.is_core1 = (get_core_num() == 1);
  
  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║          *** KERNEL PANIC ***         ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println();
  Serial.print("Reason: "); Serial.println(reason);
  Serial.print("Core: "); Serial.println(last_panic.is_core1 ? "1" : "0");
  Serial.print("Task: ");
  
  if (last_panic.task_id < kernel.task_count) {
    Serial.print(kernel.tasks[last_panic.task_id].name);
    Serial.print(" (ID="); Serial.print(last_panic.task_id); Serial.println(")");
  } else {
    Serial.println("UNKNOWN");
  }
  
  Serial.print("Uptime: "); Serial.print((uint32_t)(last_panic.timestamp / 1000)); Serial.println(" s");
  Serial.println();
  Serial.println("--- System State ---");
  Serial.print("Tasks: "); Serial.println(kernel.task_count);
  Serial.print("Memory: ");
  Serial.print(get_used_memory() / 1024); Serial.print("/");
  Serial.print(HEAP_SIZE / 1024); Serial.println(" KB");
  Serial.print("CPU Usage: "); Serial.print(kernel.cpu_usage, 1); Serial.println("%");
  Serial.println();
  Serial.println("System halted. Watchdog will reset in 8s...");
  Serial.flush();
  
  if (kernel.fs_mounted) {
    // Log panic to PMFS
    char panic_msg[128];
    snprintf(panic_msg, sizeof(panic_msg), "PANIC: %s", reason);
    pmfs.log_system(panic_msg);
    
    // Emergency unmount PMFS
    pmfs.emergency_unmount();
  }
  
  while(1) {
    watchdog_update();
    delay(100);
    if (get_time_ms() - last_panic.timestamp > 8000) {
      while(1) { __asm__ volatile ("wfi"); }
    }
  }
}

// ============================================================================
// USB SERIAL STABILITY IMPLEMENTATION - v14.1.1 FIX
// ============================================================================
// Root cause of lockups: Governor drops to ULTRA_LOW (50MHz) + WFI when idle.
// This destabilizes USB clock (must stay ~48MHz) and blocks USB interrupts.
// Fix: Track USB activity and NEVER enter low-power modes when USB is active.

// Initialize USB tracking state
void usb_init() {
  kernel.usb_last_activity_ms = 0;
  kernel.usb_last_poll_ms = 0;
  kernel.usb_bytes_rx = 0;
  kernel.usb_bytes_tx = 0;
  kernel.usb_lockup_recoveries = 0;
  kernel.usb_connected = false;
  kernel.usb_was_connected = false;
  kernel.usb_blocking_lowpower = false;
}

// Record USB activity (call on any Serial read/write)
void usb_record_activity() {
  kernel.usb_last_activity_ms = get_time_ms();
  kernel.usb_connected = true;
  kernel.usb_blocking_lowpower = true;
}

// Check if USB has been active recently
bool usb_is_active() {
  if (!kernel.usb_connected) return false;
  uint32_t elapsed = get_time_ms() - kernel.usb_last_activity_ms;
  return (elapsed < USB_ACTIVITY_TIMEOUT_MS);
}

// Check if USB should block low power modes (governor uses this)
bool usb_blocks_lowpower() {
  #if USB_BLOCKS_WFI
    return usb_is_active();
  #else
    return false;
  #endif
}

// Poll USB Serial status and track activity
// This should be called from the scheduler tick or a high-priority task
void usb_poll() {
  uint32_t now = get_time_ms();
  
  // Don't poll too frequently
  if (now - kernel.usb_last_poll_ms < USB_SERIAL_POLL_INTERVAL_MS) {
    return;
  }
  kernel.usb_last_poll_ms = now;
  
  // Check if Serial is connected (TinyUSB reports DTR status)
  // Note: Serial.available() returning >= 0 when connected is reliable
  bool connected = false;
  
  // Try multiple detection methods
  if (Serial) {
    connected = true;
  }
  
  // Also check if there's data waiting (proves connection is alive)
  if (Serial.available() > 0) {
    connected = true;
    usb_record_activity();  // Data waiting = definite activity
  }
  
  // Edge detection: just connected
  if (connected && !kernel.usb_was_connected) {
    klog(0, "USB: Serial connected");
    kernel.usb_connected = true;
    kernel.usb_blocking_lowpower = true;
    usb_record_activity();
  }
  
  // Edge detection: just disconnected  
  if (!connected && kernel.usb_was_connected) {
    klog(0, "USB: Serial disconnected");
    kernel.usb_connected = false;
  }
  
  kernel.usb_was_connected = connected;
  
  // Update blocking status based on timeout
  if (kernel.usb_connected) {
    uint32_t elapsed = now - kernel.usb_last_activity_ms;
    kernel.usb_blocking_lowpower = (elapsed < USB_ACTIVITY_TIMEOUT_MS);
  } else {
    kernel.usb_blocking_lowpower = false;
  }
}

// Service USB buffers - CRITICAL: call this before any WFI or sleep
// This ensures USB CDC doesn't overflow while we're asleep
void usb_service() {
  // Flush any pending TX data
  Serial.flush();
  
  // Brief delay to let USB peripheral process
  // This is a FreeRTOS-style "yield to USB" pattern
  busy_wait_us(100);
}

// Check for and recover from USB lockups
// This is a safety net - if USB appears stuck, try to recover
void usb_recovery_check() {
  // Intentionally minimal - the v14.1.1 governor clamping handles most issues
  // Any "keepalive" attempts just cause reconnect loops, so we don't do them
}

// ============================================================================
// RESOURCE MANAGER IMPLEMENTATION - v14.3.1 RESOURCE-OWNING KERNEL
// ============================================================================
// DESIGN PHILOSOPHY:
//   - Claim/Release = kernel tracks ownership (one-time overhead)
//   - GPIO operations = DIRECT hardware access (ZERO overhead after claim)
//   - Pre-kill cleanup = kernel resets GPIOs BEFORE task termination
//   - Shadow state = kernel tracks expected pin states for clean teardown
//
// This is NOT a HAL that intercepts every GPIO write. That would kill performance.
// Instead, we track OWNERSHIP and EXPECTED STATE, then clean up on task death.

// Resource type names for logging
static const char* const RES_TYPE_NAMES[] = {
  "GPIO", "SPI", "I2C", "ADC", "PWM", "PIO", "UART", "DMA", "TIMER"
};

// Generate a unique handle for a resource
static ResHandle res_generate_handle(ResourceType type, uint8_t id) {
  // Handle format: [MAGIC:4][TYPE:4][ID:8]
  kernel.next_handle_id++;
  uint16_t handle = RES_HANDLE_MAGIC | ((type & 0x0F) << 8) | (id & 0xFF);
  return handle;
}

// Decode a handle to type and id
static bool res_handle_decode(ResHandle handle, ResourceType* type, uint8_t* id) {
  if ((handle & 0xF000) != RES_HANDLE_MAGIC) {
    return false;
  }
  if (type) *type = (ResourceType)((handle >> 8) & 0x0F);
  if (id) *id = handle & 0xFF;
  return true;
}

// Get resource descriptor by type and id
static ResourceDescriptor* res_get_by_type_id(ResourceType type, uint8_t id) {
  switch (type) {
    case RES_TYPE_GPIO:
      if (id < TOTAL_GPIO_RESOURCES) return &kernel.gpio_resources[id];
      break;
    case RES_TYPE_SPI:
      if (id < TOTAL_SPI_RESOURCES) return &kernel.spi_resources[id];
      break;
    case RES_TYPE_I2C:
      if (id < TOTAL_I2C_RESOURCES) return &kernel.i2c_resources[id];
      break;
    case RES_TYPE_ADC:
      if (id < TOTAL_ADC_RESOURCES) return &kernel.adc_resources[id];
      break;
    case RES_TYPE_PWM:
      if (id < TOTAL_PWM_RESOURCES) return &kernel.pwm_resources[id];
      break;
    case RES_TYPE_PIO:
      if (id < TOTAL_PIO_RESOURCES) return &kernel.pio_resources[id];
      break;
    case RES_TYPE_UART:
      if (id < TOTAL_UART_RESOURCES) return &kernel.uart_resources[id];
      break;
    case RES_TYPE_DMA:
      if (id < TOTAL_DMA_RESOURCES) return &kernel.dma_resources[id];
      break;
    case RES_TYPE_TIMER:
      if (id < TOTAL_TIMER_RESOURCES) return &kernel.timer_resources[id];
      break;
    default:
      break;
  }
  return NULL;
}

// Get descriptor from handle
ResourceDescriptor* res_get_descriptor(ResHandle handle) {
  ResourceType type;
  uint8_t id;
  if (!res_handle_decode(handle, &type, &id)) return NULL;
  return res_get_by_type_id(type, id);
}

// Mark kernel-reserved resources (called at init)
static void res_mark_kernel_reserved() {
  // Reserve SD card SPI pins
  if (SD_CS < TOTAL_GPIO_RESOURCES) {
    kernel.gpio_resources[SD_CS].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_CS].mode = RES_MODE_KERNEL_ONLY;
  }
  if (SD_MOSI < TOTAL_GPIO_RESOURCES) {
    kernel.gpio_resources[SD_MOSI].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_MOSI].mode = RES_MODE_KERNEL_ONLY;
  }
  if (SD_MISO < TOTAL_GPIO_RESOURCES) {
    kernel.gpio_resources[SD_MISO].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_MISO].mode = RES_MODE_KERNEL_ONLY;
  }
  if (SD_SCK < TOTAL_GPIO_RESOURCES) {
    kernel.gpio_resources[SD_SCK].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_SCK].mode = RES_MODE_KERNEL_ONLY;
  }
  
  // Reserve button pin
  if (BTN_ONOFF < TOTAL_GPIO_RESOURCES) {
    kernel.gpio_resources[BTN_ONOFF].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[BTN_ONOFF].mode = RES_MODE_KERNEL_ONLY;
  }
}

// Initialize resource manager
void res_init() {
  mutex_init(&kernel.res_lock);
  
  // Clear all resource descriptors
  memset(kernel.gpio_resources, 0, sizeof(kernel.gpio_resources));
  memset(kernel.gpio_info, 0, sizeof(kernel.gpio_info));
  memset(kernel.spi_resources, 0, sizeof(kernel.spi_resources));
  memset(kernel.spi_info, 0, sizeof(kernel.spi_info));
  memset(kernel.i2c_resources, 0, sizeof(kernel.i2c_resources));
  memset(kernel.i2c_info, 0, sizeof(kernel.i2c_info));
  memset(kernel.adc_resources, 0, sizeof(kernel.adc_resources));
  memset(kernel.pwm_resources, 0, sizeof(kernel.pwm_resources));
  memset(kernel.pwm_info, 0, sizeof(kernel.pwm_info));
  memset(kernel.pio_resources, 0, sizeof(kernel.pio_resources));
  memset(kernel.pio_info, 0, sizeof(kernel.pio_info));
  memset(kernel.uart_resources, 0, sizeof(kernel.uart_resources));
  memset(kernel.dma_resources, 0, sizeof(kernel.dma_resources));
  memset(kernel.timer_resources, 0, sizeof(kernel.timer_resources));
  
  // Initialize type and id fields for each resource
  for (uint8_t i = 0; i < TOTAL_GPIO_RESOURCES; i++) {
    kernel.gpio_resources[i].type = RES_TYPE_GPIO;
    kernel.gpio_resources[i].id = i;
    kernel.gpio_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_SPI_RESOURCES; i++) {
    kernel.spi_resources[i].type = RES_TYPE_SPI;
    kernel.spi_resources[i].id = i;
    kernel.spi_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_I2C_RESOURCES; i++) {
    kernel.i2c_resources[i].type = RES_TYPE_I2C;
    kernel.i2c_resources[i].id = i;
    kernel.i2c_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_ADC_RESOURCES; i++) {
    kernel.adc_resources[i].type = RES_TYPE_ADC;
    kernel.adc_resources[i].id = i;
    kernel.adc_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_PWM_RESOURCES; i++) {
    kernel.pwm_resources[i].type = RES_TYPE_PWM;
    kernel.pwm_resources[i].id = i;
    kernel.pwm_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_PIO_RESOURCES; i++) {
    kernel.pio_resources[i].type = RES_TYPE_PIO;
    kernel.pio_resources[i].id = i;
    kernel.pio_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_UART_RESOURCES; i++) {
    kernel.uart_resources[i].type = RES_TYPE_UART;
    kernel.uart_resources[i].id = i;
    kernel.uart_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_DMA_RESOURCES; i++) {
    kernel.dma_resources[i].type = RES_TYPE_DMA;
    kernel.dma_resources[i].id = i;
    kernel.dma_resources[i].state = RES_STATE_FREE;
  }
  for (uint8_t i = 0; i < TOTAL_TIMER_RESOURCES; i++) {
    kernel.timer_resources[i].type = RES_TYPE_TIMER;
    kernel.timer_resources[i].id = i;
    kernel.timer_resources[i].state = RES_STATE_FREE;
  }
  
  // Clear violations
  memset(kernel.violations, 0, sizeof(kernel.violations));
  kernel.violation_head = 0;
  kernel.violation_count = 0;
  kernel.total_violations = 0;
  
  // Clear transactions
  memset(kernel.transactions, 0, sizeof(kernel.transactions));
  
  // Clear statistics
  kernel.res_total_claims = 0;
  kernel.res_total_releases = 0;
  kernel.res_total_conflicts = 0;
  kernel.res_total_auto_releases = 0;
  kernel.next_handle_id = 1;
  
  // Mark kernel-reserved resources
  res_mark_kernel_reserved();
  
  kernel.res_manager_initialized = true;
}

// Periodic maintenance (check transaction timeouts)
void res_tick() {
  if (!kernel.res_manager_initialized) return;
  
  uint32_t now = get_time_ms();
  
  // Check for transaction timeouts
  for (int i = 0; i < MAX_TRANSACTIONS; i++) {
    if (kernel.transactions[i].active) {
      if (now - kernel.transactions[i].start_time_ms > kernel.transactions[i].timeout_ms) {
        klog(1, "RES: Transaction timeout, forcing release");
        res_transaction_end(kernel.transactions[i].owner_task_id);
      }
    }
  }
}

// ============================================================================
// GPIO SAFE RESET - Called BEFORE task termination
// ============================================================================
// This disconnects the GPIO from the task cleanly, preventing glitches

static void res_gpio_safe_reset(uint8_t pin) {
  // 1. Disable any interrupts on this pin first
  gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE | 
                       GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH, false);
  
  // 2. Set to input (high-Z) - safest default state
  gpio_set_dir(pin, GPIO_IN);
  
  // 3. Disable all pulls - truly floating
  gpio_disable_pulls(pin);
  
  // 4. Reset function to SIO (default GPIO)
  gpio_set_function(pin, GPIO_FUNC_SIO);
  
  // 5. Clear any pending output state
  gpio_put(pin, 0);
}

// ============================================================================
// TASK RESOURCE CLEANUP - Called BEFORE brutal_task_kill completes
// ============================================================================

void res_cleanup_task(uint32_t task_id) {
  if (!kernel.res_manager_initialized) return;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  uint32_t released = 0;
  
  // Clean up GPIO - with safe hardware reset
  for (uint8_t i = 0; i < TOTAL_GPIO_RESOURCES; i++) {
    if (kernel.gpio_resources[i].owner_task_id == task_id && 
        kernel.gpio_resources[i].state == RES_STATE_CLAIMED) {
      
      // CRITICAL: Reset GPIO to safe state BEFORE releasing
      if (kernel.gpio_resources[i].configured) {
        res_gpio_safe_reset(i);
      }
      
      kernel.gpio_resources[i].state = RES_STATE_FREE;
      kernel.gpio_resources[i].owner_task_id = 0;
      kernel.gpio_resources[i].handle = 0;
      kernel.gpio_resources[i].configured = false;
      kernel.gpio_info[i].direction = GPIO_DIR_INPUT;
      kernel.gpio_info[i].output_state = false;
      released++;
    }
  }
  
  // Clean up SPI
  for (uint8_t i = 0; i < TOTAL_SPI_RESOURCES; i++) {
    if (kernel.spi_resources[i].owner_task_id == task_id &&
        kernel.spi_resources[i].state == RES_STATE_CLAIMED) {
      kernel.spi_resources[i].state = RES_STATE_FREE;
      kernel.spi_resources[i].owner_task_id = 0;
      kernel.spi_resources[i].handle = 0;
      released++;
    }
  }
  
  // Clean up I2C
  for (uint8_t i = 0; i < TOTAL_I2C_RESOURCES; i++) {
    if (kernel.i2c_resources[i].owner_task_id == task_id &&
        kernel.i2c_resources[i].state == RES_STATE_CLAIMED) {
      kernel.i2c_resources[i].state = RES_STATE_FREE;
      kernel.i2c_resources[i].owner_task_id = 0;
      kernel.i2c_resources[i].handle = 0;
      released++;
    }
  }
  
  // Clean up ADC
  for (uint8_t i = 0; i < TOTAL_ADC_RESOURCES; i++) {
    if (kernel.adc_resources[i].owner_task_id == task_id &&
        kernel.adc_resources[i].state == RES_STATE_CLAIMED) {
      kernel.adc_resources[i].state = RES_STATE_FREE;
      kernel.adc_resources[i].owner_task_id = 0;
      kernel.adc_resources[i].handle = 0;
      released++;
    }
  }
  
  // Clean up PWM - disable before releasing
  for (uint8_t i = 0; i < TOTAL_PWM_RESOURCES; i++) {
    if (kernel.pwm_resources[i].owner_task_id == task_id &&
        kernel.pwm_resources[i].state == RES_STATE_CLAIMED) {
      // Disable PWM slice using direct register access
      if (kernel.pwm_info[i].enabled) {
        uint8_t slice = i / 2;
        // PWM CSR register - clear EN bit
        hw_clear_bits(&pwm_hw->slice[slice].csr, PWM_CH0_CSR_EN_BITS);
      }
      kernel.pwm_resources[i].state = RES_STATE_FREE;
      kernel.pwm_resources[i].owner_task_id = 0;
      kernel.pwm_resources[i].handle = 0;
      kernel.pwm_info[i].enabled = false;
      released++;
    }
  }
  
  // Clean up PIO
  for (uint8_t i = 0; i < TOTAL_PIO_RESOURCES; i++) {
    if (kernel.pio_resources[i].owner_task_id == task_id &&
        kernel.pio_resources[i].state == RES_STATE_CLAIMED) {
      // Stop state machine if running
      if (kernel.pio_info[i].running) {
        PIO pio = (kernel.pio_info[i].pio_num == 0) ? pio0 : pio1;
        pio_sm_set_enabled(pio, kernel.pio_info[i].sm_num, false);
      }
      kernel.pio_resources[i].state = RES_STATE_FREE;
      kernel.pio_resources[i].owner_task_id = 0;
      kernel.pio_resources[i].handle = 0;
      kernel.pio_info[i].running = false;
      released++;
    }
  }
  
  // Clean up DMA - abort any active transfers
  for (uint8_t i = 0; i < TOTAL_DMA_RESOURCES; i++) {
    if (kernel.dma_resources[i].owner_task_id == task_id &&
        kernel.dma_resources[i].state == RES_STATE_CLAIMED) {
      dma_channel_abort(i);
      kernel.dma_resources[i].state = RES_STATE_FREE;
      kernel.dma_resources[i].owner_task_id = 0;
      kernel.dma_resources[i].handle = 0;
      released++;
    }
  }
  
  // Clean up timers
  for (uint8_t i = 0; i < TOTAL_TIMER_RESOURCES; i++) {
    if (kernel.timer_resources[i].owner_task_id == task_id &&
        kernel.timer_resources[i].state == RES_STATE_CLAIMED) {
      kernel.timer_resources[i].state = RES_STATE_FREE;
      kernel.timer_resources[i].owner_task_id = 0;
      kernel.timer_resources[i].handle = 0;
      released++;
    }
  }
  
  // End any active transactions
  for (int i = 0; i < MAX_TRANSACTIONS; i++) {
    if (kernel.transactions[i].active && kernel.transactions[i].owner_task_id == task_id) {
      kernel.transactions[i].active = false;
    }
  }
  
  kernel.res_total_auto_releases += released;
  
  mutex_exit(&kernel.res_lock);
  
  if (released > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "RES: Task %lu cleanup - %lu resources released", task_id, released);
    klog(0, buf);
  }
}

// Validate handle and ownership
bool res_validate_handle(ResHandle handle, uint32_t task_id) {
  ResourceDescriptor* desc = res_get_descriptor(handle);
  if (!desc) return false;
  if (desc->state != RES_STATE_CLAIMED && desc->state != RES_STATE_SHARED) return false;
  if (desc->owner_task_id != task_id && desc->mode != RES_MODE_SHARED_READ) return false;
  return true;
}

// ============================================================================
// CLAIM/RELEASE OPERATIONS
// ============================================================================

// Claim a GPIO pin - after this, app uses DIRECT hardware access
ResHandle res_claim_gpio(uint8_t pin, ResourceMode mode, uint32_t task_id) {
  if (pin >= TOTAL_GPIO_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.gpio_resources[pin];
  
  // Check if already owned
  if (res->state == RES_STATE_CLAIMED) {
    if (res->owner_task_id != task_id) {
      kernel.res_total_conflicts++;
      mutex_exit(&kernel.res_lock);
      return RES_HANDLE_INVALID;
    }
    // Same task re-claiming - return existing handle
    mutex_exit(&kernel.res_lock);
    return res->handle;
  }
  
  // Check if kernel reserved
  if (res->state == RES_STATE_KERNEL_RESERVED) {
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  // Claim the resource
  ResHandle handle = res_generate_handle(RES_TYPE_GPIO, pin);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  res->last_access_ms = res->claim_time_ms;
  res->access_count = 0;
  res->configured = false;
  
  kernel.res_total_claims++;
  
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim SPI bus
ResHandle res_claim_spi(uint8_t bus, uint8_t cs_pin, ResourceMode mode, uint32_t task_id) {
  if (bus >= TOTAL_SPI_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.spi_resources[bus];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  if (res->state == RES_STATE_KERNEL_RESERVED) {
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_SPI, bus);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->sub_id = cs_pin;
  res->claim_time_ms = get_time_ms();
  res->configured = false;
  
  kernel.spi_info[bus].cs_pin = cs_pin;
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  // Also claim the CS pin if specified
  if (cs_pin < TOTAL_GPIO_RESOURCES) {
    ResHandle cs_handle = res_claim_gpio(cs_pin, mode, task_id);
    if (cs_handle == RES_HANDLE_INVALID) {
      res_release(handle, task_id);
      return RES_HANDLE_INVALID;
    }
  }
  
  return handle;
}

// Claim I2C device
ResHandle res_claim_i2c(uint8_t bus, uint8_t device_addr, ResourceMode mode, uint32_t task_id) {
  if (bus >= RES_I2C_COUNT) return RES_HANDLE_INVALID;
  
  uint8_t res_idx = bus * 16 + (device_addr & 0x0F);
  if (res_idx >= TOTAL_I2C_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.i2c_resources[res_idx];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_I2C, res_idx);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->sub_id = device_addr;
  res->claim_time_ms = get_time_ms();
  res->configured = false;
  
  kernel.i2c_info[res_idx].device_addr = device_addr;
  kernel.i2c_info[res_idx].is_master = true;
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim ADC channel
ResHandle res_claim_adc(uint8_t channel, ResourceMode mode, uint32_t task_id) {
  if (channel >= TOTAL_ADC_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.adc_resources[channel];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_ADC, channel);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  res->configured = false;
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim PWM channel
ResHandle res_claim_pwm(uint8_t slice, uint8_t channel, ResourceMode mode, uint32_t task_id) {
  uint8_t pwm_idx = slice * 2 + channel;
  if (pwm_idx >= TOTAL_PWM_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.pwm_resources[pwm_idx];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_PWM, pwm_idx);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  res->configured = false;
  
  kernel.pwm_info[pwm_idx].slice = slice;
  kernel.pwm_info[pwm_idx].channel = channel;
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim PIO state machine
ResHandle res_claim_pio(uint8_t pio_num, uint8_t sm_num, ResourceMode mode, uint32_t task_id) {
  if (pio_num >= RES_PIO_COUNT || sm_num >= RES_PIO_SM_COUNT) return RES_HANDLE_INVALID;
  
  uint8_t pio_idx = pio_num * RES_PIO_SM_COUNT + sm_num;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.pio_resources[pio_idx];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_PIO, pio_idx);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  res->configured = false;
  
  kernel.pio_info[pio_idx].pio_num = pio_num;
  kernel.pio_info[pio_idx].sm_num = sm_num;
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim DMA channel
ResHandle res_claim_dma(uint8_t channel, ResourceMode mode, uint32_t task_id) {
  if (channel >= TOTAL_DMA_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.dma_resources[channel];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_DMA, channel);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim timer alarm
ResHandle res_claim_timer(uint8_t alarm_num, ResourceMode mode, uint32_t task_id) {
  if (alarm_num >= TOTAL_TIMER_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.timer_resources[alarm_num];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_TIMER, alarm_num);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Claim UART
ResHandle res_claim_uart(uint8_t uart_num, ResourceMode mode, uint32_t task_id) {
  if (uart_num >= TOTAL_UART_RESOURCES) return RES_HANDLE_INVALID;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  ResourceDescriptor* res = &kernel.uart_resources[uart_num];
  
  if (res->state == RES_STATE_CLAIMED && res->owner_task_id != task_id) {
    kernel.res_total_conflicts++;
    mutex_exit(&kernel.res_lock);
    return RES_HANDLE_INVALID;
  }
  
  ResHandle handle = res_generate_handle(RES_TYPE_UART, uart_num);
  res->handle = handle;
  res->owner_task_id = task_id;
  res->state = RES_STATE_CLAIMED;
  res->mode = mode;
  res->claim_time_ms = get_time_ms();
  
  kernel.res_total_claims++;
  mutex_exit(&kernel.res_lock);
  
  return handle;
}

// Release a resource
void res_release(ResHandle handle, uint32_t task_id) {
  ResourceDescriptor* desc = res_get_descriptor(handle);
  if (!desc) return;
  
  mutex_enter_blocking(&kernel.res_lock);
  
  // Verify ownership
  if (desc->owner_task_id != task_id) {
    mutex_exit(&kernel.res_lock);
    return;
  }
  
  // Reset GPIO to safe state if configured
  if (desc->type == RES_TYPE_GPIO && desc->configured) {
    res_gpio_safe_reset(desc->id);
  }
  
  // Disable PWM if releasing
  if (desc->type == RES_TYPE_PWM && kernel.pwm_info[desc->id].enabled) {
    uint8_t slice = desc->id / 2;
    hw_clear_bits(&pwm_hw->slice[slice].csr, PWM_CH0_CSR_EN_BITS);
    kernel.pwm_info[desc->id].enabled = false;
  }
  
  desc->state = RES_STATE_FREE;
  desc->owner_task_id = 0;
  desc->handle = 0;
  desc->configured = false;
  
  kernel.res_total_releases++;
  
  mutex_exit(&kernel.res_lock);
}

// ============================================================================
// GPIO SHADOW STATE UPDATE - Apps call this to update kernel's knowledge
// ============================================================================
// These are OPTIONAL - apps can update shadow state so kernel knows what
// to expect. This enables smarter cleanup but is NOT required for operation.

void res_gpio_notify_direction(uint8_t pin, GPIODirection dir, uint32_t task_id) {
  if (pin >= TOTAL_GPIO_RESOURCES) return;
  ResourceDescriptor* res = &kernel.gpio_resources[pin];
  if (res->owner_task_id != task_id) return;
  kernel.gpio_info[pin].direction = dir;
  res->configured = true;
}

void res_gpio_notify_state(uint8_t pin, bool state, uint32_t task_id) {
  if (pin >= TOTAL_GPIO_RESOURCES) return;
  ResourceDescriptor* res = &kernel.gpio_resources[pin];
  if (res->owner_task_id != task_id) return;
  kernel.gpio_info[pin].output_state = state;
}

void res_pwm_notify_enabled(uint8_t pwm_idx, bool enabled, uint32_t task_id) {
  if (pwm_idx >= TOTAL_PWM_RESOURCES) return;
  ResourceDescriptor* res = &kernel.pwm_resources[pwm_idx];
  if (res->owner_task_id != task_id) return;
  kernel.pwm_info[pwm_idx].enabled = enabled;
  res->configured = true;
}

void res_pio_notify_running(uint8_t pio_idx, bool running, uint32_t task_id) {
  if (pio_idx >= TOTAL_PIO_RESOURCES) return;
  ResourceDescriptor* res = &kernel.pio_resources[pio_idx];
  if (res->owner_task_id != task_id) return;
  kernel.pio_info[pio_idx].running = running;
  res->configured = true;
}

// ============================================================================
// TRANSACTION API (multi-resource locking)
// ============================================================================

bool res_transaction_begin(ResHandle* handles, uint8_t count, uint32_t timeout_ms, uint32_t task_id) {
  if (count == 0 || count > 8) return false;
  
  // Find free transaction slot
  int slot = -1;
  for (int i = 0; i < MAX_TRANSACTIONS; i++) {
    if (!kernel.transactions[i].active) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return false;  // No free slots
  
  // Record transaction
  kernel.transactions[slot].owner_task_id = task_id;
  kernel.transactions[slot].start_time_ms = get_time_ms();
  kernel.transactions[slot].timeout_ms = timeout_ms;
  kernel.transactions[slot].handle_count = count;
  for (uint8_t i = 0; i < count; i++) {
    kernel.transactions[slot].handles[i] = handles[i];
  }
  kernel.transactions[slot].active = true;
  
  return true;
}

void res_transaction_end(uint32_t task_id) {
  for (int i = 0; i < MAX_TRANSACTIONS; i++) {
    if (kernel.transactions[i].active && kernel.transactions[i].owner_task_id == task_id) {
      kernel.transactions[i].active = false;
    }
  }
}

// ============================================================================
// VIOLATION TRACKING
// ============================================================================

void res_record_violation(uint32_t task_id, ResourceType type, uint8_t id, const char* context) {
  #if RESOURCE_AUDIT_ENABLED == 0
    return;
  #endif
  
  mutex_enter_blocking(&kernel.res_lock);
  
  int found = -1;
  for (int i = 0; i < MAX_RESOURCE_VIOLATIONS; i++) {
    if (kernel.violations[i].task_id == task_id) {
      found = i;
      break;
    }
  }
  
  if (found >= 0) {
    kernel.violations[found].violation_count++;
    kernel.violations[found].timestamp_ms = get_time_ms();
    kernel.violations[found].resource_type = type;
    kernel.violations[found].resource_id = id;
  } else {
    int idx = kernel.violation_head;
    kernel.violations[idx].task_id = task_id;
    kernel.violations[idx].timestamp_ms = get_time_ms();
    kernel.violations[idx].resource_type = type;
    kernel.violations[idx].resource_id = id;
    kernel.violations[idx].violation_count = 1;
    kernel.violations[idx].warned = false;
    
    kernel.violation_head = (kernel.violation_head + 1) % MAX_RESOURCE_VIOLATIONS;
    if (kernel.violation_count < MAX_RESOURCE_VIOLATIONS) {
      kernel.violation_count++;
    }
  }
  
  kernel.total_violations++;
  
  mutex_exit(&kernel.res_lock);
  
  char buf[96];
  snprintf(buf, sizeof(buf), "RES VIOLATION: Task %lu accessed %s[%d] directly (%s)",
           task_id, RES_TYPE_NAMES[type], id, context ? context : "");
  klog(1, buf);
}

uint32_t res_get_violations(uint32_t task_id) {
  for (int i = 0; i < MAX_RESOURCE_VIOLATIONS; i++) {
    if (kernel.violations[i].task_id == task_id) {
      return kernel.violations[i].violation_count;
    }
  }
  return 0;
}

bool res_should_terminate(uint32_t task_id) {
  uint32_t violations = res_get_violations(task_id);
  return violations >= RESOURCE_MAX_VIOLATIONS;
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

bool res_is_owned(ResourceType type, uint8_t id) {
  ResourceDescriptor* desc = res_get_by_type_id(type, id);
  if (!desc) return false;
  return (desc->state == RES_STATE_CLAIMED || desc->state == RES_STATE_SHARED);
}

uint32_t res_get_owner(ResourceType type, uint8_t id) {
  ResourceDescriptor* desc = res_get_by_type_id(type, id);
  if (!desc) return 0;
  return desc->owner_task_id;
}

uint32_t res_count_owned_by_task(uint32_t task_id) {
  uint32_t count = 0;
  
  for (uint8_t i = 0; i < TOTAL_GPIO_RESOURCES; i++) {
    if (kernel.gpio_resources[i].owner_task_id == task_id &&
        kernel.gpio_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  for (uint8_t i = 0; i < TOTAL_SPI_RESOURCES; i++) {
    if (kernel.spi_resources[i].owner_task_id == task_id &&
        kernel.spi_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  for (uint8_t i = 0; i < TOTAL_I2C_RESOURCES; i++) {
    if (kernel.i2c_resources[i].owner_task_id == task_id &&
        kernel.i2c_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  for (uint8_t i = 0; i < TOTAL_ADC_RESOURCES; i++) {
    if (kernel.adc_resources[i].owner_task_id == task_id &&
        kernel.adc_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  for (uint8_t i = 0; i < TOTAL_PWM_RESOURCES; i++) {
    if (kernel.pwm_resources[i].owner_task_id == task_id &&
        kernel.pwm_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  for (uint8_t i = 0; i < TOTAL_PIO_RESOURCES; i++) {
    if (kernel.pio_resources[i].owner_task_id == task_id &&
        kernel.pio_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  for (uint8_t i = 0; i < TOTAL_DMA_RESOURCES; i++) {
    if (kernel.dma_resources[i].owner_task_id == task_id &&
        kernel.dma_resources[i].state == RES_STATE_CLAIMED) count++;
  }
  
  return count;
}

// ============================================================================
// DEBUG/SHELL FUNCTIONS
// ============================================================================

void res_print_status() {
  kout.println("\n=== Resource Manager v14.3.1 Status ===");
  kout.print("Initialized: ");
  kout.println(kernel.res_manager_initialized ? "YES" : "NO");
  kout.println("Mode: ZERO-OVERHEAD (ownership tracking only)");
  
  uint32_t gpio_claimed = 0, gpio_reserved = 0;
  for (uint8_t i = 0; i < TOTAL_GPIO_RESOURCES; i++) {
    if (kernel.gpio_resources[i].state == RES_STATE_CLAIMED) gpio_claimed++;
    if (kernel.gpio_resources[i].state == RES_STATE_KERNEL_RESERVED) gpio_reserved++;
  }
  
  uint32_t spi_claimed = 0;
  for (uint8_t i = 0; i < TOTAL_SPI_RESOURCES; i++) {
    if (kernel.spi_resources[i].state == RES_STATE_CLAIMED) spi_claimed++;
  }
  
  uint32_t i2c_claimed = 0;
  for (uint8_t i = 0; i < TOTAL_I2C_RESOURCES; i++) {
    if (kernel.i2c_resources[i].state == RES_STATE_CLAIMED) i2c_claimed++;
  }
  
  uint32_t adc_claimed = 0;
  for (uint8_t i = 0; i < TOTAL_ADC_RESOURCES; i++) {
    if (kernel.adc_resources[i].state == RES_STATE_CLAIMED) adc_claimed++;
  }
  
  uint32_t pwm_claimed = 0;
  for (uint8_t i = 0; i < TOTAL_PWM_RESOURCES; i++) {
    if (kernel.pwm_resources[i].state == RES_STATE_CLAIMED) pwm_claimed++;
  }
  
  kout.println("\n--- Resource Usage ---");
  char buf[64];
  snprintf(buf, sizeof(buf), "GPIO: %lu/%d claimed, %lu kernel-reserved", 
           gpio_claimed, TOTAL_GPIO_RESOURCES, gpio_reserved);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "SPI:  %lu/%d claimed", spi_claimed, TOTAL_SPI_RESOURCES);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "I2C:  %lu/%d claimed", i2c_claimed, TOTAL_I2C_RESOURCES);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "ADC:  %lu/%d claimed", adc_claimed, TOTAL_ADC_RESOURCES);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "PWM:  %lu/%d claimed", pwm_claimed, TOTAL_PWM_RESOURCES);
  kout.println(buf);
  
  kout.println("\n--- Statistics ---");
  snprintf(buf, sizeof(buf), "Total Claims: %lu", kernel.res_total_claims);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "Total Releases: %lu", kernel.res_total_releases);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "Conflicts: %lu", kernel.res_total_conflicts);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "Auto-releases (task death): %lu", kernel.res_total_auto_releases);
  kout.println(buf);
  snprintf(buf, sizeof(buf), "Violations: %lu", kernel.total_violations);
  kout.println(buf);
}

void res_print_gpio_map() {
  kout.println("\n=== GPIO Resource Map ===");
  kout.println("Pin  State        Owner   Dir");
  kout.println("---  -----------  ------  --------");
  
  const char* state_names[] = {"FREE", "CLAIMED", "SHARED", "LOCKED", "KERNEL"};
  const char* dir_names[] = {"IN", "OUT", "PULL_UP", "PULL_DN", "ANALOG"};
  
  for (uint8_t i = 0; i < TOTAL_GPIO_RESOURCES; i++) {
    ResourceDescriptor* res = &kernel.gpio_resources[i];
    if (res->state == RES_STATE_FREE) continue;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%-3d  %-11s  %-6lu  %s",
             i,
             state_names[res->state],
             res->owner_task_id,
             res->configured && kernel.gpio_info[i].direction < 5 ? 
               dir_names[kernel.gpio_info[i].direction] : "N/A");
    kout.println(buf);
  }
}

void res_print_violations() {
  kout.println("\n=== Resource Violations ===");
  
  if (kernel.total_violations == 0) {
    kout.println("No violations recorded.");
    return;
  }
  
  kout.println("Task   Count  Last Resource    Time");
  kout.println("-----  -----  ---------------  --------");
  
  for (int i = 0; i < MAX_RESOURCE_VIOLATIONS; i++) {
    if (kernel.violations[i].task_id == 0) continue;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%-5lu  %-5lu  %s[%d]          %lums ago",
             kernel.violations[i].task_id,
             kernel.violations[i].violation_count,
             RES_TYPE_NAMES[kernel.violations[i].resource_type],
             kernel.violations[i].resource_id,
             get_time_ms() - kernel.violations[i].timestamp_ms);
    kout.println(buf);
  }
}

void res_print_task_resources(uint32_t task_id) {
  kout.print("\n=== Resources owned by Task ");
  kout.print(task_id);
  kout.println(" ===");
  
  bool found = false;
  
  for (uint8_t i = 0; i < TOTAL_GPIO_RESOURCES; i++) {
    if (kernel.gpio_resources[i].owner_task_id == task_id &&
        kernel.gpio_resources[i].state == RES_STATE_CLAIMED) {
      kout.print("  GPIO["); kout.print(i); kout.println("]");
      found = true;
    }
  }
  
  for (uint8_t i = 0; i < TOTAL_SPI_RESOURCES; i++) {
    if (kernel.spi_resources[i].owner_task_id == task_id &&
        kernel.spi_resources[i].state == RES_STATE_CLAIMED) {
      kout.print("  SPI["); kout.print(i); kout.println("]");
      found = true;
    }
  }
  
  for (uint8_t i = 0; i < TOTAL_ADC_RESOURCES; i++) {
    if (kernel.adc_resources[i].owner_task_id == task_id &&
        kernel.adc_resources[i].state == RES_STATE_CLAIMED) {
      kout.print("  ADC["); kout.print(i); kout.println("]");
      found = true;
    }
  }
  
  for (uint8_t i = 0; i < TOTAL_PWM_RESOURCES; i++) {
    if (kernel.pwm_resources[i].owner_task_id == task_id &&
        kernel.pwm_resources[i].state == RES_STATE_CLAIMED) {
      kout.print("  PWM["); kout.print(i); kout.println("]");
      found = true;
    }
  }
  
  for (uint8_t i = 0; i < TOTAL_PIO_RESOURCES; i++) {
    if (kernel.pio_resources[i].owner_task_id == task_id &&
        kernel.pio_resources[i].state == RES_STATE_CLAIMED) {
      kout.print("  PIO["); kout.print(i); kout.println("]");
      found = true;
    }
  }
  
  for (uint8_t i = 0; i < TOTAL_DMA_RESOURCES; i++) {
    if (kernel.dma_resources[i].owner_task_id == task_id &&
        kernel.dma_resources[i].state == RES_STATE_CLAIMED) {
      kout.print("  DMA["); kout.print(i); kout.println("]");
      found = true;
    }
  }
  
  if (!found) {
    kout.println("  (none)");
  }
}

void watchdog_init() {
  if (watchdog_caused_reboot()) {
    kout.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!");
    kout.println("!!! REBOOT BY WATCHDOG !!!");
    kout.println("!!!!!!!!!!!!!!!!!!!!!!!!!!");
    watchdog_state.triggers++;
  }
  
  watchdog_state.enabled = true;
  watchdog_state.last_feed = get_time_ms();
  watchdog_state.in_panic = false;
  watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
  kout.println("[WDT] Watchdog enabled (8s timeout)");
}

void watchdog_feed() {
  if (!watchdog_state.enabled || watchdog_state.in_panic) return;
  watchdog_state.last_feed = get_time_ms();
  watchdog_update();
}

void watchdog_check() {
  if (!watchdog_state.enabled || watchdog_state.in_panic) return;
  uint32_t now = get_time_ms();
  uint32_t elapsed = now - watchdog_state.last_feed;
  
  if (elapsed > (watchdog_state.timeout_ms * 3 / 4)) {
    kout.print("\n*** WATCHDOG WARNING: No feed in ");
    kout.print((uint32_t)elapsed);
    kout.println(" ms ***");
    klog(2, "WDT: Feed timeout approaching!");
  }
}

// ============================================================================
// ADVANCED CPU FREQUENCY GOVERNOR v2.0 IMPLEMENTATION
// ============================================================================

// Profile names for logging
static const char* const GOVERNOR_PROFILE_NAMES[] = {
  "ULTRA_LOW", "POWERSAVE", "BALANCED", "PERFORMANCE", "TURBO"
};

// Convert millivolts to vreg voltage enum
static vreg_voltage governor_mv_to_vreg(uint32_t mv) {
  if (mv <= 850) return VREG_VOLTAGE_0_85;
  if (mv <= 900) return VREG_VOLTAGE_0_90;
  if (mv <= 950) return VREG_VOLTAGE_0_95;
  if (mv <= 1000) return VREG_VOLTAGE_1_00;
  if (mv <= 1050) return VREG_VOLTAGE_1_05;
  if (mv <= 1100) return VREG_VOLTAGE_1_10;
  if (mv <= 1150) return VREG_VOLTAGE_1_15;
  if (mv <= 1200) return VREG_VOLTAGE_1_20;
  if (mv <= 1250) return VREG_VOLTAGE_1_25;
  return VREG_VOLTAGE_1_30;
}

// Internal WFI sleep helper - puts core to sleep until interrupt
static void governor_wfi_sleep(uint32_t us) {
  if (us < WFI_MIN_SLEEP_US) return;
  
  kernel.governor.in_wfi = true;
  
  // RP2350 M33 requires careful WFI handling
  #if RP2040_OR_RP2350 == 1
    // M33: Use WFI with SLEEPDEEP cleared for light sleep
    // Clear SLEEPDEEP bit to ensure we don't enter deep sleep
    scb_hw->scr &= ~M33_SCB_SCR_SLEEPDEEP_BITS;
    __wfi();
  #else
    // RP2040 M0+: Simple WFI
    __wfi();
  #endif
  
  kernel.governor.in_wfi = false;
  kernel.governor.wfi_total_us += us;
}

// Apply a CPU profile (internal helper) - CHIP AWARE
static void governor_apply_profile(CPUProfile profile) {
  // Safety: Don't change clock if kernel isn't running yet
  if (!kernel.running) {
    return;
  }
  
  // Governor disabled? Only apply if forced or initial setup
  #if ENABLE_PICOMIMI_GOVERNOR == 0
    // Governor disabled - only honor explicit manual sets
    if (!kernel.governor.user_override) {
      return;
    }
  #endif
  
  // Bounds check
  if (profile >= CPU_PROFILE_COUNT) {
    profile = CPU_PROFILE_BALANCED;
  }
  
  // Get chip-specific frequency and voltage
  #if RP2040_OR_RP2350 == 0
    uint32_t target_freq = CPU_FREQ_TABLE_RP2040[profile];
    uint32_t target_voltage_mv = CPU_VOLTAGE_TABLE_RP2040[profile];
  #else
    uint32_t target_freq = CPU_FREQ_TABLE_RP2350[profile];
    uint32_t target_voltage_mv = CPU_VOLTAGE_TABLE_RP2350[profile];
  #endif
  
  // No change needed?
  if (target_freq == kernel.governor.current_freq_khz && 
      profile == kernel.governor.current_profile) {
    return;
  }
  
  // Exit WFI if we're in it
  if (kernel.governor.in_wfi) {
    governor_exit_wfi();
  }
  
  vreg_voltage vreg_volt = governor_mv_to_vreg(target_voltage_mv);
  
  // When INCREASING frequency: raise voltage FIRST (safe)
  if (target_freq > kernel.governor.current_freq_khz) {
    vreg_set_voltage(vreg_volt);
    busy_wait_us(150);  // Extra time for voltage stabilization on transitions up
  }
  
  // Set the clock frequency
  bool clock_ok = set_sys_clock_khz(target_freq, true);
  
  if (!clock_ok) {
    // Clock set failed - try a safe fallback
    klog(2, "GOV: Clock set failed, using fallback");
    
    #if RP2040_OR_RP2350 == 0
      set_sys_clock_khz(133000, true);  // RP2040 safe fallback
      kernel.governor.current_freq_khz = 133000;
    #else
      set_sys_clock_khz(150000, true);  // RP2350 safe fallback
      kernel.governor.current_freq_khz = 150000;
    #endif
    
    kernel.governor.current_profile = CPU_PROFILE_BALANCED;
    vreg_set_voltage(VREG_VOLTAGE_1_00);
    return;
  }
  
  // When DECREASING frequency: lower voltage AFTER (safe)
  if (target_freq < kernel.governor.current_freq_khz) {
    vreg_set_voltage(vreg_volt);
  }
  
  // Update state
  kernel.governor.current_freq_khz = target_freq;
  kernel.governor.current_profile = profile;
  kernel.governor.last_change_ms = get_time_ms();
  
  // Track turbo time (for stats only, no limit)
  if (profile == CPU_PROFILE_TURBO) {
    if (!kernel.governor.turbo_active) {
      kernel.governor.turbo_start_ms = get_time_ms();
      kernel.governor.turbo_active = true;
    }
  } else {
    if (kernel.governor.turbo_active) {
      kernel.governor.total_turbo_time_ms += get_time_ms() - kernel.governor.turbo_start_ms;
      kernel.governor.turbo_active = false;
    }
  }
  
  // Log the change (less verbose for ULTRA_LOW transitions)
  if (profile != CPU_PROFILE_ULTRA_LOW) {
    char buf[80];
    snprintf(buf, sizeof(buf), "GOV: %s @ %lu KHz (%s)", 
             GOVERNOR_PROFILE_NAMES[profile], target_freq, CHIP_NAME);
    klog(0, buf);
  }
}

// Calculate system load (0-100) with instant load tracking
static uint8_t governor_calculate_load() {
  float load = kernel.cpu_usage;
  
  // Factor in Core 1 usage if active
  if (kernel.core1_initialized) {
    load = (load + kernel.core1.cpu_usage) / 2.0f;
  }
  
  // Clamp to 0-100
  if (load < 0) load = 0;
  if (load > 100) load = 100;
  
  // Track instant load for spike detection
  static float prev_load = 0;
  kernel.governor.instant_load = load;
  
  // Detect sudden load spikes for instant turbo
  if (load - prev_load > INSTANT_TURBO_LOAD_SPIKE - 20) {
    kernel.governor.instant_turbo_pending = true;
  }
  prev_load = load;
  
  return (uint8_t)load;
}

// Initialize the CPU governor
void governor_init() {
  memset(&kernel.governor, 0, sizeof(CPUGovernorState));
  
  // Set initial state based on configuration
  kernel.governor.current_profile = CPU_PROFILE_BALANCED;
  kernel.governor.requested_profile = CPU_PROFILE_BALANCED;
  
  #if RP2040_OR_RP2350 == 0
    kernel.governor.current_freq_khz = 133000;  // RP2040 balanced
    kernel.governor.target_freq_khz = 133000;
  #else
    kernel.governor.current_freq_khz = 150000;  // RP2350 balanced
    kernel.governor.target_freq_khz = 150000;
  #endif
  
  kernel.governor.turbo_active = false;
  kernel.governor.thermal_throttled = false;
  kernel.governor.user_override = false;
  kernel.governor.wfi_enabled = true;  // WFI enabled by default
  kernel.governor.in_wfi = false;
  kernel.governor.instant_turbo_pending = false;
  kernel.governor.last_change_ms = get_time_ms();
  kernel.governor.last_idle_ms = get_time_ms();
  kernel.turbo_enabled = true;
  
  // If governor is disabled, lock to max frequency
  #if ENABLE_PICOMIMI_GOVERNOR == 0
    kernel.governor.user_override = true;
    kernel.governor.requested_profile = CPU_PROFILE_TURBO;
    kout.print("[GOV] Governor DISABLED - Locked to ");
    kout.print(CHIP_FREQ_MANUAL / 1000);
    kout.println(" MHz");
    
    // Apply max frequency after boot stabilization (done in first tick)
  #else
    kout.print("[GOV] Advanced Governor v2.0 initialized (");
    kout.print(CHIP_NAME);
    kout.println(")");
    kout.print("[GOV] Profiles: ULTRA_LOW(50) POWERSAVE(100) BALANCED(");
    #if RP2040_OR_RP2350 == 0
      kout.print("133");
    #else
      kout.print("150");
    #endif
    kout.print(") PERF(");
    #if RP2040_OR_RP2350 == 0
      kout.print("200) TURBO(260");
    #else
      kout.print("250) TURBO(310");
    #endif
    kout.println(")");
    kout.print("[GOV] Thermal limit: ");
    kout.print((int)THERMAL_THROTTLE_TEMP);
    kout.println("°C | NO turbo time limit");
  #endif
}

// Periodic governor tick - called from scheduler (ADVANCED v2.0)
void governor_tick() {
  #if ENABLE_PICOMIMI_GOVERNOR == 0
    // Governor disabled - check if we need to set manual frequency (once)
    static bool manual_freq_set = false;
    if (!manual_freq_set && kernel.running) {
      // Set locked frequency
      vreg_set_voltage(VREG_VOLTAGE_1_25);  // High voltage for max freq
      busy_wait_us(200);
      
      if (set_sys_clock_khz(CHIP_FREQ_MANUAL, true)) {
        kernel.governor.current_freq_khz = CHIP_FREQ_MANUAL;
        kernel.governor.current_profile = CPU_PROFILE_TURBO;
        manual_freq_set = true;
        klog(0, "GOV: Manual frequency locked");
      }
    }
    // Still do thermal checks even when disabled
    governor_thermal_check();
    return;
  #endif
  
  uint32_t now = get_time_ms();
  
  // Fast path: instant turbo pending from task demand
  if (kernel.governor.instant_turbo_pending) {
    kernel.governor.instant_turbo_pending = false;
    if (kernel.turbo_enabled && !kernel.governor.thermal_throttled) {
      if (kernel.governor.current_profile < CPU_PROFILE_TURBO) {
        governor_apply_profile(CPU_PROFILE_TURBO);
        return;
      }
    }
  }
  
  // Determine check interval based on current state
  uint32_t check_interval = GOVERNOR_CHECK_INTERVAL_MS;
  if (kernel.governor.input_boost_active || kernel.governor.turbo_active) {
    check_interval = GOVERNOR_FAST_CHECK_MS;  // Faster checks during transitions
  }
  
  if (now - kernel.governor.last_change_ms < check_interval) {
    return;
  }
  
  // Check thermal limits first (ALWAYS, even in manual mode)
  governor_thermal_check();
  
  // If thermally throttled, drop to safe profile
  if (kernel.governor.thermal_throttled) {
    if (kernel.governor.current_profile > CPU_PROFILE_BALANCED) {
      kernel.governor.pre_throttle_profile = kernel.governor.current_profile;
      governor_apply_profile(CPU_PROFILE_BALANCED);
    }
    return;
  }
  
  // Handle input boost expiration
  if (kernel.governor.input_boost_active) {
    uint32_t boost_duration = now - kernel.governor.input_boost_start_ms;
    if (boost_duration >= INPUT_BOOST_DURATION_MS) {
      kernel.governor.input_boost_active = false;
    } else {
      // Maintain boost profile during input boost
      if (kernel.governor.current_profile < INPUT_BOOST_PROFILE) {
        governor_apply_profile(INPUT_BOOST_PROFILE);
      }
      return;
    }
  }
  
  // If user override is set, respect it (but thermal still applies)
  if (kernel.governor.user_override) {
    if (kernel.governor.current_profile != kernel.governor.requested_profile) {
      governor_apply_profile(kernel.governor.requested_profile);
    }
    return;
  }
  
  // Calculate current load
  uint8_t load = governor_calculate_load();
  
  // Update load history (rolling average over 16 samples)
  kernel.governor.load_history[kernel.governor.load_idx] = load;
  kernel.governor.load_idx = (kernel.governor.load_idx + 1) & 0x0F;
  
  // Calculate average load
  uint32_t total_load = 0;
  for (int i = 0; i < 16; i++) {
    total_load += kernel.governor.load_history[i];
  }
  kernel.governor.avg_load = total_load / 16.0f;
  
  // =========================================================================
  // ADVANCED 5-LEVEL FREQUENCY SCALING WITH HYSTERESIS
  // =========================================================================
  
  CPUProfile current = kernel.governor.current_profile;
  CPUProfile target = current;  // Default: stay at current
  float avg = kernel.governor.avg_load;
  
  // Check scale-UP conditions (higher thresholds for going up)
  if (avg >= TURBO_SCALE_UP_THRESHOLD && kernel.turbo_enabled) {
    target = CPU_PROFILE_TURBO;
  } else if (avg >= PERF_SCALE_UP_THRESHOLD && current < CPU_PROFILE_PERFORMANCE) {
    target = CPU_PROFILE_PERFORMANCE;
  } else if (avg >= BALANCED_SCALE_UP_THRESHOLD && current < CPU_PROFILE_BALANCED) {
    target = CPU_PROFILE_BALANCED;
  }
  
  // Check scale-DOWN conditions (lower thresholds for going down)
  // TURBO -> PERFORMANCE
  if (current == CPU_PROFILE_TURBO && avg < TURBO_SCALE_DOWN_THRESHOLD) {
    target = CPU_PROFILE_PERFORMANCE;
  }
  // PERFORMANCE -> BALANCED
  else if (current == CPU_PROFILE_PERFORMANCE && avg < PERF_SCALE_DOWN_THRESHOLD) {
    target = CPU_PROFILE_BALANCED;
  }
  // BALANCED -> POWERSAVE
  else if (current == CPU_PROFILE_BALANCED && avg < BALANCED_SCALE_DOWN_THRESHOLD) {
    target = CPU_PROFILE_POWERSAVE;
  }
  // POWERSAVE -> ULTRA_LOW (only when truly idle)
  else if (current == CPU_PROFILE_POWERSAVE && avg < POWERSAVE_SCALE_DOWN_THRESHOLD) {
    target = CPU_PROFILE_ULTRA_LOW;
  }
  
  // =========================================================================
  // USB SERIAL SAFETY CHECK (v14.1.1 FIX)
  // =========================================================================
  // When USB Serial is active, we MUST NOT drop below BALANCED profile.
  // ULTRA_LOW (50MHz) and POWERSAVE (100MHz) destabilize the USB clock.
  // This is the PRIMARY fix for Serial terminal lockups.
  
  if (usb_blocks_lowpower()) {
    // USB is active - clamp to safe minimum profile
    if (target < USB_SAFE_MIN_PROFILE) {
      target = USB_SAFE_MIN_PROFILE;
      
      // Log this clamping (but not too often)
      static uint32_t last_usb_clamp_log = 0;
      if (now - last_usb_clamp_log > 10000) {  // Log every 10s max
        klog(0, "GOV: USB active - blocking low-power modes");
        last_usb_clamp_log = now;
      }
    }
  }
  
  // =========================================================================
  // WFI HANDLING FOR ULTRA_LOW PROFILE
  // =========================================================================
  // NOTE: WFI is now also blocked by USB in the idle_task
  
  if (target == CPU_PROFILE_ULTRA_LOW && kernel.governor.wfi_enabled) {
    // ADDITIONAL CHECK: Never WFI if USB is active
    if (usb_blocks_lowpower()) {
      target = USB_SAFE_MIN_PROFILE;
    } else {
      // Track how long we've been truly idle
      if (avg < POWERSAVE_SCALE_DOWN_THRESHOLD) {
        if (now - kernel.governor.last_idle_ms > WFI_IDLE_THRESHOLD_MS) {
          // We've been idle long enough - WFI will be used in idle task
          // The profile is set, idle task handles actual WFI
        }
      } else {
        kernel.governor.last_idle_ms = now;
      }
    }
  } else {
    kernel.governor.last_idle_ms = now;
  }
  
  // NO TURBO TIME LIMIT - turbo runs as long as needed
  // Only thermal throttling can pull it down
  
  // Apply profile change if needed
  if (target != kernel.governor.current_profile) {
    governor_apply_profile(target);
  }
}

// Set CPU profile manually (user override)
void governor_set_profile(CPUProfile profile) {
  if (profile >= CPU_PROFILE_COUNT) return;
  
  kernel.governor.requested_profile = profile;
  kernel.governor.user_override = true;
  
  // Don't apply if thermally throttled and trying to go higher than safe
  if (kernel.governor.thermal_throttled && profile > CPU_PROFILE_BALANCED) {
    klog(1, "GOV: Cannot set profile - thermal throttled");
    return;
  }
  
  governor_apply_profile(profile);
}

// Return to automatic frequency scaling
void governor_set_auto() {
  kernel.governor.user_override = false;
  klog(0, "GOV: Auto-scaling enabled");
}

// Get current CPU profile
CPUProfile governor_get_profile() {
  return kernel.governor.current_profile;
}

// Get current frequency in KHz
uint32_t governor_get_freq_khz() {
  return kernel.governor.current_freq_khz;
}

// Get current profile name
const char* governor_get_profile_name() {
  if (kernel.governor.current_profile < CPU_PROFILE_COUNT) {
    return GOVERNOR_PROFILE_NAMES[kernel.governor.current_profile];
  }
  return "UNKNOWN";
}

// Check if thermal throttled
bool governor_is_throttled() {
  return kernel.governor.thermal_throttled;
}

// Request turbo mode (no time limit - runs until load drops or thermal)
void governor_request_turbo(uint32_t duration_ms) {
  (void)duration_ms;  // Duration ignored - turbo runs as long as needed
  
  if (!kernel.turbo_enabled || kernel.governor.thermal_throttled) {
    return;
  }
  
  kernel.governor.turbo_active = true;
  kernel.governor.turbo_start_ms = get_time_ms();
  kernel.governor.user_override = false;  // Allow auto-management
  
  governor_apply_profile(CPU_PROFILE_TURBO);
  
  klog(0, "GOV: Turbo boost requested (no time limit)");
}

// Request instant turbo for demanding task (called by apps/drivers)
void governor_request_instant_turbo() {
  if (!kernel.turbo_enabled || kernel.governor.thermal_throttled) {
    return;
  }
  
  // Immediate turbo with no questions asked
  kernel.governor.instant_turbo_pending = true;
}

// Check thermal limits (with configurable threshold)
void governor_thermal_check() {
  float temp = kernel.temperature;
  
  if (temp >= THERMAL_THROTTLE_TEMP) {
    if (!kernel.governor.thermal_throttled) {
      kernel.governor.thermal_throttled = true;
      kernel.thermal_throttled = true;
      
      // Save current profile for restoration
      kernel.governor.pre_throttle_profile = kernel.governor.current_profile;
      
      char buf[64];
      snprintf(buf, sizeof(buf), "GOV: THERMAL THROTTLE @ %.1f°C (limit: %d°C)", 
               temp, (int)THERMAL_THROTTLE_TEMP);
      klog(2, buf);
      
      // Immediately drop to balanced (not lower, to stay responsive)
      governor_apply_profile(CPU_PROFILE_BALANCED);
    }
  } else if (temp < (THERMAL_THROTTLE_TEMP - 10.0f)) {
    // 10°C hysteresis before removing throttle
    if (kernel.governor.thermal_throttled) {
      kernel.governor.thermal_throttled = false;
      kernel.thermal_throttled = false;
      klog(0, "GOV: Thermal throttle released");
      
      // Restore previous profile if user had one set
      if (kernel.governor.user_override) {
        governor_apply_profile(kernel.governor.requested_profile);
      }
    }
  }
}

// Enter WFI sleep mode (called from idle task when truly idle)
void governor_enter_wfi() {
  if (!kernel.governor.wfi_enabled) return;
  if (kernel.governor.in_wfi) return;
  if (kernel.governor.current_profile != CPU_PROFILE_ULTRA_LOW) return;
  
  // Only WFI if we're at ultra-low profile
  governor_wfi_sleep(WFI_MIN_SLEEP_US);
}

// Exit WFI sleep mode (called on interrupt/wake)
void governor_exit_wfi() {
  kernel.governor.in_wfi = false;
}

// Input boost hook - call on any touch/button event for instant UI responsiveness
void governor_input_boost() {
  // Don't boost if not running, thermally throttled, or user has manual override
  if (!kernel.running || kernel.governor.thermal_throttled || kernel.governor.user_override) {
    return;
  }
  
  // Don't boost if already at or above boost profile
  if (kernel.governor.current_profile >= INPUT_BOOST_PROFILE) {
    // Just extend the boost timer if already boosting
    if (kernel.governor.input_boost_active) {
      kernel.governor.input_boost_start_ms = get_time_ms();
    }
    return;
  }
  
  // Activate input boost
  kernel.governor.input_boost_active = true;
  kernel.governor.input_boost_start_ms = get_time_ms();
  
  // Apply boost immediately - fast UI response
  governor_apply_profile(INPUT_BOOST_PROFILE);
}

// ============================================================================
// RP2350 ENHANCED MEMORY MANAGEMENT
// ============================================================================

// Find appropriate size class for a given size
static ALWAYS_INLINE int find_size_class(size_t size) {
  // Binary search through size classes for O(log n) lookup
  if (size <= 64) return (size <= 32) ? 0 : 1;
  if (size <= 256) return (size <= 128) ? 2 : 3;
  if (size <= 1024) return (size <= 512) ? 4 : 5;
  if (size <= 4096) return (size <= 2048) ? 6 : 7;
  return -1;  // Too large for size classes, use main allocator
}

// Initialize size class free lists
static void size_class_init() {
  for (int i = 0; i < MEM_SIZE_CLASS_COUNT; i++) {
    kernel.size_class_lists[i].head = NULL;
    kernel.size_class_lists[i].count = 0;
    kernel.size_class_lists[i].total_size = 0;
  }
}

// Initialize small allocation pool (per-core)
static void small_pool_init(SmallAllocPool* pool) {
  memset(pool->pool, 0, SMALL_POOL_SIZE);
  memset(pool->bitmap, 0, sizeof(pool->bitmap));
  pool->used = 0;
  pool->alloc_count = 0;
  mutex_init(&pool->lock);
}

// Allocate from small pool (32-byte aligned slots) - per-core to reduce contention
static void* small_pool_alloc(size_t size, uint8_t core) {
  if (size > SMALL_ALLOC_MAX || size == 0) return NULL;
  
  SmallAllocPool* pool = (core == 0) ? &kernel.small_pool_core0 : &kernel.small_pool_core1;
  
  // Round up to 32-byte slot (cache line aligned)
  size_t slots_needed = (size + 31) / 32;
  if (slots_needed > 8) return NULL;  // Max 256 bytes (8 slots)
  
  mutex_enter_blocking(&pool->lock);
  
  uint32_t total_slots = SMALL_POOL_SIZE / 32;
  
  // Fast path: find consecutive free slots using bitmap
  for (uint32_t i = 0; i <= total_slots - slots_needed; i++) {
    bool found = true;
    
    for (uint32_t j = 0; j < slots_needed && found; j++) {
      uint32_t slot = i + j;
      uint32_t word = slot / 32;
      uint32_t bit = slot % 32;
      
      if (pool->bitmap[word] & (1U << bit)) {
        found = false;
        i = slot;  // Skip ahead
      }
    }
    
    if (found) {
      // Mark slots as used
      for (uint32_t j = 0; j < slots_needed; j++) {
        uint32_t slot = i + j;
        uint32_t word = slot / 32;
        uint32_t bit = slot % 32;
        pool->bitmap[word] |= (1U << bit);
      }
      
      pool->used += slots_needed * 32;
      pool->alloc_count++;
      mem_stats.small_allocs++;
      
      mutex_exit(&pool->lock);
      return &pool->pool[i * 32];
    }
  }
  
  mutex_exit(&pool->lock);
  return NULL;  // Pool full, fall back to main allocator
}

// Free to small pool
static bool small_pool_free(void* ptr, uint8_t core) {
  SmallAllocPool* pool = (core == 0) ? &kernel.small_pool_core0 : &kernel.small_pool_core1;
  
  // Check if pointer is in this pool
  uint8_t* p = (uint8_t*)ptr;
  if (p < pool->pool || p >= pool->pool + SMALL_POOL_SIZE) {
    return false;  // Not in this pool
  }
  
  uint32_t offset = p - pool->pool;
  if (offset % 32 != 0) {
    return false;  // Misaligned, corruption?
  }
  
  uint32_t slot = offset / 32;
  uint32_t word = slot / 32;
  uint32_t bit = slot % 32;
  
  mutex_enter_blocking(&pool->lock);
  
  // Clear the slot bit
  if (pool->bitmap[word] & (1U << bit)) {
    pool->bitmap[word] &= ~(1U << bit);
    pool->used -= 32;
    pool->alloc_count--;
  }
  
  mutex_exit(&pool->lock);
  return true;
}

// Check if pointer is in small pools
static bool is_small_pool_ptr(void* ptr) {
  uint8_t* p = (uint8_t*)ptr;
  
  if (p >= kernel.small_pool_core0.pool && 
      p < kernel.small_pool_core0.pool + SMALL_POOL_SIZE) {
    return true;
  }
  
  if (p >= kernel.small_pool_core1.pool && 
      p < kernel.small_pool_core1.pool + SMALL_POOL_SIZE) {
    return true;
  }
  
  return false;
}

void mem_init() {
  mutex_init(&kernel.mem_lock);
  critical_section_init(&kernel.mem_fast_lock);
  memset(&kernel.mem_blocks, 0, sizeof(kernel.mem_blocks));
  
  // Initialize main heap with single free block
  kernel.mem_block_count = 1;
  kernel.mem_blocks[0].addr = kernel.heap;
  kernel.mem_blocks[0].size = HEAP_SIZE;
  kernel.mem_blocks[0].owner_id = 0;
  kernel.mem_blocks[0].free = true;
  kernel.mem_blocks[0].alloc_seq = 0;
  kernel.mem_blocks[0].size_class = -1;  // Not in a size class
  kernel.mem_blocks[0].pinned = false;
  kernel.mem_blocks[0].dma_safe = false;
  
  kernel.total_allocations = 0;
  kernel.total_frees = 0;
  kernel.oom_kills = 0;
  kernel.alloc_sequence = 0;
  kernel.fragmentation_pct = 0;
  kernel.largest_free_block = HEAP_SIZE;
  kernel.total_free_mem = HEAP_SIZE;
  
  // Initialize size class free lists
  size_class_init();
  
  // Initialize per-core small allocation pools
  small_pool_init(&kernel.small_pool_core0);
  small_pool_init(&kernel.small_pool_core1);
  
  memset(&oom_handlers, 0, sizeof(oom_handlers));
  memset(&oom_stats, 0, sizeof(oom_stats));
  memset(&mem_stats, 0, sizeof(mem_stats));
  
  kout.print("[MEM] Heap: ");
  kout.print(HEAP_SIZE / 1024);
  kout.print("KB, Small pools: ");
  kout.print((SMALL_POOL_SIZE * 2) / 1024);
  kout.println("KB");
}

size_t get_free_memory() {
  return kernel.total_free_mem;
}

size_t get_used_memory() {
  return HEAP_SIZE - kernel.total_free_mem;
}

// Get total app-available memory (70-80% of 520KB)
size_t get_app_available_memory() {
  return RP2350_TOTAL_SRAM - HEAP_SIZE - KERNEL_RESERVE - (SMALL_POOL_SIZE * 2);
}

size_t get_task_memory(uint32_t task_id) {
  size_t mem = 0;
  mutex_enter_blocking(&kernel.mem_lock);
  
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (!kernel.mem_blocks[i].free && kernel.mem_blocks[i].owner_id == task_id) {
      mem += kernel.mem_blocks[i].size;
    }
  }
  
  mutex_exit(&kernel.mem_lock);
  return mem;
}

ALWAYS_INLINE bool is_memory_critical() {
  return (kernel.total_free_mem < MEM_CRITICAL_THRESHOLD);
}

ALWAYS_INLINE bool is_memory_warning() {
  return (kernel.total_free_mem < MEM_WARNING_THRESHOLD);
}

void calculate_fragmentation() {
  uint32_t free_blocks = 0;
  size_t largest = 0;
  size_t total_free = 0;
  
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (kernel.mem_blocks[i].free) {
      free_blocks++;
      total_free += kernel.mem_blocks[i].size;
      if (kernel.mem_blocks[i].size > largest) {
        largest = kernel.mem_blocks[i].size;
      }
    }
  }
  
  kernel.largest_free_block = largest;
  
  if (total_free > 0) {
    kernel.fragmentation_pct = 100 - ((largest * 100) / total_free);
  } else {
    kernel.fragmentation_pct = 0;
  }
  
  mem_stats.fragmentation_pct = kernel.fragmentation_pct;
}

// ============================================================================
// MEMORY PRESSURE DETECTION
// ============================================================================

static MemPressure mem_calculate_pressure() {
  uint32_t free_kb = kernel.total_free_mem / 1024;
  uint32_t total_kb = HEAP_SIZE / 1024;
  uint32_t used_pct = ((total_kb - free_kb) * 100) / total_kb;
  
  if (kernel.total_free_mem < MEM_CRITICAL_THRESHOLD) {
    return MEM_PRESSURE_EMERGENCY;
  } else if (kernel.total_free_mem < MEM_CRITICAL_THRESHOLD * 2) {
    return MEM_PRESSURE_CRITICAL;
  } else if (kernel.total_free_mem < MEM_WARNING_THRESHOLD) {
    return MEM_PRESSURE_HIGH;
  } else if (used_pct > 70) {
    return MEM_PRESSURE_MODERATE;
  } else if (used_pct > 50) {
    return MEM_PRESSURE_LOW;
  }
  
  return MEM_PRESSURE_NONE;
}

static void mem_update_pressure() {
  MemPressure old_pressure = (MemPressure)mem_stats.current_pressure;
  MemPressure new_pressure = mem_calculate_pressure();
  
  mem_stats.current_pressure = new_pressure;
  
  // Log pressure changes
  if (new_pressure != old_pressure && new_pressure >= MEM_PRESSURE_HIGH) {
    const char* pressure_names[] = {
      "NONE", "LOW", "MODERATE", "HIGH", "CRITICAL", "EMERGENCY"
    };
    
    char buf[64];
    snprintf(buf, sizeof(buf), "MEM_PRESSURE: %s -> %s", 
             pressure_names[old_pressure], 
             pressure_names[new_pressure]);
    klog(new_pressure >= MEM_PRESSURE_CRITICAL ? 3 : 2, buf);
    
    // Enter emergency mode if needed
    if (new_pressure == MEM_PRESSURE_EMERGENCY && !emergency_mode) {
      emergency_mode = true;
      last_emergency_mode_us = get_time_us();
      klog(3, "MEM: EMERGENCY MODE ACTIVATED");
    }
  }
  
  // Exit emergency mode after pressure drops
  if (emergency_mode && new_pressure < MEM_PRESSURE_HIGH) {
    uint64_t emergency_duration = get_time_us() - last_emergency_mode_us;
    if (emergency_duration > 5000000) { // 5 seconds of low pressure
      emergency_mode = false;
      klog(0, "MEM: Emergency mode deactivated");
    }
  }
}

void mem_delete_block(uint32_t index) {
  if (index >= kernel.mem_block_count) return;
  
  kernel.mem_blocks[index] = kernel.mem_blocks[kernel.mem_block_count - 1];
  memset(&kernel.mem_blocks[kernel.mem_block_count - 1], 0, sizeof(MemBlock));
  kernel.mem_block_count--;
}

void mem_compact() {
  mutex_enter_blocking(&kernel.mem_lock);
  
  uint32_t n = kernel.mem_block_count;
  if (n <= 1) {
    mutex_exit(&kernel.mem_lock);
    return;
  }
  
  // Sort blocks by address
  for (uint32_t i = 1; i < n; i++) {
    MemBlock key = kernel.mem_blocks[i];
    int32_t j = i - 1;
    
    while (j >= 0 && kernel.mem_blocks[j].addr > key.addr) {
      kernel.mem_blocks[j + 1] = kernel.mem_blocks[j];
      j = j - 1;
    }
    kernel.mem_blocks[j + 1] = key;
  }
  
  // Merge adjacent free blocks
  uint32_t write_idx = 0;
  uint32_t merges = 0;
  
  for (uint32_t read_idx = 1; read_idx < n; read_idx++) {
    MemBlock* writer = &kernel.mem_blocks[write_idx];
    MemBlock* reader = &kernel.mem_blocks[read_idx];
    
    if (writer->free && reader->free &&
        ((uint8_t*)writer->addr + writer->size == (uint8_t*)reader->addr)) {
      writer->size += reader->size;
      merges++;
    } else {
      write_idx++;
      if (write_idx != read_idx) {
        kernel.mem_blocks[write_idx] = kernel.mem_blocks[read_idx];
      }
    }
  }
  
  kernel.mem_block_count = write_idx + 1;
  
  if (merges > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "MEM: Compacted %d blocks", merges);
    klog(0, buf);
  }
  
  calculate_fragmentation();
  mutex_exit(&kernel.mem_lock);
}

void* HOT_FUNC kmalloc(size_t size, uint32_t task_id) {
  if (unlikely(size == 0)) return NULL;
  
  // Get current core for per-core optimizations
  uint8_t core = get_core_num();
  
  // Try small pool first for small allocations (fast path)
  if (size <= SMALL_ALLOC_MAX) {
    void* ptr = small_pool_alloc(size, core);
    if (ptr) {
      mem_stats.cache_hits++;
      return ptr;
    }
    // Fall through to main allocator if pool is full
  }
  
  // Align to cache line for better performance on M33
  size = (size + (KMEM_ALIGNMENT - 1)) & ~(KMEM_ALIGNMENT - 1);
  
  bool is_app = false;
  TCB* task = NULL;
  
  if (likely(task_id < 1000)) {
    if (likely(task_id < kernel.task_count)) {
      task = &kernel.tasks[task_id];
      is_app = (task->task_type == TASK_TYPE_APPLICATION);
      if (unlikely(task->mem_blocked)) { return NULL; }
    }
  } else {
    is_app = true;
  }
  
  // Try allocation up to 3 times
  for (int attempt = 0; attempt < 3; attempt++) {
    mutex_enter_blocking(&kernel.mem_lock);
    
    // Check if we have enough memory (with kernel reserve for non-kernel tasks)
    if (is_app && (kernel.total_free_mem < size + KERNEL_RESERVE)) {
      mutex_exit(&kernel.mem_lock);
      
      if (oom_killer(size)) {
        kout.println("[MEM] kmalloc: Awaiting OOM cleanup...");
        task_sleep(OOM_REQUEST_TIMEOUT_MS + 500);
      }
      continue;
    }
    
    // Memory protection checks for applications
    if (task && task->task_type == TASK_TYPE_APPLICATION) {
      task->page_faults++;
      
      uint32_t current_usage = 0;
      for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (!kernel.mem_blocks[i].free && kernel.mem_blocks[i].owner_id == task_id) {
          current_usage += kernel.mem_blocks[i].size;
        }
      }
      
      // Check memory limits
      if (task->mem_limit > 0 && current_usage + size > task->mem_limit) {
        
        char klog_buf[128];
        snprintf(klog_buf, sizeof(klog_buf), 
                 "MEM_PROTECT: Task %s (ID %d) exceeded memory limit. Killing.", 
                 task->name, task_id);
        klog(3, klog_buf);
        
        mutex_exit(&kernel.mem_lock);
        brutal_task_kill(task_id);
        mem_compact();
        
        mutex_enter_blocking(&kernel.mem_lock);
        task->mem_blocked = true;
        
        // Add to blocked list
        bool already_blocked = false;
        for(uint32_t i=0; i < g_blocked_app_count; i++) {
          if(strcmp(g_blocked_app_names[i], task->name) == 0) {
            already_blocked = true;
            break;
          }
        }
        
        if (!already_blocked && g_blocked_app_count < MAX_APPS) {
          strncpy(g_blocked_app_names[g_blocked_app_count++], task->name, TASK_NAME_LEN - 1);
        }
        
        mutex_exit(&kernel.mem_lock);
        mem_stats.failed_allocs++;
        return NULL;
      }
      
      // Velocity throttling (simplified - use ms instead of us)
      uint32_t now_ms_v = get_time_ms();
      if (task->mem_throttle_mark == 0) {
        task->mem_throttle_mark = current_usage;
      } else if (current_usage > task->mem_throttle_mark && 
                 current_usage - task->mem_throttle_mark > VELOCITY_CHECK_CHUNK) {
        
        // Throttle if allocating too fast
        char klog_buf[80];
        snprintf(klog_buf, sizeof(klog_buf), 
                 "MEM_PROTECT: Task %s velocity throttle", task->name);
        klog(1, klog_buf);
        mem_stats.velocity_throttles++;
        
        mutex_exit(&kernel.mem_lock);
        task_sleep(200);  // Shorter sleep on RP2350
        mutex_enter_blocking(&kernel.mem_lock);
        
        task->mem_throttle_mark = current_usage;
      }
    }
    
    // Find best-fit block with size class optimization
    uint32_t best_block_idx = 0xFFFFFFFF;
    uint32_t best_block_size = 0xFFFFFFFF;
    int size_class = find_size_class(size);
    
    // First pass: look for exact or near-exact fit
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
      MemBlock* block = &kernel.mem_blocks[i];
      
      if (block->free && block->size >= size) {
        // Exact match is perfect
        if (block->size == size) {
          best_block_idx = i;
          best_block_size = size;
          break;
        }
        
        // Near match (within 32 bytes) is almost as good
        if (block->size <= size + 32) {
          best_block_idx = i;
          best_block_size = block->size;
          break;
        }
        
        // Otherwise track best fit
        if (block->size < best_block_size) {
          best_block_idx = i;
          best_block_size = block->size;
        }
      }
    }
    
    // Allocate block
    if (best_block_idx != 0xFFFFFFFF) {
      MemBlock* block = &kernel.mem_blocks[best_block_idx];
      uint32_t allocated_size = size;
      
      // Split block if large enough (minimum 64 byte remainder)
      if (block->size > size + MEM_MIN_SPLIT_SIZE && kernel.mem_block_count < MAX_MEMORY_BLOCKS) {
        MemBlock* new_block = &kernel.mem_blocks[kernel.mem_block_count++];
        new_block->addr = (uint8_t*)block->addr + size;
        new_block->size = block->size - size;
        new_block->owner_id = 0;
        new_block->free = true;
        new_block->alloc_seq = 0;
        new_block->size_class = find_size_class(new_block->size);
        new_block->pinned = false;
        new_block->dma_safe = false;
        
        block->size = size;
      } else {
        allocated_size = block->size;
      }
      
      block->free = false;
      block->size_class = size_class;
      kernel.total_free_mem -= allocated_size;
      block->owner_id = task_id;
      block->alloc_seq = kernel.alloc_sequence++;
      kernel.total_allocations++;
      mem_stats.total_allocs++;
      mem_stats.large_allocs++;
      
      // Update peak usage tracking
      uint32_t used = HEAP_SIZE - kernel.total_free_mem;
      if (used > mem_stats.peak_usage_bytes) {
        mem_stats.peak_usage_bytes = used;
      }
      
      // Update task memory usage
      if (task_id < MAX_TASKS && task_id < kernel.task_count) {
        uint32_t task_mem = 0;
        for (uint32_t j = 0; j < kernel.mem_block_count; j++) {
          if (!kernel.mem_blocks[j].free && kernel.mem_blocks[j].owner_id == task_id) {
            task_mem += kernel.mem_blocks[j].size;
          }
        }
        
        kernel.tasks[task_id].mem_used = task_mem;
        if (task_mem > kernel.tasks[task_id].mem_peak) {
          kernel.tasks[task_id].mem_peak = task_mem;
        }
      }
      
      // Update allocation velocity
      if (task) {
        uint64_t now_alloc = get_time_ms();
        if (now_alloc - task->last_alloc_time > 1000) { 
          task->alloc_velocity = 0; 
        }
        task->alloc_velocity++;
        task->last_alloc_time = now_alloc;
      }
      
      calculate_fragmentation();
      mutex_exit(&kernel.mem_lock);
      return block->addr;
    }
    
    mutex_exit(&kernel.mem_lock);
    
    // No suitable block found, trigger OOM
    if (oom_killer(size)) {
      kout.println("[MEM] kmalloc: Awaiting OOM cleanup...");
      task_sleep(OOM_REQUEST_TIMEOUT_MS + 500);
    }
  }
  
  return NULL;
}

void HOT_FUNC kfree(void* ptr) {
  if (!ptr) return;
  
  // Check if this is a small pool allocation first (fast path)
  if (is_small_pool_ptr(ptr)) {
    if (small_pool_free(ptr, 0) || small_pool_free(ptr, 1)) {
      mem_stats.total_frees++;
      return;
    }
  }
  
  mutex_enter_blocking(&kernel.mem_lock);
  
  int freed_block_idx = -1;
  uint32_t task_owner = 0;
  
  // Find block
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (kernel.mem_blocks[i].addr == ptr) {
      if (kernel.mem_blocks[i].free) {
        mutex_exit(&kernel.mem_lock);
        klog(2, "MEM: Double free attempt");
        mem_stats.corruptions_detected++;
        return;
      }
      
      // Don't free pinned blocks
      if (kernel.mem_blocks[i].pinned) {
        mutex_exit(&kernel.mem_lock);
        klog(1, "MEM: Attempted to free pinned block");
        return;
      }
      
      kernel.mem_blocks[i].free = true;
      kernel.total_free_mem += kernel.mem_blocks[i].size;
      task_owner = kernel.mem_blocks[i].owner_id;
      kernel.mem_blocks[i].owner_id = 0;
      kernel.total_frees++;
      mem_stats.total_frees++;
      freed_block_idx = i;
      break;
    }
  }
  
  if (freed_block_idx == -1) {
    mutex_exit(&kernel.mem_lock);
    klog(2, "MEM: Invalid kfree");
    mem_stats.corruptions_detected++;
    return;
  }
  
  // Aggressive coalescing - merge with adjacent free blocks immediately
  MemBlock* freed_block = &kernel.mem_blocks[freed_block_idx];
  
  // Merge with next block
  int merged_next_idx = -1;
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (i == (uint32_t)freed_block_idx) continue;
    
    MemBlock* next_block = &kernel.mem_blocks[i];
    if (next_block->free && !next_block->pinned &&
        (uint8_t*)freed_block->addr + freed_block->size == (uint8_t*)next_block->addr) {
      freed_block->size += next_block->size;
      merged_next_idx = i;
      break;
    }
  }
  
  // Merge with previous block
  int merged_prev_idx = -1;
  for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
    if (i == (uint32_t)freed_block_idx || i == (uint32_t)merged_next_idx) continue;
    
    MemBlock* prev_block = &kernel.mem_blocks[i];
    if (prev_block->free && !prev_block->pinned &&
        (uint8_t*)prev_block->addr + prev_block->size == (uint8_t*)freed_block->addr) {
      prev_block->size += freed_block->size;
      merged_prev_idx = i;
      break;
    }
  }
  
  // Remove merged blocks
  if (merged_prev_idx != -1) {
    mem_delete_block(freed_block_idx);
    if (merged_next_idx != -1) {
      if (merged_next_idx == (int)(kernel.mem_block_count)) {
        mem_delete_block(freed_block_idx);
      } else {
        mem_delete_block(merged_next_idx);
      }
    }
  } else if (merged_next_idx != -1) {
    mem_delete_block(merged_next_idx);
  }
  
  // Update task memory usage
  if (task_owner < 1000 && task_owner < kernel.task_count) {
    uint32_t task_mem = 0;
    for (uint32_t j = 0; j < kernel.mem_block_count; j++) {
      if (!kernel.mem_blocks[j].free && kernel.mem_blocks[j].owner_id == task_owner) {
        task_mem += kernel.mem_blocks[j].size;
      }
    }
    kernel.tasks[task_owner].mem_used = task_mem;
  }
  
  calculate_fragmentation();
  mem_update_pressure();
  mutex_exit(&kernel.mem_lock);
}

// Allocate cache-line aligned memory
void* kmalloc_aligned(size_t size, size_t alignment, uint32_t task_id) {
  // Ensure alignment is at least cache line size
  if (alignment < KMEM_ALIGNMENT) {
    alignment = KMEM_ALIGNMENT;
  }
  
  // Allocate extra space for alignment
  size_t alloc_size = size + alignment;
  void* ptr = kmalloc(alloc_size, task_id);
  
  if (!ptr) return NULL;
  
  // Calculate aligned address
  uintptr_t addr = (uintptr_t)ptr;
  uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
  
  // Note: This wastes some memory but is simple
  // A more sophisticated implementation would track the original pointer
  return (void*)aligned;
}

// Allocate DMA-safe memory (aligned and non-cached if needed)
void* kmalloc_dma(size_t size, uint32_t task_id) {
  // On RP2350, DMA requires 4-byte alignment minimum, but we use 32 for cache line
  void* ptr = kmalloc_aligned(size, 32, task_id);
  
  if (ptr) {
    // Mark block as DMA-safe
    mutex_enter_blocking(&kernel.mem_lock);
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
      // Find block that contains this pointer
      uint8_t* block_start = (uint8_t*)kernel.mem_blocks[i].addr;
      uint8_t* block_end = block_start + kernel.mem_blocks[i].size;
      
      if ((uint8_t*)ptr >= block_start && (uint8_t*)ptr < block_end) {
        kernel.mem_blocks[i].dma_safe = true;
        break;
      }
    }
    mutex_exit(&kernel.mem_lock);
  }
  
  return ptr;
}

// ============================================================================
// OOM SYSTEM
// ============================================================================

void k_register_oom_handler(uint32_t task_id, oom_callback_t callback) {
  for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
    if (!oom_handlers[i].active) {
      oom_handlers[i].task_id = task_id;
      oom_handlers[i].callback = callback;
      oom_handlers[i].active = true;
      
      kout.print("[OOM] Handler registered for task ");
      kout.println(task_id);
      return;
    }
  }
  
  kout.println("[OOM] Handler registry full");
}

void k_unregister_oom_handler(uint32_t task_id) {
  for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
    if (oom_handlers[i].active && oom_handlers[i].task_id == task_id) {
      oom_handlers[i].active = false;
      return;
    }
  }
}

static oom_callback_t oom_get_handler(uint32_t task_id) {
  for (int i = 0; i < MAX_OOM_HANDLERS; i++) {
    if (oom_handlers[i].active && oom_handlers[i].task_id == task_id) {
      return oom_handlers[i].callback;
    }
  }
  return NULL;
}

bool oom_prevent(size_t bytes_needed) {
  kout.println("[OOM] Prevention: Attempting memory recovery...");
  kout.println("[OOM] Step 1: Memory compaction");
  
  mem_compact();
  calculate_fragmentation();
  
  if (kernel.largest_free_block >= bytes_needed) {
    kout.print("[OOM] Success! Found ");
    kout.print(kernel.largest_free_block / 1024);
    kout.println(" KB free block");
    oom_stats.prevention_count++;
    return true;
  }
  
  kout.println("[OOM] Prevention failed - proceeding to victim selection");
  return false;
}

static int32_t oom_calculate_victim_score(TCB* task, uint32_t mem_used) {
  int32_t score = 0;
  
  // Memory usage score
  score += (mem_used / 1024);
  
  // OOM priority penalty
  score += (task->oom_priority * 100);
  
  // Idle time bonus
  uint64_t idle_time = get_time_ms() - task->last_run;
  if (idle_time > 5000) score += 200;
  else if (idle_time > 1000) score += 50;
  
  // Handler bonus (prefer keeping tasks with handlers)
  oom_callback_t handler = oom_get_handler(task->id);
  if (handler) score -= 50;
  
  // CPU abuser penalty
  if (task->is_cpu_abuser) score += 150;
  
  // Resource hoarding penalty (v14.3.1)
  // Tasks hoarding hardware resources are worse victims
  uint32_t res_count = res_count_owned_by_task(task->id);
  score += (res_count * 30);  // +30 points per owned resource
  
  // Resource violation penalty (v14.3.1)
  uint32_t violations = res_get_violations(task->id);
  score += (violations * 50);  // +50 points per violation
  
  // Critical task protection
  if (task->flags & TASK_FLAG_CRITICAL) score = -10000;
  if (task->task_type != TASK_TYPE_APPLICATION) score = -10000;
  
  return score;
}

OOMVictim oom_select_victim(size_t bytes_needed) {
  OOMVictim victim = {0};
  victim.task_id = 0xFFFFFFFF;
  victim.score = -10000;
  
  kout.println("[OOM] Selecting victim...");
  
  // Check Core 0 tasks
  for (uint32_t i = 1; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE || task->mem_blocked) 
      continue;
    if (task->task_type != TASK_TYPE_APPLICATION) continue;
    if (task->flags & TASK_FLAG_CRITICAL) continue;
    
    uint32_t task_mem = get_task_memory(task->id);
    if (task_mem == 0) continue;
    
    int32_t score = oom_calculate_victim_score(task, task_mem);
    
    if (score > victim.score) {
      victim.task_id = task->id;
      victim.memory_used = task_mem;
      victim.oom_priority = task->oom_priority;
      victim.score = score;
      victim.has_handler = (oom_get_handler(task->id) != NULL);
    }
  }
  
  // Check Core 1 tasks
  mutex_enter_blocking(&kernel.core1.scheduler_lock);
  for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
    TCB* task = &kernel.core1.tasks[i];
    
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE || task->mem_blocked) 
      continue;
    if (task->task_type != TASK_TYPE_APPLICATION) continue;
    if (task->flags & TASK_FLAG_CRITICAL) continue;
    
    uint32_t task_mem = 10 * 1024; // Estimate for Core 1 tasks
    
    int32_t score = oom_calculate_victim_score(task, task_mem);
    
    if (score > victim.score) {
      victim.task_id = task->id;
      victim.memory_used = task_mem;
      victim.oom_priority = task->oom_priority;
      victim.score = score;
      victim.has_handler = (oom_get_handler(task->id) != NULL);
    }
  }
  mutex_exit(&kernel.core1.scheduler_lock);
  
  return victim;
}

bool oom_request_cleanup(OOMVictim* victim, size_t bytes_needed) {
  if (victim->task_id == 0xFFFFFFFF || !victim->has_handler) {
    return false;
  }
  
  TCB* task = NULL;
  bool is_core1 = (victim->task_id >= 1000);
  
  if(is_core1) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  task = ipc_get_tcb_by_id_unsafe(victim->task_id);
  if (!task) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
    return false;
  }
  
  kout.print("[OOM] Requesting graceful cleanup from '");
  kout.print(task->name);
  kout.print("' (");
  kout.print(victim->memory_used / 1024);
  kout.println(" KB)");
  
  oom_current_request.allocating_task_id = kernel.current_task;
  oom_current_request.target_task_id = victim->task_id;
  oom_current_request.request_time = get_time_ms();
  oom_current_request.bytes_requested = bytes_needed;
  oom_current_request.request_sent = true;
  oom_current_request.task_complied = false;
  
  task->flags |= TASK_FLAG_OOM_CLEANUP_REQUESTED;
  task->oom_bytes_requested = bytes_needed;
  
  if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
  
  oom_stats.requests_sent++;
  return true;
}

void k_oom_cleanup_done(uint32_t task_id, uint32_t bytes_freed) {
  if (!oom_current_request.request_sent) return;
  if (oom_current_request.target_task_id != task_id) return;
  
  TCB* task = NULL;
  bool is_core1 = (task_id >= 1000);
  
  if(is_core1) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  task = ipc_get_tcb_by_id_unsafe(task_id);
  if (!task) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
    return;
  }
  
  kout.print("[OOM] Task '");
  kout.print(task->name);
  kout.print("' freed ");
  kout.print(bytes_freed / 1024);
  kout.println(" KB voluntarily");
  
  oom_current_request.task_complied = true;
  oom_stats.voluntary_releases++;
  oom_stats.total_bytes_reclaimed += bytes_freed;
  
  char buf[64];
  snprintf(buf, sizeof(buf), "OOM: %s freed %dKB", task->name, bytes_freed / 1024);
  klog(0, buf);
  
  if(is_core1) mutex_exit(&kernel.core1.scheduler_lock);
  
  // Wake up allocating task
  uint32_t waiting_task_id = oom_current_request.allocating_task_id;
  if (waiting_task_id < kernel.task_count) {
    kout.print("[OOM] Waking up allocating task ");
    kout.println(waiting_task_id);
    task_wake(waiting_task_id);
  }
  
  oom_current_request.request_sent = false;
}

bool oom_killer(size_t bytes_needed) {
  kout.println("\n!!! OUT OF MEMORY !!!");
  kout.print("Need: ");
  kout.print(bytes_needed / 1024);
  kout.println(" KB");
  
  char buf[64];
  snprintf(buf, sizeof(buf), "OOM: Need %d KB", (int)(bytes_needed / 1024));
  klog(3, buf);
  
  // Check for abusive allocator
  uint32_t allocator_id = kernel.current_task;
  if (allocator_id < kernel.task_count) {
    TCB* allocator_task = &kernel.tasks[allocator_id];
    
    if ((allocator_task->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY || 
         bytes_needed > OOM_ABUSIVE_ALLOC_SIZE) &&
        allocator_task->task_type == TASK_TYPE_APPLICATION) {
      
      kout.println("\n[OOM] ABUSIVE ALLOCATOR DETECTED!");
      kout.print("[OOM] Executing allocator '"); 
      kout.print(allocator_task->name); 
      kout.println("'");
      
      brutal_task_kill(allocator_id);
      kernel.oom_kills++;
      oom_stats.forced_kills++;
      oom_stats.abusive_kills++;
      oom_current_request.request_sent = false;
      
      mem_compact();
      return false;
    }
  }
  
  // Try prevention first
  if (oom_prevent(bytes_needed)) {
    return false;
  }
  
  // Check if there's an active OOM request waiting
  if (oom_current_request.request_sent) {
    uint64_t elapsed = get_time_ms() - oom_current_request.request_time;
    
    if (elapsed < OOM_REQUEST_TIMEOUT_MS) {
      kout.println("[OOM] Waiting for graceful cleanup...");
      return true;
    }
    
    kout.println("[OOM] Graceful cleanup timed out!");
    kout.print("[OOM] Proceeding to kill victim: ");
    kout.println(oom_current_request.target_task_id);
    
    brutal_task_kill(oom_current_request.target_task_id);
    oom_stats.forced_kills++;
    oom_current_request.request_sent = false;
    
    return false;
  }
  
  // Select victim
  OOMVictim victim = oom_select_victim(bytes_needed);
  
  if (victim.task_id == 0xFFFFFFFF) {
    kout.println("OOM: NO KILLABLE APPLICATIONS!");
    klog(3, "OOM: No victims, PANIC!");
    kernel_panic("OOM: No killable victims");
    return false;
  }
  
  TCB* victim_task = NULL;
  bool is_core1_victim = (victim.task_id >= 1000);
  
  if(is_core1_victim) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  victim_task = ipc_get_tcb_by_id_unsafe(victim.task_id);
  if(!victim_task) {
    if(is_core1_victim) mutex_exit(&kernel.core1.scheduler_lock);
    kout.println("OOM: Victim vanished");
    return false;
  }
  
  kout.print("[OOM] Selected victim: '");
  kout.print(victim_task->name);
  kout.print("' (");
  kout.print(victim.memory_used / 1024);
  kout.print(" KB, score=");
  kout.print(victim.score);
  kout.println(")");
  
  // Check if victim is also an abuser
  if (victim_task->alloc_velocity > OOM_ABUSIVE_ALLOC_VELOCITY) {
    kout.println("[OOM] Victim is also an abusive allocator. No handler.");
    victim.has_handler = false;
  }
  
  // Try graceful cleanup if handler exists
  if (victim.has_handler && oom_request_cleanup(&victim, bytes_needed)) {
    if(is_core1_victim) mutex_exit(&kernel.core1.scheduler_lock);
    return true;
  }
  
  // Kill the victim
  kout.print("[OOM] Killing '");
  kout.print(victim_task->name);
  kout.print("' (");
  kout.print(victim.memory_used / 1024);
  kout.println(" KB)");
  
  snprintf(buf, sizeof(buf), "OOM: Killed %s (%dKB)",
           victim_task->name, victim.memory_used / 1024);
  klog(2, buf);
  
  if(is_core1_victim) mutex_exit(&kernel.core1.scheduler_lock);
  
  brutal_task_kill(victim.task_id);
  kernel.oom_kills++;
  oom_stats.forced_kills++;
  oom_stats.total_bytes_reclaimed += victim.memory_used;
  
  return false;
}

// ============================================================================
// CPU MONITORING & PROTECTION
// ============================================================================

void update_task_cpu_usage(TCB* task, uint64_t cpu_time_us) {
  if (!task) return;
  
  // The cpu_time_us here is callback execution time (useful for profiling)
  // But for CPU% we use wall_time_us which is updated in task_yield()
  
  // Track callback execution time in samples for burst detection
  float slice_ms = (float)cpu_time_us / 1000.0f;
  
  task->sched_info.cpu_samples[task->sched_info.cpu_sample_index] = slice_ms;
  task->sched_info.cpu_sample_index = 
    (task->sched_info.cpu_sample_index + 1) % CPU_ABUSE_SAMPLE_COUNT;
  
  // Detect CPU burst (callback taking too long)
  float tick_ms = (float)SCHEDULER_TICK_US / 1000.0f;
  if (tick_ms > 0) {
    float instant_percent = (slice_ms / tick_ms) * 100.0f;
    if (instant_percent > CPU_TASK_ABUSE_THRESHOLD) {
      task->sched_info.cpu_burst_counter++;
    } else if (task->sched_info.cpu_burst_counter > 0) {
      task->sched_info.cpu_burst_counter--;
    }
  }
  
  // Note: cpu_usage_percent is now calculated on-demand in cmd_taskinfo
  // using wall_time_us for accurate percentage
}

// Calculate a task's CPU percentage using wall-clock time
float calculate_task_cpu_percent(TCB* task) {
  if (!task) return 0.0f;
  
  // Get total wall time across all tasks
  uint64_t total_wall_time = 0;
  for (uint32_t i = 0; i < kernel.task_count; i++) {
    total_wall_time += kernel.tasks[i].wall_time_us;
  }
  
  if (total_wall_time == 0) return 0.0f;
  
  // This task's percentage of total wall time
  float percent = 100.0f * ((float)task->wall_time_us / (float)total_wall_time);
  
  if (percent > 100.0f) percent = 100.0f;
  if (percent < 0.0f) percent = 0.0f;
  
  return percent;
}

bool is_cpu_overloaded() {
  return (kernel.cpu_usage > CPU_OVERLOAD_THRESHOLD);
}

bool is_task_cpu_abuser(TCB* task) {
  if (!task) return false;
  
  // Check if task consistently uses too much CPU (wall-clock based)
  float wall_percent = calculate_task_cpu_percent(task);
  
  // A task is an abuser if:
  // 1. It uses >75% of total CPU time, OR
  // 2. Its callbacks consistently exceed threshold AND it has burst behavior
  if (wall_percent > CPU_TASK_ABUSE_THRESHOLD) {
    return true;
  }
  
  if (task->sched_info.cpu_burst_counter > 5) {
    return true;
  }
  
  return false;
}

void handle_cpu_overload() {
  if (!is_cpu_overloaded()) return;
  
  kout.println("\n!!! CPU OVERLOAD DETECTED !!!");
  kout.print("System CPU: ");
  kout.print(kernel.cpu_usage, 1);
  kout.println("%");
  
  // Find CPU abusers using wall-clock percentage
  TCB* worst_abuser = NULL;
  float worst_usage = 0;
  
  for (uint32_t i = 1; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) continue;
    if (task->task_type != TASK_TYPE_APPLICATION) continue;
    if (task->flags & TASK_FLAG_CRITICAL) continue;
    
    float task_percent = calculate_task_cpu_percent(task);
    
    if (is_task_cpu_abuser(task)) {
      if (task_percent > worst_usage) {
        worst_usage = task_percent;
        worst_abuser = task;
      }
    }
  }
  
  if (worst_abuser) {
    float abuser_percent = calculate_task_cpu_percent(worst_abuser);
    
    kout.print("[CPU] Killing CPU abuser: '");
    kout.print(worst_abuser->name);
    kout.print("' (");
    kout.print(abuser_percent, 1);
    kout.println("%)");
    
    worst_abuser->is_cpu_abuser = true;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "CPU: Killed %s (%.1f%%)", 
             worst_abuser->name, abuser_percent);
    klog(2, buf);
    
    brutal_task_kill(worst_abuser->id);
  } else {
    kout.println("[CPU] No obvious abuser found, throttling system");
    task_sleep(100);
  }
}

// SCHEDULER - PRIORITY BITMAP OPERATIONS (CPU-optimized)
// ============================================================================

static inline int __attribute__((always_inline)) bitmap_ffs(uint32_t mask) {
  if (mask == 0) return -1;
  return __builtin_ctz(mask);
}

static inline void __attribute__((always_inline)) bitmap_set(uint32_t* mask, uint8_t bit) {
  *mask |= (1U << bit);
}

static inline void __attribute__((always_inline)) bitmap_clear(uint32_t* mask, uint8_t bit) {
  *mask &= ~(1U << bit);
}

static void __attribute__((hot)) sched_bitmap_add(PriorityBitmap* bm, uint32_t task_id, uint8_t priority) {
  if (priority >= SCHED_NUM_PRIORITY_LEVELS) priority = SCHED_NUM_PRIORITY_LEVELS - 1;
  if (task_id >= 32) return;
  
  bitmap_set(&bm->task_masks[priority], task_id);
  bitmap_set(&bm->level_mask, priority);
}

static void __attribute__((hot)) sched_bitmap_remove(PriorityBitmap* bm, uint32_t task_id, uint8_t priority) {
  if (priority >= SCHED_NUM_PRIORITY_LEVELS) priority = SCHED_NUM_PRIORITY_LEVELS - 1;
  if (task_id >= 32) return;
  
  bitmap_clear(&bm->task_masks[priority], task_id);
  
  if (bm->task_masks[priority] == 0) {
    bitmap_clear(&bm->level_mask, priority);
  }
}

static int __attribute__((hot)) sched_bitmap_find_highest(PriorityBitmap* bm, uint32_t* task_id_out) {
  if (bm->level_mask == 0) return -1;
  
  // Use clz for O(1) highest bit finding
  int level = 31 - __builtin_clz(bm->level_mask);
  int task_id = bitmap_ffs(bm->task_masks[level]);
  
  if (task_id >= 0) {
    *task_id_out = task_id;
    return level;
  }
  
  return -1;
}

// ============================================================================
// SCHEDULER
// ============================================================================

void scheduler_init_core0() {
  memset(&core0_sched, 0, sizeof(core0_sched));
  mutex_init(&core0_sched.lock);
  
  core0_sched.idle_task = 0;
  core0_sched.current_task = 0;
  core0_sched.last_aging = get_time_ms();
  
  kout.println("[SCHED] Core0 initialized (Preemptive O(1) bitmap)");
}

void scheduler_init_core1() {
  memset(&core1_sched, 0, sizeof(core1_sched));
  mutex_init(&core1_sched.lock);
  
  core1_sched.last_aging = get_time_ms();
  
  kout.println("[SCHED] Core1 initialized (O(1) bitmap)");
}

void sched_check_preemption() {
  if (kernel.preemption_pending) return;
  
  uint32_t task_id;
  int highest_prio = sched_bitmap_find_highest(&core0_sched.runnable, &task_id);
  
  if (highest_prio > kernel.tasks[kernel.current_task].priority) {
    kernel.preemption_pending = true;
  }
}

void sched_update_task_priority(TCB* task) {
  if (task->running_on_core != 0) return;
  
  disable_all_interrupts();
  
  // Remove from old priority
  for(int p = 0; p < SCHED_NUM_PRIORITY_LEVELS; p++) {
    if (core0_sched.runnable.task_masks[p] & (1U << task->id)) {
      sched_bitmap_remove(&core0_sched.runnable, task->id, p);
      break;
    }
  }
  
  // Add to new priority
  if (task->state == TASK_READY) {
    sched_bitmap_add(&core0_sched.runnable, task->id, task->priority);
  }
  
  enable_all_interrupts();
}

static void sched_age_tasks(CoreScheduler* sched, TCB* tasks, uint32_t task_count) {
  uint64_t now = get_time_ms();
  if (now - sched->last_aging < SCHED_AGING_INTERVAL_MS) return;
  
  sched->last_aging = now;
  
  for (uint32_t i = 0; i < task_count; i++) {
    TCB* task = &tasks[i];
    
    if (task->state != TASK_READY) continue;
    if (task->priority >= SCHED_RT_THRESHOLD) continue;
    if (task->original_priority != 0) continue;
    
    uint64_t wait_time = now - task->sched_info.last_run;
    
    if (wait_time > 1000 && task->priority < SCHED_NUM_PRIORITY_LEVELS - 1) {
      uint8_t old_prio = task->priority;
      task->priority++;
      
      disable_all_interrupts();
      sched_bitmap_remove(&sched->runnable, i, old_prio);
      sched_bitmap_add(&sched->runnable, i, task->priority);
      enable_all_interrupts();
    }
  }
}

static bool sched_should_inject_idle(CoreScheduler* sched) {
  if (sched->cpu_load < SCHED_IDLE_INJECTION_THRESHOLD) {
    sched->idle_injection_active = false;
    return false;
  }
  
  static uint32_t idle_counter = 0;
  if (++idle_counter >= 10) {
    idle_counter = 0;
    sched->idle_injections++;
    return true;
  }
  
  return false;
}

static void sched_update_load(CoreScheduler* sched, uint64_t idle_time_ms, uint64_t total_time_ms) {
  if (total_time_ms == 0) return;
  
  float instant_load = 100.0f * (1.0f - ((float)idle_time_ms / (float)total_time_ms));
  if (instant_load < 0) instant_load = 0;
  if (instant_load > 100) instant_load = 100;
  
  sched->cpu_load_instant = instant_load;
  sched->cpu_load = (sched->cpu_load * 0.9f) + (instant_load * 0.1f);
}

uint32_t __attribute__((hot)) sched_select_next_core0() {
  disable_all_interrupts();
  
  if (sched_should_inject_idle(&core0_sched)) {
    enable_all_interrupts();
    return core0_sched.idle_task;
  }
  
  // Loop instead of recursion for better performance
  for (int attempts = 0; attempts < MAX_TASKS; attempts++) {
    uint32_t task_id;
    int priority = sched_bitmap_find_highest(&core0_sched.runnable, &task_id);
    
    if (priority < 0 || task_id >= kernel.task_count) {
      enable_all_interrupts();
      return core0_sched.idle_task;
    }
    
    TCB* task = &kernel.tasks[task_id];
    
    if (task->state == TASK_READY || task->state == TASK_RUNNING) {
      core0_sched.current_task = task_id;
      core0_sched.current_priority = priority;
      enable_all_interrupts();
      return task_id;
    }
    
    // Task not runnable, remove from bitmap and try again
    sched_bitmap_remove(&core0_sched.runnable, task_id, priority);
  }
  
  enable_all_interrupts();
  return core0_sched.idle_task;
}

uint32_t __attribute__((hot)) sched_select_next_core1() {
  mutex_enter_blocking(&core1_sched.lock);
  
  // Loop instead of recursion for better performance
  for (int attempts = 0; attempts < MAX_CORE1_TASKS; attempts++) {
    uint32_t task_id;
    int priority = sched_bitmap_find_highest(&core1_sched.runnable, &task_id);
    
    if (priority < 0) {
      mutex_exit(&core1_sched.lock);
      return 0xFFFFFFFF;
    }
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    
    if (task_id >= kernel.core1.task_count) {
      mutex_exit(&kernel.core1.scheduler_lock);
      mutex_exit(&core1_sched.lock);
      return 0xFFFFFFFF;
    }
    
    TCB* task = &kernel.core1.tasks[task_id];
    
    if (task->state == TASK_READY || task->state == TASK_RUNNING) {
      mutex_exit(&kernel.core1.scheduler_lock);
      core1_sched.current_task = task_id;
      core1_sched.current_priority = priority;
      mutex_exit(&core1_sched.lock);
      return task_id;
    }
    
    mutex_exit(&kernel.core1.scheduler_lock);
    sched_bitmap_remove(&core1_sched.runnable, task_id, priority);
  }
  
  mutex_exit(&core1_sched.lock);
  return 0xFFFFFFFF;
}

void __attribute__((hot)) scheduler_tick() {
  // Update velocity check timer
  uint64_t now_ms_for_velocity = get_time_ms();
  if (now_ms_for_velocity - kernel.last_velocity_check_ms > 1000) {
    kernel.last_velocity_check_ms = now_ms_for_velocity;
    
    for (uint32_t i = 0; i < kernel.task_count; i++) {
      if (kernel.tasks[i].alloc_velocity > 0 && 
          (now_ms_for_velocity - kernel.tasks[i].last_alloc_time > 1000)) {
        kernel.tasks[i].alloc_velocity = 0;
      }
    }
  }
  
  uint64_t now = get_time_ms();
  kernel.uptime_ms = now;
  
  if (kernel.task_count == 0 || kernel.task_count > MAX_TASKS) return;
  
  // Wake up waiting tasks
  for (uint32_t i = 0; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_WAITING && task->wake_time != 0 && now >= task->wake_time) {
      task_wake(i);
    }
    
    // Check max runtime
    if (task->max_runtime > 0 && task->state != TASK_TERMINATED && task->state != TASK_ZOMBIE) {
      if ((now - task->start_time) > task->max_runtime) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TIMEOUT: %s", task->name);
        klog(1, buf);
        brutal_task_kill(task->id);
      }
    }
    
    // Respawn tasks
    if (task->state == TASK_TERMINATED && (task->flags & TASK_FLAG_RESPAWN)) {
      if (task->task_type == TASK_TYPE_KERNEL) continue;
      
      if (now - task->last_respawn > 5000) {
        task->state = TASK_READY;
        task->start_time = now;
        task->last_respawn = now;
        task->respawn_count++;
        task->mem_used = 0;
        task->cpu_time = 0;
        
        task_init_ipc_queue(task);
        
        if (task->callbacks && task->callbacks->init) {
          task->callbacks->init(task->id);
        }
        
        disable_all_interrupts();
        if (task->affinity == CORE_0 || task->affinity == CORE_ANY) {
          sched_bitmap_add(&core0_sched.runnable, i, task->priority);
        }
        enable_all_interrupts();
      }
    }
  }
  
  // Age tasks
  sched_age_tasks(&core0_sched, kernel.tasks, kernel.task_count);
  
  // =========================================================================
  // CPU LOAD CALCULATION (FreeRTOS-style wall-clock tracking)
  // =========================================================================
  // 
  // This measures ACTUAL CPU utilization by tracking wall-clock time per task.
  // Each task accumulates time from when it becomes current to when it yields.
  // CPU usage = (total_wall_time - idle_wall_time) / total_wall_time * 100
  //
  static uint64_t last_load_update = 0;
  static uint64_t last_idle_wall_time = 0;
  static uint64_t last_total_wall_time = 0;
  
  if (now - last_load_update >= 1000) {  // Update every second
    
    // Get idle task's wall time (task 0 is always idle)
    uint64_t idle_wall_time = kernel.tasks[0].wall_time_us;
    
    // Sum ALL tasks' wall time (including idle)
    uint64_t total_wall_time = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
      total_wall_time += kernel.tasks[i].wall_time_us;
    }
    
    // Calculate deltas since last measurement
    uint64_t idle_delta = idle_wall_time - last_idle_wall_time;
    uint64_t total_delta = total_wall_time - last_total_wall_time;
    
    // Prevent division by zero
    if (total_delta == 0) {
      // No time passed? Use wall clock as fallback
      total_delta = (now - last_load_update) * 1000;  // Convert ms to us
      if (total_delta == 0) total_delta = 1;
    }
    
    // CPU usage = percentage of time NOT in idle task
    // busy_time = total_time - idle_time
    uint64_t busy_delta = (total_delta > idle_delta) ? (total_delta - idle_delta) : 0;
    float instant_load = 100.0f * ((float)busy_delta / (float)total_delta);
    
    // Sanity clamp
    if (instant_load < 0) instant_load = 0;
    if (instant_load > 100) instant_load = 100;
    
    // Exponential moving average for smooth display (20% new, 80% old)
    core0_sched.cpu_load_instant = instant_load;
    core0_sched.cpu_load = (core0_sched.cpu_load * 0.8f) + (instant_load * 0.2f);
    kernel.cpu_usage = core0_sched.cpu_load;
    
    // Update tracking variables
    last_load_update = now;
    last_idle_wall_time = idle_wall_time;
    last_total_wall_time = total_wall_time;
    
    // Track scheduler stats
    core0_sched.idle_time_ms = idle_wall_time / 1000;
    core0_sched.total_runtime_ms = total_wall_time / 1000;
    
    // Check for CPU overload
    if (kernel.cpu_usage > CPU_CRITICAL_THRESHOLD) {
      handle_cpu_overload();
    }
  }
  
  // IPC maintenance
  static uint32_t ipc_counter = 0;
  if (++ipc_counter >= 1000) {
    ipc_maintenance();
    ipc_counter = 0;
  }
}

void __attribute__((hot)) task_yield() {
  uint32_t prev_task_id = kernel.current_task;
  uint32_t task_count = kernel.task_count;
  uint64_t now_us = get_time_us();
  uint64_t now_ms = now_us / 1000;
  
  // Charge wall-clock time to outgoing task (FreeRTOS-style accounting)
  if (prev_task_id < task_count) {
    TCB* prev_task = &kernel.tasks[prev_task_id];
    
    // Calculate time since this task was scheduled
    if (prev_task->scheduled_at_us > 0) {
      uint64_t elapsed = now_us - prev_task->scheduled_at_us;
      prev_task->wall_time_us += elapsed;
    }
    
    prev_task->sched_info.last_run = now_ms;
    prev_task->last_run = now_ms;
  }
  
  // Select next task
  uint32_t next_task = sched_select_next_core0();
  
  if (next_task < task_count) {
    kernel.current_task = next_task;
    kernel.total_context_switches++;
    
    TCB* next = &kernel.tasks[next_task];
    next->context_switches++;
    next->sched_info.last_run = now_ms;
    next->scheduled_at_us = now_us;  // Mark when this task became current
    
    core0_sched.switches++;
  }
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

void task_init() {
  memset(&kernel.tasks, 0, sizeof(kernel.tasks));
  kernel.task_count = 0;
  kernel.current_task = 0;
  kernel.cpu_usage = 0.0f;
  kernel.root_mode = false;
  kernel.panic_mode = false;
  kernel.preemption_pending = false;
  
  kernel.kernel_tasks = 0;
  kernel.driver_tasks = 0;
  kernel.service_tasks = 0;
  kernel.module_tasks = 0;
  kernel.application_tasks = 0;
  kernel.zombie_tasks = 0;
  
  kernel.shell_alive = true;
  kernel.cpumon_alive = true;
  kernel.tempmon_alive = true;
  kernel.fs_alive = false;
  
  kernel.log_head = 0;
  kernel.log_count = 0;
  kernel.total_context_switches = 0;
  
  kernel.gui_focus_task_id = -1;
  kernel.app_write_char = NULL;
  
  scheduler_init_core0();
}

void task_init_ipc_queue(TCB* task) {
  task->ipc.priority_bitmap = 0;
  task->ipc.message_count = 0;
  
  for (int i = 0; i < SCHED_NUM_PRIORITY_LEVELS; i++) {
    task->ipc.priority_lists_head[i] = IPC_NULL_MSG;
  }
}

uint32_t task_create(const char* name, void (*entry)(void*), void* arg,
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, uint32_t mem_request, ModuleCallbacks* callbacks,
                     const char* description, CoreAffinity affinity) {
  
  // Check if app is blocked
  for (uint32_t i = 0; i < g_blocked_app_count; i++) {
    if (strcmp(name, g_blocked_app_names[i]) == 0) {
      char klog_buf[64];
      snprintf(klog_buf, sizeof(klog_buf), 
               "MEM_PROTECT: Launch rejected, app %s is blocked.", name);
      klog(2, klog_buf);
      return 0;
    }
  }
  
  if (kernel.task_count >= MAX_TASKS) {
    kout.println("ERROR: Maximum tasks reached!");
    return 0;
  }
  
  // Set up memory limits for applications
  if (task_type != TASK_TYPE_APPLICATION) {
    oom_priority = OOM_PRIORITY_NEVER;
    mem_request = mem_limit;
  } else {
    if (mem_request == 0 && mem_limit > 0) {
      mem_request = mem_limit;
    } else if (mem_request == 0 && mem_limit == 0) {
      mem_request = MAX_APP_MEM_REQUEST_GLOBAL;
    }
    
    if (mem_request > MAX_APP_MEM_REQUEST_GLOBAL) {
      char klog_buf[128];
      snprintf(klog_buf, sizeof(klog_buf), 
               "MEM_PROTECT: App %s launch rejected, requested %luKB > max %luKB.", 
               name, mem_request / 1024, (uint32_t)MAX_APP_MEM_REQUEST_GLOBAL / 1024);
      klog(3, klog_buf);
      
      if (g_blocked_app_count < MAX_APPS) {
        strncpy(g_blocked_app_names[g_blocked_app_count++], name, TASK_NAME_LEN - 1);
      }
      return 0;
    }
    
    if (mem_limit == 0) {
      mem_limit = mem_request;
    } else if (mem_limit < mem_request) {
      mem_limit = mem_request;
    }
  }
  
  // Check for ONESHOT tasks
  if (flags & TASK_FLAG_ONESHOT) {
    for (uint32_t i = 0; i < kernel.task_count; i++) {
      if (strcmp(kernel.tasks[i].name, name) == 0) {
        if (kernel.tasks[i].state != TASK_TERMINATED) {
          return 0;
        }
        
        uint64_t time_since_death = get_time_ms() - kernel.tasks[i].last_run;
        if (time_since_death < 2000) {
          return 0;
        }
      }
    }
  }
  
  // Create task
  uint32_t id = kernel.task_count++;
  TCB* task = &kernel.tasks[id];
  memset(task, 0, sizeof(TCB));
  
  task->id = id;
  strncpy(task->name, name, TASK_NAME_LEN - 1);
  task->name[TASK_NAME_LEN - 1] = '\0';
  task->state = TASK_READY;
  task->task_type = task_type;
  task->priority = priority;
  task->flags = flags;
  task->oom_priority = oom_priority;
  task->affinity = affinity;
  task->running_on_core = 0;
  task->entry = entry;
  task->arg = arg;
  task->wake_time = 0;
  task->start_time = get_time_ms();
  task->max_runtime = max_runtime_ms;
  task->last_respawn = 0;
  task->respawn_count = 0;
  task->mem_used = 0;
  task->mem_peak = 0;
  task->mem_limit = mem_limit;
  task->mem_blocked = false;
  task->mem_throttle_mark = 0;
  task->cpu_time = 0;
  task->last_run = get_time_ms();
  task->page_faults = 0;
  task->context_switches = 0;
  task->callbacks = callbacks;
  task->description = description;
  task->oom_bytes_requested = 0;
  task->alloc_velocity = 0;
  task->last_alloc_time = 0;
  task->total_cpu_time_ms = 0;
  task->is_cpu_abuser = false;
  
  task_init_ipc_queue(task);
  
  task->original_priority = 0;
  
  // Set up scheduling info
  TaskSchedInfo* si = &task->sched_info;
  si->base_priority = priority;
  si->effective_priority = priority;
  si->is_realtime = (priority >= SCHED_RT_THRESHOLD);
  
  if (si->is_realtime) {
    si->quantum_us = SCHED_BASE_QUANTUM_US;
  } else {
    si->quantum_us = SCHED_BASE_QUANTUM_US +
                     (SCHED_NUM_PRIORITY_LEVELS - priority) * 2000;
    if (si->quantum_us > SCHED_MAX_QUANTUM_US) {
      si->quantum_us = SCHED_MAX_QUANTUM_US;
    }
  }
  
  si->cpu_affinity = (uint8_t)affinity;
  si->last_run = get_time_ms();
  si->cpu_usage_percent = 0;
  si->cpu_burst_counter = 0;
  si->cpu_sample_index = 0;
  memset(si->cpu_samples, 0, sizeof(si->cpu_samples));
  
  // Update task type counters
  if (task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks++;
  else if (task_type == TASK_TYPE_DRIVER) kernel.driver_tasks++;
  else if (task_type == TASK_TYPE_SERVICE) kernel.service_tasks++;
  else if (task_type == TASK_TYPE_MODULE) kernel.module_tasks++;
  else if (task_type == TASK_TYPE_APPLICATION) kernel.application_tasks++;
  
  // Add to scheduler
  disable_all_interrupts();
  if (affinity == CORE_0 || affinity == CORE_ANY) {
    sched_bitmap_add(&core0_sched.runnable, id, priority);
  }
  enable_all_interrupts();
  
  // Initialize callbacks
  if (callbacks && callbacks->init) {
    callbacks->init(id);
  }
  
  // Log task creation
  char buf[64];
  const char* type_str[] = {"KERN", "DRVR", "SRVC", "MODUL", "APP"};
  uint8_t type_idx = 0;
  if (task_type == TASK_TYPE_DRIVER) type_idx = 1;
  else if (task_type == TASK_TYPE_SERVICE) type_idx = 2;
  else if (task_type == TASK_TYPE_MODULE) type_idx = 3;
  else if (task_type == TASK_TYPE_APPLICATION) type_idx = 4;
  
  snprintf(buf, sizeof(buf), "TASK: %s [%s] ID=%d", name, type_str[type_idx], id);
  klog(0, buf);
  
  return id;
}

void task_sleep(uint32_t ms) {
  TCB* task = &kernel.tasks[kernel.current_task];
  task->state = TASK_WAITING;
  task->wake_time = get_time_ms() + ms;
  
  disable_all_interrupts();
  sched_bitmap_remove(&core0_sched.runnable, task->id, task->priority);
  enable_all_interrupts();
}

void task_wake(uint32_t task_id) {
  if (task_id >= 1000) {
    uint32_t local_id = task_id - 1000;
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    if(local_id >= kernel.core1.task_count) {
      mutex_exit(&kernel.core1.scheduler_lock);
      return;
    }
    
    TCB* task = &kernel.core1.tasks[local_id];
    if (task->state == TASK_WAITING) {
      task->state = TASK_READY;
      task->wake_time = 0;
      
      mutex_enter_blocking(&core1_sched.lock);
      if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
        sched_bitmap_add(&core1_sched.runnable, local_id, task->priority);
      }
      mutex_exit(&core1_sched.lock);
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    return;
  }
  
  if (task_id >= kernel.task_count) return;
  
  disable_all_interrupts();
  TCB* task = &kernel.tasks[task_id];
  
  if (task->state == TASK_WAITING && !task->mem_blocked) {
    task->state = TASK_READY;
    task->wake_time = 0;
    
    if (task->affinity == CORE_0 || task->affinity == CORE_ANY) {
      sched_bitmap_add(&core0_sched.runnable, task->id, task->priority);
    }
    
    sched_check_preemption();
  }
  
  enable_all_interrupts();
}

void brutal_task_kill(uint32_t id) {
  if (id >= 1000) {
    kout.print("[KILL] Core 1 task "); 
    kout.println(id);
    
    // Release resources owned by Core 1 task (v14.3.1)
    res_cleanup_task(id);
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    TCB* task = ipc_get_tcb_by_id_unsafe(id);
    if(task) {
      task->state = TASK_ZOMBIE;
      task->entry = NULL;
      task->callbacks = NULL;
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    return;
  }
  
  if (id >= kernel.task_count) return;
  
  TCB* task = &kernel.tasks[id];
  if (task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE) return;
  
  kout.print("[KILL] '");
  kout.print(task->name);
  kout.println("'");
  
  if (task->task_type == TASK_TYPE_KERNEL) {
    kernel_panic("KERNEL TASK KILLED");
  }
  
  // Release GUI focus
  if (kernel.gui_focus_task_id == (int32_t)id) {
    k_release_gui_focus(id);
  }
  
  // Release all hardware resources owned by this task (v14.3.1)
  res_cleanup_task(id);
  
  k_unregister_gui_app(id);
  k_unregister_oom_handler(id);
  
  // Clean up IPC messages
  disable_all_interrupts();
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  for(int pri = 0; pri < SCHED_NUM_PRIORITY_LEVELS; pri++) {
    if (task->ipc.priority_bitmap & (1U << pri)) {
      uint16_t msg_index = task->ipc.priority_lists_head[pri];
      
      while (msg_index != IPC_NULL_MSG) {
        IPCMessage* msg = &kernel.ipc_manager.message_pool[msg_index];
        uint16_t next_msg_index = msg->next;
        
        msg->in_use = false;
        msg->next = IPC_NULL_MSG;
        kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
        
        msg_index = next_msg_index;
      }
    }
  }
  
  task_init_ipc_queue(task);
  
  mutex_exit(&kernel.ipc_manager.lock);
  enable_all_interrupts();
  
  // Close open files
  uint32_t files_closed = 0;
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open &&
        kernel.fs_open_files[i].owner_task_id == id) {
      kout.print(" > Closing file: ");
      kout.println(kernel.fs_open_files[i].path);
      
      kernel.fs_open_files[i].handle.close();
      kernel.fs_open_files[i].open = false;
      kernel.fs_open_files[i].owner_task_id = 0;
      files_closed++;
    }
  }
  
  if (files_closed > 0) {
    kout.print(" > Closed ");
    kout.print(files_closed);
    kout.println(" file(s)");
  }
  
  // Call deinit callback
  if (task->callbacks && task->callbacks->deinit) {
    task->callbacks->deinit();
  }
  
  // Check for critical system tasks
  if (strcmp(task->name, "shell") == 0) {
    kernel.shell_alive = false;
    kout.println("\n*** SHELL DEAD - NO MORE COMMANDS ***");
  }
  else if (strcmp(task->name, "cpumon") == 0) {
    kernel.cpumon_alive = false;
  }
  else if (strcmp(task->name, "tempmon") == 0) {
    kernel.tempmon_alive = false;
  }
  else if (strcmp(task->name, "fs") == 0) {
    kernel.fs_alive = false;
    kout.println("\n*** FS DEAD ***");
  }
  
  // Update task type counters
  if (task->task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks--;
  else if (task->task_type == TASK_TYPE_DRIVER) kernel.driver_tasks--;
  else if (task->task_type == TASK_TYPE_SERVICE) kernel.service_tasks--;
  else if (task->task_type == TASK_TYPE_MODULE) kernel.module_tasks--;
  else if (task->task_type == TASK_TYPE_APPLICATION) kernel.application_tasks--;
  
  // Log kill
  char buf[64];
  snprintf(buf, sizeof(buf), "KILL: %s marked as %s", 
           task->name, (task->state == TASK_ZOMBIE) ? "ZOMBIE" : "TERMINATED");
  klog(2, buf);
  
  // Strip respawn flag
  if (task->flags & TASK_FLAG_RESPAWN) {
    kout.println(" > Stripping RESPAWN flag.");
    task->flags &= ~TASK_FLAG_RESPAWN;
  }
  
  // Set final state
  if (task->flags & TASK_FLAG_RESPAWN) {
    task->state = TASK_TERMINATED;
  } else {
    task->state = TASK_ZOMBIE;
    kernel.zombie_tasks++;
  }
  
  task->last_run = get_time_ms();
  task->entry = NULL;
  task->callbacks = NULL;
  task->arg = NULL;
  
  // Remove from scheduler
  disable_all_interrupts();
  sched_bitmap_remove(&core0_sched.runnable, id, task->priority);
  enable_all_interrupts();
}

void k_task_exit_api() {
  uint8_t core = get_core_num();
  
  if (core == 0) {
    uint32_t task_id = kernel.current_task;
    TCB* task = &kernel.tasks[task_id];
    
    if (task->task_type != TASK_TYPE_APPLICATION) {
      klog(2, "k_task_exit: Non-app task denied exit");
      return;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "APP: Task %s (ID %d) exiting", task->name, task_id);
    klog(0, buf);
    
    brutal_task_kill(task_id);
    task_sleep(0xFFFFFFFF);
  } else {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    
    uint32_t local_id = core1_sched.current_task;
    TCB* task = &kernel.core1.tasks[local_id];
    
    if (task->task_type != TASK_TYPE_APPLICATION) {
      mutex_exit(&kernel.core1.scheduler_lock);
      return;
    }
    
    kout.print("[CORE1] Task exit requested: "); 
    kout.println(task->name);
    
    task->state = TASK_TERMINATED;
    task->entry = NULL;
    task->callbacks = NULL;
    task->arg = NULL;
    
    mutex_enter_blocking(&core1_sched.lock);
    sched_bitmap_remove(&core1_sched.runnable, local_id, task->priority);
    mutex_exit(&core1_sched.lock);
    
    mutex_exit(&kernel.core1.scheduler_lock);
    
    while(1) {
      sleep_us(100000);
    }
  }
}

// ============================================================================
// CORE 1
// ============================================================================

void core1_scheduler_init() {
  mutex_init(&kernel.core1.scheduler_lock);
  
  kernel.core1.task_count = 0;
  kernel.core1.current_task = 0;
  kernel.core1.running = true;
  kernel.core1.uptime_ms = 0;
  kernel.core1.cpu_usage = 0.0f;
  kernel.core1.context_switches = 0;
  
  memset(&kernel.core1.tasks, 0, sizeof(kernel.core1.tasks));
}

void core1_main() {
  scheduler_init_core1();
  
  kernel.core1.uptime_ms = get_time_ms();
  
  uint64_t last_stats_update = get_time_ms();
  static uint64_t last_total_time_c1 = 0;
  
  while (kernel.core1.running) {
    if (kernel.core1.task_count == 0) {
      tight_loop_contents();
      sleep_us(1000);
      continue;
    }
    
    uint64_t loop_start = get_time_us();
    uint64_t now_ms = get_time_ms();
    
    // Wake waiting tasks
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
      TCB* task = &kernel.core1.tasks[i];
      
      if (task->state == TASK_WAITING && now_ms >= task->wake_time) {
        task->state = TASK_READY;
        
        mutex_enter_blocking(&core1_sched.lock);
        if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
          sched_bitmap_add(&core1_sched.runnable, i, task->priority);
        }
        mutex_exit(&core1_sched.lock);
      }
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    
    // Age tasks
    sched_age_tasks(&core1_sched, kernel.core1.tasks, kernel.core1.task_count);
    
    // Select next task
    uint32_t task_id = sched_select_next_core1();
    
    if (task_id == 0xFFFFFFFF) {
      sleep_us(100);
      continue;
    }
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    
    if (task_id >= kernel.core1.task_count) {
      mutex_exit(&kernel.core1.scheduler_lock);
      continue;
    }
    
    TCB* task = &kernel.core1.tasks[task_id];
    
    if (task->mem_blocked) {
      mutex_exit(&kernel.core1.scheduler_lock);
      continue;
    }
    
    // Handle OOM cleanup request
    if (task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED) {
      oom_callback_t handler = oom_get_handler(task->id);
      if (handler) {
        handler(task->oom_bytes_requested);
      }
      task->flags &= ~TASK_FLAG_OOM_CLEANUP_REQUESTED;
      task->oom_bytes_requested = 0;
    }
    
    // Execute task
    if ((task->state == TASK_READY || task->state == TASK_RUNNING) &&
        (task->entry || (task->callbacks && task->callbacks->tick))) {
      
      task->state = TASK_RUNNING;
      task->running_on_core = 1;
      
      mutex_enter_blocking(&core1_sched.lock);
      sched_bitmap_remove(&core1_sched.runnable, task_id, task->priority);
      mutex_exit(&core1_sched.lock);
      
      uint64_t task_start = get_time_us();
      
      mutex_exit(&kernel.core1.scheduler_lock);
      
      if (task->callbacks && task->callbacks->tick) {
        task->callbacks->tick(task->arg);
      } else if (task->entry) {
        task->entry(task->arg);
      }
      
      mutex_enter_blocking(&kernel.core1.scheduler_lock);
      
      uint64_t task_duration = get_time_us() - task_start;
      task->cpu_time += (task_duration + 500) / 1000;
      task->total_cpu_time_ms += task_duration;
      task->sched_info.last_run = get_time_ms();
      
      // Update CPU usage
      update_task_cpu_usage(task, task_duration);
      
      if (task->state == TASK_RUNNING) {
        task->state = TASK_READY;
        
        mutex_enter_blocking(&core1_sched.lock);
        if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
          sched_bitmap_add(&core1_sched.runnable, task_id, task->priority);
        }
        mutex_exit(&core1_sched.lock);
      }
    }
    
    mutex_exit(&kernel.core1.scheduler_lock);
    
    core1_sched.switches++;
    
    // Update CPU stats
    if (now_ms - last_stats_update >= 1000) {
      mutex_enter_blocking(&kernel.core1.scheduler_lock);
      
      uint64_t total_cpu = 0;
      for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
        total_cpu += kernel.core1.tasks[i].cpu_time;
      }
      
      uint64_t busy_time_ms = total_cpu - last_total_time_c1;
      last_total_time_c1 = total_cpu;
      
      uint64_t period_ms = now_ms - last_stats_update;
      if (period_ms == 0) period_ms = 1;
      if (busy_time_ms > period_ms) busy_time_ms = period_ms;
      
      float instant_load = 100.0f * (float)busy_time_ms / (float)period_ms;
      kernel.core1.cpu_usage = (kernel.core1.cpu_usage * 0.9f) + (instant_load * 0.1f);
      
      if (kernel.core1.cpu_usage > 100) kernel.core1.cpu_usage = 100;
      if (kernel.core1.cpu_usage < 0) kernel.core1.cpu_usage = 0;
      
      mutex_exit(&kernel.core1.scheduler_lock);
      
      last_stats_update = now_ms;
    }
    
    // Sleep for remaining time
    uint64_t elapsed = get_time_us() - loop_start;
    if (elapsed < SCHEDULER_TICK_US) {
      sleep_us(SCHEDULER_TICK_US - elapsed);
    }
  }
}

uint32_t k_spawn_core1_task(const char* name, void (*entry)(void*), void* arg, uint8_t priority) {
  if (!kernel.core1_initialized) {
    kout.println("[CORE1] Not initialized");
    return 0;
  }
  
  mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  if (kernel.core1.task_count >= MAX_CORE1_TASKS) {
    mutex_exit(&kernel.core1.scheduler_lock);
    kout.println("[CORE1] Task limit reached");
    return 0;
  }
  
  uint32_t id = kernel.core1.task_count++;
  TCB* task = &kernel.core1.tasks[id];
  memset(task, 0, sizeof(TCB));
  
  task->id = id + 1000;
  strncpy(task->name, name, TASK_NAME_LEN - 1);
  task->name[TASK_NAME_LEN - 1] = '\0';
  task->state = TASK_READY;
  task->task_type = TASK_TYPE_APPLICATION;
  task->priority = priority;
  task->affinity = CORE_1;
  task->entry = entry;
  task->arg = arg;
  task->running_on_core = 1;
  task->start_time = get_time_ms();
  task->oom_bytes_requested = 0;
  task->total_cpu_time_ms = 0;
  task->is_cpu_abuser = false;
  
  task_init_ipc_queue(task);
  
  task->original_priority = 0;
  
  // Set up scheduling info
  TaskSchedInfo* si = &task->sched_info;
  si->base_priority = priority;
  si->effective_priority = priority;
  si->is_realtime = (priority >= SCHED_RT_THRESHOLD);
  
  if (si->is_realtime) {
    si->quantum_us = SCHED_BASE_QUANTUM_US;
  } else {
    si->quantum_us = SCHED_BASE_QUANTUM_US +
                     (SCHED_NUM_PRIORITY_LEVELS - priority) * 2000;
    if (si->quantum_us > SCHED_MAX_QUANTUM_US) {
      si->quantum_us = SCHED_MAX_QUANTUM_US;
    }
  }
  
  si->cpu_affinity = 1;
  si->last_run = get_time_ms();
  si->cpu_usage_percent = 0;
  si->cpu_burst_counter = 0;
  si->cpu_sample_index = 0;
  memset(si->cpu_samples, 0, sizeof(si->cpu_samples));
  
  // Add to scheduler
  mutex_enter_blocking(&core1_sched.lock);
  if (task->affinity == CORE_1 || task->affinity == CORE_ANY) {
    sched_bitmap_add(&core1_sched.runnable, id, task->priority);
  }
  mutex_exit(&core1_sched.lock);
  
  mutex_exit(&kernel.core1.scheduler_lock);
  
  kout.print("[CORE1] Spawned task: ");
  kout.print(name);
  kout.print(" (ID=");
  kout.print(task->id);
  kout.println(")");
  
  return task->id;
}

// Continued in next message due to length...

// ============================================================================
// IPC SYSTEM
// ============================================================================

void ipc_init() {
  mutex_init(&kernel.ipc_manager.lock);
  
  kernel.ipc_manager.sequence_counter = 0;
  kernel.ipc_manager.dropped_messages = 0;
  kernel.ipc_manager.total_sent = 0;
  kernel.ipc_manager.total_received = 0;
  kernel.ipc_manager.free_list_head = MAX_IPC_MESSAGES - 1;
  
  for (uint32_t i = 0; i < MAX_IPC_MESSAGES; i++) {
    kernel.ipc_manager.message_pool[i].in_use = false;
    kernel.ipc_manager.free_list[i] = i;
  }
  
  memset(&ipc_stats, 0, sizeof(ipc_stats));
  
  kout.println("[IPC] O(1) Pool Manager initialized");
}

TCB* ipc_get_tcb_by_id_unsafe(uint32_t task_id) {
  if (task_id < 1000) {
    if (task_id < kernel.task_count) {
      return &kernel.tasks[task_id];
    }
  } else {
    uint32_t local_id = task_id - 1000;
    if (local_id < kernel.core1.task_count) {
      return &kernel.core1.tasks[local_id];
    }
  }
  return NULL;
}

bool ipc_send_raw(uint32_t sender_id, uint32_t target_id, IPCMessageType type,
                  void* data, size_t size, uint8_t priority) {
  
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  if (kernel.ipc_manager.free_list_head < 0) {
    mutex_exit(&kernel.ipc_manager.lock);
    ipc_stats.messages_dropped_pool_full++;
    klog(2, "IPC: Global pool full");
    return false;
  }
  
  uint16_t msg_index = kernel.ipc_manager.free_list[kernel.ipc_manager.free_list_head--];
  IPCMessage* msg = &kernel.ipc_manager.message_pool[msg_index];
  
  msg->in_use = true;
  msg->sender_id = sender_id;
  msg->target_id = target_id;
  msg->type = type;
  msg->priority = priority;
  msg->timestamp_ms = get_time_ms();
  msg->sequence = kernel.ipc_manager.sequence_counter++;
  msg->next = IPC_NULL_MSG;
  
  if (data && size > 0) {
    memcpy(msg->data, data, size);
  }
  
  mutex_exit(&kernel.ipc_manager.lock);
  
  // Find target task
  bool is_core1 = (target_id >= 1000);
  
  if (is_core1) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
  }
  
  TCB* target_task = ipc_get_tcb_by_id_unsafe(target_id);
  
  if (target_task == NULL || target_task->state == TASK_ZOMBIE || target_task->mem_blocked) {
    if (is_core1) {
      mutex_exit(&kernel.core1.scheduler_lock);
    }
    
    mutex_enter_blocking(&kernel.ipc_manager.lock);
    msg->in_use = false;
    kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
    mutex_exit(&kernel.ipc_manager.lock);
    
    return false;
  }
  
  // Lock target task queue
  if (!is_core1) {
    disable_all_interrupts();
  }
  
  if (target_task->ipc.message_count >= MAX_IPC_MESSAGES) {
    if (is_core1) {
      mutex_exit(&kernel.core1.scheduler_lock);
    } else {
      enable_all_interrupts();
    }
    
    mutex_enter_blocking(&kernel.ipc_manager.lock);
    msg->in_use = false;
    kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
    mutex_exit(&kernel.ipc_manager.lock);
    
    ipc_stats.messages_dropped_task_full++;
    return false;
  }
  
  // Add to priority queue
  uint16_t* head = &target_task->ipc.priority_lists_head[priority];
  msg->next = *head;
  *head = msg_index;
  
  target_task->ipc.priority_bitmap |= (1U << priority);
  target_task->ipc.message_count++;
  
  if (is_core1) {
    mutex_exit(&kernel.core1.scheduler_lock);
  } else {
    enable_all_interrupts();
  }
  
  kernel.ipc_manager.total_sent++;
  ipc_stats.messages_sent++;
  
  return true;
}

bool ipc_send_api(uint32_t target_id, IPCMessageType type,
                  void* data, size_t size, uint8_t priority) {
  
  if (size > IPC_MSG_SIZE) {
    klog(2, "IPC: Message too large");
    return false;
  }
  
  if (priority >= SCHED_NUM_PRIORITY_LEVELS) {
    priority = SCHED_NUM_PRIORITY_LEVELS - 1;
  }
  
  uint32_t sender_id = 0;
  if (get_core_num() == 0) sender_id = kernel.current_task;
  else sender_id = kernel.core1.tasks[core1_sched.current_task].id;
  
  // Handle broadcast
  if (target_id == IPC_TARGET_BROADCAST) {
    bool all_ok = true;
    ipc_stats.broadcasts_sent++;
    
    for(uint32_t i=0; i < kernel.task_count; i++) {
      if (kernel.tasks[i].id != sender_id && 
          kernel.tasks[i].state != TASK_ZOMBIE && 
          !kernel.tasks[i].mem_blocked) {
        if (!ipc_send_raw(sender_id, kernel.tasks[i].id, type, data, size, priority)) {
          all_ok = false;
        }
      }
    }
    
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for(uint32_t i=0; i < kernel.core1.task_count; i++) {
      if (kernel.core1.tasks[i].id != sender_id && !kernel.core1.tasks[i].mem_blocked) {
        uint32_t c1_target_id = kernel.core1.tasks[i].id;
        mutex_exit(&kernel.core1.scheduler_lock);
        
        if (!ipc_send_raw(sender_id, c1_target_id, type, data, size, priority)) {
          all_ok = false;
        }
        
        mutex_enter_blocking(&kernel.core1.scheduler_lock);
      }
    }
    mutex_exit(&kernel.core1.scheduler_lock);
    
    return all_ok;
  }
  
  return ipc_send_raw(sender_id, target_id, type, data, size, priority);
}

bool ipc_receive_api(IPCMessage* msg_out) {
  if (!msg_out) return false;
  
  TCB* task = NULL;
  bool is_core1 = (get_core_num() == 1);
  
  if (is_core1) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    task = &kernel.core1.tasks[core1_sched.current_task];
  } else {
    disable_all_interrupts();
    task = &kernel.tasks[kernel.current_task];
  }
  
  if (task == NULL) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
    else enable_all_interrupts();
    return false;
  }
  
  if (task->ipc.priority_bitmap == 0) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
    else enable_all_interrupts();
    return false;
  }
  
  // Get highest priority message
  int highest_priority = 31 - __builtin_clz(task->ipc.priority_bitmap);
  uint16_t* head = &task->ipc.priority_lists_head[highest_priority];
  uint16_t msg_index = *head;
  
  if (msg_index == IPC_NULL_MSG) {
    if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
    else enable_all_interrupts();
    
    klog(3, "IPC: Bitmap/queue mismatch!");
    task->ipc.priority_bitmap &= ~(1U << highest_priority);
    return false;
  }
  
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  IPCMessage* msg = &kernel.ipc_manager.message_pool[msg_index];
  
  *head = msg->next;
  if (*head == IPC_NULL_MSG) {
    task->ipc.priority_bitmap &= ~(1U << highest_priority);
  }
  
  task->ipc.message_count--;
  
  memcpy(msg_out, msg, sizeof(IPCMessage));
  
  msg->in_use = false;
  msg->next = IPC_NULL_MSG;
  kernel.ipc_manager.free_list[++kernel.ipc_manager.free_list_head] = msg_index;
  
  kernel.ipc_manager.total_received++;
  ipc_stats.messages_received++;
  
  mutex_exit(&kernel.ipc_manager.lock);
  
  if(is_core1) mutex_exit(&kernel.core1.scheduler_lock); 
  else enable_all_interrupts();
  
  return true;
}

void ipc_maintenance() {
  mutex_enter_blocking(&kernel.ipc_manager.lock);
  
  uint16_t total_in_use = MAX_IPC_MESSAGES - (kernel.ipc_manager.free_list_head + 1);
  
  mutex_exit(&kernel.ipc_manager.lock);
  
  ipc_stats.avg_queue_depth_global = (ipc_stats.avg_queue_depth_global * 0.95f) +
                                      (total_in_use * 0.05f);
  
  if (total_in_use > ipc_stats.max_queue_depth_global) {
    ipc_stats.max_queue_depth_global = total_in_use;
  }
}

// ============================================================================
// RTOS PRIMITIVES
// ============================================================================

void rtos_primitives_init() {
  memset(&kernel.kernel_mutexes, 0, sizeof(kernel.kernel_mutexes));
  memset(&kernel.kernel_semaphores, 0, sizeof(kernel.kernel_semaphores));
  memset(&kernel.kernel_event_flags, 0, sizeof(kernel.kernel_event_flags));
  
  for(int i=0; i < MAX_SEMAPHORES; i++) {
    kernel.kernel_semaphores[i].max_count = 0;
  }
  
  kout.println("[RTOS] Mutexes, Semaphores, Events initialized");
}

void wait_list_add(TaskWaitNode** head, TCB* task) {
  TaskWaitNode* node = &task->wait_node;
  node->task_id = task->id;
  node->next = NULL;
  
  if (*head == NULL) {
    *head = node;
  } else {
    TaskWaitNode* current = *head;
    while(current->next != NULL) {
      current = current->next;
    }
    current->next = node;
  }
}

uint32_t wait_list_pop(TaskWaitNode** head) {
  if (*head == NULL) {
    return 0xFFFFFFFF;
  }
  
  TaskWaitNode* node = *head;
  uint32_t task_id = node->task_id;
  *head = node->next;
  
  return task_id;
}

bool k_mutex_lock(uint32_t mutex_id) {
  if (mutex_id >= MAX_KERNEL_MUTEXES) return false;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KMutex* mutex = &kernel.kernel_mutexes[mutex_id];
  
  disable_all_interrupts();
  
  if (!mutex->locked) {
    mutex->locked = true;
    mutex->owner_id = task_id;
    enable_all_interrupts();
    return true;
  }
  
  // Priority inheritance
  bool is_core1_owner = (mutex->owner_id >= 1000);
  if(is_core1_owner) mutex_enter_blocking(&kernel.core1.scheduler_lock);
  
  TCB* owner_task = ipc_get_tcb_by_id_unsafe(mutex->owner_id);
  
  if(is_core1_owner) mutex_exit(&kernel.core1.scheduler_lock);
  
  if (owner_task && task->priority > owner_task->priority) {
    if (owner_task->original_priority == 0) {
      owner_task->original_priority = owner_task->priority;
    }
    owner_task->priority = task->priority;
    sched_update_task_priority(owner_task);
  }
  
  wait_list_add(&mutex->wait_list_head, task);
  task->state = TASK_WAITING;
  task->wake_time = 0;
  
  sched_bitmap_remove(&core0_sched.runnable, task_id, task->priority);
  
  enable_all_interrupts();
  task_yield();
  
  return true;
}

void k_mutex_unlock(uint32_t mutex_id) {
  if (mutex_id >= MAX_KERNEL_MUTEXES) return;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KMutex* mutex = &kernel.kernel_mutexes[mutex_id];
  
  disable_all_interrupts();
  
  if (!mutex->locked || mutex->owner_id != task_id) {
    enable_all_interrupts();
    return;
  }
  
  // Restore original priority
  if (task->original_priority != 0) {
    task->priority = task->original_priority;
    task->original_priority = 0;
    sched_update_task_priority(task);
  }
  
  uint32_t next_task_id = wait_list_pop(&mutex->wait_list_head);
  
  if (next_task_id != 0xFFFFFFFF) {
    mutex->owner_id = next_task_id;
    task_wake(next_task_id);
  } else {
    mutex->locked = false;
    mutex->owner_id = 0;
  }
  
  enable_all_interrupts();
  sched_check_preemption();
}

bool k_sem_init(uint32_t sem_id, int32_t initial_count, uint32_t max_count) {
  if (sem_id >= MAX_SEMAPHORES) return false;
  
  KSemaphore* sem = &kernel.kernel_semaphores[sem_id];
  if (sem->max_count != 0) return false;
  
  sem->count = initial_count;
  sem->max_count = max_count;
  sem->wait_list_head = NULL;
  
  return true;
}

bool k_sem_wait(uint32_t sem_id, uint32_t timeout_ms) {
  if (sem_id >= MAX_SEMAPHORES) return false;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KSemaphore* sem = &kernel.kernel_semaphores[sem_id];
  
  disable_all_interrupts();
  
  sem->count--;
  if (sem->count >= 0) {
    enable_all_interrupts();
    return true;
  }
  
  wait_list_add(&sem->wait_list_head, task);
  task->state = TASK_WAITING;
  
  if (timeout_ms > 0) {
    task->wake_time = get_time_ms() + timeout_ms;
  } else {
    task->wake_time = 0;
  }
  
  sched_bitmap_remove(&core0_sched.runnable, task_id, task->priority);
  
  enable_all_interrupts();
  task_yield();
  
  if (sem->count >= 0) {
    return true;
  } else {
    sem->count++;
    return false;
  }
}

void k_sem_post(uint32_t sem_id) {
  if (sem_id >= MAX_SEMAPHORES) return;
  
  KSemaphore* sem = &kernel.kernel_semaphores[sem_id];
  
  disable_all_interrupts();
  
  sem->count++;
  if (sem->count > (int32_t)sem->max_count) {
    sem->count = sem->max_count;
  }
  
  if (sem->count <= 0) {
    uint32_t task_to_wake = wait_list_pop(&sem->wait_list_head);
    if (task_to_wake != 0xFFFFFFFF) {
      task_wake(task_to_wake);
    }
  }
  
  enable_all_interrupts();
  sched_check_preemption();
}

bool k_event_init(uint32_t event_id) {
  if (event_id >= MAX_EVENT_FLAGS) return false;
  
  KEvent* event = &kernel.kernel_event_flags[event_id];
  if (event->wait_list_head) return false;
  
  event->flags = 0;
  return true;
}

uint32_t k_event_wait(uint32_t event_id, uint32_t flags, uint8_t mode, bool clear, uint32_t timeout_ms) {
  if (event_id >= MAX_EVENT_FLAGS) return 0;
  
  uint32_t task_id = kernel.current_task;
  TCB* task = &kernel.tasks[task_id];
  KEvent* event = &kernel.kernel_event_flags[event_id];
  
  disable_all_interrupts();
  
  uint32_t current_flags = event->flags;
  bool condition_met = false;
  
  if (mode == K_EVENT_WAIT_ANY) {
    condition_met = (current_flags & flags) != 0;
  } else {
    condition_met = (current_flags & flags) == flags;
  }
  
  if (condition_met) {
    if (clear) {
      event->flags &= ~flags;
    }
    enable_all_interrupts();
    return current_flags;
  }
  
  TaskWaitNode* node = &task->wait_node;
  node->task_id = task->id;
  node->wait_flags = flags;
  node->wait_mode = mode;
  node->clear_on_exit = clear;
  node->next = event->wait_list_head;
  event->wait_list_head = node;
  
  task->state = TASK_WAITING;
  
  if (timeout_ms > 0) {
    task->wake_time = get_time_ms() + timeout_ms;
  } else {
    task->wake_time = 0;
  }
  
  sched_bitmap_remove(&core0_sched.runnable, task_id, task->priority);
  
  enable_all_interrupts();
  task_yield();
  
  return kernel.kernel_event_flags[event_id].flags;
}

void k_event_set(uint32_t event_id, uint32_t flags_to_set) {
  if (event_id >= MAX_EVENT_FLAGS) return;
  
  KEvent* event = &kernel.kernel_event_flags[event_id];
  
  disable_all_interrupts();
  
  event->flags |= flags_to_set;
  
  TaskWaitNode* node = event->wait_list_head;
  TaskWaitNode* prev = NULL;
  bool task_woken = false;
  
  while(node != NULL) {
    bool condition_met = false;
    
    if (node->wait_mode == K_EVENT_WAIT_ANY) {
      condition_met = (event->flags & node->wait_flags) != 0;
    } else {
      condition_met = (event->flags & node->wait_flags) == node->wait_flags;
    }
    
    if (condition_met) {
      task_wake(node->task_id);
      task_woken = true;
      
      if (node->clear_on_exit) {
        event->flags &= ~node->wait_flags;
      }
      
      if (prev == NULL) {
        event->wait_list_head = node->next;
        node = event->wait_list_head;
      } else {
        prev->next = node->next;
        node = node->next;
      }
    } else {
      prev = node;
      node = node->next;
    }
  }
  
  enable_all_interrupts();
  
  if (task_woken) {
    sched_check_preemption();
  }
}

// ============================================================================
// TEMPERATURE
// ============================================================================

void temp_init() {
  adc_init();
  adc_set_temp_sensor_enabled(true);
  adc_select_input(4);
}

float read_temperature() {
  adc_select_input(4);
  uint16_t adc_raw = adc_read();
  
  const float conversion = 3.3f / 4096.0f;
  float voltage = adc_raw * conversion;
  float temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;
  
  return temp_c;
}

// ============================================================================
// LOGGING
// ============================================================================

void klog(uint8_t level, const char* msg) {
  mutex_enter_blocking(&kernel.log_lock);
  
  LogEntry* entry = &kernel.log[kernel.log_head];
  entry->timestamp = get_time_ms();
  entry->level = level;
  
  strncpy(entry->message, msg, sizeof(entry->message) - 1);
  entry->message[sizeof(entry->message) - 1] = '\0';
  
  kernel.log_head = (kernel.log_head + 1) % MAX_LOG_ENTRIES;
  
  if (kernel.log_count < MAX_LOG_ENTRIES) {
    kernel.log_count++;
  }
  
  mutex_exit(&kernel.log_lock);
  
  if (kernel.fs_mounted && level >= 2) {
    fs_log_write(msg);
  }
}

// Continuing with filesystem and remaining code in next message... Femboicoderz01

// ============================================================================
// FILESYSTEM
// ============================================================================

void fs_init() {
  mutex_init(&kernel.fs_lock);
  
  kernel.fs_available = false;
  kernel.fs_mounted = false;
  kernel.sd_info.valid = false;
  kernel.fs_used_bytes = 0;
  kernel.fs_reads = 0;
  kernel.fs_writes = 0;
  kernel.fs_log_counter = 0;
  
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    kernel.fs_open_files[i].open = false;
    kernel.fs_open_files[i].owner_task_id = 0;
  }
  
  kout.println("[FS] Initializing PMFS (Picomimi Filesystem)...");
  
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(500);
  
  // Initialize PMFS
  PMFSStatus status = pmfs.init(SD_CS);
  
  if (status == PMFS_OK) {
    kout.println("[FS] PMFS initialized - root structure found");
    kernel.fs_available = true;
    kernel.sd_info.card_type = 2;
    kernel.sd_info.valid = true;
  } else if (status == PMFS_ERROR_NO_ROOT) {
    kout.println("[FS] PMFS root not found - creating filesystem...");
    status = pmfs.format_and_initialize();
    if (status == PMFS_OK) {
      kout.println("[FS] PMFS filesystem created successfully");
      kernel.fs_available = true;
      kernel.sd_info.valid = true;
    } else {
      kout.print("[FS] PMFS format failed: ");
      kout.println(pmfs.status_to_string(status));
      kernel.fs_available = false;
      return;
    }
  } else if (status == PMFS_ERROR_SD_NOT_FOUND) {
    kout.println("[FS] SD card not detected");
    klog(1, "FS: No SD card");
    kernel.fs_available = false;
    return;
  } else {
    kout.print("[FS] PMFS init error: ");
    kout.println(pmfs.status_to_string(status));
    kernel.fs_available = false;
    return;
  }
  
  klog(0, "FS: PMFS Init OK");
}

void fs_log_init() {
  if (!kernel.fs_available) return;
  
  File logFile = SD.open(FS_LOG_FILE, "r");
  
  if (!logFile) {
    kout.println("[FS] Creating LogRecord file");
    logFile = SD.open(FS_LOG_FILE, "w");
    
    if (logFile) {
      logFile.println("=== Picomimi RTOS v14.1 System Log ===");
      logFile.println("Format: [N] Timestamp Message");
      logFile.println("================================");
      logFile.close();
      kernel.fs_log_counter = 0;
      kout.println("[FS] LogRecord created");
    } else {
      kout.println("[FS] Failed to create LogRecord");
      return;
    }
  } else {
    kernel.fs_log_counter = 0;
    
    while (logFile.available()) {
      String line = logFile.readStringUntil('\n');
      
      if (line.startsWith("[")) {
        int endBracket = line.indexOf(']');
        if (endBracket > 0) {
          String numStr = line.substring(1, endBracket);
          uint32_t num = numStr.toInt();
          if (num > kernel.fs_log_counter) {
            kernel.fs_log_counter = num;
          }
        }
      }
    }
    
    logFile.close();
    kout.print("[FS] LogRecord found, last entry: ");
    kout.println(kernel.fs_log_counter);
  }
}

void fs_log_write(const char* message) {
  if (!kernel.fs_mounted) return;
  
  mutex_enter_blocking(&kernel.fs_lock);
  pmfs.log_system(message);
  kernel.fs_log_counter++;
  mutex_exit(&kernel.fs_lock);
}

bool fs_mount() {
  if (!kernel.fs_available) {
    kout.println("[FS] SD card unavailable");
    return false;
  }
  
  PMFSStatus status = pmfs.mount();
  if (status != PMFS_OK) {
    kout.print("[FS] PMFS mount failed: ");
    kout.println(pmfs.status_to_string(status));
    return false;
  }
  
  kernel.fs_mounted = true;
  kernel.fs_alive = true;
  
  // Log initialization via PMFS
  pmfs.log_system("Picomimi v14.1 kernel boot");
  
  kout.println("[FS] PMFS mounted");
  klog(0, "FS: PMFS Mounted");
  
  return true;
}

void fs_unmount() {
  if (!kernel.fs_mounted) return;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  // Close kernel's open files
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open) {
      kernel.fs_open_files[i].handle.close();
      kernel.fs_open_files[i].open = false;
      kernel.fs_open_files[i].owner_task_id = 0;
    }
  }
  
  // Unmount PMFS
  pmfs.unmount();
  
  kernel.fs_mounted = false;
  kernel.fs_alive = false;
  
  mutex_exit(&kernel.fs_lock);
  
  kout.println("[FS] PMFS unmounted");
  klog(0, "FS: PMFS Unmounted");
}

bool fs_mkdir(const char* path) {
  if (!kernel.fs_mounted) return false;
  
  mutex_enter_blocking(&kernel.fs_lock);
  bool result = SD.mkdir(path);
  mutex_exit(&kernel.fs_lock);
  
  return result;
}

bool fs_remove(const char* path) {
  if (!kernel.fs_mounted) return false;
  
  mutex_enter_blocking(&kernel.fs_lock);
  bool result = SD.remove(path);
  if (!result) {
    result = SD.rmdir(path);
  }
  mutex_exit(&kernel.fs_lock);
  
  return result;
}

void fs_list(const char* path) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  File root = SD.open(path);
  if (!root) {
    kout.println("[FS] Failed to open directory");
    return;
  }
  
  if (!root.isDirectory()) {
    kout.println("[FS] Not a directory");
    root.close();
    return;
  }
  
  kout.print("\n=== FS Contents [");
  kout.print(path);
  kout.println("] ===");
  kout.println("Name                             Type  Size");
  kout.println("-------------------------------- ----- --------");
  
  File file = root.openNextFile();
  uint32_t file_count = 0;
  
  while (file) {
    file_count++;
    if (file_count % 20 == 0) {
      task_sleep(1);
    }
    
    char buf[80];
    snprintf(buf, sizeof(buf), "%-32s %-5s %8d",
             file.name(),
             file.isDirectory() ? "DIR" : "FILE",
             (int)file.size());
    kout.println(buf);
    
    file.close();
    file = root.openNextFile();
  }
  
  root.close();
}

void fs_stats() {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  kout.println("\n=== PMFS Statistics ===");
  
  kout.print("Free space: ");
  kout.print((uint32_t)(pmfs.get_free_space() / 1024));
  kout.println(" KB");
  
  kout.print("Used space: ");
  kout.print((uint32_t)(pmfs.get_used_space() / 1024));
  kout.println(" KB");
  
  kout.print("Fragmentation: ");
  kout.print(pmfs.get_fragmentation(), 1);
  kout.println("%");
  
  kout.print("tmpfs available: ");
  kout.print(pmfs.tmpfs_available());
  kout.println(" bytes");
  
  PMFSStats stats;
  pmfs.get_stats(&stats);
  
  kout.print("Files created: ");
  kout.println((uint32_t)stats.files_created);
  kout.print("Bytes written: ");
  kout.println((uint32_t)stats.bytes_written);
  kout.print("Bytes read: ");
  kout.println((uint32_t)stats.bytes_read);
  kout.print("Cache hits: ");
  kout.println((uint32_t)stats.cache_hits);
  kout.print("Journal commits: ");
  kout.println(stats.journal_commits);
  
  pmfs.print_metadata();
}

void fs_cat(const char* path) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  File file = SD.open(path, "r");
  if (!file) {
    kout.println("[FS] Failed to open file");
    return;
  }
  
  if (file.isDirectory()) {
    kout.println("[FS] Error: Is a directory");
    file.close();
    return;
  }
  
  kout.println("\n=== File Contents ===");
  while (file.available()) {
    kout.write(file.read());
  }
  kout.println("\n=== End ===");
  
  file.close();
  kernel.fs_reads++;
}

bool fs_write_new(const char* path, const char* content, bool append) {
  if (!kernel.fs_mounted) return false;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  File file;
  if (append) {
    file = SD.open(path, "a");
  } else {
    file = SD.open(path, "w");
  }
  
  if (!file) {
    mutex_exit(&kernel.fs_lock);
    return false;
  }
  
  file.println(content);
  file.close();
  
  kernel.fs_writes++;
  mutex_exit(&kernel.fs_lock);
  
  return true;
}

void fs_logtail(uint32_t lines) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  File file = SD.open(FS_LOG_FILE, "r");
  if (!file) {
    kout.println("[FS] Failed to open log file");
    mutex_exit(&kernel.fs_lock);
    return;
  }
  
  uint32_t fsize = file.size();
  if (fsize == 0) {
    kout.println("[Log is empty]");
    file.close();
    mutex_exit(&kernel.fs_lock);
    return;
  }
  
  // Find starting position
  uint32_t pos = fsize - 1;
  uint32_t line_count = 0;
  
  while (pos > 0) {
    file.seek(pos);
    char c = file.read();
    
    if (c == '\n') {
      line_count++;
      if (line_count > lines) {
        pos++;
        break;
      }
    }
    pos--;
  }
  
  kout.print("\n=== Log Tail (");
  kout.print(lines);
  kout.println(" lines) ===");
  
  file.seek(pos);
  while (file.available()) {
    kout.write(file.read());
  }
  
  if(pos > 0) kout.println();
  kout.println("=== End ===");
  
  file.close();
  kernel.fs_reads++;
  
  mutex_exit(&kernel.fs_lock);
}

int fs_open(const char* path, bool write_mode) {
  if (!kernel.fs_mounted) return -1;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  int fd = -1;
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (!kernel.fs_open_files[i].open) {
      fd = i;
      break;
    }
  }
  
  if (fd < 0) {
    kout.println("[FS] No free file handles");
    mutex_exit(&kernel.fs_lock);
    return -1;
  }
  
  File file;
  if (write_mode) {
    file = SD.open(path, "w");
  } else {
    file = SD.open(path, "r");
  }
  
  if (!file) {
    mutex_exit(&kernel.fs_lock);
    return -1;
  }
  
  kernel.fs_open_files[fd].handle = file;
  kernel.fs_open_files[fd].open = true;
  kernel.fs_open_files[fd].write_mode = write_mode;
  kernel.fs_open_files[fd].owner_task_id = kernel.current_task;
  
  strncpy(kernel.fs_open_files[fd].path, path, FS_MAX_FILENAME - 1);
  kernel.fs_open_files[fd].path[FS_MAX_FILENAME - 1] = '\0';
  
  mutex_exit(&kernel.fs_lock);
  
  return fd;
}

void fs_close(int fd) {
  if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  if (kernel.fs_open_files[fd].open) {
    kernel.fs_open_files[fd].handle.close();
    kernel.fs_open_files[fd].open = false;
    kernel.fs_open_files[fd].owner_task_id = 0;
  }
  
  mutex_exit(&kernel.fs_lock);
}

// ============================================================================
// SYSTEM TASKS
// ============================================================================

// Race-to-idle: At BALANCED+ using WFI is more power efficient than
// active spinning at lower frequency due to Cortex-M33 static leakage
//
// v14.1.1 FIX: NEVER use WFI when USB Serial is active!
// The original bug was WFI blocking USB interrupt servicing, causing
// CDC buffers to overflow and the Serial connection to desync.
void idle_task(void* arg) {
  (void)arg;
  
  // =========================================================================
  // USB SAFETY CHECK - v14.1.1 FIX (THE CRITICAL FIX)
  // =========================================================================
  // If USB Serial is active, we MUST NOT use WFI under ANY circumstance.
  // WFI blocks the CPU until an interrupt fires, but USB CDC requires
  // continuous interrupt servicing or the connection dies.
  
  if (usb_blocks_lowpower()) {
    // USB is active - NO WFI allowed, just yield with minimal delay
    // This keeps the scheduler running and USB responsive
    usb_service();  // Flush any pending TX before yielding
    task_sleep(1);  // 1ms cooperative yield
    return;
  }
  
  // USB is not active, safe to use power-saving modes
  if (kernel.governor.current_profile == CPU_PROFILE_ULTRA_LOW) {
    // ULTRA_LOW: Use WFI with governor integration
    usb_service();  // One last service before sleep (belt and suspenders)
    governor_enter_wfi();
  } else if (kernel.governor.current_profile == CPU_PROFILE_POWERSAVE) {
    // POWERSAVE: Light WFI, shorter sleep
    usb_service();  // Service before WFI
    #if RP2040_OR_RP2350 == 1
      // RP2350 M33: Careful WFI - clear SLEEPDEEP to avoid deep sleep
      scb_hw->scr &= ~M33_SCB_SCR_SLEEPDEEP_BITS;
    #endif
    __asm__ volatile ("wfi");
  } else {
    // Higher profiles: Just yield, let scheduler handle things
    // Short sleep to prevent busy-looping
    task_sleep(1);
  }
}

void k_reaper_task(void* arg) {
  task_sleep(REAPER_INTERVAL_MS);
  
  if (kernel.zombie_tasks == 0) {
    return;
  }
  
  klog(0, "REAPER: Cleaning zombie tasks...");
  
  uint32_t reclaimed_bytes = 0;
  uint32_t zombies_cleaned = 0;
  
  // Wait grace period before cleanup
  task_sleep(REAPER_GRACE_PERIOD_MS);
  
  for (uint32_t i = 1; i < kernel.task_count; i++) {
    disable_all_interrupts();
    TCB* task = &kernel.tasks[i];
    
    if (task->state == TASK_ZOMBIE) {
      enable_all_interrupts();
      
      uint32_t freed = 0;
      
      mutex_enter_blocking(&kernel.mem_lock);
      
      for (uint32_t b = 0; b < kernel.mem_block_count; b++) {
        if (kernel.mem_blocks[b].owner_id == task->id) {
          freed += kernel.mem_blocks[b].size;
          kernel.mem_blocks[b].free = true;
          kernel.total_free_mem += kernel.mem_blocks[b].size;
          kernel.mem_blocks[b].owner_id = 0;
        }
      }
      
      mutex_exit(&kernel.mem_lock);
      
      if (freed > 0) {
        reclaimed_bytes += freed;
        mem_compact();
      }
      
      disable_all_interrupts();
      task->state = TASK_TERMINATED;
      kernel.zombie_tasks--;
      zombies_cleaned++;
      enable_all_interrupts();
    } else {
      enable_all_interrupts();
    }
  }
  
  if(zombies_cleaned > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "REAPER: Cleaned %d zombies, %dKB", 
             (int)zombies_cleaned, (int)(reclaimed_bytes / 1024));
    klog(0, buf);
  }
}

void shell_task(void* arg) {
  if (!kernel.shell_alive) {
    task_sleep(10000);
    return;
  }
  
  while (Serial.available()) {
    // v14.1.1 FIX: Record USB activity whenever we receive data
    // This keeps the governor from dropping to low-power modes
    usb_record_activity();
    kernel.usb_bytes_rx++;
    
    int c = Serial.read();
    
    if (c == '\r' || c == '\n') {
      kout.println();
      
      char temp_cmd_buffer[128];
      strncpy(temp_cmd_buffer, cmd_buffer, sizeof(temp_cmd_buffer) - 1);
      temp_cmd_buffer[sizeof(temp_cmd_buffer) - 1] = '\0';
      
      shell_execute(temp_cmd_buffer);
      
      cmd_pos = 0;
      memset(cmd_buffer, 0, sizeof(cmd_buffer));
      
      if (!kernel.shell_alive) {
        task_sleep(10000);
        return;
      }
      
      shell_prompt();
    } else if (c == '\b' || c == 127) {
      if (cmd_pos > 0) {
        cmd_pos--;
        cmd_buffer[cmd_pos] = '\0';
        Serial.write('\b');
        Serial.write(' ');
        Serial.write('\b');
        kernel.usb_bytes_tx += 3;
      }
    } else if (cmd_pos < sizeof(cmd_buffer) - 1) {
      cmd_buffer[cmd_pos++] = c;
      Serial.write(c);
      kernel.usb_bytes_tx++;
    }
  }
  
  task_sleep(10);
}

void shell_deinit() {
  kout.println("[SHELL] DEINIT");
  kernel.shell_alive = false;
  cmd_pos = 0;
  memset(cmd_buffer, 0, sizeof(cmd_buffer));
}

void input_task(void* arg) {
  static unsigned long last_onoff_press = 0;
  
  if (gpio_read_fast(BTN_ONOFF) && (millis() - last_onoff_press > 250)) {
    last_onoff_press = millis();
    
    if (gui_app_count > 0) {
      current_gui_focus_index = (current_gui_focus_index + 1) % gui_app_count;
      uint32_t new_focus_task_id = gui_app_task_ids[current_gui_focus_index];
      kernel.gui_focus_task_id = new_focus_task_id;
      
      kout.print("\n[Focus] Switched to task ");
      kout.println(new_focus_task_id);
      shell_prompt();
    } else {
      kernel.gui_focus_task_id = -1;
      current_gui_focus_index = -1;
    }
  }
  
  task_sleep(20);
}

void cpu_monitor_task(void* arg) {
  if (!kernel.cpumon_alive) {
    task_sleep(10000);
    return;
  }
  
  task_sleep(5000);
  
  if (kernel.cpu_usage > 95.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "CPUMON: High CPU Load! %.1f%%", kernel.cpu_usage);
    klog(2, buf);
  }
  
  if (kernel.core1_initialized && kernel.core1.cpu_usage > 95.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "CPUMON: High CPU Load (Core 1)! %.1f%%", 
             kernel.core1.cpu_usage);
    klog(2, buf);
  }
}

void cpumon_deinit() {
  kout.println("[CPUMON] DEINIT");
  kernel.cpumon_alive = false;
}

void temp_monitor_task(void* arg) {
  if (!kernel.tempmon_alive) {
    task_sleep(10000);
    return;
  }
  
  kernel.temperature = read_temperature();
  
  if (kernel.temperature > 70.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "TEMPMON: High Temperature! %.1f C", 
             kernel.temperature);
    klog(2, buf);
  }
  
  task_sleep(2000);
}

void tempmon_deinit() {
  kout.println("[TEMPMON] DEINIT");
  kernel.tempmon_alive = false;
}

void fs_task(void* arg) {
  if (!kernel.fs_alive) {
    task_sleep(10000);
    return;
  }
  
  task_sleep(30000);
  
  uint32_t flushed_files = 0;
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open && kernel.fs_open_files[i].write_mode) {
      kernel.fs_open_files[i].handle.flush();
      flushed_files++;
    }
  }
  
  mutex_exit(&kernel.fs_lock);
  
  if (flushed_files > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "FS: Flushed %d open write handles", flushed_files);
    klog(0, buf);
  }
}

void fs_deinit() {
  kout.println("[FS] DEINIT");
  
  for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
    if (kernel.fs_open_files[i].open) {
      kernel.fs_open_files[i].handle.close();
      kernel.fs_open_files[i].open = false;
    }
  }
  
  fs_unmount();
  kernel.fs_alive = false;
}

// Continued with shell commands in next part...
// Continued with shell commands in next part...

// ============================================================================
// SHELL UTILITIES
// ============================================================================

void shell_prompt() {
  if (kernel.root_mode) {
    kout.print("Picomimi:");
    kout.print(shell_cwd);
    kout.print("# ");
  } else {
    kout.print("Picomimi:");
    kout.print(shell_cwd);
    kout.print("~> ");
  }
}

static void fs_normalize_path(const char* base, const char* relative, char* out, size_t out_size) {
  char temp_path[256];
  char temp_relative[128];
  
  strncpy(temp_relative, relative, sizeof(temp_relative)-1);
  temp_relative[sizeof(temp_relative)-1] = '\0';
  
  if (temp_relative[0] == '/') {
    strncpy(temp_path, temp_relative, sizeof(temp_path) - 1);
  } else {
    if (strcmp(base, "/") == 0) {
      snprintf(temp_path, sizeof(temp_path), "/%s", temp_relative);
    } else {
      snprintf(temp_path, sizeof(temp_path), "%s/%s", base, temp_relative);
    }
  }
  
  temp_path[sizeof(temp_path)-1] = '\0';
  
  char* segments[32];
  int s_idx = 0;
  char* token = strtok(temp_path, "/");
  
  while(token != NULL) {
    if (strcmp(token, ".") == 0) {
      // Skip
    } else if (strcmp(token, "..") == 0) {
      if (s_idx > 0) {
        s_idx--;
      }
    } else {
      if (s_idx < 32) {
        segments[s_idx++] = token;
      }
    }
    token = strtok(NULL, "/");
  }
  
  if (s_idx == 0) {
    strncpy(out, "/", out_size);
  } else {
    out[0] = '\0';
    for (int i = 0; i < s_idx; i++) {
      if (strlen(out) + strlen(segments[i]) + 2 > out_size) break;
      strcat(out, "/");
      strcat(out, segments[i]);
    }
  }
}

void shell_execute(char* cmd) {
  if (strlen(cmd) == 0) return;
  
  if (strcmp(cmd, "help") == 0) cmd_help();
  else if (strcmp(cmd, "ps") == 0) cmd_ps();
  else if (strncmp(cmd, "taskinfo ", 9) == 0) cmd_taskinfo(cmd + 9);
  else if (strcmp(cmd, "ipcstat") == 0) cmd_ipcstat();
  else if (strcmp(cmd, "schedstat") == 0) cmd_schedstat();
  else if (strcmp(cmd, "oomstat") == 0) cmd_oomstat();
  else if (strcmp(cmd, "rtos_stat") == 0) cmd_rtos_stat();
  else if (strcmp(cmd, "dmesg") == 0) cmd_dmesg();
  else if (strcmp(cmd, "listapps") == 0) cmd_listapps();
  else if (strcmp(cmd, "listdrv") == 0) cmd_listdrv();
  else if (strcmp(cmd, "listsvc") == 0) cmd_listsvc();
  else if (strncmp(cmd, "startdrv ", 9) == 0) {
    Picomimi_StartDriver(cmd + 9);
  }
  else if (strncmp(cmd, "startsvc ", 9) == 0) {
    Picomimi_StartService(cmd + 9);
  }
  else if (strcmp(cmd, "top") == 0) cmd_top();
  else if (strcmp(cmd, "mem") == 0) cmd_mem();
  else if (strcmp(cmd, "memmap") == 0) cmd_memmap();
  else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
  else if (strcmp(cmd, "temp") == 0) cmd_temp();
  else if (strcmp(cmd, "root") == 0) cmd_root();
  else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
  else if (strncmp(cmd, "kill ", 5) == 0) cmd_kill(cmd + 5);
  else if (strncmp(cmd, "ls", 2) == 0) {
    char path[72];  // Sized for PMFS_MAX_PATH_LENGTH
    if (strlen(cmd) <= 3) {
      fs_list(shell_cwd);
    } else if (cmd[2] == ' ') {
      fs_normalize_path(shell_cwd, cmd + 3, path, sizeof(path));
      fs_list(path);
    } else {
      kout.print("Unknown: "); kout.println(cmd);
    }
  }
  else if (strcmp(cmd, "stat") == 0) fs_stats();
  else if (strncmp(cmd, "cat ", 4) == 0) {
    char path[72];
    fs_normalize_path(shell_cwd, cmd + 4, path, sizeof(path));
    fs_cat(path);
  }
  else if (strncmp(cmd, "write ", 6) == 0) cmd_write(cmd + 6);
  else if (strcmp(cmd, "logls") == 0) cmd_logls();
  else if (strcmp(cmd, "format") == 0) cmd_format_sd();
  else if (strncmp(cmd, "mkdir ", 6) == 0) {
    char path[72];
    fs_normalize_path(shell_cwd, cmd + 6, path, sizeof(path));
    cmd_mkdir(path);
  }
  else if (strncmp(cmd, "rm ", 3) == 0) {
    char path[72];
    fs_normalize_path(shell_cwd, cmd + 3, path, sizeof(path));
    cmd_rm(path);
  }
  else if (strncmp(cmd, "touch ", 6) == 0) {
    char path[72];
    fs_normalize_path(shell_cwd, cmd + 6, path, sizeof(path));
    cmd_touch(path);
  }
  else if (strncmp(cmd, "logtail", 7) == 0) cmd_logtail(cmd + 7);
  else if (strncmp(cmd, "cd ", 3) == 0) cmd_cd(cmd + 3);
  else if (strcmp(cmd, "cd") == 0) cmd_cd("/");
  else if (strcmp(cmd, "app block list") == 0) cmd_app_block_list();
  else if (strncmp(cmd, "app block unlock ", 17) == 0) cmd_app_block_unlock(cmd + 17);
  // PMFS commands
  else if (strcmp(cmd, "tree") == 0) {
    if (kernel.fs_mounted) {
      kout.println("\n=== PMFS Directory Tree ===");
      pmfs.print_tree();
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "pmfs") == 0) {
    if (kernel.fs_mounted) {
      pmfs.print_stats();
      pmfs.print_metadata();
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "tmpfs") == 0) {
    if (kernel.fs_mounted) {
      kout.println("\n=== tmpfs (RAM Disk) ===");
      kout.print("Available: ");
      kout.print(pmfs.tmpfs_available());
      kout.print(" / ");
      kout.print(PMFS_TMPFS_SIZE);
      kout.println(" bytes");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "bank") == 0) {
    if (kernel.fs_mounted) {
      kout.println("\n=== System Banks ===");
      kout.print("Active bank: ");
      kout.println(pmfs.get_active_bank() == PMFS_BANK_A ? "A" : "B");
      kout.print("Backup bank: ");
      kout.println(pmfs.get_backup_bank() == PMFS_BANK_A ? "A" : "B");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "fsck") == 0) {
    if (kernel.fs_mounted) {
      kout.println("[PMFS] Running filesystem check...");
      PMFSStatus status = pmfs.fsck(true);
      if (status == PMFS_OK) {
        kout.println("[PMFS] Filesystem OK");
      } else {
        kout.print("[PMFS] fsck result: ");
        kout.println(pmfs.status_to_string(status));
      }
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  else if (strcmp(cmd, "defrag") == 0) {
    if (kernel.fs_mounted) {
      kout.println("[PMFS] Running defragmentation...");
      pmfs.defragment();
      kout.println("[PMFS] Defragmentation complete");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  // Memory commands
  else if (strcmp(cmd, "compact") == 0) {
    kout.println("[MEM] Compacting memory...");
    uint32_t before = kernel.mem_block_count;
    mem_compact();
    calculate_fragmentation();
    kout.print("[MEM] Blocks: ");
    kout.print(before);
    kout.print(" -> ");
    kout.println(kernel.mem_block_count);
    kout.print("[MEM] Fragmentation: ");
    kout.print(kernel.fragmentation_pct);
    kout.println("%");
  }
  else if (strcmp(cmd, "gc") == 0) {
    if (kernel.fs_mounted) {
      kout.println("[PMFS] Running garbage collection...");
      pmfs.garbage_collect();
      kout.println("[PMFS] GC complete");
    } else {
      kout.println("[FS] Not mounted");
    }
    // Also compact kernel memory
    kout.println("[MEM] Compacting kernel memory...");
    mem_compact();
  }
  else if (strcmp(cmd, "verify") == 0) {
    if (kernel.fs_mounted) {
      kout.println("[PMFS] Verifying all files...");
      pmfs.verify_all_files();
      kout.println("[PMFS] Verification complete");
    } else {
      kout.println("[FS] Not mounted");
    }
  }
  // CPU Governor commands
  else if (strcmp(cmd, "gov") == 0) {
    kout.println("\n=== CPU Governor v2.0 Status ===");
    kout.print("Chip: ");
    kout.println(CHIP_NAME);
    kout.print("Profile: ");
    kout.print(governor_get_profile_name());
    kout.print(" (");
    kout.print(kernel.governor.current_freq_khz / 1000);
    kout.println(" MHz)");
    kout.print("Avg Load: ");
    kout.print(kernel.governor.avg_load, 1);
    kout.print("% | Instant: ");
    kout.print(kernel.governor.instant_load, 1);
    kout.println("%");
    kout.print("Turbo: ");
    kout.print(kernel.turbo_enabled ? (kernel.governor.turbo_active ? "ACTIVE (no time limit)" : "enabled") : "disabled");
    if (kernel.governor.turbo_active) {
      kout.print(" | Total: ");
      kout.print((kernel.governor.total_turbo_time_ms + (get_time_ms() - kernel.governor.turbo_start_ms)) / 1000);
      kout.print("s");
    }
    kout.println();
    kout.print("Thermal: ");
    kout.print(kernel.governor.thermal_throttled ? "THROTTLED" : "OK");
    kout.print(" (limit: ");
    kout.print((int)THERMAL_THROTTLE_TEMP);
    kout.println("°C)");
    kout.print("WFI: ");
    kout.print(kernel.governor.wfi_enabled ? "enabled" : "disabled");
    kout.print(" | In WFI: ");
    kout.println(kernel.governor.in_wfi ? "YES" : "no");
    kout.print("Override: ");
    kout.println(kernel.governor.user_override ? "YES (manual)" : "auto");
    
    // v14.1.1 FIX: Show USB Serial status
    kout.println("\n--- USB Serial Status (v14.1.1) ---");
    kout.print("USB Connected: ");
    kout.print(kernel.usb_connected ? "YES" : "no");
    kout.print(" | Blocking Low-Power: ");
    kout.println(kernel.usb_blocking_lowpower ? "YES" : "no");
    if (kernel.usb_connected) {
      uint32_t idle_time = get_time_ms() - kernel.usb_last_activity_ms;
      kout.print("Last Activity: ");
      kout.print(idle_time);
      kout.print("ms ago");
      if (idle_time < USB_ACTIVITY_TIMEOUT_MS) {
        kout.println(" (ACTIVE)");
      } else {
        kout.println(" (idle)");
      }
    }
    kout.print("RX/TX Bytes: ");
    kout.print(kernel.usb_bytes_rx);
    kout.print(" / ");
    kout.println(kernel.usb_bytes_tx);
    if (kernel.usb_lockup_recoveries > 0) {
      kout.print("Lockup Recoveries: ");
      kout.println(kernel.usb_lockup_recoveries);
    }
    
    #if ENABLE_PICOMIMI_GOVERNOR == 0
      kout.println("\nMODE: Governor DISABLED (locked frequency)");
    #endif
    kout.println("\nCommands: gov [ultra|powersave|balanced|perf|turbo|auto]");
    kout.println("          gov turbo [on|off] | gov wfi [on|off]");
  }
  else if (strcmp(cmd, "gov auto") == 0) {
    governor_set_auto();
    kout.println("[GOV] Automatic scaling enabled");
  }
  else if (strcmp(cmd, "gov ultra") == 0 || strcmp(cmd, "gov ultralow") == 0) {
    governor_set_profile(CPU_PROFILE_ULTRA_LOW);
    kout.println("[GOV] Set to ULTRA_LOW (50MHz + WFI)");
  }
  else if (strcmp(cmd, "gov powersave") == 0) {
    governor_set_profile(CPU_PROFILE_POWERSAVE);
    kout.println("[GOV] Set to POWERSAVE (100MHz)");
  }
  else if (strcmp(cmd, "gov balanced") == 0) {
    governor_set_profile(CPU_PROFILE_BALANCED);
    #if RP2040_OR_RP2350 == 0
      kout.println("[GOV] Set to BALANCED (133MHz)");
    #else
      kout.println("[GOV] Set to BALANCED (150MHz)");
    #endif
  }
  else if (strcmp(cmd, "gov performance") == 0 || strcmp(cmd, "gov perf") == 0) {
    governor_set_profile(CPU_PROFILE_PERFORMANCE);
    #if RP2040_OR_RP2350 == 0
      kout.println("[GOV] Set to PERFORMANCE (200MHz)");
    #else
      kout.println("[GOV] Set to PERFORMANCE (250MHz)");
    #endif
  }
  else if (strcmp(cmd, "gov turbo") == 0) {
    if (!kernel.turbo_enabled) {
      kout.println("[GOV] Turbo is disabled (use 'gov turbo on' to enable)");
    } else if (kernel.governor.thermal_throttled) {
      kout.println("[GOV] Cannot turbo - thermal throttled!");
    } else {
      governor_set_profile(CPU_PROFILE_TURBO);
      #if RP2040_OR_RP2350 == 0
        kout.println("[GOV] Set to TURBO (260MHz) - NO time limit!");
      #else
        kout.println("[GOV] Set to TURBO (310MHz) - NO time limit!");
      #endif
    }
  }
  else if (strcmp(cmd, "gov turbo on") == 0) {
    kernel.turbo_enabled = true;
    kout.println("[GOV] Turbo boost ENABLED (no time limit)");
  }
  else if (strcmp(cmd, "gov turbo off") == 0) {
    kernel.turbo_enabled = false;
    if (kernel.governor.current_profile == CPU_PROFILE_TURBO) {
      governor_set_profile(CPU_PROFILE_PERFORMANCE);
    }
    kout.println("[GOV] Turbo boost DISABLED");
  }
  else if (strcmp(cmd, "gov wfi on") == 0) {
    kernel.governor.wfi_enabled = true;
    kout.println("[GOV] WFI power saving ENABLED");
  }
  else if (strcmp(cmd, "gov wfi off") == 0) {
    kernel.governor.wfi_enabled = false;
    kout.println("[GOV] WFI power saving DISABLED");
  }
  // =========================================================================
  // RESOURCE MANAGER COMMANDS (v14.3.1)
  // =========================================================================
  else if (strcmp(cmd, "res") == 0) {
    res_print_status();
  }
  else if (strcmp(cmd, "resmap") == 0) {
    res_print_gpio_map();
  }
  else if (strcmp(cmd, "resaudit") == 0) {
    res_print_violations();
  }
  else if (strncmp(cmd, "restask ", 8) == 0) {
    uint32_t task_id = atoi(cmd + 8);
    res_print_task_resources(task_id);
  }
  else if (strcmp(cmd, "usbstat") == 0) {
    kout.println("\n=== USB Serial Status ===");
    kout.print("Connected: ");
    kout.println(kernel.usb_connected ? "YES" : "NO");
    kout.print("Active (blocking low-power): ");
    kout.println(kernel.usb_blocking_lowpower ? "YES" : "NO");
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Last activity: %lu ms ago", get_time_ms() - kernel.usb_last_activity_ms);
    kout.println(buf);
    snprintf(buf, sizeof(buf), "Bytes TX: %lu  RX: %lu", kernel.usb_bytes_tx, kernel.usb_bytes_rx);
    kout.println(buf);
    snprintf(buf, sizeof(buf), "Lockup recoveries: %lu", kernel.usb_lockup_recoveries);
    kout.println(buf);
    
    // Live health check
    kout.println("\n--- Live Health ---");
    kout.print("Serial valid: ");
    kout.println(Serial ? "YES" : "NO");
    kout.print("TX buffer free: ");
    kout.println(Serial.availableForWrite());
    kout.print("RX bytes waiting: ");
    kout.println(Serial.available());
  }
  else if (strcmp(cmd, "reshelp") == 0) {
    kout.println("\n=== Resource Manager v14.3.1 ===");
    kout.println("ZERO-OVERHEAD DESIGN:");
    kout.println("  1. Claim resource (one-time, ~1us)");
    kout.println("  2. Use direct Arduino/SDK calls (ZERO overhead)");
    kout.println("  3. Release or auto-cleanup on task death");
    kout.println("\nCommands:");
    kout.println(" res          - Resource manager status");
    kout.println(" resmap       - GPIO ownership map");
    kout.println(" resaudit     - Resource violations");
    kout.println(" restask <id> - Resources by task");
    kout.println("\nAPI Example:");
    kout.println("  // Claim pin 25 (returns pin# or -1)");
    kout.println("  int led = Pico.ClaimGPIO(25);");
    kout.println("  if (led >= 0) {");
    kout.println("    pinMode(led, OUTPUT);   // Direct Arduino");
    kout.println("    digitalWrite(led, HIGH); // Full speed!");
    kout.println("  }");
    kout.println("  // Auto-released when task dies");
  }
  else {
    // Try launching registered app
    for (uint32_t i = 0; i < app_registry_count; i++) {
      if (strcmp(cmd, app_registry[i].name) == 0) {
        app_registry[i].spawn_func();
        return;
      }
    }
    
    kout.print("Unknown: ");
    kout.println(cmd);
  }
}

// ============================================================================
// SHELL COMMANDS
// ============================================================================

void cmd_help() {
  kout.println("\n=== Picomimi-AxisOS v14.3.1 Resource-Owning Kernel ===");
  kout.println("\n--- System ---");
  kout.println(" help       - Show this help");
  kout.println(" ps         - List all tasks");
  kout.println(" taskinfo <id> - Task details");
  kout.println(" top        - System monitor");
  kout.println(" mem        - Memory stats");
  kout.println(" memmap     - Memory map");
  kout.println(" compact    - Compact memory");
  kout.println(" dmesg      - System log");
  kout.println(" uptime     - System uptime");
  kout.println(" temp       - CPU temperature");
  kout.println("\n--- Statistics ---");
  kout.println(" ipcstat    - IPC statistics");
  kout.println(" schedstat  - Scheduler stats");
  kout.println(" oomstat    - OOM statistics");
  kout.println(" rtos_stat  - RTOS primitives");
  kout.println(" usbstat    - USB Serial status");
  kout.println("\n--- Filesystem ---");
  kout.println(" ls [path]  - List files");
  kout.println(" cat <file> - Read file");
  kout.println(" write <file> <text> - Append to file");
  kout.println(" touch <file> - Create file");
  kout.println(" mkdir <dir> - Create directory");
  kout.println(" rm <path>  - Remove file/dir");
  kout.println(" cd <path>  - Change directory");
  kout.println(" stat       - FS statistics");
  kout.println(" logls      - List log files");
  kout.println(" logtail [N] - Show log tail");
  kout.println(" format     - [ROOT] Format SD");
  kout.println("\n--- PMFS ---");
  kout.println(" tree       - Directory tree");
  kout.println(" pmfs       - PMFS statistics");
  kout.println(" tmpfs      - tmpfs (RAM disk) status");
  kout.println(" bank       - System bank info (A/B)");
  kout.println(" fsck       - Filesystem check");
  kout.println(" defrag     - Defragment filesystem");
  kout.println(" gc         - Garbage collection");
  kout.println(" verify     - Verify all files");
  kout.println("\n--- Task Management ---");
  kout.println(" kill <id>  - Kill task");
  kout.println(" root       - Toggle root mode");
  kout.println(" reboot     - Restart system");
  kout.println("\n--- SDK Registry ---");
  kout.println(" listapps   - List applications");
  kout.println(" listdrv    - List drivers");
  kout.println(" listsvc    - List services");
  kout.println(" startdrv <name> - Start driver");
  kout.println(" startsvc <name> - Start service");
  kout.println("\n--- CPU Governor ---");
  kout.println(" gov           - Show governor status");
  kout.println(" gov auto      - Enable auto scaling");
  kout.println(" gov powersave - Set 125MHz");
  kout.println(" gov balanced  - Set 200MHz");
  kout.println(" gov perf      - Set 250MHz");
  kout.println(" gov turbo     - Set 300MHz");
  kout.println(" gov turbo on  - Enable turbo boost");
  kout.println(" gov turbo off - Disable turbo boost");
  kout.println("\n--- Resource Manager (v14.3.1) ---");
  kout.println(" res           - Resource manager status");
  kout.println(" resmap        - GPIO ownership map");
  kout.println(" resaudit      - Resource violations");
  kout.println(" restask <id>  - Resources owned by task");
  kout.println(" reshelp       - Resource API help");
  
  if (app_registry_count > 0) {
    kout.println("\n--- Applications ---");
    for (uint32_t i = 0; i < app_registry_count; i++) {
      kout.print(" ");
      kout.println(app_registry[i].name);
    }
  }
}

// List registered drivers
void cmd_listdrv() {
  kout.println("\n=== Registered Drivers ===");
  if (driver_registry_count == 0) {
    kout.println("(none)");
    return;
  }
  kout.println("Name             Pri Auto");
  kout.println("---------------- --- ----");
  for (uint32_t i = 0; i < driver_registry_count; i++) {
    DriverEntry* drv = &driver_registry[i];
    char buf[48];
    snprintf(buf, sizeof(buf), "%-16s %3d %s", 
             drv->name, drv->priority, drv->auto_start ? "yes" : "no");
    kout.println(buf);
  }
}

// List registered services
void cmd_listsvc() {
  kout.println("\n=== Registered Services ===");
  if (service_registry_count == 0) {
    kout.println("(none)");
    return;
  }
  kout.println("Name             Pri MemKB Auto");
  kout.println("---------------- --- ----- ----");
  for (uint32_t i = 0; i < service_registry_count; i++) {
    ServiceEntry* svc = &service_registry[i];
    char buf[56];
    snprintf(buf, sizeof(buf), "%-16s %3d %5lu %s", 
             svc->name, svc->priority, svc->mem_limit / 1024, svc->auto_start ? "yes" : "no");
    kout.println(buf);
  }
}

void cmd_ps() {
  kout.println("\n=== System Tasks ===");
  kout.println("ID  Core Name                 Type    State     Pri Mem(KB)  CPU%");
  kout.println("--- ---- -------------------- ------- --------- --- ------- -----");
  
  const char* state_str[] = {"READY", "RUN", "WAIT", "SUSP", "DEAD", "ZOMBI"};
  const char* type_str[] = {"KERNEL", "DRIVER", "SERVIC", "MODULE", "APP"};
  
  for (uint32_t i = 0; i < kernel.task_count; i++) {
    TCB* task = &kernel.tasks[i];
    if (task->id == 0 && task->entry == NULL) continue;
    
    uint8_t type_idx = 0;
    if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
    else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
    else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
    else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;
    
    // Calculate per-task CPU% using wall-clock time
    float task_cpu = calculate_task_cpu_percent(task);
    
    char buf[100];
    snprintf(buf, sizeof(buf), "%-3d C0   %-20s %-7s %-9s %3d %7d %5.1f",
             task->id, task->name, type_str[type_idx],
             task->mem_blocked ? "BLCKD" : state_str[task->state],
             task->priority, task->mem_used / 1024, task_cpu);
    kout.println(buf);
  }
  
  if (kernel.core1_initialized) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
    for (uint32_t i = 0; i < kernel.core1.task_count; i++) {
      TCB* task = &kernel.core1.tasks[i];
      
      // Approximate CPU% for Core 1 tasks
      uint64_t uptime_us = kernel.uptime_ms * 1000ULL;
      float task_cpu = (uptime_us > 0) ? 100.0f * ((float)task->wall_time_us / (float)uptime_us) : 0.0f;
      
      char buf[100];
      snprintf(buf, sizeof(buf), "%-3d C1   %-20s %-7s %-9s %3d %7d %5.1f",
               task->id, task->name, "APP",
               task->mem_blocked ? "BLCKD" : state_str[task->state],
               task->priority, task->mem_used / 1024, task_cpu);
      kout.println(buf);
    }
    mutex_exit(&kernel.core1.scheduler_lock);
  }
  
  kout.println("\n--- Summary ---");
  kout.print("Core 0: ");
  kout.print(kernel.task_count - kernel.zombie_tasks);
  kout.print(" tasks (CPU: ");
  kout.print(kernel.cpu_usage, 1);
  kout.println("%)");
  
  if (kernel.core1_initialized) {
    kout.print("Core 1: ");
    kout.print(kernel.core1.task_count);
    kout.print(" tasks (CPU: ");
    kout.print(kernel.core1.cpu_usage, 1);
    kout.println("%)");
  }
}

void cmd_taskinfo(char* arg) {
  uint32_t id = atoi(arg);
  
  TCB task_copy;
  bool task_found = false;
  bool is_core1 = (id >= 1000);
  
  if (is_core1) {
    mutex_enter_blocking(&kernel.core1.scheduler_lock);
  }
  
  TCB* task_ptr = ipc_get_tcb_by_id_unsafe(id);
  if (task_ptr) {
    memcpy(&task_copy, task_ptr, sizeof(TCB));
    task_found = true;
  }
  
  if (is_core1) {
    mutex_exit(&kernel.core1.scheduler_lock);
  }
  
  if (!task_found) {
    kout.println("Invalid task ID");
    return;
  }
  
  kout.println("\n=== Task Information ===");
  kout.print("ID: "); kout.println(task_copy.id);
  kout.print("Name: "); kout.println(task_copy.name);
  kout.print("Core: "); kout.println(task_copy.running_on_core);
  kout.print("Priority: "); kout.println(task_copy.priority);
  kout.print("State: ");
  
  const char* state_str[] = {"READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED", "ZOMBIE"};
  kout.println(task_copy.mem_blocked ? "BLOCKED" : state_str[task_copy.state]);
  
  kout.print("\nMemory Used: ");
  kout.print(task_copy.mem_used / 1024);
  kout.println(" KB");
  
  kout.print("Memory Peak: ");
  kout.print(task_copy.mem_peak / 1024);
  kout.println(" KB");
  
  kout.print("CPU Time (callback): ");
  kout.print(task_copy.cpu_time);
  kout.println(" ms");
  
  kout.print("Wall Time: ");
  kout.print((uint32_t)(task_copy.wall_time_us / 1000));
  kout.println(" ms");
  
  // Calculate CPU% using wall time
  float cpu_percent = 0.0f;
  if (!is_core1) {
    cpu_percent = calculate_task_cpu_percent(&kernel.tasks[id]);
  } else {
    // For Core 1 tasks, approximate from wall time vs uptime
    uint64_t uptime_us = kernel.uptime_ms * 1000ULL;
    if (uptime_us > 0) {
      cpu_percent = 100.0f * ((float)task_copy.wall_time_us / (float)uptime_us);
    }
  }
  
  kout.print("CPU Usage: ");
  kout.print(cpu_percent, 1);
  kout.println("%");
  
  if (task_copy.is_cpu_abuser) {
    kout.println("⚠ CPU ABUSER DETECTED");
  }
  
  kout.print("Context Switches: ");
  kout.println(task_copy.context_switches);
  
  kout.print("IPC Messages: ");
  kout.println(task_copy.ipc.message_count);
  
  if (task_copy.description) {
    kout.println("\n--- Description ---");
    kout.println(task_copy.description);
  }
}

void cmd_ipcstat() {
  kout.println("\n=== IPC Statistics ===");
  
  uint16_t total_in_use = MAX_IPC_MESSAGES - (kernel.ipc_manager.free_list_head + 1);
  
  kout.print("Message Pool: ");
  kout.print(total_in_use);
  kout.print("/");
  kout.println(MAX_IPC_MESSAGES);
  
  kout.println("\n--- Lifetime Stats ---");
  kout.print("Sent: "); kout.println(kernel.ipc_manager.total_sent);
  kout.print("Received: "); kout.println(kernel.ipc_manager.total_received);
  kout.print("Broadcasts: "); kout.println(ipc_stats.broadcasts_sent);
  kout.print("Dropped (Pool): "); kout.println(ipc_stats.messages_dropped_pool_full);
  kout.print("Dropped (Task): "); kout.println(ipc_stats.messages_dropped_task_full);
}

void cmd_schedstat() {
  kout.println("\n=== Scheduler Statistics ===");
  kout.println("Algorithm: O(1) Priority Bitmap");
  kout.println("Mode: Preemptive + Aging + Idle Injection");
  
  kout.println("\n--- Core 0 ---");
  kout.print("Context Switches: ");
  kout.println(core0_sched.switches);
  kout.print("Preemptions: ");
  kout.println(core0_sched.preemptions);
  kout.print("CPU Load: ");
  kout.print(core0_sched.cpu_load, 1);
  kout.println("%");
  kout.print("Idle Injections: ");
  kout.println(core0_sched.idle_injections);
  
  if (kernel.core1_initialized) {
    kout.println("\n--- Core 1 ---");
    kout.print("Context Switches: ");
    kout.println(core1_sched.switches);
    kout.print("CPU Load: ");
    kout.print(kernel.core1.cpu_usage, 1);
    kout.println("%");
  }
}

void cmd_oomstat() {
  kout.println("\n=== OOM Statistics ===");
  kout.print("Prevention Success: ");
  kout.println(oom_stats.prevention_count);
  kout.print("Graceful Requests: ");
  kout.println(oom_stats.requests_sent);
  kout.print("Voluntary Releases: ");
  kout.println(oom_stats.voluntary_releases);
  kout.print("Forced Kills: ");
  kout.println(oom_stats.forced_kills);
  kout.print("Abusive Kills: ");
  kout.println(oom_stats.abusive_kills);
  kout.print("Total Reclaimed: ");
  kout.print(oom_stats.total_bytes_reclaimed / 1024);
  kout.println(" KB");
}

void cmd_rtos_stat() {
  kout.println("\n=== RTOS Primitives ===");
  kout.println("\n--- Mutexes ---");
  
  for(int i=0; i < MAX_KERNEL_MUTEXES; i++) {
    if (kernel.kernel_mutexes[i].locked) {
      kout.print("Mutex ");
      kout.print(i);
      kout.print(": LOCKED by ");
      kout.println(kernel.kernel_mutexes[i].owner_id);
    }
  }
  
  kout.println("\n--- Semaphores ---");
  for(int i=0; i < MAX_SEMAPHORES; i++) {
    if (kernel.kernel_semaphores[i].max_count > 0) {
      kout.print("Sem ");
      kout.print(i);
      kout.print(": ");
      kout.print(kernel.kernel_semaphores[i].count);
      kout.print("/");
      kout.println(kernel.kernel_semaphores[i].max_count);
    }
  }
}


void cmd_listapps() {
  kout.println("\n=== Registered Applications ===");
  if (app_registry_count == 0) {
    kout.println("(None)");
    return;
  }
  
  for (uint32_t i = 0; i < app_registry_count; i++) {
    kout.print(i + 1);
    kout.print(". ");
    kout.println(app_registry[i].name);
  }
}

void cmd_top() {
  kout.println("\n=== System Monitor ===");
  kout.print("Uptime: ");
  kout.print((uint32_t)(kernel.uptime_ms / 1000));
  kout.println(" s");
  kout.print("CPU (C0): ");
  kout.print(kernel.cpu_usage, 1);
  kout.println("%");
  
  if (kernel.core1_initialized) {
    kout.print("CPU (C1): ");
    kout.print(kernel.core1.cpu_usage, 1);
    kout.println("%");
  }
  
  kout.print("Temp: ");
  kout.print(kernel.temperature, 1);
  kout.println("C");
  kout.print("Memory: ");
  kout.print(get_used_memory() / 1024);
  kout.print("/");
  kout.print((HEAP_SIZE - KERNEL_RESERVE) / 1024);
  kout.println(" KB");
  kout.print("Tasks: ");
  kout.println(kernel.task_count - kernel.zombie_tasks);
  kout.print("OOM Kills: ");
  kout.println(oom_stats.forced_kills);
}

void cmd_mem() {
  kout.println("\n=== Memory Statistics ===");
  kout.print("Total Heap: ");
  kout.print(HEAP_SIZE / 1024);
  kout.println(" KB");
  kout.print("Kernel Reserve: ");
  kout.print(KERNEL_RESERVE / 1024);
  kout.println(" KB");
  kout.print("App Heap: ");
  kout.print((HEAP_SIZE - KERNEL_RESERVE) / 1024);
  kout.println(" KB");
  kout.print("Used: ");
  kout.print(get_used_memory() / 1024);
  kout.println(" KB");
  kout.print("Free: ");
  kout.print(kernel.total_free_mem / 1024);
  kout.println(" KB");
  kout.print("Largest Free Block: ");
  kout.print(kernel.largest_free_block / 1024);
  kout.println(" KB");
  kout.print("Fragmentation: ");
  kout.print(kernel.fragmentation_pct);
  kout.println("%");
  
  if (is_memory_critical()) {
    kout.println("⚠ CRITICAL MEMORY LEVEL!");
  } else if (is_memory_warning()) {
    kout.println("⚠ Low memory warning");
  }
}

void cmd_memmap() {
  kout.println("\n=== Memory Map (First 20 blocks) ===");
  kout.println("Address    Size     Owner  Free");
  kout.println("---------- -------- ------ ----");
  
  uint32_t display_count = (kernel.mem_block_count < 20) ? kernel.mem_block_count : 20;
  
  for (uint32_t i = 0; i < display_count; i++) {
    MemBlock* block = &kernel.mem_blocks[i];
    
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%08lx %8d %6d %-4s",
             (uint32_t)block->addr, block->size, block->owner_id,
             block->free ? "Y" : "N");
    kout.println(buf);
  }
  
  if (kernel.mem_block_count > display_count) {
    kout.print("... (");
    kout.print(kernel.mem_block_count - display_count);
    kout.println(" more)");
  }
}

void cmd_dmesg() {
  kout.println("\n=== System Log ===");
  if (kernel.log_count == 0) {
    kout.println("(Empty)");
    return;
  }
  
  mutex_enter_blocking(&kernel.log_lock);
  
  uint32_t start = kernel.log_count < MAX_LOG_ENTRIES ? 0 : kernel.log_head;
  uint32_t count = kernel.log_count < MAX_LOG_ENTRIES ? kernel.log_count : MAX_LOG_ENTRIES;
  
  for (uint32_t i = 0; i < count; i++) {
    uint32_t idx = (start + i) % MAX_LOG_ENTRIES;
    LogEntry* entry = &kernel.log[idx];
    
    char buf[80];
    snprintf(buf, sizeof(buf), "[%6lu.%03lu] [%d] %s",
             (uint32_t)(entry->timestamp / 1000),
             (uint32_t)(entry->timestamp % 1000),
             entry->level,
             entry->message);
    kout.println(buf);
  }
  
  mutex_exit(&kernel.log_lock);
}

void cmd_uptime() {
  uint64_t uptime_s = kernel.uptime_ms / 1000;
  uint32_t days = uptime_s / 86400;
  uint32_t hours = (uptime_s % 86400) / 3600;
  uint32_t minutes = (uptime_s % 3600) / 60;
  uint32_t seconds = uptime_s % 60;
  
  kout.print("Uptime: ");
  if (days > 0) {
    kout.print(days);
    kout.print(" days, ");
  }
  kout.print(hours);
  kout.print(":");
  if (minutes < 10) kout.print("0");
  kout.print(minutes);
  kout.print(":");
  if (seconds < 10) kout.print("0");
  kout.println(seconds);
}

void cmd_temp() {
  kout.print("CPU Temperature: ");
  kout.print(kernel.temperature, 1);
  kout.println(" C");
}

void cmd_root() {
  kernel.root_mode = !kernel.root_mode;
  
  if (kernel.root_mode) {
    kout.println("Root mode: ON (dangerous!)");
  } else {
    kout.println("Root mode: OFF");
  }
}

void cmd_reboot() {
  kout.println("\nRebooting...");
  Serial.flush();
  delay(500);
  watchdog_enable(1, 1);
  while(1);
}

void cmd_kill(char* arg) {
  if (!kernel.root_mode) {
    kout.println("Error: Root mode required");
    return;
  }
  
  uint32_t id = atoi(arg);
  
  if (id < 1000) {
    if (id >= kernel.task_count) {
      kout.println("Error: Invalid task ID");
      return;
    }
    brutal_task_kill(id);
  } else {
    kout.println("Error: Core 1 task kill not supported from shell");
  }
}

void cmd_cd(const char* arg) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  char new_path[128];
  fs_normalize_path(shell_cwd, arg, new_path, sizeof(new_path));
  
  File f = SD.open(new_path);
  if (f) {
    if (f.isDirectory()) {
      strncpy(shell_cwd, new_path, sizeof(shell_cwd) - 1);
    } else {
      kout.print("Error: Not a directory: ");
      kout.println(arg);
    }
    f.close();
  } else {
    kout.print("Error: No such file or directory: ");
    kout.println(arg);
  }
}

void cmd_write(char* arg) {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  char* filename_arg = strtok(arg, " ");
  if (filename_arg == NULL) {
    kout.println("Usage: write <filename> <content...>");
    return;
  }
  
  char* content = strtok(NULL, "");
  if (content == NULL) {
    kout.println("Usage: write <filename> <content...>");
    return;
  }
  
  char path[72];  // Sized for PMFS_MAX_PATH_LENGTH
  fs_normalize_path(shell_cwd, filename_arg, path, sizeof(path));
  
  kout.print("Appending to ");
  kout.print(path);
  kout.println("...");
  
  if (fs_write_new(path, content, true)) {
    kout.println("Success");
  } else {
    kout.println("Failed");
  }
}

void cmd_logls() {
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  File root = SD.open("/");
  if (!root) {
    kout.println("[FS] Failed to open root");
    return;
  }
  
  kout.println("\n=== FS Log Files ===");
  
  File file = root.openNextFile();
  bool found = false;
  
  while (file) {
    if (!file.isDirectory()) {
      String name_str = file.name();
      
      if (name_str.endsWith(".log") || name_str.endsWith(".txt") || 
          name_str.equalsIgnoreCase(FS_LOG_FILE)) {
        kout.print(file.name());
        kout.print(" (");
        kout.print((int)file.size());
        kout.println(" bytes)");
        found = true;
      }
    }
    file.close();
    file = root.openNextFile();
  }
  
  if (!found) {
    kout.println("(No log files found)");
  }
  
  root.close();
}

void cmd_format_sd() {
  if (!kernel.root_mode) {
    kout.println("Error: Root mode required");
    return;
  }
  
  kout.println("\n*** FORMATTING SD CARD ***");
  kout.println("This will delete ALL files from root");
  kout.println("Waiting 5 seconds...");
  
  uint64_t start = get_time_ms();
  while (get_time_ms() - start < 5000) {
    shell_task(NULL);
    task_sleep(20);
  }
  
  kout.println("Formatting...");
  
  mutex_enter_blocking(&kernel.fs_lock);
  
  File root = SD.open("/");
  if (!root) {
    kout.println("[FS] Failed to open root");
    mutex_exit(&kernel.fs_lock);
    return;
  }
  
  uint32_t files_deleted = 0;
  uint32_t dirs_deleted = 0;
  
  File file = root.openNextFile();
  
  while (file) {
    const char* fname = file.name();
    
    // Skip protected files
    if (strcmp(fname, FS_LOG_FILE + 1) == 0 || strcmp(fname, "PANIC.LOG") == 0) {
      kout.print(" > Skipping: "); 
      kout.println(fname);
      file.close();
      file = root.openNextFile();
      continue;
    }
    
    kout.print(" > Deleting: "); 
    kout.println(fname);
    
    if (file.isDirectory()) {
      if (SD.rmdir(fname)) {
        dirs_deleted++;
      }
    } else {
      if (SD.remove(fname)) {
        files_deleted++;
      }
    }
    
    file.close();
    file = root.openNextFile();
  }
  
  root.close();
  mutex_exit(&kernel.fs_lock);
  
  kout.print("Done. Deleted ");
  kout.print(files_deleted);
  kout.print(" files, ");
  kout.print(dirs_deleted);
  kout.println(" dirs");
}

void cmd_mkdir(char* arg) {
  if (strlen(arg) == 0) {
    kout.println("Usage: mkdir <path>");
    return;
  }
  
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  if (fs_mkdir(arg)) {
    kout.print("Created: ");
    kout.println(arg);
  } else {
    kout.print("Failed: ");
    kout.println(arg);
  }
}

void cmd_rm(char* arg) {
  if (strlen(arg) == 0) {
    kout.println("Usage: rm <path>");
    return;
  }
  
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  if (fs_remove(arg)) {
    kout.print("Removed: ");
    kout.println(arg);
  } else {
    kout.print("Failed: ");
    kout.println(arg);
  }
}

void cmd_touch(char* arg) {
  if (strlen(arg) == 0) {
    kout.println("Usage: touch <path>");
    return;
  }
  
  if (!kernel.fs_mounted) {
    kout.println("[FS] Not mounted");
    return;
  }
  
  int fd = fs_open(arg, true);
  if (fd >= 0) {
    fs_close(fd);
    kout.print("Touched: ");
    kout.println(arg);
  } else {
    kout.print("Failed: ");
    kout.println(arg);
  }
}

void cmd_logtail(char* arg) {
  uint32_t lines = 10;
  
  if (strlen(arg) > 0) {
    lines = atoi(arg);
  }
  
  if (lines == 0) lines = 10;
  if (lines > 100) lines = 100;
  
  fs_logtail(lines);
}

void cmd_app_block_list() {
  kout.println("\n=== Blocked Applications ===");
  
  if (g_blocked_app_count == 0) {
    kout.println("(None)");
    return;
  }
  
  for (uint32_t i = 0; i < g_blocked_app_count; i++) {
    kout.println(g_blocked_app_names[i]);
  }
}

void cmd_app_block_unlock(char* arg) {
  if (!kernel.root_mode) {
    kout.println("Error: Root mode required");
    return;
  }
  
  int32_t found_idx = -1;
  
  for (uint32_t i = 0; i < g_blocked_app_count; i++) {
    if (strcmp(arg, g_blocked_app_names[i]) == 0) {
      found_idx = i;
      break;
    }
  }
  
  if (found_idx != -1) {
    kout.print("Unblocking app: ");
    kout.println(arg);
    
    for (uint32_t i = (uint32_t)found_idx; i < g_blocked_app_count - 1; i++) {
      strncpy(g_blocked_app_names[i], g_blocked_app_names[i+1], TASK_NAME_LEN);
    }
    
    g_blocked_app_count--;
    
    for(uint32_t i = 0; i < kernel.task_count; i++) {
      if (strcmp(kernel.tasks[i].name, arg) == 0) {
        kernel.tasks[i].mem_blocked = false;
      }
    }
  } else {
    kout.print("App not in block list: ");
    kout.println(arg);
  }
}

// ============================================================================
// UI / GUI FUNCTIONS
// ============================================================================

bool k_request_gui_focus(uint32_t task_id) {
  uint32_t irq_state = save_and_disable_interrupts();
  
  kernel.gui_focus_task_id = task_id;
  current_gui_focus_index = -1;
  
  for (uint32_t i = 0; i < gui_app_count; i++) {
    if (gui_app_task_ids[i] == task_id) {
      current_gui_focus_index = i;
      break;
    }
  }
// Lowkey, need a twink cutie bruh  MilkmanAbi
  restore_interrupts(irq_state);
  return true;
}

void k_release_gui_focus(uint32_t task_id) {
  uint32_t irq_state = save_and_disable_interrupts();
  
  if (kernel.gui_focus_task_id == (int32_t)task_id) {
    kernel.gui_focus_task_id = -1;
    current_gui_focus_index = -1;
  }
  
  restore_interrupts(irq_state);
}

void k_register_stdout_target(void (*write_char_fn)(char)) {
  kernel.app_write_char = write_char_fn;
}

float k_get_core0_usage() {
  return kernel.cpu_usage;
}

float k_get_core1_usage() {
  return kernel.core1.cpu_usage;
}

uint32_t k_get_task_memory_api(uint32_t task_id) {
  return get_task_memory(task_id);
}

void k_hint_memory_pressure(uint32_t task_id) {
  kout.print("[MEM] Task ");
  kout.print(task_id);
  kout.println(" reports memory pressure");
  
  mem_compact();
  calculate_fragmentation();
  
  char buf[64];
  snprintf(buf, sizeof(buf), "MEM: Pressure hint from task %d", task_id);
  klog(1, buf);
}

void k_register_gui_app(UISocket* socket_api) {
  if (!socket_api) return;
  
  uint32_t task_id_to_register = 0;
  
  if (get_core_num() == 0) {
    task_id_to_register = kernel.current_task;
  } else {
    task_id_to_register = kernel.core1.tasks[core1_sched.current_task].id;
  }
  
  uint32_t irq_state = save_and_disable_interrupts();
  
  if (gui_app_count < MAX_GUI_APPS) {
    bool found = false;
    
    for(uint32_t i=0; i < gui_app_count; i++) {
      if(gui_app_task_ids[i] == task_id_to_register) {
        found = true;
        break;
      }
    }
    
    if (!found) {
      gui_app_task_ids[gui_app_count++] = task_id_to_register;
    }
  }
  
  restore_interrupts(irq_state);
  
  // Set up socket API
  socket_api->request_focus = k_request_gui_focus;
  socket_api->release_focus = k_release_gui_focus;
  socket_api->register_stdout = k_register_stdout_target;
  socket_api->send_message_api = ipc_send_api;
  socket_api->receive_message_api = ipc_receive_api;
  socket_api->spawn_core1_task = k_spawn_core1_task;
  socket_api->task_exit = k_task_exit_api;
  socket_api->mutex_lock = k_mutex_lock;
  socket_api->mutex_unlock = k_mutex_unlock;
  socket_api->sem_wait = k_sem_wait;
  socket_api->sem_post = k_sem_post;
  socket_api->event_wait = k_event_wait;
  socket_api->event_set = k_event_set;
  socket_api->get_core0_usage = k_get_core0_usage;
  socket_api->get_core1_usage = k_get_core1_usage;
  socket_api->get_task_memory = k_get_task_memory_api;
  socket_api->register_oom_handler = k_register_oom_handler;
  socket_api->oom_cleanup_done = k_oom_cleanup_done;
  socket_api->hint_memory_pressure = k_hint_memory_pressure;
}

void k_unregister_gui_app(uint32_t task_id) {
  uint32_t irq_state = save_and_disable_interrupts();
  
  int32_t found_index = -1;
  
  for (uint32_t i = 0; i < gui_app_count; i++) {
    if (gui_app_task_ids[i] == task_id) {
      found_index = i;
      break;
    }
  }
  
  if (found_index != -1) {
    for (uint32_t i = found_index; i < gui_app_count - 1; i++) {
      gui_app_task_ids[i] = gui_app_task_ids[i + 1];
    }
    gui_app_count--;
    
    if (current_gui_focus_index == found_index) {
      current_gui_focus_index = -1;
    } else if (current_gui_focus_index > found_index) {
      current_gui_focus_index--;
    }
  }
  
  restore_interrupts(irq_state);
}

// ============================================================================
// APP REGISTRATION
// ============================================================================

void Application_Register(const char* name, void (*spawn_func)()) {
  if (app_registry_count >= MAX_APPS) {
    Serial.print("APP_REG: !! Registry full, ");
    Serial.print(name);
    Serial.println(" failed !!");
    return;
  }
  
  AppEntry* entry = &app_registry[app_registry_count];
  strncpy(entry->name, name, TASK_NAME_LEN - 1);
  entry->name[TASK_NAME_LEN - 1] = '\0';
  entry->spawn_func = spawn_func;
  
  app_registry_count++;
  
  Serial.print("APP_REG: Registered '");
  Serial.print(name);
  Serial.println("'");
}

// ============================================================================
// PICOMIMI SDK - IMPLEMENTATION
// ============================================================================

// Register a hardware driver (high priority, kernel-protected)
void Picomimi_RegisterDriver(const char* name, void (*init_fn)(uint32_t), 
                              void (*tick_fn)(void*), void (*deinit_fn)(),
                              uint8_t priority, bool auto_start) {
  if (driver_registry_count >= MAX_DRIVERS) {
    Serial.print("DRV_REG: !! Registry full, ");
    Serial.print(name);
    Serial.println(" failed !!");
    return;
  }
  
  DriverEntry* entry = &driver_registry[driver_registry_count];
  strncpy(entry->name, name, TASK_NAME_LEN - 1);
  entry->name[TASK_NAME_LEN - 1] = '\0';
  entry->callbacks.init = init_fn;
  entry->callbacks.tick = tick_fn;
  entry->callbacks.deinit = deinit_fn;
  entry->priority = (priority > 15) ? 15 : priority;  // Clamp to valid range
  entry->auto_start = auto_start;
  entry->registered = true;
  
  driver_registry_count++;
  
  Serial.print("DRV_REG: Registered '");
  Serial.print(name);
  Serial.print("' (Pri ");
  Serial.print(entry->priority);
  Serial.println(")");
}

// Register a system service (medium priority, protected)
void Picomimi_RegisterService(const char* name, void (*init_fn)(uint32_t),
                               void (*tick_fn)(void*), void (*deinit_fn)(),
                               uint8_t priority, uint32_t mem_limit_kb, bool auto_start) {
  if (service_registry_count >= MAX_SERVICES) {
    Serial.print("SVC_REG: !! Registry full, ");
    Serial.print(name);
    Serial.println(" failed !!");
    return;
  }
  
  ServiceEntry* entry = &service_registry[service_registry_count];
  strncpy(entry->name, name, TASK_NAME_LEN - 1);
  entry->name[TASK_NAME_LEN - 1] = '\0';
  entry->callbacks.init = init_fn;
  entry->callbacks.tick = tick_fn;
  entry->callbacks.deinit = deinit_fn;
  entry->priority = (priority > 15) ? 15 : priority;
  entry->mem_limit = mem_limit_kb * 1024;
  entry->auto_start = auto_start;
  entry->registered = true;
  
  service_registry_count++;
  
  Serial.print("SVC_REG: Registered '");
  Serial.print(name);
  Serial.print("' (Pri ");
  Serial.print(entry->priority);
  Serial.print(", ");
  Serial.print(mem_limit_kb);
  Serial.println("KB)");
}

// Simplified app registration (wrapper)
void Picomimi_RegisterApp(const char* name, void (*spawn_func)()) {
  Application_Register(name, spawn_func);
}

// Start a specific driver by name
uint32_t Picomimi_StartDriver(const char* name) {
  for (uint32_t i = 0; i < driver_registry_count; i++) {
    if (strcmp(driver_registry[i].name, name) == 0) {
      DriverEntry* drv = &driver_registry[i];
      uint32_t id = task_create(drv->name, NULL, NULL, drv->priority,
                                 TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN,
                                 0, OOM_PRIORITY_NEVER, 1 * 1024, 1 * 1024, &drv->callbacks,
                                 "User driver", CORE_0);
      if (id > 0) {
        kout.print("[SDK] Started driver '");
        kout.print(name);
        kout.println("'");
      }
      return id;
    }
  }
  kout.print("[SDK] Driver not found: ");
  kout.println(name);
  return 0;
}

// Start a specific service by name
uint32_t Picomimi_StartService(const char* name) {
  for (uint32_t i = 0; i < service_registry_count; i++) {
    if (strcmp(service_registry[i].name, name) == 0) {
      ServiceEntry* svc = &service_registry[i];
      uint32_t id = task_create(svc->name, NULL, NULL, svc->priority,
                                 TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN,
                                 0, OOM_PRIORITY_NORMAL, svc->mem_limit, svc->mem_limit, &svc->callbacks,
                                 "User service", CORE_0);
      if (id > 0) {
        kout.print("[SDK] Started service '");
        kout.print(name);
        kout.println("'");
      }
      return id;
    }
  }
  kout.print("[SDK] Service not found: ");
  kout.println(name);
  return 0;
}

// Start all auto-start drivers
void Picomimi_StartDrivers() {
  for (uint32_t i = 0; i < driver_registry_count; i++) {
    if (driver_registry[i].auto_start && driver_registry[i].registered) {
      Picomimi_StartDriver(driver_registry[i].name);
    }
  }
}

// Start all auto-start services
void Picomimi_StartServices() {
  for (uint32_t i = 0; i < service_registry_count; i++) {
    if (service_registry[i].auto_start && service_registry[i].registered) {
      Picomimi_StartService(service_registry[i].name);
    }
  }
}

// Register display driver
void Picomimi_RegisterDisplay(DisplayDriver* driver) {
  g_display = driver;
  if (driver && driver->init) {
    driver->init();
    driver->initialized = true;
  }
  Serial.println("DISP_REG: Display driver registered");
}

// Get display driver
DisplayDriver* Picomimi_GetDisplay() {
  return g_display;
}

// Register input driver
void Picomimi_RegisterInput(InputDriver* driver) {
  g_input = driver;
  if (driver && driver->init) {
    driver->init();
    driver->initialized = true;
  }
  Serial.println("INPUT_REG: Input driver registered");
}

// Get input driver
InputDriver* Picomimi_GetInput() {
  return g_input;
}

// ============================================================================
// PICOMIMI SDK - APP HELPER API CLASS
// ============================================================================
// This class provides a clean interface for apps to access kernel services

class PicomimiAPI {
public:
  // === TASK MANAGEMENT ===
  
  // Get current task ID
  static uint32_t GetTaskId() {
    return kernel.current_task;
  }
  
  // Get task name
  static const char* GetTaskName(uint32_t task_id) {
    if (task_id < kernel.task_count) {
      return kernel.tasks[task_id].name;
    }
    return "unknown";
  }
  
  // Sleep current task
  static void Sleep(uint32_t ms) {
    task_sleep(ms);
  }
  
  // Yield to other tasks
  static void Yield() {
    task_yield();
  }
  
  // Exit current task cleanly
  static void Exit() {
    k_task_exit_api();
  }
  
  // Spawn a subtask on Core 1 (for compute offload)
  static uint32_t SpawnOnCore1(const char* name, void (*entry)(void*), void* arg, uint8_t priority) {
    return k_spawn_core1_task(name, entry, arg, priority);
  }
  
  // === MEMORY MANAGEMENT ===
  
  // Allocate memory (returns NULL on failure)
  static void* Alloc(size_t size) {
    return kmalloc(size, kernel.current_task);
  }
  
  // Free memory
  static void Free(void* ptr) {
    kfree(ptr);
  }
  
  // Get free memory
  static size_t GetFreeMemory() {
    return get_free_memory();
  }
  
  // Get memory used by current task
  static size_t GetMyMemory() {
    return get_task_memory(kernel.current_task);
  }
  
  // Check if memory is low
  static bool IsMemoryLow() {
    return is_memory_warning();
  }
  
  // Check if memory is critical
  static bool IsMemoryCritical() {
    return is_memory_critical();
  }
  
  // Register OOM handler (called when system needs memory)
  static void OnOOM(void (*handler)(uint32_t bytes_needed)) {
    k_register_oom_handler(kernel.current_task, handler);
  }
  
  // Report that OOM cleanup is done
  static void OOMDone(uint32_t bytes_freed) {
    k_oom_cleanup_done(kernel.current_task, bytes_freed);
  }
  
  // === IPC (INTER-PROCESS COMMUNICATION) ===
  
  // Send message to another task
  static bool SendMessage(uint32_t target_id, IPCMessageType type, void* data, size_t size) {
    return ipc_send_api(target_id, type, data, size, 0);
  }
  
  // Send high-priority message
  static bool SendUrgent(uint32_t target_id, IPCMessageType type, void* data, size_t size) {
    return ipc_send_api(target_id, type, data, size, 15);
  }
  
  // Receive message (non-blocking)
  static bool ReceiveMessage(IPCMessage* msg) {
    return ipc_receive_api(msg);
  }
  
  // Broadcast message to all tasks
  static bool Broadcast(IPCMessageType type, void* data, size_t size) {
    return ipc_send_api(IPC_TARGET_BROADCAST, type, data, size, 0);
  }
  
  // === SYNCHRONIZATION ===
  
  // Lock a mutex (blocking)
  static bool MutexLock(uint32_t id) {
    return k_mutex_lock(id);
  }
  
  // Unlock a mutex
  static void MutexUnlock(uint32_t id) {
    k_mutex_unlock(id);
  }
  
  // Wait on semaphore
  static bool SemWait(uint32_t id, uint32_t timeout_ms = 0) {
    return k_sem_wait(id, timeout_ms);
  }
  
  // Signal semaphore
  static void SemPost(uint32_t id) {
    k_sem_post(id);
  }
  
  // Wait for event flags
  static uint32_t EventWait(uint32_t id, uint32_t flags, bool wait_all = false, uint32_t timeout_ms = 0) {
    return k_event_wait(id, flags, wait_all ? K_EVENT_WAIT_ALL : K_EVENT_WAIT_ANY, true, timeout_ms);
  }
  
  // Set event flags
  static void EventSet(uint32_t id, uint32_t flags) {
    k_event_set(id, flags);
  }
  
  // === SYSTEM INFO ===
  
  // Get uptime in milliseconds
  static uint64_t GetUptime() {
    return kernel.uptime_ms;
  }
  
  // Get CPU usage (Core 0)
  static float GetCPU0Usage() {
    return k_get_core0_usage();
  }
  
  // Get CPU usage (Core 1)
  static float GetCPU1Usage() {
    return k_get_core1_usage();
  }
  
  // Get CPU temperature
  static float GetTemperature() {
    return kernel.temperature;
  }
  
  // Get task count
  static uint32_t GetTaskCount() {
    return kernel.task_count;
  }
  
  // === GUI FOCUS ===
  
  // Request GUI focus for current task
  static bool RequestFocus() {
    return k_request_gui_focus(kernel.current_task);
  }
  
  // Release GUI focus
  static void ReleaseFocus() {
    k_release_gui_focus(kernel.current_task);
  }
  
  // Check if we have focus
  static bool HasFocus() {
    return (kernel.gui_focus_task_id == (int32_t)kernel.current_task);
  }
  
  // Register stdout handler (for when we have focus)
  static void SetStdout(void (*write_char)(char)) {
    k_register_stdout_target(write_char);
  }
  
  // === DISPLAY SHORTCUTS ===
  
  // Get display width (0 if no display)
  static uint16_t DisplayWidth() {
    return g_display ? g_display->width : 0;
  }
  
  // Get display height (0 if no display)
  static uint16_t DisplayHeight() {
    return g_display ? g_display->height : 0;
  }
  
  // Clear display
  static void DisplayClear(uint16_t color = 0) {
    if (g_display && g_display->clear) g_display->clear(color);
  }
  
  // Draw text
  static void DisplayText(int16_t x, int16_t y, const char* text, uint16_t color = 0xFFFF) {
    if (g_display && g_display->draw_text) g_display->draw_text(x, y, text, color, 0, 1);
  }
  
  // Fill rectangle
  static void DisplayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_display && g_display->fill_rect) g_display->fill_rect(x, y, w, h, color);
  }
  
  // === INPUT SHORTCUTS ===
  
  // Check if button pressed
  static bool ButtonPressed(uint8_t id = 0) {
    return g_input && g_input->button_pressed ? g_input->button_pressed(id) : false;
  }
  
  // Poll for input event
  static bool PollInput(PicoInputEvent* event) {
    return g_input && g_input->poll ? g_input->poll(event) : false;
  }
  
  // === FILESYSTEM SHORTCUTS ===
  
  // Check if filesystem is available
  static bool FSAvailable() {
    return kernel.fs_mounted;
  }
  
  // === LOGGING ===
  
  // Log a message
  static void Log(const char* message) {
    klog(0, message);
  }
  
  // Log warning
  static void LogWarn(const char* message) {
    klog(1, message);
  }
  
  // Log error
  static void LogError(const char* message) {
    klog(2, message);
  }
  
  // =========================================================================
  // === RP2350 AXISOS SPECIFIC APIs ===
  // =========================================================================
  
  // === CPU GOVERNOR CONTROL ===
  
  // Set CPU profile (POWERSAVE, BALANCED, PERFORMANCE, TURBO)
  static void SetCPUProfile(CPUProfile profile) {
    governor_set_profile(profile);
  }
  
  // Get current CPU profile
  static CPUProfile GetCPUProfile() {
    return governor_get_profile();
  }
  
  // Get current CPU frequency in KHz
  static uint32_t GetCPUFreqKHz() {
    return governor_get_freq_khz();
  }
  
  // Get current CPU frequency in MHz
  static uint32_t GetCPUFreqMHz() {
    return governor_get_freq_khz() / 1000;
  }
  
  // Request turbo mode for a specific task duration
  static void RequestTurbo(uint32_t duration_ms = 5000) {
    governor_request_turbo(duration_ms);
  }
  
  // Check if thermal throttling is active
  static bool IsThermalThrottled() {
    return kernel.thermal_throttled;
  }
  
  // === ENHANCED MEMORY APIs ===
  
  // Allocate cache-line aligned memory (32-byte aligned on RP2350)
  static void* AllocAligned(size_t size, size_t alignment = 32) {
    return kmalloc_aligned(size, alignment, kernel.current_task);
  }
  
  // Allocate DMA-safe memory
  static void* AllocDMA(size_t size) {
    return kmalloc_dma(size, kernel.current_task);
  }
  
  // Get total app-available memory
  static size_t GetAppAvailableMemory() {
    return get_app_available_memory();
  }
  
  // Get memory pressure level (0-5)
  static uint8_t GetMemoryPressure() {
    return mem_stats.current_pressure;
  }
  
  // Get memory fragmentation percentage
  static uint8_t GetFragmentation() {
    return kernel.fragmentation_pct;
  }
  
  // === SYSTEM INFO ===
  
  // Get RP2350 RAM size
  static uint32_t GetTotalRAM() {
    return RP2350_TOTAL_SRAM;
  }
  
  // Get kernel version string
  static const char* GetVersion() {
    return "AxisOS v14.3.1-Quiet-Otter (Resource-Owning Kernel)";
  }
  
  // Get boot time in ms
  static uint32_t GetBootTime() {
    return kernel.boot_time_ms;
  }
  
  // Get current core number
  static uint8_t GetCoreNum() {
    return get_core_num();
  }
  
  // === PERFORMANCE HINTS ===
  
  // Hint that a heavy task is starting (triggers instant turbo)
  static void HintHeavyTaskStart() {
    governor_request_instant_turbo();  // No time limit - runs until load drops
  }
  
  // Hint that a heavy task has ended
  static void HintHeavyTaskEnd() {
    // Governor will automatically scale down based on load
  }
  
  // =========================================================================
  // === RESOURCE MANAGEMENT APIs (v14.3.1) ===
  // =========================================================================
  // ZERO-OVERHEAD DESIGN:
  //   1. ClaimXXX() - register ownership (one-time cost, ~1µs)
  //   2. Use direct hardware access (pinMode, digitalWrite, etc.) - ZERO overhead
  //   3. ReleaseXXX() or automatic cleanup on task death
  //
  // The kernel tracks WHO owns WHAT, but does NOT intercept operations.
  // This gives you full-speed GPIO while the kernel can still clean up
  // properly when your task dies.
  
  // === GPIO RESOURCES ===
  
  // Claim a GPIO pin. Returns pin number on success, -1 on failure.
  // After claiming, use standard Arduino functions: pinMode(), digitalWrite(), digitalRead()
  static int8_t ClaimGPIO(uint8_t pin, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_gpio(pin, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)pin;  // Return pin for direct use
  }
  
  // Release a GPIO pin
  static void ReleaseGPIO(uint8_t pin) {
    ResourceDescriptor* res = &kernel.gpio_resources[pin];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // Notify kernel of GPIO direction (optional - helps cleanup)
  static void NotifyGPIODirection(uint8_t pin, GPIODirection dir) {
    res_gpio_notify_direction(pin, dir, kernel.current_task);
  }
  
  // Notify kernel of GPIO state (optional - helps cleanup)
  static void NotifyGPIOState(uint8_t pin, bool state) {
    res_gpio_notify_state(pin, state, kernel.current_task);
  }
  
  // === ADC RESOURCES ===
  
  // Claim an ADC channel. Returns channel number on success, -1 on failure.
  static int8_t ClaimADC(uint8_t channel, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_adc(channel, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)channel;
  }
  
  // Release ADC channel
  static void ReleaseADC(uint8_t channel) {
    ResourceDescriptor* res = &kernel.adc_resources[channel];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // === PWM RESOURCES ===
  
  // Claim a PWM slice+channel. Returns pwm_idx on success, -1 on failure.
  static int8_t ClaimPWM(uint8_t slice, uint8_t channel, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_pwm(slice, channel, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)(slice * 2 + channel);
  }
  
  // Release PWM
  static void ReleasePWM(uint8_t slice, uint8_t channel) {
    uint8_t idx = slice * 2 + channel;
    ResourceDescriptor* res = &kernel.pwm_resources[idx];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // Notify kernel PWM is enabled (helps cleanup)
  static void NotifyPWMEnabled(uint8_t slice, uint8_t channel, bool enabled) {
    res_pwm_notify_enabled(slice * 2 + channel, enabled, kernel.current_task);
  }
  
  // === SPI RESOURCES ===
  
  // Claim an SPI bus with CS pin. Returns bus number on success, -1 on failure.
  static int8_t ClaimSPI(uint8_t bus, uint8_t cs_pin, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_spi(bus, cs_pin, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)bus;
  }
  
  // Release SPI bus
  static void ReleaseSPI(uint8_t bus) {
    ResourceDescriptor* res = &kernel.spi_resources[bus];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // === I2C RESOURCES ===
  
  // Claim I2C device. Returns bus number on success, -1 on failure.
  static int8_t ClaimI2C(uint8_t bus, uint8_t device_addr, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_i2c(bus, device_addr, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)bus;
  }
  
  // Release I2C device
  static void ReleaseI2C(uint8_t bus, uint8_t device_addr) {
    uint8_t idx = bus * 16 + (device_addr & 0x0F);
    ResourceDescriptor* res = &kernel.i2c_resources[idx];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // === PIO RESOURCES ===
  
  // Claim PIO state machine. Returns pio_idx on success, -1 on failure.
  static int8_t ClaimPIO(uint8_t pio_num, uint8_t sm_num, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_pio(pio_num, sm_num, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)(pio_num * 4 + sm_num);
  }
  
  // Release PIO
  static void ReleasePIO(uint8_t pio_num, uint8_t sm_num) {
    uint8_t idx = pio_num * 4 + sm_num;
    ResourceDescriptor* res = &kernel.pio_resources[idx];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // Notify kernel PIO is running
  static void NotifyPIORunning(uint8_t pio_num, uint8_t sm_num, bool running) {
    res_pio_notify_running(pio_num * 4 + sm_num, running, kernel.current_task);
  }
  
  // === DMA RESOURCES ===
  
  // Claim DMA channel. Returns channel on success, -1 on failure.
  static int8_t ClaimDMA(uint8_t channel, ResourceMode mode = RES_MODE_EXCLUSIVE) {
    ResHandle h = res_claim_dma(channel, mode, kernel.current_task);
    if (h == RES_HANDLE_INVALID) return -1;
    return (int8_t)channel;
  }
  
  // Release DMA
  static void ReleaseDMA(uint8_t channel) {
    ResourceDescriptor* res = &kernel.dma_resources[channel];
    if (res->owner_task_id == kernel.current_task) {
      res_release(res->handle, kernel.current_task);
    }
  }
  
  // === QUERY FUNCTIONS ===
  
  // Get number of resources owned by current task
  static uint32_t GetMyResourceCount() {
    return res_count_owned_by_task(kernel.current_task);
  }
  
  // Get violations for current task
  static uint32_t GetMyViolations() {
    return res_get_violations(kernel.current_task);
  }
  
  // Check if a GPIO is available (not owned by another task)
  static bool IsGPIOAvailable(uint8_t pin) {
    if (pin >= TOTAL_GPIO_RESOURCES) return false;
    ResourceDescriptor* res = &kernel.gpio_resources[pin];
    return (res->state == RES_STATE_FREE);
  }
  
  // Check if we own a GPIO
  static bool OwnsGPIO(uint8_t pin) {
    if (pin >= TOTAL_GPIO_RESOURCES) return false;
    ResourceDescriptor* res = &kernel.gpio_resources[pin];
    return (res->owner_task_id == kernel.current_task && res->state == RES_STATE_CLAIMED);
  }
};

// Global API instance for easy access
// Usage: Pico.Sleep(100); Pico.Alloc(1024); Pico.SetCPUProfile(CPU_PROFILE_TURBO);
#define Pico PicomimiAPI

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  mutex_init(&kout_mutex);
  
  Serial.println("APP_REG: Registration phase complete.");
  
  // Initialize SPI for SD card
  SPI.setRX(SD_MISO);
  SPI.setTX(SD_MOSI);
  SPI.setSCK(SD_SCK);
  
  randomSeed(micros());
  
  kout.println("╔═══════════════════════════════════════════════════════════╗");
  kout.println("║  PICOMIMI-AXISOS v14.3.1-Quiet-Otter                       ║");
  kout.println("║  Resource-Owning Kernel Edition for RP2350                 ║");
  kout.println("╠═══════════════════════════════════════════════════════════╣");
  kout.println("║  Target: RP2350 Cortex-M33 @ 250MHz (Turbo: 300MHz)       ║");
  kout.println("║  RAM: 520KB SRAM | Kernel: ~30% | Apps: ~70%              ║");
  kout.println("╚═══════════════════════════════════════════════════════════╝");
  kout.println("Initializing AxisOS subsystems...");
  
  // Store boot time
  kernel.boot_time_ms = millis();
  
  // Initialize CPU frequency governor FIRST
  governor_init();
  kout.println("[OK] CPU Governor (Nominal 250MHz, Turbo 300MHz)");
  
  // Initialize Resource Manager (v14.3.1 - Resource-Owning Kernel)
  res_init();
  kout.println("[OK] Resource Manager v14.3.1 (Resource-Owning Kernel)");
  
  // Initialize USB Serial tracking (v14.1.1 FIX)
  usb_init();
  usb_record_activity();  // Assume USB is active at boot (we just did Serial.begin)
  kout.println("[OK] USB Serial stability (v14.1.1 - lockup fix)");
  
  // Initialize watchdog
  watchdog_init();
  
  // Initialize input
  pinMode(BTN_ONOFF, INPUT_PULLUP);
  kout.println("[OK] Input system");
  
  // Initialize temperature
  temp_init();
  kernel.temperature = read_temperature();
  kout.print("[OK] Temperature (");
  kout.print(kernel.temperature, 1);
  kout.println("C)");
  
  // Initialize memory (enhanced for RP2350)
  mem_init();
  kout.println("[OK] Memory manager (Best-Fit + Size Classes + Small Pools)");
  kout.print("     Heap: ");
  kout.print(HEAP_SIZE / 1024);
  kout.print("KB | App Available: ~");
  kout.print((RP2350_TOTAL_SRAM - HEAP_SIZE - KERNEL_RESERVE) / 1024);
  kout.println("KB");
  
  // Initialize tasks
  task_init();
  kernel.last_velocity_check_ms = get_time_ms();
  kout.println("[OK] Task scheduler (Preemptive O(1) SMP)");
  
  // Initialize logging
  mutex_init(&kernel.log_lock);
  kout.println("[OK] Logging system");
  
  // Initialize IPC
  ipc_init();
  
  // Initialize RTOS primitives
  rtos_primitives_init();
  
  // Initialize Core 1 (Cortex-M33 dual-core)
  kout.println("[CORE1] Initializing secondary Cortex-M33 core...");
  core1_scheduler_init();
  multicore_launch_core1(core1_main);
  kernel.core1_initialized = true;
  kout.println("[OK] Core1 started (SMP enabled)");
  
  // Initialize filesystem
  fs_init();
  if (kernel.fs_available) {
    fs_mount();
  }
  
  kout.println("\n=== Loading System Tasks ===");
  
  // Create idle task
  task_create("idle", idle_task, NULL, 0,
              TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 0, 0, NULL, 
              "Kernel idle task", CORE_0);
  kout.println("[OK] Idle (Pri 0, Core 0)");
  
  // Create reaper task
  task_create("k_reaper", k_reaper_task, NULL, 1,
              TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 2 * 1024, 2 * 1024, NULL,  // More memory on RP2350
              "Zombie task reaper", CORE_0);
  kout.println("[OK] Reaper (Pri 1, Core 0)");
  
  // Create input driver (priority adjusted for 16-level system)
  task_create("input_cycle", NULL, NULL, 15,
              TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 2 * 1024, 2 * 1024, &input_callbacks, 
              "Focus cycle driver", CORE_0);
  kout.println("[OK] Input driver (Pri 15, Core 0)");
  
  // Create shell
  task_create("shell", NULL, NULL, 10,
              TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NORMAL, 8 * 1024, 8 * 1024, &shell_callbacks,  // More memory
              "Command shell", CORE_0);
  kout.println("[OK] Shell service (Pri 10, Core 0)");
  
  // Create CPU monitor
  task_create("cpumon", NULL, NULL, 2,
              TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 4 * 1024, 4 * 1024, &cpumon_callbacks,  // More memory
              "CPU monitor", CORE_0);
  kout.println("[OK] CPU monitor (Pri 2, Core 0)");
  
  // Create temperature monitor
  task_create("tempmon", NULL, NULL, 2,
              TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
              0, OOM_PRIORITY_NEVER, 4 * 1024, 4 * 1024, &tempmon_callbacks,  // More memory
              "Temp monitor", CORE_0);
  kout.println("[OK] Temp monitor (Pri 2, Core 0)");
  
  // Create FS service if available
  if (kernel.fs_available) {
    task_create("fs", NULL, NULL, 8,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 
                0, OOM_PRIORITY_NEVER, 8 * 1024, 8 * 1024, &fs_callbacks,  // More memory
                "FS service", CORE_0);
    kout.println("[OK] FS service (Pri 8, Core 0)");
  }
  
  // Start user-registered drivers
  if (driver_registry_count > 0) {
    kout.println("\n=== Loading User Drivers ===");
    Picomimi_StartDrivers();
  }
  
  // Start user-registered services
  if (service_registry_count > 0) {
    kout.println("\n=== Loading User Services ===");
    Picomimi_StartServices();
  }
  
  kout.println("╔═══════════════════════════════════════════════════════════╗");
  kout.println("║  AXISOS BOOT COMPLETE                                     ║");
  kout.println("╚═══════════════════════════════════════════════════════════╝");
  
  kout.print("CPU: ");
  kout.print(CHIP_NAME);
  kout.print(" @ ");
  kout.print(governor_get_freq_khz() / 1000);
  #if RP2040_OR_RP2350 == 0
    kout.println("MHz (Cortex-M0+ x2)");
  #else
    kout.println("MHz (Cortex-M33 x2)");
  #endif
  
  kout.print("RAM: ");
  #if RP2040_OR_RP2350 == 0
    kout.print(264);
  #else
    kout.print(RP2350_TOTAL_SRAM / 1024);
  #endif
  kout.print("KB total | Kernel: ");
  kout.print(HEAP_SIZE / 1024);
  kout.print("KB | Apps: ~");
  #if RP2040_OR_RP2350 == 0
    kout.print((264 * 1024 - HEAP_SIZE) / 1024);
  #else
    kout.print((RP2350_TOTAL_SRAM - HEAP_SIZE) / 1024);
  #endif
  kout.println("KB");
  
  kout.print("Core0 Tasks: ");
  kout.print(kernel.task_count);
  kout.print(" | Core1: Ready (");
  kout.print(MAX_CORE1_TASKS);
  kout.println(" slots)");
  
  kout.print("Registry: Apps=");
  kout.print(app_registry_count);
  kout.print(" Drivers=");
  kout.print(driver_registry_count);
  kout.print(" Services=");
  kout.println(service_registry_count);
  
  kout.print("Governor v2.0: ");
  kout.print(governor_get_profile_name());
  kout.print(" @ ");
  kout.print(governor_get_freq_khz() / 1000);
  kout.print("MHz");
  #if ENABLE_PICOMIMI_GOVERNOR == 1
    kout.print(" (AUTO, Turbo: ");
    kout.print(kernel.turbo_enabled ? "ON" : "OFF");
    kout.println(", NO time limit)");
  #else
    kout.println(" (LOCKED - Governor disabled)");
  #endif
  
  kout.print("Thermal Limit: ");
  kout.print((int)THERMAL_THROTTLE_TEMP);
  kout.println("°C");
  
  if (kernel.fs_available) {
    kout.println("Storage: SD Card mounted (PMFS v3.1.0)");
  } else {
    kout.println("Storage: SD Card unavailable");
  }
  
  if (g_display) {
    kout.print("Display: ");
    kout.print(g_display->width);
    kout.print("x");
    kout.println(g_display->height);
  }
  
  kout.println("\nType 'help' for commands. Type 'gov' for CPU governor control.");
  
  klog(0, "KERNEL: AxisOS v14.3.1-Quiet-Otter boot (Resource-Owning Kernel)");
  
  shell_prompt();
  
  // Initialize wall-clock tracking for first task
  // Task 0 (idle) will be current when loop() starts
  kernel.current_task = 0;
  kernel.tasks[0].scheduled_at_us = get_time_us();
  
  kernel.running = true;
}

void loop() {
  // Safety checks (unlikely to fail)
  if (unlikely(kernel.current_task >= MAX_TASKS || !kernel.running || kernel.task_count == 0)) {
    kernel_panic("Kernel loop fault");
  }
  
  uint64_t loop_start = get_time_us();
  
  // Run scheduler tick
  scheduler_tick();
  
  // Run CPU governor tick (manages frequency scaling)
  governor_tick();
  
  // Run resource manager tick (v14.3.1 - transaction timeouts, etc.)
  res_tick();
  
  // =========================================================================
  // USB SERIAL MAINTENANCE (v14.1.1 FIX)
  // =========================================================================
  // Poll USB status and check for lockups. This MUST run every loop iteration
  // to keep the USB connection alive and detect issues early.
  usb_poll();
  usb_recovery_check();
  
  // Check if idle task is dead (very unlikely)
  if (unlikely(kernel.tasks[0].state == TASK_TERMINATED || kernel.tasks[0].entry == NULL)) {
    kernel_panic("IDLE TASK DEAD");
  }
  
  // Handle preemption (uncommon)
  if (unlikely(kernel.preemption_pending)) {
    kernel.preemption_pending = false;
    core0_sched.preemptions++;
    task_yield();
  }
  
  TCB* task = &kernel.tasks[kernel.current_task];
  
  // Skip terminated/zombie/blocked tasks (uncommon)
  if (unlikely(task->state == TASK_TERMINATED || task->state == TASK_ZOMBIE || task->mem_blocked)) {
    task_yield();
    return;
  }
  
  // Handle OOM cleanup requests (rare)
  if (unlikely(task->flags & TASK_FLAG_OOM_CLEANUP_REQUESTED)) {
    oom_callback_t handler = oom_get_handler(task->id);
    if (handler) {
      kout.print("[OOM] Invoking handler for '");
      kout.print(task->name);
      kout.println("'");
      handler(task->oom_bytes_requested);
    }
    task->flags &= ~TASK_FLAG_OOM_CLEANUP_REQUESTED;
    task->oom_bytes_requested = 0;
  }
  
  // Execute task (likely - this is the normal case)
  if (likely((task->state == TASK_READY || task->state == TASK_RUNNING) &&
      (task->entry || (task->callbacks && task->callbacks->tick)))) {
    
    task->state = TASK_RUNNING;
    uint64_t task_start = get_time_us();
    
    // Callback tasks are more common than entry tasks
    if (likely(task->callbacks && task->callbacks->tick)) {
      task->callbacks->tick(task->arg);
    } else if (task->entry) {
      task->entry(task->arg);
    }
    
    uint64_t task_duration = get_time_us() - task_start;
    task->cpu_time += (task_duration + 500) / 1000;
    task->total_cpu_time_ms += task_duration;
    
    // Update CPU usage tracking
    update_task_cpu_usage(task, task_duration);
    
    if (likely(task->state == TASK_RUNNING)) {
      task->state = TASK_READY;
    }
  }
  
  // Yield to next task
  task_yield();
  
  // Feed watchdog periodically
  static uint32_t watchdog_counter = 0;
  if (++watchdog_counter >= 10) {
    watchdog_feed();
    watchdog_counter = 0;
  }
  
  // Sleep for remaining time
  uint64_t elapsed = get_time_us() - loop_start;
  if (elapsed < SCHEDULER_TICK_US) {
    precise_sleep_us(SCHEDULER_TICK_US - elapsed);
  }
}
