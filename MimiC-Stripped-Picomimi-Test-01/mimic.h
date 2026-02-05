/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC - Self-Hosted C Compiler & Runtime for RP2040/RP2350               ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  VERSION: 1.0.0-alpha                                                     ║
 * ║  ARCHITECTURE: Disk-buffered compilation, kernel-managed execution        ║
 * ║  KERNEL BASE: Stripped Picomimi v14.3.1 resource-owning kernel            ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 * 
 * KEY PRINCIPLES:
 *   1. Binaries have NO hardcoded addresses - kernel allocates and patches
 *   2. Compilation uses SD card as working memory (multi-pass, disk-buffered)
 *   3. Full C98 support - not a DSL, a real compiler
 *   4. Custom minimal FAT32 - no Arduino SD library bloat
 *   5. pico-sdk compatible API for user code
 */

#ifndef MIMIC_H
#define MIMIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// PLATFORM DETECTION
// ============================================================================

#ifndef MIMIC_TARGET_RP2350
  #define MIMIC_TARGET_RP2350   0   // 0 = RP2040, 1 = RP2350
#endif

#if MIMIC_TARGET_RP2350
  #define MIMIC_TOTAL_RAM       (520 * 1024)
  #define MIMIC_CHIP_NAME       "RP2350"
  #define MIMIC_CORE_COUNT      2
  #define MIMIC_HAS_FPU         1
#else
  #define MIMIC_TOTAL_RAM       (264 * 1024)
  #define MIMIC_CHIP_NAME       "RP2040"
  #define MIMIC_CORE_COUNT      2
  #define MIMIC_HAS_FPU         0
#endif

// ============================================================================
// MEMORY LAYOUT
// ============================================================================

// Memory allocation strategy:
//   ~30% kernel (heap + structures)
//   ~70% user programs + compilation workspace

#if MIMIC_TARGET_RP2350
  #define MIMIC_KERNEL_HEAP     (80 * 1024)    // 80KB kernel heap
  #define MIMIC_USER_HEAP       (380 * 1024)   // 380KB for user programs
  #define MIMIC_MAX_TASKS       16
  #define MIMIC_MAX_MEM_BLOCKS  128
#else
  #define MIMIC_KERNEL_HEAP     (50 * 1024)    // 50KB kernel heap  
  #define MIMIC_USER_HEAP       (180 * 1024)   // 180KB for user programs
  #define MIMIC_MAX_TASKS       8
  #define MIMIC_MAX_MEM_BLOCKS  64
#endif

#define MIMIC_MEM_ALIGN         32      // Cache line alignment
#define MIMIC_MIN_BLOCK_SPLIT   64      // Minimum block to split off
#define MIMIC_KERNEL_RESERVE    (8 * 1024)  // Reserved for kernel emergencies

// ============================================================================
// .mimi BINARY FORMAT
// ============================================================================
// Custom format optimized for kernel loader - NOT ELF
// Binaries are position-independent, kernel patches at load time

#define MIMI_MAGIC              0x494D494D  // "MIMI" in little-endian
#define MIMI_VERSION            1

// Architecture codes
#define MIMI_ARCH_CORTEX_M0P    0   // RP2040 ARM Cortex-M0+
#define MIMI_ARCH_CORTEX_M33    1   // RP2350 ARM Cortex-M33
#define MIMI_ARCH_RISCV         2   // RP2350 RISC-V (Hazard3)

// Section IDs
#define MIMI_SECT_NULL          0
#define MIMI_SECT_TEXT          1   // Executable code
#define MIMI_SECT_RODATA        2   // Read-only data (strings, constants)
#define MIMI_SECT_DATA          3   // Initialized global data
#define MIMI_SECT_BSS           4   // Uninitialized global data (zeroed)

// Binary header - 64 bytes
typedef struct __attribute__((packed)) {
    uint32_t magic;             // "MIMI"
    uint8_t  version;           // Binary format version
    uint8_t  flags;             // Feature flags
    uint8_t  arch;              // Target architecture
    uint8_t  _pad0;
    
    uint32_t entry_offset;      // Entry point offset in .text
    
    uint32_t text_size;         // .text section size
    uint32_t rodata_size;       // .rodata section size
    uint32_t data_size;         // .data section size
    uint32_t bss_size;          // .bss section size (not in file)
    
    uint32_t reloc_count;       // Number of relocations
    uint32_t symbol_count;      // Number of symbols
    
    uint32_t stack_request;     // Requested stack size (0 = default)
    uint32_t heap_request;      // Requested heap size (0 = default)
    
    char     name[16];          // Program name (null-terminated)
    
    uint32_t _reserved[2];      // Future use
} MimiHeader;

// Relocation types
#define MIMI_RELOC_ABS32        0   // 32-bit absolute
#define MIMI_RELOC_REL32        1   // 32-bit PC-relative
#define MIMI_RELOC_THUMB_CALL   2   // Thumb BL instruction
#define MIMI_RELOC_THUMB_BRANCH 3   // Thumb B instruction
#define MIMI_RELOC_DATA_PTR     4   // Data pointer in .data/.rodata

// Relocation entry - 12 bytes
typedef struct __attribute__((packed)) {
    uint32_t offset;            // Offset within section
    uint16_t section;           // Which section (text/rodata/data)
    uint8_t  type;              // Relocation type
    uint8_t  _pad;
    uint32_t symbol_idx;        // Symbol table index
} MimiReloc;

// Symbol types
#define MIMI_SYM_LOCAL          0   // Local symbol
#define MIMI_SYM_GLOBAL         1   // Global symbol (exported)
#define MIMI_SYM_EXTERN         2   // External symbol (imported)
#define MIMI_SYM_SYSCALL        3   // Kernel syscall

// Symbol entry - 24 bytes
typedef struct __attribute__((packed)) {
    char     name[16];          // Symbol name
    uint32_t value;             // Value (offset in section, or syscall #)
    uint8_t  section;           // Which section
    uint8_t  type;              // Symbol type
    uint16_t _pad;
} MimiSymbol;

// ============================================================================
// TASK CONTROL BLOCK
// ============================================================================

typedef enum {
    TASK_STATE_FREE = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_BLOCKED,
    TASK_STATE_SLEEPING,
    TASK_STATE_ZOMBIE
} MimicTaskState;

// Task memory layout (filled by loader)
typedef struct {
    uintptr_t base;             // Base address of allocation
    uint32_t  total_size;       // Total allocated bytes
    
    uint32_t  text_start;       // Offset to .text
    uint32_t  text_size;
    uint32_t  rodata_start;     // Offset to .rodata
    uint32_t  rodata_size;
    uint32_t  data_start;       // Offset to .data
    uint32_t  data_size;
    uint32_t  bss_start;        // Offset to .bss
    uint32_t  bss_size;
    
    uint32_t  heap_start;       // Offset to heap
    uint32_t  heap_size;
    uint32_t  heap_used;
    
    uint32_t  stack_top;        // Stack top address
    uint32_t  stack_size;
} MimicTaskMem;

// Task Control Block
typedef struct {
    uint32_t        id;
    char            name[16];
    MimicTaskState  state;
    uint8_t         priority;       // 0 = highest
    uint8_t         running_core;   // Which core (0 or 1)
    
    // Entry point
    void*           entry;
    
    // Memory layout
    MimicTaskMem    mem;
    
    // Scheduling
    uint32_t        wake_time;      // Wake time for sleeping tasks
    uint32_t        time_slice;     // Remaining time slice (us)
    uint32_t        total_time_us;  // Total CPU time
    
    // Statistics
    uint32_t        start_time;
    uint32_t        alloc_count;
    uint32_t        free_count;
    uint32_t        syscall_count;
    
    // Context (saved on task switch)
    uint32_t        sp;             // Stack pointer
    uint32_t        regs[16];       // R0-R15
} MimicTCB;

// ============================================================================
// MEMORY BLOCK TRACKING
// ============================================================================

typedef struct {
    void*       addr;
    uint32_t    size;
    uint32_t    task_id;        // 0 = kernel, >0 = user
    bool        free;
    bool        pinned;         // Cannot be freed/moved
} MimicMemBlock;

// ============================================================================
// SYSCALL NUMBERS
// ============================================================================

#define MIMIC_SYS_EXIT          0
#define MIMIC_SYS_YIELD         1
#define MIMIC_SYS_SLEEP         2
#define MIMIC_SYS_TIME          3

// Memory
#define MIMIC_SYS_MALLOC        10
#define MIMIC_SYS_FREE          11
#define MIMIC_SYS_REALLOC       12

// I/O
#define MIMIC_SYS_OPEN          20
#define MIMIC_SYS_CLOSE         21
#define MIMIC_SYS_READ          22
#define MIMIC_SYS_WRITE         23
#define MIMIC_SYS_SEEK          24

// Console
#define MIMIC_SYS_PUTCHAR       30
#define MIMIC_SYS_GETCHAR       31
#define MIMIC_SYS_PUTS          32

// GPIO
#define MIMIC_SYS_GPIO_INIT     40
#define MIMIC_SYS_GPIO_DIR      41
#define MIMIC_SYS_GPIO_PUT      42
#define MIMIC_SYS_GPIO_GET      43
#define MIMIC_SYS_GPIO_PULL     44

// PWM
#define MIMIC_SYS_PWM_INIT      50
#define MIMIC_SYS_PWM_SET_WRAP  51
#define MIMIC_SYS_PWM_SET_LEVEL 52
#define MIMIC_SYS_PWM_ENABLE    53

// ADC
#define MIMIC_SYS_ADC_INIT      60
#define MIMIC_SYS_ADC_SELECT    61
#define MIMIC_SYS_ADC_READ      62
#define MIMIC_SYS_ADC_TEMP      63

// SPI
#define MIMIC_SYS_SPI_INIT      70
#define MIMIC_SYS_SPI_WRITE     71
#define MIMIC_SYS_SPI_READ      72
#define MIMIC_SYS_SPI_TRANSFER  73

// I2C
#define MIMIC_SYS_I2C_INIT      80
#define MIMIC_SYS_I2C_WRITE     81
#define MIMIC_SYS_I2C_READ      82

// ============================================================================
// ERROR CODES
// ============================================================================

#define MIMIC_OK                0
#define MIMIC_ERR_NOMEM         (-1)    // Out of memory
#define MIMIC_ERR_INVAL         (-2)    // Invalid argument
#define MIMIC_ERR_NOENT         (-3)    // No such file/entry
#define MIMIC_ERR_IO            (-4)    // I/O error
#define MIMIC_ERR_BUSY          (-5)    // Resource busy
#define MIMIC_ERR_PERM          (-6)    // Permission denied
#define MIMIC_ERR_NOSYS         (-7)    // Syscall not implemented
#define MIMIC_ERR_CORRUPT       (-8)    // Corrupted data
#define MIMIC_ERR_TOOLARGE      (-9)    // Too large
#define MIMIC_ERR_NOEXEC        (-10)   // Not executable

// ============================================================================
// KERNEL API (used internally)
// ============================================================================

// Initialization
void mimic_kernel_init(void);
void mimic_kernel_run(void);

// Memory management (kernel heap)
void* mimic_kmalloc(size_t size);
void  mimic_kfree(void* ptr);
void* mimic_krealloc(void* ptr, size_t size);

// User memory management (per-task)
void* mimic_umalloc(uint32_t task_id, size_t size);
void  mimic_ufree(uint32_t task_id, void* ptr);
void  mimic_task_free_all_memory(uint32_t task_id);

// Task management
int   mimic_task_load(const char* path, uint8_t priority);
int   mimic_task_spawn(const MimiHeader* hdr, const uint8_t* data, uint8_t priority);
void  mimic_task_exit(int code);
void  mimic_task_yield(void);
void  mimic_task_sleep(uint32_t ms);
void  mimic_task_kill(uint32_t task_id);

// Loader
int   mimic_load_binary(const char* path, MimicTCB* task);
int   mimic_validate_header(const MimiHeader* hdr);

// Statistics
uint32_t mimic_get_free_memory(void);
uint32_t mimic_get_task_count(void);
float    mimic_get_cpu_usage(void);
uint32_t mimic_get_uptime_ms(void);

// Debug
void mimic_dump_tasks(void);
void mimic_dump_memory(void);

// ============================================================================
// COMPILER CONFIGURATION
// ============================================================================

// Disk-buffered compilation passes
#define MIMIC_CC_TMP_DIR        "/mimic/tmp"
#define MIMIC_CC_SDK_DIR        "/mimic/sdk"
#define MIMIC_CC_BIN_DIR        "/mimic/bin"
#define MIMIC_CC_SRC_DIR        "/mimic/src"

// Buffer sizes for disk I/O during compilation
#define MIMIC_CC_IO_BUFFER      (4 * 1024)  // 4KB I/O buffer
#define MIMIC_CC_MAX_TOKENS     1024        // Tokens in memory at once
#define MIMIC_CC_MAX_SYMBOLS    512         // Symbol table entries

// Intermediate file extensions
#define MIMIC_EXT_TOK           ".tok"      // Tokenized source
#define MIMIC_EXT_AST           ".ast"      // AST (serialized)
#define MIMIC_EXT_IR            ".ir"       // Intermediate representation
#define MIMIC_EXT_OBJ           ".o"        // Object code
#define MIMIC_EXT_MIMI          ".mimi"     // Final binary

#endif // MIMIC_H
