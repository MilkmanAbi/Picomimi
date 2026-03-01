# Picomimi v18.0.0 Internal Release, ref code samples

**Microkernel for RP2040/RP2350 ARM Cortex-M**

## Features

### Scheduler Hypervisor
Hierarchical scheduler with pluggable policies:
- **Cooperative** - Fibers/coroutines with voluntary yielding
- **Realtime (EDF)** - Earliest Deadline First scheduling
- **Fair (CFS-like)** - Completely Fair Scheduler with vruntime
- **Batch** - Throughput-optimized for background tasks
- **Idle** - Always preemptible background class

### Task Management
- 32 concurrent tasks
- 8 priority levels
- Core affinity (dual-core support)
- Sleep, suspend, resume
- Task statistics

### Memory Management
- Simple heap allocator with coalescing
- Fixed-size memory pools
- DMA-safe buffer allocation
- Per-task stack allocation

### Synchronization
- Spinlocks (for dual-core)
- Mutexes (recursive, with timeout)
- Counting semaphores
- Message queues
- Event flags

### Cooperative Fibers
- Lightweight stackful coroutines
- 256-byte stacks
- Cooperative yielding
- Up to 16 concurrent fibers

### Dual-Core Support
- Core1 launch
- Inter-core FIFO
- Per-core scheduling
- Core affinity

### Built-in Shell
Commands:
- `help` - Show help
- `ps` - List tasks
- `mem` - Memory stats
- `sched` - Scheduler info
- `domains` - List scheduler domains
- `top` - Live monitor
- `gpio <pin> [val]` - GPIO control
- `led [on|off]` - LED control
- `temp` - Temperature sensor
- `uptime` - System uptime
- `reboot` - Soft reboot

### Hardware Drivers
- UART (115200 baud)
- GPIO (all 30 pins)
- SysTick timer (1000 Hz)
- ADC (temperature sensor)
- Watchdog

## Project Structure

```
picomimi-arm/
├── include/
│   └── picomimi.h      # Main header
├── core/
│   ├── kernel.c        # Entry point, init
│   ├── task.c          # Task management
│   ├── fiber.c         # Cooperative fibers
│   └── sync.c          # Mutex, semaphore, queue
├── sched/
│   └── sched_hyper.c   # Scheduler hypervisor
├── mm/
│   └── memory.c        # Heap & pools
├── shell/
│   └── shell.c         # Interactive shell
├── drivers/
│   └── drivers.c       # UART, GPIO, Timer
├── picomimi.ld         # Linker script
├── Makefile
└── README.md
```

## Building

```bash
# Install ARM toolchain
# Ubuntu/Debian:
sudo apt install gcc-arm-none-eabi

# Build
make

# Flash to RP2040
# Copy build/picomimi.uf2 to RPI-RP2 drive
# Or use picotool:
picotool load build/picomimi.uf2
```

## Usage

```c
#include "picomimi.h"

void my_task(void *arg) {
    while (1) {
        led_toggle();
        task_sleep(500);
    }
}

int main(void) {
    picomimi_init();
    
    // Create task in fair domain
    task_t *t = task_create("blinker", my_task, NULL, 10);
    sched_domain_t *d = sched_domain_create("app", SCHED_CLASS_FAIR, 5);
    sched_domain_add_task(d, t);
    
    picomimi_start();
    return 0;
}
```

## Scheduler Domains

```
┌─────────────────────────────────────────┐
│           Scheduler Hypervisor          │
├─────────────────────────────────────────┤
│  Domain: realtime (EDF)     Priority: 0 │
│  Domain: interactive (Fair) Priority: 5 │
│  Domain: normal (Fair)      Priority: 10│
│  Domain: batch (Batch)      Priority: 20│
│  Domain: idle (Idle)        Priority:255│
└─────────────────────────────────────────┘
```

## Memory Map (RP2040)

```
0x10000000  ┌──────────────┐
            │    Flash     │ 2MB
            │  (Code/RO)   │
0x10200000  └──────────────┘

0x20000000  ┌──────────────┐
            │     BSS      │
            ├──────────────┤
            │    Heap      │ 128KB
            ├──────────────┤
            │  Stack (C0)  │ 4KB
            ├──────────────┤
            │  Stack (C1)  │ 4KB
0x20042000  └──────────────┘
            Total SRAM: 264KB
```

## License

MIT License

## Author

Inspired by Picomimi for embedded systems.
