/**
 * PICOMIMI-AXISOS v15.0.0-Alpha Configuration
 * Complete port from v14.3.1 "Quiet Otter"
 * 
 * This file contains ALL configuration defines from the original 12,000 line codebase.
 */
#ifndef PICOMIMI_CONFIG_H
#define PICOMIMI_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// HARDWARE CONFIGURATION - EDIT THESE FOR YOUR SETUP
// ============================================================================

#ifndef RP2040_OR_RP2350
#define RP2040_OR_RP2350          0       // 0 = RP2040, 1 = RP2350
#endif

#ifndef ENABLE_PICOMIMI_GOVERNOR
#define ENABLE_PICOMIMI_GOVERNOR  1       // 1 = Auto frequency scaling ON
#endif

#define RP2040_FREQ_MAX           250     // MHz - used when governor disabled on RP2040
#define RP2350_FREQ_MAX           300     // MHz - used when governor disabled on RP2350

#define CORE_TEMP_LIMIT           70      // Thermal throttle temp in °C

// SD Card pins
#define SD_CS                     5
#define SD_MOSI                   19
#define SD_MISO                   16
#define SD_SCK                    18
#define BTN_ONOFF                 9

// ============================================================================
// VERSION INFO
// ============================================================================

#define PICOMIMI_VERSION_MAJOR    15
#define PICOMIMI_VERSION_MINOR    0
#define PICOMIMI_VERSION_PATCH    0
#define PICOMIMI_VERSION_STRING   "15.0.0-Alpha"
#define PICOMIMI_CODENAME         "Moonlit Vixen"

// ============================================================================
// ARCHITECTURE DEFINES
// ============================================================================

#if RP2040_OR_RP2350 == 0
    // RP2040 Configuration
    #define PICOMIMI_TOTAL_SRAM         (264 * 1024)    // 264KB SRAM
    #define PICOMIMI_HEAP_SIZE          (64 * 1024)     // 64KB kernel heap
    #define PICOMIMI_MAX_TASKS          12
    #define PICOMIMI_MAX_GPIO           30
    #define PICOMIMI_MAX_ADC            4
    #define PICOMIMI_MAX_MEMORY_BLOCKS  48
    #define PICOMIMI_MAX_APPS           8
    #define PICOMIMI_MAX_IPC_MESSAGES   12
    #define PICOMIMI_IPC_MSG_SIZE       24
    #define PICOMIMI_PMFS_WRITE_CACHE   1024
    #define PICOMIMI_TMPFS_SIZE         0
    #define PICOMIMI_CHIP_NAME          "RP2040"
    #define PICOMIMI_CORE_ARCH          "Cortex-M0+"
    #define PICOMIMI_FREQ_MAX_KHZ       260000
    #define PICOMIMI_HAS_FPU            0
    #define PICOMIMI_HAS_DSP            0
    #define PICOMIMI_CACHE_LINE_SIZE    4
#else
    // RP2350 Configuration  
    #define PICOMIMI_TOTAL_SRAM         (520 * 1024)    // 520KB SRAM
    #define PICOMIMI_HEAP_SIZE          (100 * 1024)    // 100KB kernel heap
    #define PICOMIMI_MAX_TASKS          20
    #define PICOMIMI_MAX_GPIO           48
    #define PICOMIMI_MAX_ADC            8
    #define PICOMIMI_MAX_MEMORY_BLOCKS  96
    #define PICOMIMI_MAX_APPS           12
    #define PICOMIMI_MAX_IPC_MESSAGES   24
    #define PICOMIMI_IPC_MSG_SIZE       32
    #define PICOMIMI_PMFS_WRITE_CACHE   2048
    #define PICOMIMI_TMPFS_SIZE         8192
    #define PICOMIMI_CHIP_NAME          "RP2350"
    #define PICOMIMI_CORE_ARCH          "Cortex-M33"
    #define PICOMIMI_FREQ_MAX_KHZ       310000
    #define PICOMIMI_HAS_FPU            1
    #define PICOMIMI_HAS_DSP            1
    #define PICOMIMI_CACHE_LINE_SIZE    32
#endif

#define PICOMIMI_CORE_COUNT             2

// ============================================================================
// CPU FREQUENCY GOVERNOR PROFILES (v2.0 - 5-LEVEL SCALING)
// ============================================================================

// Frequency values in Hz
#if RP2040_OR_RP2350 == 0
    #define PICOMIMI_FREQ_ULTRA_LOW     48000000    // 48 MHz
    #define PICOMIMI_FREQ_POWERSAVE     96000000    // 96 MHz
    #define PICOMIMI_FREQ_BALANCED      133000000   // 133 MHz
    #define PICOMIMI_FREQ_PERFORMANCE   200000000   // 200 MHz
    #define PICOMIMI_FREQ_TURBO         260000000   // 260 MHz
#else
    #define PICOMIMI_FREQ_ULTRA_LOW     48000000    // 48 MHz
    #define PICOMIMI_FREQ_POWERSAVE     96000000    // 96 MHz
    #define PICOMIMI_FREQ_BALANCED      150000000   // 150 MHz
    #define PICOMIMI_FREQ_PERFORMANCE   250000000   // 250 MHz
    #define PICOMIMI_FREQ_TURBO         310000000   // 310 MHz
#endif

// Governor thresholds (with hysteresis to prevent thrashing)
#define PICOMIMI_GOV_CHECK_INTERVAL_MS   50     // Check interval
#define PICOMIMI_GOV_FAST_CHECK_MS       10     // Fast check during transitions

// Hysteresis thresholds
#define PICOMIMI_TURBO_SCALE_UP          80     // Boost to TURBO when load >= 80%
#define PICOMIMI_TURBO_SCALE_DOWN        65     // Drop from TURBO when load < 65%
#define PICOMIMI_PERF_SCALE_UP           50     // Boost to PERF when load >= 50%
#define PICOMIMI_PERF_SCALE_DOWN         35     // Drop from PERF when load < 35%
#define PICOMIMI_BAL_SCALE_UP            20     // Boost to BAL when load >= 20%
#define PICOMIMI_BAL_SCALE_DOWN          12     // Drop from BAL when load < 12%
#define PICOMIMI_PS_SCALE_DOWN           5      // Drop to ULTRA when load < 5%
#define PICOMIMI_INSTANT_TURBO_SPIKE     70     // Immediate turbo on sudden spike

// Thermal
#define PICOMIMI_THERMAL_LIMIT           CORE_TEMP_LIMIT

// Input boost
#define PICOMIMI_INPUT_BOOST_DURATION_MS 300

// WFI (Wait For Interrupt)
#define PICOMIMI_WFI_IDLE_THRESHOLD_MS   100
#define PICOMIMI_WFI_MIN_SLEEP_US        50

// Load calculation thresholds
#define PM_LOAD_VERY_HIGH               80
#define PM_LOAD_HIGH                    50
#define PM_LOAD_MEDIUM                  25
#define PM_LOAD_LOW                     10

// ============================================================================
// USB SERIAL STABILITY - v14.1.1 FIX
// ============================================================================

#define PICOMIMI_USB_MIN_FREQ_KHZ        133000
#define PICOMIMI_USB_ACTIVITY_TIMEOUT_MS 5000
#define PICOMIMI_USB_POLL_INTERVAL_MS    10
#define PICOMIMI_USB_BLOCKS_WFI          1

// ============================================================================
// RESOURCE MANAGEMENT SYSTEM - v14.3.1 RESOURCE-OWNING KERNEL
// ============================================================================

#define PICOMIMI_RES_STRICT_MODE         0       // 1 = BLOCK direct access, 0 = AUDIT
#define PICOMIMI_RES_AUDIT_ENABLED       1
#define PICOMIMI_RES_AUTO_RELEASE        1
#define PICOMIMI_RES_MAX_VIOLATIONS      10
#define PICOMIMI_RES_VIOLATION_WINDOW_MS 60000

// Resource counts
#if RP2040_OR_RP2350 == 0
    #define PICOMIMI_RES_GPIO_COUNT      30
    #define PICOMIMI_RES_ADC_COUNT       4
    #define PICOMIMI_RES_PIO_COUNT       2
#else
    #define PICOMIMI_RES_GPIO_COUNT      48
    #define PICOMIMI_RES_ADC_COUNT       8
    #define PICOMIMI_RES_PIO_COUNT       3
#endif

#define PICOMIMI_RES_SPI_COUNT           2
#define PICOMIMI_RES_I2C_COUNT           2
#define PICOMIMI_RES_UART_COUNT          2
#define PICOMIMI_RES_PWM_SLICE_COUNT     8
#define PICOMIMI_RES_PWM_CHANNEL_COUNT   16
#define PICOMIMI_RES_PIO_SM_COUNT        4
#define PICOMIMI_RES_DMA_COUNT           12
#define PICOMIMI_RES_TIMER_ALARM_COUNT   4

#define PICOMIMI_MAX_RESOURCE_VIOLATIONS 32
#define PICOMIMI_MAX_TRANSACTIONS        8

// ============================================================================
// FEATURE TOGGLES
// ============================================================================

#ifndef PICOMIMI_SD_ENABLED
#define PICOMIMI_SD_ENABLED              1       // Enable SD card / PMFS
#endif

#ifndef PICOMIMI_OOM_KILLER_ENABLED  
#define PICOMIMI_OOM_KILLER_ENABLED      1       // Enable OOM killer
#endif

#ifndef PICOMIMI_DUAL_CORE
#define PICOMIMI_DUAL_CORE               1       // Enable dual-core scheduling
#endif

// ============================================================================
// SCHEDULER CONFIGURATION
// ============================================================================

// Scheduler tick
#define PICOMIMI_SCHED_TICK_US           500     // 500µs scheduler tick
#define PICOMIMI_SCHED_PRIORITY_LEVELS   16
#define PICOMIMI_SCHED_BASE_QUANTUM_US   5000    // 5ms base time slice
#define PICOMIMI_SCHED_RT_THRESHOLD      12      // Priority >= 12 is realtime

// Preemption
#define PICOMIMI_PREEMPTION_ENABLED      1
#define PICOMIMI_PRIORITY_AGING_ENABLED  1
#define PICOMIMI_IDLE_INJECTION_ENABLED  1

// CPU abuse detection
#define PICOMIMI_CPU_ABUSE_THRESHOLD     90      // CPU% threshold
#define PICOMIMI_CPU_ABUSE_DURATION_MS   5000    // Duration before flagging
#define PICOMIMI_CPU_ABUSE_MAX_WARNINGS  3

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

#define PICOMIMI_MEM_ALIGNMENT           32
#define PICOMIMI_MEM_SIZE_CLASS_COUNT    8
#define PICOMIMI_MEM_SMALL_POOL_SIZE     2048
#define PICOMIMI_MEM_WARNING_THRESHOLD   8192    // 8KB
#define PICOMIMI_MEM_CRITICAL_THRESHOLD  2048    // 2KB

// Size class boundaries
#define PICOMIMI_MEM_SIZE_CLASS_0        32
#define PICOMIMI_MEM_SIZE_CLASS_1        64
#define PICOMIMI_MEM_SIZE_CLASS_2        128
#define PICOMIMI_MEM_SIZE_CLASS_3        256
#define PICOMIMI_MEM_SIZE_CLASS_4        512
#define PICOMIMI_MEM_SIZE_CLASS_5        1024
#define PICOMIMI_MEM_SIZE_CLASS_6        2048
#define PICOMIMI_MEM_SIZE_CLASS_7        4096

// Kernel reserve
#define PICOMIMI_KERNEL_RESERVE          (PICOMIMI_HEAP_SIZE / 10)

// ============================================================================
// IPC CONFIGURATION
// ============================================================================

#define PICOMIMI_IPC_TASK_QUEUE_SIZE     8
#define PICOMIMI_MAX_KERNEL_MUTEXES      16
#define PICOMIMI_MAX_SEMAPHORES          16
#define PICOMIMI_MAX_EVENT_FLAGS         16
#define PICOMIMI_MUTEX_TIMEOUT_DEFAULT   5000    // 5 seconds

// ============================================================================
// LOGGING
// ============================================================================

#define PICOMIMI_MAX_LOG_ENTRIES         32
#define PICOMIMI_LOG_MSG_SIZE            64
#define PICOMIMI_LOG_LEVEL_TRACE         0
#define PICOMIMI_LOG_LEVEL_DEBUG         1
#define PICOMIMI_LOG_LEVEL_INFO          2
#define PICOMIMI_LOG_LEVEL_WARN          3
#define PICOMIMI_LOG_LEVEL_ERROR         4
#define PICOMIMI_LOG_LEVEL_FATAL         5
#define PICOMIMI_LOG_LEVEL_DEFAULT       PICOMIMI_LOG_LEVEL_INFO

// ============================================================================
// TASK CONFIGURATION
// ============================================================================

#define PICOMIMI_TASK_NAME_LEN           20
#define PICOMIMI_MAX_DRIVERS             8
#define PICOMIMI_MAX_SERVICES            8
#define PICOMIMI_MAX_CORE1_TASKS         8

// Task types
#define PICOMIMI_TASK_TYPE_KERNEL        0
#define PICOMIMI_TASK_TYPE_DRIVER        1
#define PICOMIMI_TASK_TYPE_SERVICE       2
#define PICOMIMI_TASK_TYPE_MODULE        3
#define PICOMIMI_TASK_TYPE_APPLICATION   4

// Task flags
#define PICOMIMI_TASK_FLAG_NONE          0x00
#define PICOMIMI_TASK_FLAG_PROTECTED     0x01    // Cannot be killed
#define PICOMIMI_TASK_FLAG_REALTIME      0x02    // RT scheduling
#define PICOMIMI_TASK_FLAG_GUI           0x04    // Has GUI
#define PICOMIMI_TASK_FLAG_CRITICAL      0x08    // Critical system task
#define PICOMIMI_TASK_FLAG_RESPAWN       0x10    // Auto-respawn on death
#define PICOMIMI_TASK_FLAG_CORE1         0x20    // Pinned to Core 1
#define PICOMIMI_TASK_FLAG_OOM_CLEANUP   0x40    // OOM cleanup requested
#define PICOMIMI_TASK_FLAG_PINNED        0x80    // Cannot migrate cores
#define PICOMIMI_TASK_FLAG_CPU_ABUSER    0x100   // Flagged as CPU abuser
#define PICOMIMI_TASK_FLAG_OOM_TARGET    0x200   // Currently selected OOM victim

// Aliases for code compatibility
#define TASK_FLAG_CRITICAL      PICOMIMI_TASK_FLAG_CRITICAL
#define TASK_FLAG_PINNED        PICOMIMI_TASK_FLAG_PINNED
#define TASK_FLAG_CPU_ABUSER    PICOMIMI_TASK_FLAG_CPU_ABUSER
#define TASK_FLAG_OOM_TARGET    PICOMIMI_TASK_FLAG_OOM_TARGET

// Task types
#define TASK_TYPE_KERNEL    PICOMIMI_TASK_TYPE_KERNEL
#define TASK_TYPE_DRIVER    PICOMIMI_TASK_TYPE_DRIVER
#define TASK_TYPE_SERVICE   PICOMIMI_TASK_TYPE_SERVICE
#define TASK_TYPE_MODULE    PICOMIMI_TASK_TYPE_MODULE
#define TASK_TYPE_APP       PICOMIMI_TASK_TYPE_APPLICATION

// OOM priorities
#define PICOMIMI_OOM_PRIORITY_NEVER      0       // Never kill (kernel)
#define PICOMIMI_OOM_PRIORITY_CRITICAL   1       // Kill last
#define PICOMIMI_OOM_PRIORITY_HIGH       2       // High importance
#define PICOMIMI_OOM_PRIORITY_NORMAL     3       // Normal
#define PICOMIMI_OOM_PRIORITY_LOW        4       // Kill first

// ============================================================================
// PMFS FILESYSTEM
// ============================================================================

#define PICOMIMI_PMFS_ENABLED            1
#define PICOMIMI_PMFS_MAX_FILES          64
#define PICOMIMI_PMFS_MAX_OPEN_FILES     8
#define PICOMIMI_PMFS_MAX_PATH_LENGTH    64
#define PICOMIMI_PMFS_MAX_FILENAME       32
#define PICOMIMI_PMFS_BLOCK_SIZE         512
#define PICOMIMI_PMFS_JOURNAL_SIZE       16
#define PICOMIMI_PMFS_METADATA_SIZE      128

// ============================================================================
// DISPLAY & INPUT
// ============================================================================

#define PICOMIMI_MAX_DISPLAY_WIDTH       320
#define PICOMIMI_MAX_DISPLAY_HEIGHT      240
#define PICOMIMI_MAX_INPUT_BUTTONS       8

// ============================================================================
// COMPILER HINTS & OPTIMIZATION
// ============================================================================

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define PICOMIMI_CACHE_ALIGNED  __attribute__((aligned(PICOMIMI_CACHE_LINE_SIZE)))
#define PICOMIMI_HOT_FUNC       __attribute__((hot, optimize("O3")))
#define PICOMIMI_COLD_FUNC      __attribute__((cold))
#define PICOMIMI_ALWAYS_INLINE  __attribute__((always_inline)) inline
#define PICOMIMI_NOINLINE       __attribute__((noinline))
#define PICOMIMI_PACKED         __attribute__((packed))

// Interrupt control
#define PICOMIMI_DISABLE_IRQ()  __asm__ volatile ("cpsid i" : : : "memory")
#define PICOMIMI_ENABLE_IRQ()   __asm__ volatile ("cpsie i" : : : "memory")
#define PICOMIMI_DMB()          __asm__ volatile ("dmb" : : : "memory")
#define PICOMIMI_DSB()          __asm__ volatile ("dsb" : : : "memory")
#define PICOMIMI_ISB()          __asm__ volatile ("isb" : : : "memory")

// ============================================================================
// WATCHDOG
// ============================================================================

#define PICOMIMI_WATCHDOG_TIMEOUT_MS     8000    // 8 second watchdog
#define PICOMIMI_WATCHDOG_FEED_INTERVAL  10      // Feed every 10 ticks

// ============================================================================
// CORE AFFINITY
// ============================================================================

#define PICOMIMI_CORE_ANY                0xFF
#define PICOMIMI_CORE_0                  0
#define PICOMIMI_CORE_1                  1

// ============================================================================
// ERROR CODES
// ============================================================================

typedef enum {
    PM_OK = 0,
    PM_ERROR_INVALID = -1,
    PM_ERROR_NOMEM = -2,
    PM_ERROR_TIMEOUT = -3,
    PM_ERROR_BUSY = -4,
    PM_ERROR_NOTFOUND = -5,
    PM_ERROR_EXISTS = -6,
    PM_ERROR_DENIED = -7,
    PM_ERROR_FULL = -8,
    PM_ERROR_EMPTY = -9,
    PM_ERROR_IO = -10,
    PM_ERROR_CORRUPT = -11,
    PM_ERROR_NOT_IMPL = -12,
} pm_result_t;

// ============================================================================
// INVALID VALUES
// ============================================================================

#define PM_INVALID_TASK     ((pm_task_id_t)0xFFFF)
#define PM_INVALID_HANDLE   ((uint16_t)0xFFFF)
#define PM_INVALID_MSG      ((uint16_t)0xFFFF)

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

typedef uint16_t pm_task_id_t;
typedef uint16_t pm_res_handle_t;
typedef void (*pm_task_entry_t)(void* arg);
typedef void (*pm_oom_callback_t)(uint32_t bytes_needed);

#endif // PICOMIMI_CONFIG_H
