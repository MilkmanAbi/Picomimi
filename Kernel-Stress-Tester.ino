/*
 * Kernel-Stress-Tester.ino (FIXED)
 * * This application is designed to stress the Picomimi v11 kernel's
 * most sensitive components: the Memory Manager (kmalloc/kfree) and 
 * the Preemptive Scheduler.
 * * It allocates memory in random sizes to cause heap fragmentation and
 * performs a high-load calculation without yielding, forcing the
 * preemptive scheduler to interrupt it repeatedly.
 * * It will eventually be terminated by the kernel's OOM Killer.
 */

// We assume the necessary Picomimi RTOS APIs are available globally
// (e.g., kout, klog, kmalloc, kfree, task_create, k_task_exit_api, task_sleep).

// --- CONFIGURATION ---
// Set max_allocations low so we force a termination event faster
#define MAX_FRAG_ALLOCS 100 // The number of blocks to hold onto to fragment memory
#define MAX_MEM_SIZE    1024 // Max size of a single allocation (in bytes)

// --- TASK STATE ---
typedef struct {
    char name[TASK_NAME_LEN];
    void* frag_blocks[MAX_FRAG_ALLOCS]; // Array to hold allocated memory pointers
    int frag_count;
    uint32_t current_loop;
    uint32_t total_cycles;
    uint32_t task_id; // Store our own task ID
} StressTaskState_t;

// This state is global for this "application" file
StressTaskState_t stress_state;
char klog_buf[128]; // Buffer for formatting klog messages

// --- 1. MEMORY FRAGMENTATION AND LEAK TEST ---

void fragment_memory_heap() {
    // Attempt to allocate memory blocks of random sizes
    uint16_t size = (micros() % MAX_MEM_SIZE) + 1;
    
    // FIX: Call kmalloc with the task's own ID, not MALLOC_TASK_HEAP
    void* ptr = kmalloc(size, stress_state.task_id); 

    if (ptr == NULL) {
        // FIX: Use klog() function with a formatted buffer
        snprintf(klog_buf, sizeof(klog_buf), "OOM Test: kmalloc failed to allocate %d bytes. Kernel is stressed.", size);
        klog(3, klog_buf); // Use level 3 for error
        return;
    }
    
    // Fill the memory with a known pattern (optional, for corruption testing)
    memset(ptr, (char)(stress_state.current_loop % 256), size);

    // If the frag_blocks array is full, free the oldest block
    if (stress_state.frag_count >= MAX_FRAG_ALLOCS) {
        kfree(stress_state.frag_blocks[0]);
        
        // Shift all pointers down to make space for the new one (simulate FIFO)
        for (int i = 0; i < MAX_FRAG_ALLOCS - 1; i++) {
            stress_state.frag_blocks[i] = stress_state.frag_blocks[i+1];
        }
        stress_state.frag_count--;
    }
    
    // Store the new pointer
    stress_state.frag_blocks[stress_state.frag_count++] = ptr;

    // Log the current status every 10 cycles
    if (stress_state.current_loop % 10 == 0) {
        // FIX: Use klog() function with a formatted buffer
        snprintf(klog_buf, sizeof(klog_buf), "Stress: Loop %d. Holding %d fragments.", stress_state.current_loop, stress_state.frag_count);
        klog(0, klog_buf); // Use level 0 for info
    }
}


// --- 2. SCHEDULER STRESS TEST (CPU HOG) ---

void busy_wait_calculate() {
    // Perform a non-yielding, CPU-intensive calculation.
    // This calculation is designed to take LONGER than the scheduler tick (1000us)
    // forcing the kernel to preempt this task multiple times.
    
    // We use a simple, predictable calculation (e.g., calculating Pi or a large Fibonacci number)
    // for a fixed number of iterations.
    volatile double result = 0.0;
    const int iterations = 50000; // This value is tuned to ensure it exceeds 1ms on a Pico.

    for (int i = 0; i < iterations; i++) {
        // Simple trigonometric calculation to keep the FPU busy
        result += sin(i * 0.0001) * cos(i * 0.0001);
    }
    
    // Prevents the compiler from optimizing the loop away
    if (result == 99999.0) { /* intentionally false */ } 
    
    stress_state.total_cycles++;
}


// --- THE MAIN APPLICATION TASK ---

void stress_test_app(void* arg) {
    // The spawner function passes the task's ID as the argument
    uint32_t self_task_id = (uint32_t)(uintptr_t)arg;

    kout.println("Starting Kernel Stress Tester App...");
    
    // Initialize state
    memset(&stress_state, 0, sizeof(StressTaskState_t));
    strcpy(stress_state.name, "CPU/MEM_HOG");
    stress_state.task_id = self_task_id; // Store our ID

    while (1) {
        // Step 1: Fragment the heap and push the memory manager
        fragment_memory_heap();

        // Step 2: Hog the CPU and force preemption
        busy_wait_calculate();
        
        stress_state.current_loop++;
        
        // A minimal sleep to allow other tasks to run and print logs.
        // The scheduler will handle preemption during the busy-wait, 
        // but this sleep allows background tasks to finish.
        task_sleep(5); 

        // We expect this loop to eventually terminate due to the OOM killer
        // once the memory fragmentation reaches a critical point and a high-priority
        // kernel task can't allocate memory.
    }

    // Clean up if we somehow exit gracefully (unlikely)
    kout.println("Stress Test App Exited Gracefully.");
    
    // FIX: Use the correct kernel API function to exit
    k_task_exit_api();
}

// --- MISSING SPAWNER FUNCTION ---
// FIX: Added the spawner function that 'setup()' in the main file calls.

void spawn_stress_app() {
    kout.println("[APP] Spawning Stress Tester...");
    uint32_t task_id = task_create(
        "stress_app",           // const char* name
        stress_test_app,        // void (*entry)(void*)
        NULL,                   // void* arg (will be set after creation)
        10,                     // uint8_t priority
        TASK_TYPE_APPLICATION,  // uint8_t task_type
        TASK_FLAG_ONESHOT,      // uint32_t flags
        0,                      // uint64_t max_runtime_ms (0=unlimited)
        OOM_PRIORITY_LOW,       // uint8_t oom_priority
        50 * 1024,              // uint32_t mem_limit (50KB)
        NULL,                   // ModuleCallbacks* callbacks
        "Stresses CPU and MEM", // const char* description
        CORE_ANY                // CoreAffinity affinity
    );

    if (task_id > 0 && task_id < MAX_TASKS) {
        // Now that the task is created, we set its 'arg' to its own ID.
        // This is a common pattern for tasks to identify themselves.
        kernel.tasks[task_id].arg = (void*)(uintptr_t)task_id;
        kout.print("[APP] Stress app alive with ID: ");
        kout.println(task_id);
    } else {
        kout.println("[APP] Failed to spawn stress app!");
    }
}
