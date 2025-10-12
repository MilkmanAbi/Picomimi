# Picomimi

This document lists the core features, drivers, modules, and built-in software.

---

## Core Kernel & Scheduler
- **Cooperative (non-preemptive) multitasking scheduler**
- Supports a **maximum of 32 tasks**
- **Task Control Block (TCB)** supports multiple states: `READY`, `RUNNING`, `WAITING`, `SUSPENDED`, `TERMINATED`
- **Time-based task sleeping** via `task_sleep`
- **Task creation** with configurable **priority, type, and flags**
- **Task flags**: `PROTECTED`, `CRITICAL`, `RESPAWN`, `ONESHOT`
- **Automatic respawning** of critical tasks upon termination
- **Module support** with lifecycle callbacks: `init`, `tick`, `deinit`

## Memory Management
- Custom **dynamic memory manager** with a static 180 KB heap
- **Block-based allocation** (`kmalloc`/`kfree`) supporting up to **256 blocks**
- **Per-task memory tracking**: current usage and peak usage
- Optional **memory limits** for application-type tasks
- **On-demand memory compaction** to reduce fragmentation
- **Out-of-Memory (OOM) killer** selectively terminates tasks based on type and priority
- Reports **memory fragmentation percentage**

## Task Architecture
- **Five distinct task types** defining system hierarchy:
  1. `KERNEL`
  2. `DRIVER`
  3. `SERVICE`
  4. `MODULE`
  5. `APPLICATION`
- **Built-in protection**: only APPLICATION tasks are killable by the OOM killer
- **Root mode** for administrators to override protections and terminate any non-kernel task

## Virtual Filesystem (VFS)
- **In-memory, non-persistent filesystem** (data lost on reboot)
- **128 KB total storage** with 256-byte blocks
- Supports up to **16 files**, each up to 16 KB
- **File operations**: `create`, `read`, `write`, `delete`
- **File metadata tracking**: name, type, size, timestamps, owner task ID
- **Bitmap-based block allocation** tracking

## Shell & Commands
- **Serial-based command-line interface (shell)**

**System Information:**  
`ps`, `mem`, `memmap`, `dmesg`, `uptime`, `temp`, `top`, `arch`

**Task Management:**  
`kill`, `root kill`, `suspend`, `resume`

**Memory Management:**  
`compact`

**Filesystem Operations:**  
`ls`, `vfsstat`, `mkfile`, `rmfile`, `cat`

**System Control:**  
`reboot`, `clear`, `root`

## Drivers & Monitoring
- **ILI9341 SPI display driver** with basic graphical status bar
- **GPIO driver** for up to 9 hardware buttons
- **On-chip temperature sensor driver**
- **System services** for monitoring CPU usage and temperature
- **System logging (`klog`)** with a 40-entry in-memory circular buffer

## Built-in Software
**Applications:**  
- Snake game  
- Digital clock  
- Graphical system monitor  
- Memory stress test  
- CPU burn-in test

**Modules:**  
- Simple counter  
- System health watchdog

So much shit is broken that it's not even funny, I barely know what I'm doing tbh or how to properly track changes and stuff.
