# Picomimi v8.7 - Complete Feature Documentation

## Overview
Picomimi v8.7 is a custom operating system kernel for the Raspberry Pi Pico. This version introduces critical fixes to the scheduler, memory management, and resource handling. It features a pre-emptive, priority-based task scheduler, a memory manager with OOM protection, dual storage systems (volatile RAM and persistent SD card), and a full command-line interface with a virtual keyboard.

### v8.7 Changelog
- **Scheduler Overhaul:** Replaced the round-robin scheduler with a pre-emptive, priority-based system. Higher priority tasks now receive more CPU time.
- **VFS Fragmentation Fix:** The VFS (RAM disk) now supports non-contiguous block allocation, preventing write failures on a fragmented memory space.
- **Resource Leak Fix:** Task termination (`brutal_task_kill`) now properly closes all open SD card file handles owned by the task, preventing resource leaks.

## Core Architecture

### Task System
The kernel supports five distinct task types, each with different privilege levels:

| Task Type    | Description |
|--------------|-------------|
| KERNEL       | Core system tasks (e.g., the idle loop). Highest privilege. |
| DRIVER       | Hardware interface tasks (display, input). |
| SERVICE      | System services (shell, filesystem monitors). |
| MODULE       | User-loadable extensions (counter, watchdog). |
| APPLICATION  | User programs (games, utilities). Lowest privilege. |

### Task States
- **READY:** Waiting to execute.  
- **RUNNING:** Currently executing.  
- **WAITING:** Sleeping for a specified time.  
- **SUSPENDED:** Manually paused by the user.  
- **TERMINATED:** Execution is complete.  

### Task Properties
- **Priority Levels:** 0-255. Higher value = higher priority.  
- **Memory Limits:** Per-task memory allocation caps.  
- **Respawn Flag:** Automatically restart a task after it terminates.  
- **Protected Flag:** Prevent a task from being killed without root mode.  
- **Critical Flag:** A system-critical task.  
- **OneShot Flag:** Prevent more than one instance of a task from running.  

## Memory Management

### Dynamic Memory Allocator
- **Heap Size:** 180 KB  
- Block-based allocation with automatic and manual compaction.  
- First-fit algorithm for memory allocation requests.  
- Fragmentation tracking and reporting via the `mem` command.  
- Memory statistics tracked per task (current and peak usage).  

### Out-of-Memory (OOM) Protection
The kernel implements priority-based memory protection when the heap is exhausted:

#### OOM Priority Levels
| Level | Description |
|-------|-------------|
| 0     | NEVER - Cannot be killed (kernel/drivers/services/modules) |
| 1     | CRITICAL - Last resort to be killed |
| 2     | HIGH - Less likely to be killed |
| 3     | NORMAL - Standard killability |
| 4     | LOW - First to be killed |

#### OOM Behavior
1. Attempt memory compaction to merge free blocks.  
2. If memory is still insufficient, select a victim task.  
3. Only APPLICATION tasks can be killed by the OOM killer.  
4. The task with the highest OOM priority number is selected first.  
5. If multiple tasks share the highest priority, the one using the most memory is killed.  

## Storage Systems

### VFS (Virtual Filesystem)
- **Capacity:** 128 KB  
- **Max Files:** 16  
- **File Size Limit:** 16 KB per file  
- **Block Size:** 256 bytes  
- Status: Inactive by default (`vfscreate` to activate)  

**Features:**
- Fragmented file support to maximize space utilization.  
- Extremely fast in-memory read/write operations.  
- Volatile storage (all data is lost on reboot).  
- Can be committed to the SD card using `vfsdedicate`.  

### FS (Filesystem - SD Card)
- **Card Support:** Up to 4 GB  
- **Max Open Files:** 8 concurrently  
- Status: Auto-initializes on boot if a card is detected  

**Features:**
- Persistent storage across reboots.  
- Directory and file support.  
- Automatic SD card detection on boot.  

## Hardware Support

### Display
- **Controller:** ILI9341 (320x240)  
- **Interface:** SPI  
- Primary output for shell and applications.  

### Input
- **Buttons:** Left, Right, Top, Bottom, Select, Start, A, B, On/Off  
- Terminal scrolling with Up/Down buttons.  
- Virtual keyboard toggle with On/Off button.  

### Temperature Sensor
- Internal ADC-based temperature monitoring.  
- Automatic conversion to Celsius.  
- Updated periodically by a dedicated system service.  

## System Services

### Scheduler
- Pre-emptive, priority-based multitasking  
- Round-robin for equal priority tasks  
- **Tick Rate:** 1000 μs (1ms)  
- Context switching and CPU time tracking  
- Task timeout handling  

### CPU Monitor
- Real-time CPU usage calculation  
- Per-task CPU time tracking  

### System Logger
- 40 log entries in circular buffer  
- 4 severity levels: INFO, WARN, ERR, CRIT  
- Timestamps on all entries  
- Persistent logging: high-severity logs written to `/LogRecord` on SD card  

### Watchdog
- Memory monitoring with high-usage alerts  
- Temperature monitoring with overheat warnings  
- Fragmentation detection and alerts  

## Complete Command Reference

### System Information
| Command | Description |
|---------|-------------|
| help    | Display all available commands |
| arch    | Show task architecture explanation |
| oom     | Explain the OOM killer's behavior |
| ps      | List all tasks with details |
| listapps| List only running applications |
| listmods| List only loaded modules |
| top     | Display live system resources |
| uptime  | Show system uptime (D/H/M/S) |
| temp    | Display current CPU temperature |
| dmesg   | Show the system log buffer (last 30 entries) |

### Memory Management
| Command | Description |
|---------|-------------|
| mem     | Display memory statistics |
| memmap  | Show a detailed map of the first 20 memory blocks |
| compact | Force an immediate memory compaction cycle |

### Task Management
| Command       | Description |
|---------------|-------------|
| kill <id>     | Terminate an application task by ID |
| suspend <id>  | Pause an application task |
| resume <id>   | Resume a suspended application task |
| root          | Toggle root mode |
| root kill <id>| Force-kill any task (including system tasks) |

### VFS Commands (RAM Storage)
| Command           | Description |
|------------------|-------------|
| vfscreate         | Allocate memory for and mount the VFS |
| vfsls             | List all files in the VFS |
| vfsstat           | Show VFS usage statistics |
| vfsmkfile <name> <type> | Create a new file in the VFS |
| vfsrm <id>        | Delete a VFS file by ID |
| vfscat <id>       | Display contents of a VFS file |
| vfsdedicate       | Save all files from the VFS to the SD card |

### FS Commands (SD Card Storage)
| Command       | Description |
|---------------|-------------|
| ls [path]     | List files and directories on the SD card |
| stat          | Show SD card filesystem statistics |
| mkdir <path>  | Create a new directory |
| rm <path>     | Delete a file or directory |
| cat <path>    | Display contents of a file |
| write <path> <text> | Write text to a file |
| logcat        | View persistent error log from the SD card |

### System Control
| Command    | Description |
|------------|-------------|
| clear      | Clear the display screen |
| reboot     | Restart the system via hardware watchdog |
| shutdown   | Perform a safe shutdown |

### Built-in Applications
| Application      | Command | Description | OOM Priority |
|-----------------|---------|-------------|--------------|
| Snake            | snake   | Classic snake game | NORMAL |
| Calculator       | calc    | Basic calculator | NORMAL |
| Digital Clock    | clock   | Displays elapsed time | NORMAL |
| System Monitor   | sysmon  | Real-time system stats | HIGH |
| Memory Stress    | memhog  | Allocates memory blocks to test OOM | LOW |
| CPU Stress       | cpuburn | Calculates prime numbers to stress CPU | NORMAL |
| Full System Stress| stress | Combined memory and CPU stress test | LOW |

## Special Features

### Resource Safe Task Killing
- Full resource cleanup on task kill  
- Closes any SD card files owned by the task  

### Safe Shutdown
- Checks VFS for unsaved files and prompts user  
- Closes all open SD card file handles  
- Stops all services gracefully before halting  

### Root Mode
- Privileged mode for system maintenance (`root`)  
- Allows termination of protected tasks  
- WARNING: Killing critical tasks (like IDLE, ID 0) will crash the system  

### Task Respawn
- Tasks with `RESPAWN` flag automatically restart 5 seconds after terminating  

## System Limits
| Resource             | Limit |
|---------------------|-------|
| Maximum Tasks        | 32    |
| Maximum Memory Blocks| 256   |
| Heap Size            | 180 KB|
| VFS Files            | 16    |
| VFS Storage          | 128 KB|
| FS Open Files        | 8     |

## Performance Characteristics
| Service              | Update / Polling Rate |
|---------------------|----------------------|
| Scheduler Tick       | 1 ms (1000 Hz)       |
| Display Update       | ~33 ms (~30 Hz)      |
| Input Polling        | 20 ms (50 Hz)        |
| CPU Monitor Update   | 1000 ms (1 Hz)       |
| Temperature Update   | 2000 ms (0.5 Hz)     |
| FS Maintenance Check | 60 seconds           |
| VFS Maintenance Check| 30 seconds           |

## Error Handling

### Memory Allocation Failure
1. Attempt 1: Standard first-fit allocation  
2. On Fail → Compact: Trigger memory compaction  
3. Attempt 2: Retry allocation  
4. On Fail → OOM Kill: Trigger OOM killer  
5. Attempt 3: Retry allocation  
6. On Fail → Return NULL  

### Task Failures
- **Timeout:** Tasks exceeding `max_runtime` are killed  
- **Respawn:** Critical services automatically restart  
- **Logging:** All critical failures and task terminations are logged
