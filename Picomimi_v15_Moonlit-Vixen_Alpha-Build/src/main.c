/**
 * PICOMIMI-AXISOS v15.0.0-Alpha Entry Point
 */
#include "api/picomimi_kernel.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#if PICOMIMI_ENABLE_SMP
static void core1_entry(void) {
    // Core 1 idle loop
    while (pm_kernel_is_running()) {
        __wfi();
    }
}
#endif

int main(void) {
    // Initialize kernel
    pm_result_t result = pm_kernel_init();
    if (result != PM_OK) {
        // Failed to initialize
        while (1) {
            tight_loop_contents();
        }
    }
    
    // Start kernel
    result = pm_kernel_start();
    if (result != PM_OK) {
        pm_kernel_panic("Failed to start kernel");
    }
    
#if PICOMIMI_ENABLE_SMP
    // Launch Core 1
    multicore_launch_core1(core1_entry);
#endif
    
    // Main loop - Core 0
    while (pm_kernel_is_running()) {
        pm_kernel_tick();
    }
    
    // Should never reach here
    return 0;
}
