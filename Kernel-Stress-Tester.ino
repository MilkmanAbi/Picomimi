/*
 * Kernel-Stress-Tester.ino
 * * This application is designed to stress the Picomimi v11 kernel's
 * most sensitive components: the Memory Manager (kmalloc/kfree) and 
 * the Preemptive Scheduler.
 * * It allocates memory in random sizes to cause heap fragmentation and
 * performs a high-load calculation without yielding, forcing the
 * preemptive scheduler to interrupt it repeatedly.
 * * It will eventually be terminated by the kernel's OOM Killer.
 */

// We assume the necessary Picomimi RTOS APIs are available globally
// (e.g., kout, klog, kmalloc, kfree, task_get_info, task_exit).
// This structure holds the state for our stress test task.

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
} StressTaskState_t;

StressTaskState_t stress_state;


// --- 1. MEMORY FRAGMENTATION AND LEAK TEST ---

void fragment_memory_heap() {
    // Attempt to allocate memory blocks of random sizes
    uint16_t size = (micros() % MAX_MEM_SIZE) + 1;
    void* ptr = kmalloc(size, MALLOC_TASK_HEAP); 

    if (ptr == NULL) {
        klog.error("OOM Test: kmalloc failed to allocate %d bytes. Kernel is stressed.", size);
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
        klog.info("Stress: Loop %d. Holding %d fragments.", stress_state.current_loop, stress_state.frag_count);
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
    kout.println("Starting Kernel Stress Tester App...");
    
    // Initialize state
    memset(&stress_state, 0, sizeof(StressTaskState_t));
    strcpy(stress_state.name, "CPU/MEM_HOG");

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
    task_exit();
}
