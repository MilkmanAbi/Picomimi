# **Picomimi v10 M2 - Official Kernel Documentation**

**A Dual-Core Microkernel for RP2040 with High-Grade Scheduling, Memory Management, and IPC**


## **Table of Contents**

1. [Introduction](https://www.google.com/search?q=%231-introduction)

2. [Architecture](https://www.google.com/search?q=%232-architecture)

3. [Project Workflow and Quick Start](https://www.google.com/search?q=%233-project-workflow-and-quick-start)

4. [Task System](https://www.google.com/search?q=%234-task-system)

5. [Memory Management](https://www.google.com/search?q=%235-memory-management)

6. [Scheduling](https://www.google.com/search?q=%236-scheduling)

7. [IPC System](https://www.google.com/search?q=%237-ipc-system)

8. [File Systems](https://www.google.com/search?q=%238-file-systems)

9. [Application Development](https://www.google.com/search?q=%239-application-development)

10. [Shell Commands](https://www.google.com/search?q=%2310-shell-commands)

11. [Best Practices](https://www.google.com/search?q=%2311-best-practices)

12. [API Reference](https://www.google.com/search?q=%2312-api-reference)

13. [Troubleshooting](https://www.google.com/search?q=%2313-troubleshooting)

14. [Advanced Topics](https://www.google.com/search?q=%2314-advanced-topics)

15. [System Limits](https://www.google.com/search?q=%2315-system-limits)

16. [Configuration Reference](https://www.google.com/search?q=%2316-configuration-reference)

17. [License](https://www.google.com/search?q=%2317-license)


## **1. Introduction**

### **What is Picomimi?**

Picomimi is a lightweight, real-time microkernel for the RP2040, featuring:

- **Dual-core SMP**: Utilize both ARM Cortex-M0+ cores with tasks pinned to specific cores.

- **O(1) Scheduler**: Constant-time, priority-based task selection with 32 distinct priority levels.

- **Smart Memory**: A 180KB heap with a graceful Out-of-Memory (OOM) killer, per-task limits, and application callbacks.

- **Priority IPC**: A robust, priority-aware Inter-Process Communication system with flow control.

- **Real-time Support**: Guaranteed scheduling for tasks marked with real-time priority.


### **System Requirements**

- **Hardware**: Raspberry Pi Pico or compatible RP2040 board.

- **Flash**: \~200KB for the kernel and libraries.

- **RAM**: 264KB total (180KB heap available for dynamic allocations).

- **Optional**: SD card (FAT16/FAT32) for persistent storage and logging features.

- **Clock**: 125-276 MHz (225 MHz recommended).


### **Key Features**

\- 32 tasks on Core0 + 16 tasks on Core1\
\- Priority-based preemptive multitasking\
\- Graceful OOM killer with app callbacks\
\- Hardware watchdog integration\
\- Kernel panic handler with diagnostics\
\- RAM filesystem (VFS) + SD card support\
\- Interactive shell with 30+ commands\
\- Mutex-based resource locking\
\- Per-task memory limits and tracking


## **2. Architecture**

### **Core Design**

┌─────────────────────────────────────────────┐\
│              Applications (Separate .ino)   │\
├─────────────────────────────────────────────┤\
│           UISocket API Layer                │\
├─────────────────────────────────────────────┤\
│  Services  │ Drivers │ Modules              │\
├─────────────────────────────────────────────┤\
│      Kernel (Task, Mem, IPC, Sched)         │\
├──────────────────┬──────────────────────────┤\
│   Core 0         │        Core 1            │\
│   (Main Logic)   │   (Offload/Render)       │\
└──────────────────┴──────────────────────────┘


### **Task Types Hierarchy**

TASK\_TYPE\_KERNEL      (0x01)  - Core kernel tasks\
TASK\_TYPE\_DRIVER      (0x02)  - Hardware drivers\
TASK\_TYPE\_SERVICE     (0x04)  - System services\
TASK\_TYPE\_MODULE      (0x08)  - Optional modules\
TASK\_TYPE\_APPLICATION (0x10)  - User applications


### **Memory Layout**

Total SRAM: 264KB\
├─ Kernel Heap: 180KB (dynamic allocation)\
├─ Kernel Structures: \~64KB (TCBs, buffers)\
├─ Stack Space: \~20KB (both cores)


## **3. Project Workflow and Quick Start**

A Picomimi project consists of the main kernel sketch (Picomimi\_v10\_Manifest-v2.ino) and one or more separate application files. The Arduino IDE is used to combine and compile them.


### **1. Project Setup (The "Add File" Method)**

It is critical to understand that applications are **not** included via #include. Instead, they are added to the main kernel sketch using the Arduino IDE:

1. Open the Picomimi\_v10\_Manifest-v2.ino file in the Arduino IDE. This is your main kernel sketch.

2. Create your application logic in a **separate .ino file** (e.g., MyApp.ino). A template is provided in the "Application Development" section.

3. In the Arduino IDE, go to **Sketch -> Add File...** and select your MyApp.ino file.

4. This adds a new tab to your IDE, linking the application file to the kernel sketch. The IDE will now compile both files together as part of the same project.


### **2. Hardware Configuration**

All hardware pin definitions are centralized within the main kernel file, Picomimi\_v10\_Manifest-v2.ino.

**Do not** define pins in your application files. To change hardware, edit the CORE PIN DEFINITIONS section at the top of Picomimi\_v10\_Manifest-v2.ino:

// --- CORE PIN DEFINITIONS ---\
\#define SD\_CS       5\
\#define SD\_MOSI     19\
\#define SD\_MISO     16\
\#define SD\_SCK      18\
\#define BTN\_ONOFF   9\
// Add other device pinouts here...


### **3. Build and Upload**

1. **Board:** Raspberry Pi Pico

2. **CPU Speed:** 225 MHz (Recommended)

3. **Optimization:** -O3 (Optimize More)

4. Click **Upload** to compile and flash the combined kernel and application(s) to your board.


### **4. First Application Example**

Create this code in a **new file** (e.g., FirstApp.ino) and add it to your kernel sketch using Sketch -> Add File....

// FirstApp.ino\
// DO NOT #include the kernel file.\
// Add this file to the kernel sketch via the IDE menu.\
\
static UISocket ui;\
static uint32\_t my\_task\_id;\
\
void spawn\_firstapp() {\
    // Register with kernel GUI/Socket system\
    k\_register\_gui\_app(\&ui);\
    my\_task\_id = kernel.current\_task; // Get our own task ID\
   \
    kout.println("\[FirstApp] Application Started!");\
   \
    // Main application loop\
    while(1) {\
        kout.println("\[FirstApp] Running...");\
        task\_sleep(1000); // Sleep for 1 second\
    }\
}\
\
// Global registration object\
// This code runs at startup to register the app\
struct FirstAppReg {\
    FirstAppReg() {\
        Application\_Register("firstapp", spawn\_firstapp);\
    }\
} \_firstapp\_reg;


### **5. Launch Your Application**

After uploading, open the Serial Monitor and use the built-in shell:

Picomimi\~> help\
Picomimi\~> listapps\
1\. firstapp\
Picomimi\~> firstapp\
\[FirstApp] Application Started!\
\[FirstApp] Running...


## **4. Task System**

### **Task States**

TASK\_READY      // Waiting for CPU\
TASK\_RUNNING    // Currently executing\
TASK\_WAITING    // Sleeping or blocked\
TASK\_SUSPENDED  // Manually suspended\
TASK\_TERMINATED // Killed or exited\
TASK\_ZOMBIE     // Dead but not cleaned up


### **Task Flags**

TASK\_FLAG\_PROTECTED   (0x01)  // Immune to normal termination\
TASK\_FLAG\_CRITICAL    (0x02)  // Panic if killed\
TASK\_FLAG\_RESPAWN     (0x04)  // Auto-restart after death\
TASK\_FLAG\_ONESHOT     (0x08)  // Single instance only\
TASK\_FLAG\_PERSISTENT  (0x10)  // Survives resets (future)


### **Creating Tasks**

uint32\_t task\_create(\
    const char\* name,           // Task name (24 chars max)\
    void (\*entry)(void\*),       // Entry function\
    void\* arg,                  // User argument\
    uint8\_t priority,           // 0-31 (higher = more important)\
    uint8\_t task\_type,          // TASK\_TYPE\_\*\
    uint32\_t flags,             // TASK\_FLAG\_\* combined\
    uint64\_t max\_runtime\_ms,    // Timeout (0 = unlimited)\
    uint8\_t oom\_priority,       // 0-4 (0 = never kill)\
    uint32\_t mem\_limit,         // Soft cap bytes (0 = unlimited)\
    ModuleCallbacks\* callbacks, // Optional callbacks\
    const char\* description     // Human-readable purpose\
);


### **Example: Create High-Priority Task**

task\_create(\
    "audio\_driver",              // Name\
    audio\_task\_fn,               // Entry point\
    NULL,                        // No arguments\
    28,                          // High priority (RT)\
    TASK\_TYPE\_DRIVER,            // Type\
    TASK\_FLAG\_PROTECTED | TASK\_FLAG\_RESPAWN,\
    0,                           // No timeout\
    OOM\_PRIORITY\_NEVER,          // Never kill\
    0,                           // No memory limit\
    NULL,                        // No callbacks\
    "Audio processing driver"    // Description\
);


### **Using Module Callbacks**

// Stateless task using callbacks\
void my\_init(uint32\_t id) {\
    kout.println("\[MyModule] Initialized");\
}\
\
void my\_tick(void\* arg) {\
    // Called periodically by scheduler\
    static int counter = 0;\
    kout.print("Tick: ");\
    kout.println(counter++);\
    task\_sleep(500);\
}\
\
void my\_deinit() {\
    kout.println("\[MyModule] Cleanup");\
}\
\
ModuleCallbacks my\_callbacks = {\
    my\_init,    // Called on creation\
    my\_tick,    // Called periodically\
    my\_deinit   // Called before termination\
};\
\
// Create task with callbacks\
task\_create("mymodule", NULL, NULL, 10,\
            TASK\_TYPE\_MODULE, TASK\_FLAG\_RESPAWN,\
            0, OOM\_PRIORITY\_NORMAL, 4096,\
            \&my\_callbacks, "Example module");


### **Task Sleep**

void task\_sleep(uint32\_t ms);  // Sleep current task\
\
// Example:\
while(1) {\
    process\_data();\
    task\_sleep(16);  // \~60 FPS\
}


### **Task Information**

\# View all tasks\
Picomimi\~> ps\
\
\# Detailed info\
Picomimi\~> taskinfo 5\
\
\# System monitor\
Picomimi\~> top


## **5. Memory Management**

### **Allocation Functions**

void\* kmalloc(size\_t size, uint32\_t task\_id);\
void kfree(void\* ptr);\
\
// Example:\
void\* buffer = kmalloc(1024, kernel.current\_task);\
if (!buffer) {\
    kout.println("Allocation failed!");\
    return;\
}\
// Use buffer...\
kfree(buffer);


### **Memory Compaction**

// Manual compaction\
mem\_compact();\
\
// Check fragmentation\
Picomimi\~> mem\
Fragmentation: 15%


### **Per-Task Memory Limits**

// Set 8KB soft limit\
task\_create("limited\_app", app\_fn, NULL, 10,\
            TASK\_TYPE\_APPLICATION, 0, 0,\
            OOM\_PRIORITY\_NORMAL,\
            8 \* 1024,  // 8KB limit\
            NULL, "Limited memory app");


### **OOM Priority Levels**

OOM\_PRIORITY\_NEVER    (0)  // Kernel/critical tasks\
OOM\_PRIORITY\_CRITICAL (1)  // Very important apps\
OOM\_PRIORITY\_HIGH     (2)  // Important apps\
OOM\_PRIORITY\_NORMAL   (3)  // Standard apps\
OOM\_PRIORITY\_LOW      (4)  // Expendable apps


### **Graceful OOM Handling**

static UISocket ui;\
static void\* my\_cache = NULL;\
static size\_t cache\_size = 0;\
\
void my\_oom\_handler(uint32\_t bytes\_requested) {\
    kout.println("\[MyApp] OOM request received");\
   \
    uint32\_t freed = 0;\
   \
    // Free your cache\
    if (my\_cache) {\
        freed = cache\_size;\
        kfree(my\_cache);\
        my\_cache = NULL;\
        cache\_size = 0;\
    }\
   \
    // Notify kernel\
    ui.oom\_cleanup\_done(kernel.current\_task, freed);\
   \
    kout.print("\[MyApp] Freed ");\
    kout.print(freed);\
    kout.println(" bytes");\
}\
\
void spawn\_myapp() {\
    k\_register\_gui\_app(\&ui);\
   \
    // Register OOM handler\
    ui.register\_oom\_handler(kernel.current\_task, my\_oom\_handler);\
   \
    // Allocate cache\
    cache\_size = 16 \* 1024;\
    my\_cache = kmalloc(cache\_size, kernel.current\_task);\
   \
    // ... rest of app\
}


### **Memory Pressure Hints**

// Proactively trigger compaction\
if (get\_free\_memory() < 10240) {  // Less than 10KB\
    ui.hint\_memory\_pressure(kernel.current\_task);\
}


### **Memory Commands**

\# Memory stats\
Picomimi\~> mem\
Total:         180 KB\
Used:          87 KB\
Free:          93 KB\
\
\# Memory map\
Picomimi\~> memmap\
Addr       Size     Owner  Free  Seq\
0x20010000 4096     3      N     42\
0x20011000 8192     5      N     43\
\
\# Manual compaction\
Picomimi\~> compact


## **6. Scheduling**

### **Priority Levels**

0-23:  Normal priority (aging enabled)\
24-31: Real-time priority (no aging)\
\
Recommended:\
  0:    Idle task\
  2-5:  Background services\
  8-12: Standard applications\
  16-20: Important applications\
  24-27: Soft real-time (sensors, input)\
  28-31: Hard real-time (audio, display)


### **O(1) Scheduler**

The kernel uses bitmap-based constant-time task selection:

// Always O(1) regardless of task count\
uint32\_t next\_task = sched\_select\_next\_core0();


### **Dynamic Priority Adjustment**

// CPU-bound tasks (use full quantum)\
Priority decreases over time\
\
// I/O-bound tasks (yield early)\
Priority increases over time\
\
// Starving tasks (wait > 1 second)\
Priority boosted automatically


### **Time Quantum**

Base quantum:  5ms\
Max quantum:   80ms\
\
Real-time tasks: Fixed 5ms quantum\
Normal tasks:    5ms + (32 - priority) \* 2ms


### **Idle Injection**

// Automatic at >85% CPU load\
// Injects 10% idle time to prevent overheating


### **Scheduler Statistics**

Picomimi\~> schedstat\
\=== Scheduler Statistics ===\
Algorithm: O(1) Priority Bitmap\
Priority Levels: 32\
\
\--- Core 0 ---\
Context Switches: 15420\
CPU Load (avg):   45.2%\
CPU Load (inst):  52.1%\
Idle Injections:  3\
\
\--- Core 1 ---\
Context Switches: 8912\
CPU Load:         38.7%\
\
\--- Priority Distribution ---\
Pri 28: 1 task \[RT]\
Pri 10: 2 tasks\
Pri  2: 2 tasks\
Pri  0: 1 task


## **7. IPC System**

### **Message Structure**

struct IPCMessage {\
    uint32\_t sender\_id;\
    uint32\_t target\_id;\
    IPCMessageType type;\
    uint8\_t priority;        // 0-255\
    uint64\_t timestamp;\
    uint16\_t sequence;\
    uint8\_t data\[64];        // Payload\
};


### **Message Types**

IPC\_NONE\
IPC\_RENDER\_FRAME     // Graphics data\
IPC\_PROCESS\_INPUT    // Input events\
IPC\_COMPUTE\_DATA     // Computation results\
IPC\_AUDIO\_SAMPLE     // Audio data\
IPC\_USER\_DEFINED     // Custom messages


### **Sending Messages**

// Normal priority (150)\
ui.send\_to\_core1(target\_id, IPC\_RENDER\_FRAME,\
&#x20;                \&frame\_data, sizeof(frame\_data));\
\
// High priority (200+)\
ui.send\_priority(target\_id, IPC\_RENDER\_FRAME,\
&#x20;                \&frame\_data, sizeof(frame\_data), 200);


### **Receiving Messages**

void core1\_task(void\* arg) {\
    k\_register\_gui\_app(\&ui);\
   \
    while(1) {\
        IPCMessage msg;\
        if (ui.receive\_message(\&msg)) {\
            switch(msg.type) {\
                case IPC\_RENDER\_FRAME:\
                    render\_frame((GameState\*)msg.data);\
                    break;\
                   \
                case IPC\_PROCESS\_INPUT:\
                    handle\_input((InputEvent\*)msg.data);\
                    break;\
            }\
        }\
        task\_sleep(1);  // Check frequently\
    }\
}


### **Priority Queues**

High Priority (200-255):   Urgent, time-critical\
Normal Priority (100-199): Standard messages\
Low Priority (0-99):       Background tasks\
\
Messages delivered highest-priority first.\
Within same priority: FIFO order.


### **Flow Control**

// Automatic throttling at 75% queue capacity\
// High-priority messages bypass throttle\
// Recovers at 50% capacity


### **IPC Statistics**

Picomimi\~> ipcstat\
\=== IPC Statistics ===\
Total Messages: 8/32\
  High Priority:   2\
  Normal Priority: 5\
  Low Priority:    1\
\
Flow Control: Inactive\
Threshold: 24\
\
\--- Lifetime Stats ---\
Sent:     1542\
Received: 1540\
Dropped:  2


## **8. File Systems**

### **VFS (RAM Filesystem)**

\# Create VFS (128KB RAM)\
Picomimi\~> vfscreate\
\
\# List files\
Picomimi\~> vfsls\
\
\# Statistics\
Picomimi\~> vfsstat


### **VFS API**

// Create file\
int fd = vfs\_create("myfile.txt", FILE\_TYPE\_TEXT, task\_id);\
\
// Write data\
const char\* data = "Hello, VFS!";\
vfs\_write(fd, data, strlen(data));\
\
// Read data\
char buffer\[256];\
vfs\_read(fd, buffer, sizeof(buffer));\
\
// Delete file\
vfs\_delete(fd);


### **FS (SD Card)**

\# List SD card\
Picomimi\~> ls\
Picomimi\~> ls /data\
\
\# Show file\
Picomimi\~> cat /config.txt\
\
\# Statistics\
Picomimi\~> stat\
Total space:   4096 MB\
Used space:    512 MB\
Free space:    3584 MB


### **FS API**

// Open file\
int fd = fs\_open("/data/log.txt", true);  // Write mode\
\
// Write string\
fs\_write\_str(fd, "Log entry\n");\
\
// Read string\
char buffer\[512];\
fs\_read\_str(fd, buffer, sizeof(buffer));\
\
// Close file\
fs\_close(fd);\
\
// Check existence\
if (fs\_exists("/config.txt")) {\
    // File exists\
}\
\
// Create directory\
fs\_mkdir("/data");\
\
// Remove file\
fs\_remove("/old\_file.txt");


### **Error Logging to SD**

// High-severity logs automatically written to /LogRecord\
klog(2, "Error occurred");  // Saved to SD if mounted\
klog(3, "Critical failure");\
\
// View log\
Picomimi\~> cat /LogRecord


## **9. Application Development**

### **Complete Application Template**

This file (GameApp.ino) must be added to the main Picomimi\_v10\_Manifest-v2.ino sketch using the Arduino IDE's **Sketch -> Add File...** menu.

// GameApp.ino\
//\
// This file must be added to the main Picomimi kernel sketch\
// using the Arduino IDE's "Sketch -> Add File..." menu.\
// DO NOT #include the kernel .ino file.\
\
// === Application State ===\
static UISocket ui;\
static uint32\_t main\_task\_id;\
static uint32\_t render\_task\_id;\
\
struct GameState {\
    int player\_x, player\_y;\
    int score;\
    bool running;\
};\
\
static GameState game\_state;\
static void\* frame\_buffer = NULL;\
\
// === OOM Handler ===\
void game\_oom\_handler(uint32\_t bytes\_requested) {\
    kout.println("\[Game] OOM - releasing frame buffer");\
   \
    uint32\_t freed = 0;\
    if (frame\_buffer) {\
        freed = 76800;  // 320x240 display\
        kfree(frame\_buffer);\
        frame\_buffer = NULL;\
    }\
   \
    ui.oom\_cleanup\_done(main\_task\_id, freed);\
}\
\
// === Core1 Renderer ===\
void render\_loop(void\* arg) {\
    k\_register\_gui\_app(\&ui);\
   \
    kout.println("\[Render] Core1 started");\
   \
    while(1) {\
        IPCMessage msg;\
        if (ui.receive\_message(\&msg)) {\
            if (msg.type == IPC\_RENDER\_FRAME) {\
                GameState\* state = (GameState\*)msg.data;\
               \
                // Lock display resource\
                ui.lock\_resource(0);\
               \
                // Render game (pseudo-code)\
                // draw\_player(state->player\_x, state->player\_y);\
                // draw\_score(state->score);\
               \
                ui.unlock\_resource(0);\
            }\
        }\
       \
        task\_sleep(16);  // \~60 FPS\
    }\
}\
\
// === Core0 Main Logic ===\
void spawn\_game() {\
    k\_register\_gui\_app(\&ui);\
    ui.request\_focus(kernel.current\_task);\
    main\_task\_id = kernel.current\_task;\
   \
    kout.println("\[Game] Starting...");\
   \
    // Register OOM handler\
    ui.register\_oom\_handler(main\_task\_id, game\_oom\_handler);\
   \
    // Allocate frame buffer\
    frame\_buffer = kmalloc(76800, main\_task\_id);\
    if (!frame\_buffer) {\
        kout.println("\[Game] Failed to allocate frame buffer!");\
        return;\
    }\
   \
    // Spawn high-priority renderer on Core1\
    render\_task\_id = ui.spawn\_core1\_task(\
        "game\_render",\
        render\_loop,\
        NULL,\
        28  // High priority\
    );\
   \
    // Initialize game state\
    game\_state.player\_x = 160;\
    game\_state.player\_y = 120;\
    game\_state.score = 0;\
    game\_state.running = true;\
   \
    kout.println("\[Game] Running! Press button to cycle focus.");\
   \
    // Main game loop\
    while(game\_state.running) {\
        // Update game logic\
        game\_state.player\_x += 1;\
        if (game\_state.player\_x > 320) game\_state.player\_x = 0;\
        game\_state.score++;\
       \
        // Send frame to renderer (high priority)\
        ui.send\_priority(\
            render\_task\_id,\
            IPC\_RENDER\_FRAME,\
            \&game\_state,\
            sizeof(game\_state),\
            200  // High priority\
        );\
       \
        // Check memory pressure\
        if (get\_free\_memory() < 10240) {\
            ui.hint\_memory\_pressure(main\_task\_id);\
        }\
       \
        task\_sleep(16);  // \~60 FPS logic\
    }\
   \
    kout.println("\[Game] Exiting...");\
    ui.release\_focus(main\_task\_id);\
}\
\
// === Registration ===\
struct GameAppReg {\
    GameAppReg() {\
        Application\_Register("game", spawn\_game);\
    }\
} \_game\_reg;


### **Launch the Game**

Picomimi\~> listapps\
1\. game\
\
Picomimi\~> game\
\[Game] Starting...\
\[Render] Core1 started\
\[Game] Running! Press button to cycle focus.


## **10. Shell Commands**

### **System Information**

help          # Show all commands\
ps            # List all tasks (both cores)\
taskinfo \<id> # Detailed task information\
top           # System monitor\
uptime        # System uptime\
temp          # CPU temperature


### **Memory Commands**

mem           # Memory statistics\
memmap        # Memory block visualization\
compact       # Manual memory compaction


### **Scheduler Commands**

schedstat     # Scheduler statistics


### **IPC Commands**

ipcstat       # IPC queue statistics


### **OOM Commands**

oomstat       # OOM killer statistics


### **Application Commands**

listapps      # Show registered apps\
\<appname>     # Launch application


### **File System Commands**

\# VFS (RAM filesystem)\
vfscreate     # Create VFS\
vfsls         # List VFS files\
vfsstat       # VFS statistics\
\
\# FS (SD card)\
ls \[path]     # List files\
stat          # SD card statistics\
cat \<path>    # Display file contents


### **Logging Commands**

dmesg         # View kernel log


### **System Control**

root          # Toggle root mode\
kill \<id>     # Terminate task (requires root)\
reboot        # Restart system


## **11. Best Practices**

### **Task Design**

- **DO**: Use task\_sleep() to yield CPU voluntarily.

- **DO**: Set appropriate priorities (do not overuse real-time).

- **DO**: Register OOM handlers for applications with caches.

- **DO**: Use module callbacks for stateless tasks.

- **DO**: Set memory limits for applications where appropriate.

- **DO**: Handle errors gracefully.

- **DON'T**: Busy-wait in loops.

- **DON'T**: Use priority >24 unless truly real-time.

- **DON'T**: Ignore OOM requests.

- **DON'T**: Leak memory.

- **DON'T**: Hold locks indefinitely.


### **Memory Management**

- **DO**: Free all allocated memory.

- **DO**: Check allocation return values.

- **DO**: Use memory hints when under pressure.

- **DO**: Release caches in OOM handlers.

- **DO**: Track your allocations.

- **DON'T**: Assume kmalloc always succeeds.

- **DON'T**: Forget to free memory.

- **DON'T**: Exceed your memory limit.

- **DON'T**: Allocate in tight loops.


### **Dual-Core Usage**

- **DO**: Use Core1 for rendering, audio, or heavy computation.

- **DO**: Send high-priority IPC for time-critical data.

- **DO**: Lock shared resources.

- **DO**: Check message receive success.

- **DON'T**: Share memory without locking.

- **DON'T**: Flood the IPC queue.

- **DON'T**: Ignore flow control.

- **DON'T**: Assume instant delivery.


### **IPC Patterns**

// Producer (Core0)\
GameState state = get\_game\_state();\
ui.send\_priority(renderer\_id, IPC\_RENDER\_FRAME,\
&#x20;                \&state, sizeof(state), 200);\
\
// Consumer (Core1)\
IPCMessage msg;\
if (ui.receive\_message(\&msg)) {\
    if (msg.type == IPC\_RENDER\_FRAME) {\
        GameState\* state = (GameState\*)msg.data;\
        render(state);\
    }\
}


### **Resource Locking Pattern**

// Acquire lock\
if (ui.lock\_resource(0)) {\
    // Critical section\
    display.drawPixel(x, y, color);\
   \
    // Release lock\
    ui.unlock\_resource(0);\
}


### **Error Handling**

void\* buffer = kmalloc(size, task\_id);\
if (!buffer) {\
    kout.println("Allocation failed!");\
    ui.hint\_memory\_pressure(task\_id);\
    return false;\
}\
\
// Use buffer...\
\
kfree(buffer);


## **12. API Reference**

### **Core Functions**

// Time\
uint64\_t get\_time\_us();\
uint64\_t get\_time\_ms();\
void task\_sleep(uint32\_t ms);\
\
// Memory\
void\* kmalloc(size\_t size, uint32\_t task\_id);\
void kfree(void\* ptr);\
size\_t get\_free\_memory();\
size\_t get\_used\_memory();\
void mem\_compact();\
\
// Logging\
void klog(uint8\_t level, const char\* msg);\
// Levels: 0=info, 1=warning, 2=error, 3=critical\
\
// Registration\
void Application\_Register(const char\* name, void (\*spawn\_func)());\
void k\_register\_gui\_app(UISocket\* socket\_api);


### **UISocket API**

struct UISocket {\
    // Focus management\
    bool (\*request\_focus)(uint32\_t task\_id);\
    void (\*release\_focus)(uint32\_t task\_id);\
   \
    // Output redirection\
    void (\*register\_stdout)(void (\*write\_char\_fn)(char));\
   \
    // IPC\
    bool (\*send\_to\_core1)(uint32\_t target\_id, IPCMessageType type,\
                          void\* data, size\_t size);\
    bool (\*send\_priority)(uint32\_t target\_id, IPCMessageType type,\
                          void\* data, size\_t size, uint8\_t priority);\
    bool (\*receive\_message)(IPCMessage\* msg\_out);\
   \
    // Task management\
    uint32\_t (\*spawn\_core1\_task)(const char\* name, void (\*entry)(void\*),\
&#x20;                                void\* arg, uint8\_t priority);\
   \
    // Resource locking\
    bool (\*lock\_resource)(uint32\_t resource\_id);\
    void (\*unlock\_resource)(uint32\_t resource\_id);\
   \
    // Monitoring\
    float (\*get\_core0\_usage)();\
    float (\*get\_core1\_usage)();\
    uint32\_t (\*get\_task\_memory)(uint32\_t task\_id);\
   \
    // OOM handling\
    void (\*register\_oom\_handler)(uint32\_t task\_id,\
&#x20;                                void (\*handler)(uint32\_t bytes\_requested));\
    void (\*oom\_cleanup\_done)(uint32\_t task\_id, uint32\_t bytes\_freed);\
    void (\*hint\_memory\_pressure)(uint32\_t task\_id);\
};


### **Task Creation**

uint32\_t task\_create(\
    const char\* name,\
    void (\*entry)(void\*),\
    void\* arg,\
    uint8\_t priority,           // 0-31\
    uint8\_t task\_type,          // TASK\_TYPE\_\*\
    uint32\_t flags,             // TASK\_FLAG\_\*\
    uint64\_t max\_runtime\_ms,    // 0 = unlimited\
    uint8\_t oom\_priority,       // 0-4\
    uint32\_t mem\_limit,         // 0 = unlimited\
    ModuleCallbacks\* callbacks,\
    const char\* description\
);


### **VFS API**

int vfs\_create(const char\* name, uint8\_t type, uint32\_t owner\_id);\
int vfs\_write(int fd, const void\* data, uint32\_t size);\
int vfs\_read(int fd, void\* buffer, uint32\_t size);\
void vfs\_delete(int fd);\
void vfs\_list();\
void vfs\_stats();


### **FS API**

bool fs\_mount();\
void fs\_unmount();\
bool fs\_exists(const char\* path);\
bool fs\_mkdir(const char\* path);\
bool fs\_remove(const char\* path);\
void fs\_list(const char\* path);\
void fs\_stats();\
int fs\_open(const char\* path, bool write\_mode);\
void fs\_close(int fd);\
int fs\_write\_str(int fd, const char\* data);\
int fs\_read\_str(int fd, char\* buffer, size\_t size);\
void fs\_cat(const char\* path);


## **13. Troubleshooting**

### **Common Issues**

**Issue: Task not running**

\# Check if task exists\
Picomimi\~> ps\
\
\# Check task state\
Picomimi\~> taskinfo \<id>\
\
\# Check priority\
Picomimi\~> schedstat

**Issue: Out of memory**

\# Check memory usage\
Picomimi\~> mem\
\
\# Check which tasks use memory\
Picomimi\~> ps\
\
\# Check OOM stats\
Picomimi\~> oomstat\
\
\# Manually compact\
Picomimi\~> compact

**Issue: IPC messages not received**

\# Check queue status\
Picomimi\~> ipcstat\
\
\# Check if flow control active\
\# Increase message priority if needed\
ui.send\_priority(target, type, data, size, 200);

**Issue: System hangs**

- Watchdog will reset after 8 seconds.

- Check dmesg after reboot for panic info.

- Check if a critical task died.

**Issue: SD card not detected**

\# Check FS status\
Picomimi\~> stat\
\
\# Check wiring (defined in kernel .ino file):\
\# SD\_CS   = 5\
\# SD\_MOSI = 19\
\# SD\_MISO = 16\
\# SD\_SCK  = 18


### **Debug Tips**

// Add logging\
klog(0, "Reached checkpoint A");\
\
// Check memory\
kout.print("Free memory: ");\
kout.println(get\_free\_memory());\
\
// Monitor CPU\
kout.print("Core0 CPU: ");\
kout.println(ui.get\_core0\_usage());\
\
// Check task state\
Picomimi\~> taskinfo \<id>\
\
// Monitor IPC queue\
Picomimi\~> ipcstat\
\
// Track allocations\
void\* ptr = kmalloc(size, task\_id);\
kout.print("Allocated at: 0x");\
kout.println((uint32\_t)ptr, HEX);


## **14. Advanced Topics**

### **Real-Time Tasks**

Real-time tasks (priority 24-31) receive guaranteed scheduling:

// Hard real-time audio task\
task\_create("audio\_rt", audio\_fn, NULL,\
            31,  // Highest RT priority\
            TASK\_TYPE\_DRIVER,\
            TASK\_FLAG\_PROTECTED | TASK\_FLAG\_CRITICAL,\
            0, OOM\_PRIORITY\_NEVER, 0, NULL,\
            "48kHz audio processor");\
\
// RT task characteristics:\
// - Fixed 5ms quantum\
// - No priority aging\
// - Always scheduled before normal tasks\
// - Preempts lower priorities instantly


### **Custom IPC Message Types**

// Define custom message types\
\#define IPC\_GAME\_EVENT    (IPC\_USER\_DEFINED + 1)\
\#define IPC\_NETWORK\_DATA  (IPC\_USER\_DEFINED + 2)\
\#define IPC\_SENSOR\_UPDATE (IPC\_USER\_DEFINED + 3)\
\
// Send custom message\
struct SensorData {\
    float temperature;\
    float humidity;\
    uint32\_t timestamp;\
};\
\
SensorData data = {25.5, 60.0, get\_time\_ms()};\
ui.send\_priority(logger\_task, IPC\_SENSOR\_UPDATE,\
&#x20;                \&data, sizeof(data), 150);\
\
// Receive custom message\
IPCMessage msg;\
if (ui.receive\_message(\&msg)) {\
    if (msg.type == IPC\_SENSOR\_UPDATE) {\
        SensorData\* sensor = (SensorData\*)msg.data;\
        log\_sensor\_data(sensor);\
    }\
}


### **Persistent Configuration**

// Save config to SD card\
void save\_config(Config\* cfg) {\
    int fd = fs\_open("/config.bin", true);\
    if (fd >= 0) {\
        fs\_write\_str(fd, (char\*)cfg);\
        fs\_close(fd);\
        klog(0, "Config saved");\
    }\
}\
\
// Load config from SD card\
bool load\_config(Config\* cfg) {\
    if (!fs\_exists("/config.bin")) return false;\
   \
    int fd = fs\_open("/config.bin", false);\
    if (fd >= 0) {\
        fs\_read\_str(fd, (char\*)cfg, sizeof(Config));\
        fs\_close(fd);\
        klog(0, "Config loaded");\
        return true;\
    }\
    return false;\
}\
\
// Usage\
void spawn\_myapp() {\
    Config cfg;\
    if (!load\_config(\&cfg)) {\
        // Use defaults\
        cfg.volume = 50;\
        cfg.brightness = 100;\
        save\_config(\&cfg);\
    }\
    // Use config...\
}


### **Watchdog Integration**

The kernel automatically feeds the hardware watchdog. If the system hangs:

1. Watchdog expires after 8 seconds.

2. RP2040 resets automatically.

3. Kernel detects watchdog reboot on startup.

4. Panic info preserved in /PANIC.LOG (if FS available).

Check for watchdog reboots:

Picomimi\~> dmesg\
\[0.123] \[0] KERNEL: Boot v10.0.0 Grand Release\
\[0.245] \[1] WDT: Watchdog caused reboot!


### **Kernel Panic Handling**

If a critical error occurs:

╔═══════════════════════════════════════╗\
║     \*\*\* KERNEL PANIC \*\*\* ║\
╚═══════════════════════════════════════╝\
\
Reason:    IDLE TASK DEAD\
Core:      0\
Task:      idle (ID=0)\
Uptime:    1234 s\
\
\--- System State ---\
Tasks:     12\
Memory:    45/180 KB\
CPU Usage: 34.5%\
\
System halted. Watchdog will reset in 8s...


## **15. System Limits**

### **Hard Limits**

Max Core0 Tasks:           32\
Max Core1 Tasks:           16\
Max Memory Blocks:         256\
Heap Size:                 180 KB\
Max IPC Messages:          32\
IPC Message Payload:       64 bytes\
Max Log Entries:           40\
Max Registered Apps:       16\
Max GUI Apps:              8\
Max Resource Locks:        16\
Max OOM Handlers:          16\
Priority Levels:           32 (0-31)\
Task Name Length:          24 chars\
File Name Length (VFS):    16 chars\
File Name Length (FS):     32 chars\
VFS Max Files:             16\
VFS Storage:               128 KB\
FS Max Open Files:         8


### **Recommended Limits**

Typical task count:        8-16 tasks\
Safe CPU usage:            <85% (before idle injection)\
Recommended clock:         225 MHz\
Conservative clock:        125 MHz\
Aggressive overclock:      250 MHz\
Maximum overclock:         276 MHz\
IPC message rate:          <1000/sec sustained\
Memory fragmentation:      <30% acceptable\
Task memory limit:         4-16 KB typical apps


## **16. Configuration Reference**

### **Compile-Time Constants**

Edit these in the main Picomimi\_v10\_Manifest-v2.ino file before compiling:

// Task limits\
\#define MAX\_TASKS 32\
\#define MAX\_CORE1\_TASKS 16\
\
// Memory\
\#define HEAP\_SIZE (180 \* 1024)\
\#define MAX\_MEMORY\_BLOCKS 256\
\
// IPC\
\#define MAX\_IPC\_MESSAGES 32\
\#define IPC\_MSG\_SIZE 64\
\
// Scheduler\
\#define SCHED\_NUM\_PRIORITY\_LEVELS 32\
\#define SCHED\_RT\_THRESHOLD 24\
\
// Watchdog\
\#define WATCHDOG\_TIMEOUT\_MS 8000\
\
// VFS\
\#define VFS\_STORAGE\_SIZE (128 \* 1024)


### **Pin Configuration**

Edit these definitions at the top of the main Picomimi\_v10\_Manifest-v2.ino file:

\#define SD\_CS       5\
\#define SD\_MOSI     19\
\#define SD\_MISO     16\
\#define SD\_SCK      18\
\#define BTN\_ONOFF   9


### **Arduino IDE Settings**

Board: Raspberry Pi Pico\
CPU Speed: 225 MHz (recommended)\
Optimize: -O3 (Optimize More)\
USB Stack: Pico SDK\
Debug Level: None


## **17. License**

**MIT License**

Copyright (c) 2024 Picomimi Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

_End of Documentation_

__
