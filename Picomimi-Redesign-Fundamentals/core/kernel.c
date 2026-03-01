/**
 * Picomimi Kernel Main
 * 
 * Entry point and system initialization for RP2040/RP2350
 */

#include "picomimi.h"

// ============================================================================
// SYSTEM STATE
// ============================================================================

static struct {
    bool initialized;
    bool running;
    u32 boot_time_us;
    u8 num_cores;
} kernel_state;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

extern void heap_init(void);
extern void clock_init(void);
extern void led_init(void);
extern void shell_task(void *arg);

// ============================================================================
// IDLE TASK
// ============================================================================

static void idle_task(void *arg) {
    (void)arg;
    while (1) {
        __asm__ volatile("wfi");  // Wait for interrupt
    }
}

// ============================================================================
// BLINK TASK (demo)
// ============================================================================

static void blink_task(void *arg) {
    (void)arg;
    while (1) {
        led_toggle();
        task_sleep(500);
    }
}

// ============================================================================
// CORE1 ENTRY
// ============================================================================

static void core1_entry(void) {
    // Core1 scheduler loop
    while (1) {
        sched_schedule();
        __asm__ volatile("wfi");
    }
}

// ============================================================================
// PANIC HANDLER
// ============================================================================

void picomimi_panic(const char *msg) {
    // Disable interrupts
    __asm__ volatile("cpsid i");
    
    uart_puts("\n\n!!! KERNEL PANIC !!!\n");
    uart_puts(msg);
    uart_puts("\n\nSystem halted.\n");
    
    // Blink LED rapidly
    while (1) {
        led_toggle();
        for (volatile int i = 0; i < 100000; i++);
    }
}

// ============================================================================
// KERNEL MAIN
// ============================================================================

void picomimi_init(void) {
    // Initialize hardware
    clock_init();
    uart_init(115200);
    led_init();
    
    // Print banner
    uart_puts("\n\n");
    uart_puts("=========================================\n");
    uart_puts("  Picomimi v" PICOMIMI_VERSION "\n");
    uart_puts("  Advanced Microkernel for RP2040/RP2350\n");
    uart_puts("=========================================\n\n");
    
    // Initialize memory
    uart_puts("[INIT] Memory...\n");
    heap_init();
    
    // Initialize timer
    uart_puts("[INIT] Timer...\n");
    timer_init(PICOMIMI_TICK_HZ);
    
    // Initialize IRQs
    uart_puts("[INIT] Interrupts...\n");
    irq_init();
    
    // Initialize scheduler hypervisor
    uart_puts("[INIT] Scheduler Hypervisor...\n");
    sched_hypervisor_init();
    uart_puts("  Registered classes:\n");
    uart_puts("    - cooperative (fibers)\n");
    uart_puts("    - realtime (EDF)\n");
    uart_puts("    - fair (CFS-like)\n");
    uart_puts("    - batch (throughput)\n");
    uart_puts("    - idle (background)\n");
    
    // Create default tasks
    uart_puts("[INIT] Creating tasks...\n");
    
    task_t *idle = task_create("idle", idle_task, NULL, 255);
    sched_domain_add_task(sched_domain_create("idle", SCHED_CLASS_IDLE, 255), idle);
    
    task_t *blinker = task_create("blinker", blink_task, NULL, 10);
    sched_domain_add_task(sched_domain_create("normal", SCHED_CLASS_FAIR, 10), blinker);
    
    task_t *shell = task_create("shell", shell_task, NULL, 5);
    sched_domain_add_task(sched_domain_create("interactive", SCHED_CLASS_FAIR, 5), shell);
    
    uart_puts("  Created: idle, blinker, shell\n");
    
    kernel_state.initialized = true;
    kernel_state.num_cores = 1;
    
    uart_puts("\n[INIT] Kernel initialization complete!\n\n");
}

void picomimi_start(void) {
    if (!kernel_state.initialized) {
        picomimi_panic("Kernel not initialized!");
    }
    
    kernel_state.running = true;
    
    // Optionally launch core1
    #if NUM_CORES > 1
    uart_puts("[INIT] Launching Core1...\n");
    core1_launch(core1_entry);
    kernel_state.num_cores = 2;
    #endif
    
    // Enable interrupts
    __asm__ volatile("cpsie i");
    
    // Start scheduler - never returns
    uart_puts("[INIT] Starting scheduler...\n\n");
    sched_schedule();
    
    // Should never reach here
    picomimi_panic("Scheduler returned!");
}

// ============================================================================
// RESET HANDLER (Entry Point)
// ============================================================================

extern u32 __stack_top;
extern u32 __bss_start, __bss_end;
extern u32 __data_start, __data_end, __data_load;

void Reset_Handler(void) {
    // Initialize stack
    __asm__ volatile("ldr sp, =__stack_top");
    
    // Copy .data section
    u32 *src = &__data_load;
    u32 *dst = &__data_start;
    while (dst < &__data_end) {
        *dst++ = *src++;
    }
    
    // Zero .bss section
    dst = &__bss_start;
    while (dst < &__bss_end) {
        *dst++ = 0;
    }
    
    // Initialize and start kernel
    picomimi_init();
    picomimi_start();
    
    // Never returns
    while (1);
}

// ============================================================================
// VECTOR TABLE
// ============================================================================

void Default_Handler(void);
void SysTick_Handler(void);
void PendSV_Handler(void);

__attribute__((section(".vectors")))
const void *vector_table[] = {
    &__stack_top,           // Initial SP
    Reset_Handler,          // Reset
    Default_Handler,        // NMI
    Default_Handler,        // HardFault
    Default_Handler,        // MemManage
    Default_Handler,        // BusFault
    Default_Handler,        // UsageFault
    0, 0, 0, 0,            // Reserved
    Default_Handler,        // SVCall
    Default_Handler,        // Debug
    0,                      // Reserved
    PendSV_Handler,         // PendSV
    SysTick_Handler,        // SysTick
    // IRQs 0-31
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
};
