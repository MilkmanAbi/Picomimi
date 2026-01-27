/**
 * PICOMIMI Example: Hello World Service
 * 
 * Demonstrates callback-based task creation
 */
#include "api/picomimi_kernel.h"
#include <stdio.h>

static void hello_init(pm_task_id_t id) {
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Hello service initialized (ID %d)", id);
}

static void hello_tick(void* arg) {
    (void)arg;
    static uint32_t last_hello = 0;
    static int count = 0;
    
    uint32_t now = pm_get_time_ms();
    if (now - last_hello >= 5000) {
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Hello #%d from PICOMIMI!", ++count);
        last_hello = now;
    }
}

static void hello_deinit(void) {
    pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Hello service stopped");
}

static pm_module_callbacks_t hello_callbacks = {
    .init = hello_init,
    .tick = hello_tick,
    .deinit = hello_deinit
};

void example_hello_init(void) {
    pm_task_create_callback("hello", &hello_callbacks, NULL,
                            3, PICOMIMI_TASK_TYPE_SERVICE,
                            CORE_AFFINITY_ANY);
}
