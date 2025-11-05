# Picomimi Milestone v10 M2 - Complete Feature List

> **A Dual-Core Microkernel for RP2040 with High-Grade Scheduling, Memory Management, and IPC**

---

## Core Architecture

### Dual-Core Processing
- **Symmetric Multiprocessing (SMP)** architecture utilizing both ARM Cortex-M0+ cores
- **Independent schedulers** per core with synchronized state management
- **Core affinity system** allowing tasks to be pinned to specific cores or float between them
- **Lock-free IPC queue** for inter-core communication with priority-based routing
- **Automatic core initialization** on boot with health monitoring
- **Core-specific statistics tracking** including CPU usage, context switches, and uptime
- **Isolated task execution environments** preventing cross-core interference
- **Dynamic load balancing** hints for optimal core utilization

### Microkernel Design Philosophy
- **Minimal kernel footprint** with only essential services running in kernel space
- **Service-oriented architecture** where hardware drivers run as user tasks
- **Policy-free core** - applications define their own behavior patterns
- **Zero-copy message passing** where possible for maximum performance
- **Protected kernel memory** with task isolation boundaries
- **Modular subsystem design** allowing components to be disabled or replaced
- **Clean separation** between kernel, drivers, services, modules, and applications

---

## Advanced Scheduling System

### O(1) Bitmap Scheduler
- **Constant-time task selection** regardless of number of tasks (O(1) complexity)
- **32 priority levels** (0-31) providing fine-grained control over task execution
- **Real-time task support** with priorities 24-31 receiving guaranteed scheduling
- **Priority bitmap indexing** using hardware bit scanning for ultra-fast lookup
- **Per-core independent schedulers** eliminating cross-core scheduling contention
- **Lock-optimized design** minimizing critical sections during context switches

### Dynamic Priority Management
- **Base priority preservation** while allowing runtime effective priority changes
- **Automatic priority aging** to prevent task starvation
- **Starvation detection** boosting priority of tasks waiting over 1 second
- **CPU-bound task penalty** reducing priority of tasks that consume full quantum
- **I/O-bound task boost** increasing priority of tasks that yield early
- **Priority ceiling protocols** for system-critical tasks
- **Real-time deadline preservation** for time-sensitive operations

### Time Quantum System
- **Variable quantum allocation** based on task priority (5ms - 80ms range)
- **Dynamic quantum adjustment** rewarding yielding behavior
- **Preemptive multitasking** with forced quantum expiration
- **Voluntary yield support** allowing cooperative scheduling
- **Quantum inheritance** for spawned child tasks
- **Per-task quantum tracking** for performance analysis
- **Quantum statistics** available via `schedstat` command

### Load Management
- **Exponential weighted moving average** for smooth CPU load calculation
- **Instantaneous load tracking** for burst detection
- **Idle injection mechanism** preventing thermal throttling at >85% CPU
- **Per-core load balancing** with cross-core migration hints
- **Load history tracking** for trend analysis
- **Thermal-aware scheduling** integrating temperature monitoring

### Context Switching
- **Ultra-fast context switches** optimized for ARM Cortex-M0+
- **Context switch counters** per task and per core
- **Preemption tracking** distinguishing voluntary from forced switches
- **State preservation** across context switches
- **Register bank management** for full state recovery
- **Context switch statistics** accessible via shell commands

---

## Intelligent Memory Management

### Custom Allocator (kmalloc/kfree)
- **180KB heap** with efficient block management
- **4-byte alignment** for optimal memory access
- **First-fit allocation** strategy with block splitting
- **Automatic block coalescing** on free operations
- **Per-task memory accounting** tracking every allocation
- **Memory sequence numbering** for allocation tracking
- **Thread-safe operations** with mutex-protected critical sections
- **Zero-overhead for kernel tasks** with minimal bookkeeping

### Memory Blocks System
- **256 block tracking entries** with detailed metadata
- **Block ownership tracking** by task ID
- **Allocation timestamps** for leak detection
- **Allocation sequence numbers** for chronological analysis
- **Peak memory tracking** per task
- **Block state management** (free/allocated)
- **Memory map visualization** via `memmap` command

### Fragmentation Management
- **Real-time fragmentation calculation** showing percentage of unusable memory
- **Largest free block tracking** for allocation planning
- **Multi-pass compaction algorithm** with up to 3 passes
- **Automatic compaction triggers** when fragmentation exceeds thresholds
- **Manual compaction** via `compact` shell command
- **Adjacent block merging** eliminating gaps
- **Sorted block management** by memory address for optimal merging

### Per-Task Memory Limits
- **Configurable memory caps** per application task
- **Soft limit warnings** before hard enforcement
- **Limit enforcement at allocation time** preventing overruns
- **Memory quota system** for resource fairness
- **Application-type-specific limits** (kernel tasks get unlimited)
- **Limit violation logging** for debugging

### Memory Statistics
- **Total allocation counter** across system lifetime
- **Total free counter** for leak detection
- **Active block count** showing current allocations
- **Used vs. free memory** real-time tracking
- **Fragmentation percentage** calculation
- **Largest free block size** reporting
- **Per-task memory usage** with peak tracking
- **Comprehensive `mem` command** output for analysis

---

## Graceful Out-of-Memory (OOM) Killer

### Prevention System
- **Multi-stage prevention** attempting recovery before killing tasks
- **Automatic memory compaction** as first recovery attempt
- **Aggressive multi-pass compaction** with up to 3 iterations
- **Fragmentation analysis** before resorting to task termination
- **Prevention success tracking** via statistics
- **Early warning system** detecting low memory conditions

### Task Handler Registration
- **Per-task OOM handlers** allowing graceful cleanup
- **Handler callback system** for memory release requests
- **16 handler slots** for concurrent registrations
- **Handler priority consideration** in victim selection
- **Automatic handler unregistration** on task termination
- **Handler invocation tracking** for compliance monitoring

### Victim Selection Algorithm
- **Multi-factor scoring system** for fair victim selection
- **Memory usage weighting** (more memory = higher score)
- **OOM priority levels** (0-4) controlling kill likelihood
  - `OOM_PRIORITY_NEVER` (0) - Never killed (kernel/critical tasks)
  - `OOM_PRIORITY_CRITICAL` (1) - Last resort only
  - `OOM_PRIORITY_HIGH` (2) - Protected but killable
  - `OOM_PRIORITY_NORMAL` (3) - Standard applications
  - `OOM_PRIORITY_LOW` (4) - Expendable tasks
- **Idle time consideration** favoring inactive tasks
- **Handler penalty** giving tasks with cleanup handlers more time
- **System importance weighting** protecting drivers and services
- **Task type filtering** (only applications are killable)

### Request and Cleanup Flow
- **Grace period system** giving tasks 2 seconds to comply
- **Cleanup request tracking** monitoring task responses
- **Voluntary release counting** rewarding cooperative behavior
- **Forced kill escalation** after timeout expiration
- **Cleanup completion API** (`k_oom_cleanup_done()`)
- **Bytes reclaimed tracking** for effectiveness measurement

### OOM Statistics
- **Prevention success counter** showing avoided kills
- **Graceful request counter** tracking cooperative attempts
- **Voluntary release counter** measuring task cooperation
- **Forced kill counter** showing escalated terminations
- **Total bytes reclaimed** across all operations
- **Handler registration count** showing prepared tasks
- **Comprehensive `oomstat` command** for analysis

### Memory Pressure Hints
- **Application-initiated hints** via `hint_memory_pressure()`
- **Proactive compaction triggers** before critical shortage
- **Pressure event logging** for pattern analysis
- **Cooperative memory management** between kernel and apps

---

## Inter-Process Communication (IPC)

### Priority-Aware Message Queue
- **32 message slots** with efficient management
- **Three priority tiers** for message routing:
  - High Priority (200-255): Time-critical data
  - Normal Priority (100-199): Standard messages
  - Low Priority (0-99): Background tasks
- **Per-priority queue management** with independent head/tail pointers
- **Priority-ordered delivery** ensuring high-priority messages jump queue
- **Sequence numbering** for ordering within same priority
- **FIFO within priority level** for fairness

### Flow Control Mechanism
- **Automatic throttling** when queue reaches 75% capacity
- **High-priority bypass** allowing critical messages during throttling
- **Flow control event tracking** for congestion analysis
- **Automatic recovery** when queue drops below 50% threshold
- **Backpressure signaling** to sending tasks
- **Flow control statistics** via `ipcstat` command

### Message Structure
- **64-byte data payload** per message
- **Sender/target ID tracking** for routing
- **Message type enumeration** (RENDER_FRAME, PROCESS_INPUT, COMPUTE_DATA, etc.)
- **Priority field** for queue placement
- **Timestamp tracking** for latency analysis
- **Sequence numbers** for ordering
- **Retry counters** for reliability
- **Flags field** for future extensions
- **In-use tracking** for slot management

### Message Lifecycle
- **Atomic message send** with collision protection
- **Priority-based insertion** into appropriate queue
- **FIFO retrieval** within priority levels
- **Automatic expiration** of messages older than 5 seconds
- **Periodic maintenance** cleaning stale messages
- **Dropped message tracking** when queue is full

### IPC Statistics
- **Messages sent per priority** level tracking
- **Messages received per priority** level tracking
- **Dropped message counters** per priority
- **Queue full event counting** for capacity planning
- **Flow control event tracking** for congestion analysis
- **Average queue depth calculation** via exponential smoothing
- **Maximum queue depth** high-water mark
- **Total sent/received** lifetime counters
- **Comprehensive `ipcstat` command** output

### Core1 Task Spawning
- **Remote task creation** from Core0
- **Priority-aware spawning** (0-31 priority levels)
- **Argument passing** to spawned tasks
- **Task ID return** for message targeting
- **Automatic scheduler registration** on spawn
- **Task limit enforcement** (16 tasks per Core1)

---

## Resource Locking System

### Mutex-Based Locks
- **16 lockable resources** for shared hardware
- **Hardware mutex implementation** using RP2040 primitives
- **Blocking lock acquisition** with automatic waiting
- **Owner tracking** by task ID
- **Deadlock prevention** via timeout mechanisms
- **Lock state monitoring** via `ipcstat` command

### Common Use Cases
- **Display buffer locking** preventing screen tearing
- **SPI bus arbitration** for SD card and peripherals
- **Shared memory regions** for inter-task data
- **Hardware peripheral access** (ADC, PWM, etc.)
- **VFS superblock protection** during file operations

---

## Task Management System

### Task Control Block (TCB)
- **Comprehensive state tracking** per task
- **64-byte aligned** for cache optimization
- **32 tasks on Core0** + **16 tasks on Core1**
- **Task naming** (24 characters) for identification
- **Unique task IDs** (0-999 Core0, 1000+ Core1)
- **State machine** (READY, RUNNING, WAITING, SUSPENDED, TERMINATED, ZOMBIE)
- **Core affinity** (ANY, CORE_0, CORE_1)
- **Running core tracking** for migration analysis

### Task Types
- **TASK_TYPE_KERNEL** (0x01): Core kernel tasks (idle, etc.)
- **TASK_TYPE_DRIVER** (0x02): Hardware drivers (input, display)
- **TASK_TYPE_SERVICE** (0x04): System services (shell, cpumon)
- **TASK_TYPE_MODULE** (0x08): Optional modules (VFS, FS)
- **TASK_TYPE_APPLICATION** (0x10): User applications
- **Type-specific counters** tracking distribution
- **Type-based policies** (only apps killable by OOM)

### Task Flags
- **TASK_FLAG_PROTECTED** (0x01): Immune to normal termination
- **TASK_FLAG_CRITICAL** (0x02): Triggers kernel panic if killed
- **TASK_FLAG_RESPAWN** (0x04): Automatic resurrection after death
- **TASK_FLAG_ONESHOT** (0x08): Single instance enforcement
- **TASK_FLAG_PERSISTENT** (0x10): Survives system resets
- **Combinable flags** via bitwise OR

### Task Lifecycle
- **Creation via `task_create()`** with full parameter control
- **Name, priority, type, flags** specification
- **Entry point function** or callback registration
- **Argument passing** to task functions
- **OOM priority assignment** for kill protection
- **Memory limits** (soft caps)
- **Maximum runtime limits** for runaway protection
- **Automatic state transitions** during execution
- **Sleep/wake mechanisms** for timed waits
- **Respawn system** with 5-second cooldown
- **Respawn counting** tracking revival attempts
- **Graceful termination** with cleanup callbacks
- **Brutal kill support** via `kill` command (root only)

### Task Statistics
- **CPU time tracking** in milliseconds
- **Context switch counting** per task
- **Page fault tracking** (memory access patterns)
- **Memory usage tracking** (current and peak)
- **Uptime calculation** from creation timestamp
- **Last run timestamp** for idle detection
- **Voluntary yield counting** for I/O bound detection
- **Preemption counting** for CPU bound analysis

### Module Callbacks
- **Init callback** on task creation
- **Tick callback** for periodic execution
- **Deinit callback** on termination
- **Callback system** supporting stateless modules

---

## Shell and Command Interface

### Interactive Shell
- **Serial console** at 115200 baud
- **Command line editing** with backspace support
- **Command history** (via up/down arrows - future)
- **Root mode toggle** for dangerous operations
- **128-byte command buffer** for input
- **Prompt customization** (~ for user, # for root)
- **Real-time response** with no input lag

### System Information Commands
- **`help`** - Comprehensive command listing with descriptions
- **`ps`** - Process list showing all tasks on both cores
  - Task ID, core, name, type, state, priority, memory, CPU time
  - Summary statistics for both cores
  - Combined view of entire system
- **`taskinfo <id>`** - Detailed single-task analysis
  - Full state information
  - Memory usage and limits
  - CPU time and context switches
  - Uptime and runtime limits
  - Flags and description
  - Works for both Core0 and Core1 tasks
- **`top`** - System overview dashboard
  - Uptime, CPU per core, temperature
  - Memory usage, task count
  - OOM kill counter
- **`uptime`** - System uptime in days, hours, minutes, seconds

### Scheduler Commands
- **`schedstat`** - Scheduler statistics and analysis
  - Algorithm information (O(1) Bitmap)
  - Per-core context switch counters
  - CPU load (average and instantaneous)
  - Idle injection counters
  - Current running task per core
  - Priority distribution histogram
  - Real-time task identification

### Memory Commands
- **`mem`** - Memory statistics overview
  - Total heap size
  - Used and free memory
  - Largest free block
  - Fragmentation percentage
  - Allocation/free counters
  - Active block count
  - OOM kill counter
- **`memmap`** - Memory block visualization
  - Address, size, owner, free status
  - Allocation sequence numbers
  - First 20 blocks displayed
  - Continuation indicator for large maps
- **`compact`** - Manual memory compaction trigger

### IPC Commands
- **`ipcstat`** - IPC queue analysis
  - Total message count and capacity
  - Per-priority queue depths
  - Flow control status
  - Lifetime statistics (sent/received/dropped)
  - Queue full event counter
  - Average and max queue depth
  - Resource lock status display

### OOM Commands
- **`oomstat`** - OOM killer statistics
  - Prevention success count
  - Graceful request count
  - Voluntary release count
  - Forced kill count
  - Total bytes reclaimed
  - Registered handler count
  - List of tasks with handlers

### Application Commands
- **`listapps`** - Show registered applications
  - Numbered list of all apps
  - Launch by typing app name
- **Application launching** - Direct execution by name

### VFS Commands
- **`vfscreate`** - Initialize RAM-based VFS
- **`vfsls`** - List VFS files
  - File ID, name, type, size, block count, owner
  - File count and capacity
- **`vfsstat`** - VFS statistics
  - Total/free block counts
  - File count and capacity
  - Read/write operation counters

### FS (SD Card) Commands
- **`ls [path]`** - List SD card contents
  - Name, type (FILE/DIR), size
  - Defaults to root (/)
- **`stat`** - SD card statistics
  - Total space (if detectable)
  - Used and free space
  - Usage percentage
  - Read/write operation counters
- **`cat <path>`** - Display file contents
  - Full file output to terminal
  - Read operation counted

### Logging Commands
- **`dmesg`** - Kernel log viewer
  - Timestamped entries (ms resolution)
  - Log level indicators
  - Last 40 entries (ring buffer)
  - Chronological ordering

### System Control Commands
- **`temp`** - CPU temperature reading (Celsius)
- **`root`** - Toggle root mode (enables dangerous commands)
- **`kill <id>`** - Terminate task (requires root)
  - Graceful cleanup with callbacks
  - Memory release
  - File closure
  - OOM handler unregistration
  - Service state updates
- **`reboot`** - System restart via watchdog

---

## Kernel Services

### Idle Task (Priority 0)
- **Always runnable** fallback task
- **CPU idle state** when no other work
- **100ms sleep** for minimal overhead
- **Protected and critical** flags preventing termination
- **Automatic respawn** if somehow killed

### Shell Service (Priority 10)
- **Command processing** via serial console
- **128-byte command buffer** for input
- **Command parsing and execution** with error handling
- **Protected service** with respawn flag
- **4KB memory limit** for efficiency
- **Graceful deinit** on termination

### Input Cycle Driver (Priority 28)
- **High-priority driver** for responsive input
- **Button debouncing** (250ms threshold)
- **GUI focus cycling** between registered apps
- **Protected driver** with respawn capability
- **1KB memory limit** (minimal overhead)
- **20ms polling interval** for responsiveness

### CPU Monitor Service (Priority 2)
- **Background service** for load tracking
- **1-second update interval** for smooth metrics
- **Exponential weighted average** calculation
- **Protected service** with respawn
- **2KB memory limit**

### Temperature Monitor Service (Priority 2)
- **ADC-based temperature sensing** from RP2040's internal sensor
- **2-second update interval** for thermal tracking
- **Protected service** with respawn
- **2KB memory limit**
- **Temperature-based warnings** (future enhancement)

### VFS Service (Priority 8)
- **Optional in-RAM filesystem**
- **Superblock management** with metadata
- **Block bitmap allocation** tracking
- **File chain management** for fragmentation handling
- **Protected service** with respawn
- **4KB memory limit**

### FS Service (Priority 8)
- **SD card interface** via SPI
- **FAT filesystem support** (via Arduino SD library)
- **Error log persistence** to SD card
- **File handle management** (8 simultaneous open files)
- **Protected service** with respawn
- **4KB memory limit**
- **Automatic card detection** on boot

---

## Watchdog and Panic Handler

### Hardware Watchdog
- **8-second timeout** for hang detection
- **Automatic reboot** on timeout
- **Feed tracking** with timestamps
- **Trigger counting** across reboots
- **Panic mode detection** preventing reset during diagnostic output
- **Reboot reason detection** on startup
- **Warning at 75% timeout** (6 seconds)

### Kernel Panic System
- **Recursive panic prevention** avoiding infinite loops
- **Interrupt disabling** for stable diagnostic output
- **Panic information capture**:
  - Reason string
  - Faulting task ID and name
  - Core number (0 or 1)
  - Timestamp
  - Program counter, link register, stack pointer (architecture-dependent)
- **Formatted panic output** with clear headers
- **System state snapshot**:
  - Task count
  - Memory usage
  - CPU usage
- **Panic log to SD card** if filesystem available
- **Controlled halt** with 8-second delay before watchdog reset
- **Panic info preservation** across resets (future enhancement)

### Panic Triggers
- **Kernel loop faults** (invalid task ID, no tasks, etc.)
- **Idle task death** (critical system failure)
- **Kernel task termination** (via `kill` command)
- **OOM killer failure** (no killable tasks remaining)
- **Manual panic** via `kernel_panic()` function

---

## Logging System

### Kernel Log (klog)
- **40-entry ring buffer** for recent events
- **Millisecond-precision timestamps** for event ordering
- **Log levels** (0=info, 1=warning, 2=error, 3=critical)
- **Mutex-protected writes** for thread safety
- **Automatic wraparound** for continuous operation
- **Persistent logging** to SD card for level ≥ 2
- **Viewable via `dmesg`** command

### Log Entry Format
- **64-byte entries** with alignment padding
- **56-character message field** for detail
- **Timestamp field** (64-bit milliseconds)
- **Level field** for filtering
- **Cache-aligned** for performance

### SD Card Error Log
- **Separate error log file** (`/LogRecord`)
- **Numbered entries** for uniqueness
- **Timestamp prefixes** on each entry
- **Auto-creation** on first use
- **Persistence across reboots** with sequence continuation

---

## File Systems

### Virtual File System (VFS)
- **RAM-based filesystem** for temporary data
- **256-byte block size** for efficient allocation
- **512 blocks total** (128KB storage)
- **16 file capacity** with metadata
- **Block chain management** for file spanning
- **Free block bitmap** for fast allocation
- **Superblock magic** (0x52503230) for integrity
- **Version field** for future upgrades
- **File types** (TEXT, LOG, DATA, CONFIG)
- **Owner tracking** by task ID
- **Creation/modification timestamps**

#### VFS Operations
- **`vfs_create()`** - Create new file
- **`vfs_write()`** - Write data to file
- **`vfs_read()`** - Read data from file
- **`vfs_delete()`** - Remove file
- **`vfs_list()`** - List all files
- **`vfs_stats()`** - Filesystem statistics
- **`vfs_mount()`/`vfs_unmount()`** - Lifecycle management

### Physical File System (FS)
- **SD card support** via SPI interface
- **FAT16/FAT32 compatibility** (Arduino SD library)
- **Automatic card detection** with size estimation
- **Multi-GB support** with proper sector addressing
- **File handle management** (8 simultaneous opens)
- **Owner tracking** per file handle
- **Read/write mode support**
- **Directory operations** (list, traverse)

#### FS Operations
- **`fs_mount()`/`fs_unmount()`** - Card lifecycle
- **`fs_exists()`** - Check file presence
- **`fs_mkdir()`** - Create directory
- **`fs_remove()`** - Delete file
- **`fs_open()`/`fs_close()`** - File handle management
- **`fs_write_str()`** - String write
- **`fs_read_str()`** - String read
- **`fs_list()`** - Directory listing
- **`fs_stats()`** - Card usage statistics
- **`fs_cat()`** - File content display

---

## UISocket API (Application Interface)

### Focus Management
- **`request_focus(task_id)`** - Request GUI focus
- **`release_focus(task_id)`** - Release GUI focus
- **Focus tracking** by kernel
- **Automatic focus cycling** via button press

### Output Redirection
- **`register_stdout(write_char_fn)`** - Redirect output
- **Character-level callback** for custom displays
- **Combined serial + app output** via `MultiPrint` class

### IPC Functions
- **`send_to_core1(target, type, data, size)`** - Normal priority send
- **`send_priority(target, type, data, size, priority)`** - Priority send
- **`receive_message(msg_out)`** - Receive highest-priority message
- **Priority range** 0-255 for granular control

### Task Management
- **`spawn_core1_task(name, entry, arg, priority)`** - Remote task creation
- **Returns task ID** for message targeting
- **Priority specification** (0-31)

### Resource Locking
- **`lock_resource(resource_id)`** - Acquire lock (blocking)
- **`unlock_resource(resource_id)`** - Release lock
- **16 resource IDs** available

### System Monitoring
- **`get_core0_usage()`** - Core0 CPU percentage
- **`get_core1_usage()`** - Core1 CPU percentage
- **`get_task_memory(task_id)`** - Task memory usage in bytes

### OOM Handling
- **`register_oom_handler(task_id, handler_fn)`** - Register cleanup callback
- **`oom_cleanup_done(task_id, bytes_freed)`** - Signal completion
- **`hint_memory_pressure(task_id)`** - Trigger compaction

### Application Registration
- **`Application_Register(name, spawn_func)`** - Register app with kernel
- **Global registration** during static initialization
- **Shell integration** for launching by name
- **`k_register_gui_app(socket_api)`** - Initialize UISocket for task

---

## Hardware Support

### RP2040 Peripherals
- **Dual ARM Cortex-M0+ cores** at up to 276 MHz
- **ADC support** for temperature sensing (channel 4)
- **SPI support** for SD card (configurable pins)
- **GPIO support** with fast read/write operations
- **Hardware watchdog** with automatic reset
- **Hardware mutex** primitives for locking
- **Multicore launch** support

### Pin Definitions (Customizable)
- **SD_CS** (5): SD card chip select
- **SD_MOSI** (19): SD card data out
- **SD_MISO** (16): SD card data in
- **SD_SCK** (18): SD card clock
- **BTN_ONOFF** (9): Focus cycle button (active low)

### Peripheral Libraries
- **Arduino Pico Core** for RP2040 support
- **Adafruit GFX** for display applications (optional)
- **Adafruit ILI9341** for TFT displays (optional)
- **Arduino SD** for filesystem support

---

## Performance and Optimization

### Code Optimization
- **O3 optimization** recommended (-O3 flag)
- **Inline critical functions** for speed
- **Cache-aligned structures** (64-byte TCB)
- **Packed structures** where appropriate
- **Bitmap operations** using hardware bit scan
- **Minimal function call overhead** in hot paths

### Overclocking Support
- **225 MHz** (recommended stable)
- **240 MHz** (moderate overclock)
- **250 MHz** (aggressive overclock)
- **276 MHz** (maximum overclock)
- **Stability warnings** for high frequencies

### Memory Efficiency
- **180KB heap** from 264KB total RAM
- **Efficient block management** minimizing overhead
- **Zero-copy IPC** where possible
- **Minimal kernel footprint** leaving space for apps

### Timing Precision
- **Microsecond resolution** for scheduling
- **1ms base scheduler tick** for responsiveness
- **Hardware timer usage** for accurate delays
- **Context switch timing** tracking for analysis

---

## Debugging and Diagnostics

### Real-Time Monitoring
- **`top` command** for live system overview
- **`ps` command** for task listing
- **`schedstat`** for scheduler analysis
- **`ipcstat`** for message queue status
- **`oomstat`** for memory killer metrics

### Detailed Analysis
- **`taskinfo <id>`** for deep task inspection
- **`memmap`** for memory block visualization
- **`dmesg`** for kernel event log
- **`vfsstat`/`stat`** for filesystem health

### Error Handling
- **Kernel panic** with diagnostic output
- **OOM killer** with victim selection transparency
- **File operation error logging**
- **IPC queue overflow tracking**

---

## System Statistics

### Global Counters
- **System uptime** in milliseconds
- **Total context switches** across all tasks
- **Total memory allocations/frees** lifetime
- **Total IPC messages sent/received** lifetime
- **OOM killer invocations** count
- **Watchdog triggers** across reboots

### Per-Core Statistics
- **Task count** per core
- **Context switches** per core
- **CPU usage** per core (0-100%)
- **Uptime** per core (Core1 tracks independently)

### Per-Task Statistics
- **CPU time** in milliseconds
- **Context switches** count
- **Memory usage** (current and peak)
- **Page faults** count
- **Voluntary yields** count
- **Preemptions** count
- **Uptime** since creation
- **Respawn count** (if applicable)

---

## Configuration and Customization

### Compile-Time Constants
- **MAX_TASKS** (32): Core0 task limit
- **MAX_CORE1_TASKS** (16): Core1 task limit
- **MAX_MEMORY_BLOCKS** (256): Memory block tracking limit
- **HEAP_SIZE** (180KB): Total heap size
- **MAX_IPC_MESSAGES** (32): IPC queue capacity
- **MAX_LOG_ENTRIES** (40): Kernel log ring buffer size
- **MAX_APPS** (16): Application registry capacity
- **MAX_GUI_APPS** (8): GUI focus cycling limit
- **MAX_RESOURCES** (16): Lockable resource count
- **MAX_OOM_HANDLERS** (16): OOM callback registration limit

### Scheduler Configuration
- **SCHED_NUM_PRIORITY_LEVELS** (32): Priority levels
- **SCHED_RT_THRESHOLD** (24): Real-time priority cutoff
- **SCHED_BASE_QUANTUM_US** (5ms): Minimum time quantum
- **SCHED_MAX_QUANTUM_US** (80ms): Maximum time quantum
- **SCHED_AGING_INTERVAL_MS** (500ms): Priority boost interval
- **SCHED_IDLE_INJECTION_THRESHOLD** (85%): CPU throttle point

### OOM Configuration
- **OOM_REQUEST_TIMEOUT_MS** (2s): Grace period for cleanup

### Watchdog Configuration
- **WATCHDOG_TIMEOUT_MS** (8s): Reset timeout

### VFS Configuration
- **VFS_BLOCK_SIZE** (256 bytes): Block size
- **VFS_MAX_FILES** (16): File limit
- **VFS_STORAGE_SIZE** (128KB): Total storage

### FS Configuration
- **FS_MAX_FILENAME** (32 chars): Filename length
- **FS_MAX_OPEN_FILES** (8): Simultaneous open files
- **FS_BUFFER_SIZE** (512 bytes): I/O buffer

---

## Application Development

### Task Creation Parameters
- **Name** (24 chars): Human-readable identifier
- **Entry function**: Task main loop
- **Argument**: Void pointer for user data
- **Priority** (0-31): Scheduling priority
- **Task type**: KERNEL/DRIVER/SERVICE/MODULE/APPLICATION
- **Flags**: Protection, respawn, oneshot, etc.
- **Max runtime**: Optional timeout in milliseconds
- **OOM priority** (0-4): Kill resistance level
- **Memory limit**: Soft cap in bytes
- **Callbacks**: Optional init/tick/deinit functions
- **Description**: Human-readable purpose string

### Module Callback System
- **Init callback** - Called once on task creation
  - Receives task ID as parameter
  - Use for initialization that requires task context
- **Tick callback** - Called periodically by schedulerWhether you're building a **game console**, **data logger**, **sensor platform**, **smart watch**, or **custom embedded system**, Picomimi provides a **stable foundation** you cam rely on to focus on your application logic rather than infrastructure.

**Build something awesome, share with the community. Have fun. (＾_＾)**
  - Stateless periodic execution
  - Ideal for services that poll hardware
  - Reduces boilerplate code
- **Deinit callback** - Called before task termination
  - Cleanup resources
  - Close file handles
  - Release hardware locks
  - Free allocated memory

### Application Structure Pattern
```c
// Typical application structure:
static uint32_t my_task_id;
static UISocket ui;

void my_oom_handler(uint32_t bytes_requested) {
    // Free caches, buffers, etc.
    uint32_t freed = 0;
    // ... cleanup code ...
    ui.oom_cleanup_done(my_task_id, freed);
}

void spawn_myapp() {
    k_register_gui_app(&ui);
    ui.request_focus(kernel.current_task);
    my_task_id = kernel.current_task;
    ui.register_oom_handler(my_task_id, my_oom_handler);
    
    // Main loop
    while(1) {
        // Application logic
        task_sleep(16); // ~60 FPS
    }
}

struct MyAppReg {
    MyAppReg() {
        Application_Register("myapp", spawn_myapp);
    }
} _myapp_reg;
```

### Best Practices
- **Use Core1 for rendering** - Offload graphics to second core
- **Use Core1 for heavy computation** - Keep Core0 responsive
- **Send high-priority IPC** for time-critical data (priority 200+)
- **Register OOM handlers** for graceful memory management
- **Lock resources** when accessing shared hardware
- **Use task_sleep()** to yield CPU voluntarily
- **Check free memory** periodically and hint pressure
- **Close files** before task termination
- **Free all memory** in OOM handlers and deinit callbacks

---

## System Reliability Features

### Automatic Recovery
- **Task respawn system** for critical services
- **5-second respawn cooldown** preventing rapid death loops
- **Respawn counter** tracking revival attempts
- **Graceful degradation** when services die
- **Service state tracking** (shell_alive, cpumon_alive, etc.)

### Error Detection
- **Invalid task ID detection** preventing kernel crashes
- **Null pointer checks** throughout critical paths
- **Bounds checking** on arrays and buffers
- **State validation** before operations
- **Timeout enforcement** for runaway tasks

### Fault Isolation
- **Task memory isolation** via ownership tracking
- **Per-task cleanup** on termination
- **Kernel protection** from application faults
- **Core isolation** preventing cross-core contamination
- **Resource tracking** for automatic cleanup

### Health Monitoring
- **Watchdog integration** for hang detection
- **CPU temperature tracking** for thermal issues
- **Memory pressure monitoring** for OOM prevention
- **Queue depth monitoring** for congestion
- **Context switch tracking** for scheduler health

---

## Power Management

### Sleep States
- **Task sleep** (task_sleep()) for voluntary idle
- **CPU idle injection** at high load (>85%)
- **Low-priority task throttling** via quantum adjustment
- **Idle task execution** as fallback
- **Microsecond-precision delays** for efficiency

### Thermal Management
- **Real-time temperature monitoring** via internal sensor
- **Temperature-aware idle injection** preventing overheating
- **Overclock stability recommendations** (225 MHz default)
- **2-second temperature update interval**

---

## Security Features

### Root Mode Protection
- **Two-tier command system** (user/root)
- **Dangerous commands gated** behind root mode
- **`kill` command requires root** preventing accidents
- **Root mode toggle** via `root` command
- **Visual indication** in shell prompt (~ vs #)

### Task Protection
- **TASK_FLAG_PROTECTED** preventing normal termination
- **TASK_FLAG_CRITICAL** triggering panic if killed
- **Kernel task protection** from OOM killer
- **Driver task protection** from accidental termination
- **Service task protection** with respawn capability

### Memory Protection
- **Per-task accounting** preventing leaks across tasks
- **Ownership tracking** ensuring cleanup
- **Limit enforcement** for application tasks
- **OOM killer** as last-resort protection

---

## Real-Time Capabilities

### Real-Time Priority Levels
- **Priorities 24-31** designated as real-time
- **Guaranteed scheduling** for RT tasks
- **Minimal quantum** (5ms) for RT tasks
- **No priority aging** for RT tasks
- **Preemption of lower priorities** instantly

### Deterministic Behavior
- **O(1) task selection** constant-time scheduling
- **Predictable context switches** no priority inversion
- **Hardware mutex** for atomic operations
- **Interrupt masking** for critical sections
- **Microsecond timing** for precise control

### Use Cases
- **Audio processing** (28-31 priority)
- **Display refresh** (28-31 priority)
- **Input drivers** (24-27 priority)
- **Time-critical sensors** (24-27 priority)
- **Communication protocols** (24-27 priority)

---

## Documentation and Help

### Built-In Help System
- **`help` command** comprehensive command listing
- **Command descriptions** explaining purpose
- **Parameter syntax** showing usage
- **Application listing** showing registered apps
- **Categorized commands** (System, VFS, FS, Task Management, etc.)

### Code Comments
- **Function headers** explaining purpose
- **Parameter documentation** inlineRokudenashi - Starry Silent Night【Official Music Video】

- **Algorithm explanations** for complex logic
- **Architecture notes** describing design decisions
- **Developer guide** at end of source file

### Developer Guide (Embedded)
- **Complete API reference** via comments
- **Example application** showing best practices
- **UISocket API documentation** with all functions
- **Dual-core patterns** for efficient design
- **Step-by-step walkthrough** of application creation

---

## Testing and Validation

### Boot Sequence Validation
- **Service startup logging** tracking initialization
- **Priority assignment verification** via ps command
- **Memory allocation testing** during boot
- **SD card detection** with error reporting
- **Core1 launch confirmation** with status output

### Runtime Verification
- **Live system monitoring** via top command
- **Scheduler verification** via schedstat
- **Memory leak detection** via mem command
- **IPC queue health** via ipcstat
- **Task health checks** via ps command

### Stress Testing Support
- **High memory pressure** handling via OOM killer
- **Queue overflow** handling with flow control
- **CPU saturation** handling with idle injection
- **Long runtime** testing with uptime tracking

---

## Limitations and Known Constraints

### Memory Constraints
- **180KB heap** on RP2040 (264KB total RAM)
- **256 memory blocks** maximum tracking
- **32 tasks on Core0** hard limit
- **16 tasks on Core1** hard limit
- **No dynamic block expansion** (fixed at compile time)

### IPC Constraints
- **32 message queue slots** fixed size
- **64-byte message payload** maximum
- **No guaranteed delivery** (messages can be dropped)
- **5-second message expiration** automatic cleanup
- **No message fragmentation** single-packet only

### File System Constraints
- **VFS limited to 16 files** and 128KB storage
- **8 simultaneous open files** on SD card
- **No directory support in VFS** flat namespace only
- **No file permissions** or access control
- **No journaling** or crash recovery

### Task Constraints
- **24-character task names** truncation possible
- **No task migration** between cores after creation
- **No task priority inheritance** for mutex holders
- **No gang scheduling** tasks run independently

### Scheduler Constraints
- **No deadline scheduling** only priority-based
- **No rate-monotonic** or earliest-deadline-first
- **No CPU affinity masks** (only single core pinning)
- **32 priority levels** maximum

---

## Future Enhancement Roadmap (Post-M2)

### Planned Features
- **Network stack** (WiFi via Pico W)
- **USB support** (device and host modes)
- **I2C/SPI drivers** as kernel modules
- **DMA support** for high-speed transfers
- **Hardware PWM** for motor control
- **Interrupt-driven GPIO** for faster response
- **Task migration** between cores
- **CPU affinity masks** for complex scheduling
- **Message fragmentation** for large IPC transfers
- **File permissions** and access control
- **VFS directories** and hierarchical namespace
- **Journaling filesystem** for reliability

### Performance Improvements
- **Zero-copy IPC** for large messages
- **Lock-free queues** where possible
- **SIMD optimizations** for graphics
- **Assembly optimizations** for hot paths
- **Profile-guided optimization** based on real usage

### Developer Experience
- **Application templates** for common patterns
- **Build system integration** (CMake/Arduino IDE)
- **Debug logging levels** per subsystem
- **GDB stub** for on-chip debugging
- **Profiling tools** for performance analysis
- **Memory leak detectors** built into kernel

---

## Hardware Compatibility

### Tested Boards
- **Raspberry Pi Pico** (RP2040)
- **Raspberry Pi Pico W** (RP2040 + WiFi)
- **Generic RP2040 boards** with standard pinouts
- **Adafruit Feather RP2040** compatible
- **Sparkfun RP2040 boards** compatible
- **Pimoroni RP2040 boards** compatible

### Peripheral Compatibility
- **SD cards** (FAT16/FAT32, up to 128GB tested)
- **ILI9341 displays** via SPI (320x240)
- **Generic buttons** via GPIO
- **ADC sensors** via analog pins
- **I2C devices** (partial support, user-space)
- **SPI devices** (partial support, user-space)

### Clock Speeds
- **Default 125 MHz** conservative stable
- **Recommended 225 MHz** for most boards
- **Up to 276 MHz** for performance builds
- **Tested stable at 250 MHz** on most hardware

---

## Size and Resource Usage

### Flash Usage
- **~150KB kernel code** (with O3 optimization)
- **~50KB libraries** (SD, SPI, etc.)
- **Remaining flash** available for applications
- **~2MB typical** flash on RP2040 boards

### RAM Usage
- **180KB heap** for dynamic allocation
- **~84KB kernel structures** (TCBs, buffers, etc.)
- **Stack space** per core (~8KB each)
- **~264KB total** SRAM on RP2040

### Performance Characteristics
- **~10,000 context switches/sec** at 225 MHz
- **~1μs context switch latency** typical
- **~100 IPC messages/sec** sustained throughput
- **~1ms worst-case scheduler tick** under load

---

## Acronyms and Terminology

### Common Abbreviations
- **TCB**: Task Control Block
- **IPC**: Inter-Process Communication
- **OOM**: Out Of Memory
- **VFS**: Virtual File System
- **FS**: (Physical) File System
- **RT**: Real-Time
- **SMP**: Symmetric Multiprocessing
- **FIFO**: First In, First Out
- **DMA**: Direct Memory Access
- **GPIO**: General Purpose Input/Output
- **SPI**: Serial Peripheral Interface
- **I2C**: Inter-Integrated Circuit
- **ADC**: Analog to Digital Converter
- **PWM**: Pulse Width Modulation
- **WDT**: Watchdog Timer

### Kernel-Specific Terms
- **Quantum**: Time slice allocated to a task
- **Aging**: Priority boost for starved tasks
- **Preemption**: Forced context switch
- **Yield**: Voluntary context switch
- **Affinity**: Core preference for task execution
- **Victim**: Task selected for OOM termination
- **Graceful kill**: OOM handler invocation
- **Brutal kill**: Forced immediate termination
- **Respawn**: Automatic task resurrection
- **Bitmap**: Priority level bit array for O(1) scheduling

---

## License and Credits

### MIT License
- **Open source** under MIT License
- **Free to use** for any purpose
- **Modification allowed** with attribution
- **No warranty** provided

### Attribution Request
- **Gentle credit** appreciated but not required
- **Share back** improvements welcome
- **Community contributions** encouraged
- **Issue reporting** helps improve stability

---

## Version History

### v10.0.0 (M2) - Current
- **O(1) Bitmap Scheduler** implemented
- **Priority-Aware IPC System** with flow control
- **Graceful OOM Killer** with callbacks
- **Hardware Watchdog Integration** for reliability
- **Kernel Panic Handler** for diagnostics
- **Unified `ps` command** showing both cores
- **New commands** (taskinfo, schedstat, oomstat)
- **Enhanced UISocket API** with OOM functions
- **Complete documentation** and developer guide

### v10.0.0 (M1) - Previous
- **Dual-core support** with Core1 offloading
- **Basic IPC system** without priorities
- **Simple OOM killer** without graceful requests
- **Round-robin scheduler** (O(n) complexity)
- **VFS and FS** filesystem support
- **Shell interface** with basic commands
- **Task management** with respawn

---

## Support and Community

### Getting Help
- **GitHub Issues** for bug reports
- **GitHub Discussions** for questions
- **Code comments** for inline documentation
- **Developer guide** embedded in source

### Contributing
- **Pull requests** welcome
- **Issue reports** appreciated
- **Documentation improvements** encouraged
- **Example applications** help others learn

### Contact
- **Repository**: GitHub (check README for link)
- **License**: MIT (see LICENSE file)
- **Author**: Credits in README

---

## Conclusion

Picomimi v10 M2 represents a **production-ready microkernel** for the RP2040, for you, offering:

- **High-grade scheduling** with O(1) complexity
- **Intelligent memory management** with graceful recovery
- **Robust dual-core support** with priority IPC
- **Comprehensive monitoring** and debugging tools
- **Flexible application framework** with UISocket API
- **Reliable operation** with watchdog and panic handling
- **Extensive documentation** for developers

Whether you're building a **game console**, **data logger**, **sensor platform**, **smart watch**, or **custom embedded system**, Picomimi provides a **stable foundation** you cam rely on to focus on your application logic rather than infrastructure.

**Build something awesome, share with the community. Have fun. (＾_＾)**

---

*End of Feature List*
