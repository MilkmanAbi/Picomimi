# Picomimi v8 - Complete Feature Documentation

## Overview
A custom operating system kernel for the Raspberry Pi Pico microcontroller, featuring task scheduling, memory management, dual storage systems, and a command-line interface.

---

## Core Architecture

### Task System
The kernel supports five distinct task types, each with different privilege levels:

- **KERNEL** - Core system tasks (idle loop)
- **DRIVER** - Hardware interface tasks (display, input)
- **SERVICE** - System services (shell, filesystem)
- **MODULE** - User extensions (counter, watchdog)
- **APPLICATION** - User programs (games, utilities)

### Task States
- `READY` - Waiting to execute
- `RUNNING` - Currently executing
- `WAITING` - Sleeping for specified time
- `SUSPENDED` - Manually paused
- `TERMINATED` - Execution complete
- `ZOMBIE` - Terminated but not cleaned up

### Task Properties
- **Priority Levels** - 0-255 (0 = highest)
- **Memory Limits** - Per-task memory allocation caps
- **Respawn Flag** - Automatic restart after termination
- **Protected Flag** - Cannot be killed without root mode
- **Critical Flag** - System-critical task
- **OneShot Flag** - Prevents duplicate instances

---

## Memory Management

### Dynamic Memory Allocator
- **Heap Size**: 180 KB
- **Block-based allocation** with automatic compaction
- **First-fit algorithm** for memory allocation
- **Fragmentation tracking** and reporting
- **Memory statistics** per task

### Out-of-Memory (OOM) Protection
The kernel implements priority-based memory protection:

**OOM Priority Levels:**
- `0 - NEVER` - Cannot be killed (kernel/drivers/services/modules)
- `1 - CRITICAL` - Last resort only
- `2 - HIGH` - Protected unless critical
- `3 - NORMAL` - Standard killability
- `4 - LOW` - First to be killed

**OOM Behavior:**
1. Attempt memory compaction
2. If still out of memory, select victim
3. Only APPLICATION tasks can be killed
4. Higher OOM priority = more likely to be killed
5. Among same priority, largest memory user is killed

---

## Storage Systems

### VFS (Virtual Filesystem)
RAM-based temporary storage for fast access:

- **Capacity**: 128 KB
- **Max Files**: 16
- **File Size Limit**: 16 KB per file
- **Block Size**: 256 bytes
- **Status**: Inactive by default (use `vfscreate` to activate)

**Features:**
- In-memory file operations
- Fast read/write
- Volatile (lost on reboot)
- Can be committed to SD card

### FS (Filesystem - SD Card)
Persistent storage on microSD card:

- **Card Support**: Up to 4 GB
- **Speed**: 400 kHz init, 4 MHz operation
- **Max Open Files**: 8 concurrent
- **Buffer Size**: 512 bytes
- **Status**: Auto-initializes on boot if card detected

**Features:**
- Persistent storage
- Directory support
- Text file operations
- Automatic detection

---

## Hardware Support

### Display
- **Controller**: ILI9341 (320x240)
- **Interface**: SPI
- **Features**: Real-time status display
  - Task count
  - Uptime
  - Memory usage
  - CPU usage
  - Temperature

### Input
- **9 Buttons**: Left, Right, Top, Bottom, Select, Start, A, B, On/Off
- **Debounced input** with state tracking

### Temperature Sensor
- **Internal ADC-based** temperature monitoring
- **Automatic conversion** to Celsius
- **Periodic updates** every 2 seconds

---

## System Services

### Scheduler
- **Cooperative multitasking** with round-robin scheduling
- **1000 μs tick rate** for precise timing
- **Context switching** with CPU time tracking
- **Task timeout enforcement** for max runtime limits

### CPU Monitor
- **Real-time CPU usage** calculation
- **Per-task CPU time** tracking
- **Idle time** measurement

### System Logger
- **40 log entries** circular buffer
- **4 severity levels**: INFO, WARN, ERR, CRIT
- **Timestamp** on all entries
- **Persistent logging** (optional to VFS/FS)

### Watchdog
- **Memory monitoring** with high-usage alerts
- **Temperature monitoring** with overheat warnings
- **Fragmentation detection** and alerts

---

## Complete Command Reference

### System Information
| Command | Description |
|---------|-------------|
| `help` | Display all available commands |
| `arch` | Show task architecture explanation |
| `oom` | Explain OOM killer behavior |
| `ps` | List all tasks with details (ID, name, type, state, priority, memory) |
| `listapps` | List only running applications |
| `listmods` | List only loaded modules |
| `top` | Display live system resources (CPU, memory, temp, uptime) |
| `uptime` | Show system uptime in days/hours/minutes/seconds |
| `temp` | Display CPU temperature |
| `dmesg` | Show system log (last 30 entries) |

### Memory Management
| Command | Description |
|---------|-------------|
| `mem` | Display memory statistics (total, used, free, fragmentation, OOM kills) |
| `memmap` | Show detailed memory map (first 20 blocks) |
| `compact` | Force immediate memory compaction |

### Task Management
| Command | Description |
|---------|-------------|
| `kill <id>` | Terminate application task by ID |
| `suspend <id>` | Pause application task |
| `resume <id>` | Resume suspended application task |
| `root` | Toggle root mode (enables killing protected tasks) |
| `root kill <id>` | Force kill any task (including system tasks) |

### VFS Commands (RAM Storage)
| Command | Description |
|---------|-------------|
| `vfscreate` | Allocate and mount VFS in RAM |
| `vfsls` | List all VFS files |
| `vfsstat` | Show VFS statistics (blocks, files, reads, writes) |
| `vfsmkfile <name> <type>` | Create VFS file (type: 1=TEXT, 2=LOG, 3=DATA, 4=CONFIG) |
| `vfsrm <id>` | Delete VFS file by ID |
| `vfscat <id>` | Display VFS file contents |
| `vfsdedicate` | Save all VFS files to SD card (prefix: vfs_) |

### FS Commands (SD Card Storage)
| Command | Description |
|---------|-------------|
| `ls [path]` | List SD card files and directories |
| `stat` | Show filesystem statistics (total, used, free space) |
| `mkdir <path>` | Create directory on SD card |
| `rm <path>` | Delete file or directory from SD card |
| `cat <path>` | Display file contents from SD card |
| `write <path> <text>` | Write text to file on SD card |

### System Control
| Command | Description |
|---------|-------------|
| `clear` | Clear screen (both serial terminal and display) |
| `reboot` | Restart the system using watchdog |
| `shutdown` | Safe shutdown with VFS commit prompt |

---

## Built-in Applications

### Games
**Snake**
- Command: `snake`
- Controls: Use hardware buttons
- OOM Priority: NORMAL
- Memory Limit: 4 KB

**Calculator**
- Command: `calc`
- Basic calculator application
- OOM Priority: NORMAL
- Memory Limit: 4 KB

### Utilities
**Digital Clock**
- Command: `clock`
- Displays HH:MM:SS on screen
- Tracks elapsed time since start
- OOM Priority: NORMAL
- Memory Limit: 2 KB

**System Monitor**
- Command: `sysmon`
- Real-time display of system stats
- CPU, memory, fragmentation, tasks, temperature, uptime, OOM kills, VFS/SD status
- Updates every second
- OOM Priority: HIGH (protected from OOM)
- Memory Limit: 2 KB

### Stress Tests
**Memory Stress Test**
- Command: `memhog`
- Allocates 50 blocks of random size (2-10 KB)
- Tests memory allocation and OOM killer
- 30 second timeout
- OOM Priority: LOW (easily killed)
- Memory Limit: 50 KB

**CPU Stress Test**
- Command: `cpuburn`
- Calculates prime numbers in 100 iterations
- Tests CPU usage tracking
- 30 second timeout
- OOM Priority: NORMAL
- Memory Limit: 4 KB

**Full System Stress**
- Command: `stress`
- Combined memory and CPU stress
- 20 cycles of allocation + computation
- Random deallocation to test fragmentation
- 60 second timeout
- OOM Priority: LOW
- Memory Limit: 100 KB

---

## Special Features

### Safe Shutdown
When issuing the `shutdown` command:
1. Checks if VFS has unsaved files
2. Prompts user to commit to SD (y/n with 10-second timeout)
3. Closes all open file handles
4. Verifies filesystem integrity
5. Flushes system logs
6. Stops all services gracefully
7. Enters low-power halt state

### Root Mode
Protective mechanism for system tasks:
- Toggle with `root` command
- Enables killing of KERNEL, DRIVER, SERVICE, and MODULE tasks
- **WARNING**: Killing critical tasks can crash the system
- Auto-disables after force kill
- Killing the idle task (ID 0) will completely halt the kernel

### Task Respawn
Tasks with the RESPAWN flag automatically restart:
- 5-second cooldown between respawns
- Respawn counter tracks number of restarts
- Full memory cleanup before restart
- Preserves task configuration
- Re-runs initialization callbacks

### Memory Compaction
Automatic and manual defragmentation:
- Triggered automatically when allocation fails
- Merges adjacent free blocks
- Sorts blocks by address
- Reports number of merges performed
- Reduces fragmentation percentage

---

## System Limits

| Resource | Limit |
|----------|-------|
| Maximum Tasks | 32 |
| Maximum Memory Blocks | 256 |
| Heap Size | 180 KB |
| Task Name Length | 24 characters |
| Log Entries | 40 |
| VFS Files | 16 |
| VFS Storage | 128 KB |
| VFS File Size | 16 KB |
| VFS Filename Length | 16 characters |
| FS Open Files | 8 concurrent |
| FS Filename Length | 32 characters |
| FS Max Card Size | 4 GB |

---

## Boot Sequence

1. Serial initialization (115200 baud)
2. GPIO initialization (9 buttons)
3. Temperature sensor initialization
4. Memory manager initialization (180 KB heap)
5. Task scheduler initialization
6. VFS initialization (inactive state)
7. SD card detection and initialization
   - 400 kHz slow init
   - 4 MHz operational speed
   - Size detection
8. Core task creation:
   - Idle (KERNEL)
   - Display driver (DRIVER)
   - Input driver (DRIVER)
   - Shell service (SERVICE)
   - CPU monitor (SERVICE)
   - Temperature monitor (SERVICE)
   - Filesystem service (SERVICE, if SD present)
9. User module loading:
   - Counter module
   - Watchdog module
10. Display welcome banner
11. Enter main scheduler loop

---

## Pin Configuration

### Display (ILI9341)
- CS: GPIO 21
- RESET: GPIO 20
- DC: GPIO 17
- MOSI: GPIO 19
- SCK: GPIO 18
- LED: GPIO 22

### SD Card
- CS: GPIO 5
- MOSI: GPIO 19 (shared with display)
- MISO: GPIO 16
- SCK: GPIO 18 (shared with display)

### Buttons
- Left: GPIO 3
- Right: GPIO 1
- Top: GPIO 0
- Bottom: GPIO 2
- Select: GPIO 4
- Start: GPIO 6
- A: GPIO 7
- B: GPIO 8
- On/Off: GPIO 9

---

## File Type Codes

| Code | Type | Description |
|------|------|-------------|
| 1 | TEXT | Plain text files |
| 2 | LOG | System log files |
| 3 | DATA | Binary data files |
| 4 | CONFIG | Configuration files |

---

## Performance Characteristics

- **Scheduler Tick**: 1000 μs (1 ms)
- **Display Update**: 500 ms
- **CPU Monitor Update**: 1000 ms
- **Temperature Update**: 2000 ms
- **FS Maintenance**: 60000 ms (1 minute)
- **VFS Maintenance**: 30000 ms (30 seconds)
- **Input Polling**: 50 ms

---

## Error Handling

### Memory Allocation Failure
1. First attempt: standard allocation
2. If failed: trigger memory compaction
3. Second attempt: allocation after compaction
4. If failed: trigger OOM killer
5. Third attempt: allocation after OOM kill
6. If still failed: return NULL

### Task Failures
- Timeout: Automatic termination if max runtime exceeded
- Respawn: Automatic restart for critical services
- Logging: All terminations logged with reason

### Filesystem Errors
- SD card not present: FS marked as unavailable
- File not found: Error message to serial
- Write failure: Reports failure and closes file
- Out of file handles: Reports "no free file handles"

---
