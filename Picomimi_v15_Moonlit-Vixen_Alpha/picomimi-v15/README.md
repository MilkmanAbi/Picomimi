# PICOMIMI-AXISOS v15.0.0-Alpha "Moonlit Vixen"

A resource-owning embedded operating system for RP2040/RP2350 microcontrollers.

## Features

### Core Kernel
- Preemptive multitasking with priority-based scheduling
- Task management (create, suspend, resume, terminate)
- Memory management with per-task tracking
- IPC: Message passing, mutexes, semaphores, event flags

### Interactive Shell
Commands available:
- `help` - Show available commands
- `ps` - List all tasks
- `mem` - Show memory status
- `uptime` - Show system uptime
- `temp` - Show CPU temperature
- `kill <id>` - Terminate a task
- `reboot` - Reboot the system
- `clear` - Clear screen
- `gov` - CPU governor control
- `gov <profile>` - Set profile (low/save/bal/perf/turbo)
- `gov auto` - Enable automatic scaling
- `gov manual` - Lock current frequency
- `res` - Show resource ownership
- `gpio` - Show GPIO status

### CPU Governor
5-level frequency scaling with thermal management:
- **ULTRA_LOW** - 48 MHz (deep power saving)
- **POWERSAVE** - 96 MHz (light tasks)
- **BALANCED** - 133 MHz (default)
- **PERFORMANCE** - 200 MHz (demanding tasks)
- **TURBO** - 260/310 MHz (maximum performance)

Automatic thermal throttling when temperature exceeds limit.

### HAL Layer
- GPIO with ownership tracking
- SPI (SPI0, SPI1)
- I2C (I2C0, I2C1)
- ADC with temperature sensor
- PWM

### SD Card Support
- Pure Pico-SDK SPI driver (no Arduino dependencies)
- FatFS ready (download ff.c from elm-chan.org)

## Building

```bash
# Set Pico SDK path
export PICO_SDK_PATH=/path/to/pico-sdk

# Build for RP2040 (default)
mkdir build && cd build
cmake ..
make -j4

# Build for RP2350
cmake -DPICOMIMI_TARGET_RP2350=ON ..
make -j4
```

## Configuration

Edit `include/config/picomimi_config.h` or pass CMake options:
- `-DPICOMIMI_TARGET_RP2350=ON` - Build for RP2350
- `-DPICOMIMI_USB_STDIO=OFF` - Use UART instead of USB
- `-DPICOMIMI_SD_CS_PIN=17` - Change SD card CS pin

## Usage

Flash `picomimi.uf2` to your Pico, connect via serial (115200 baud), and interact with the shell.

```
picomimi:/~> help
picomimi:/~> ps
picomimi:/~> gov turbo
picomimi:/~> temp
```

## Creating Apps

```c
#include "api/picomimi.h"

void my_app(void* arg) {
    int led = Pico.ClaimGPIO(25);
    if (led >= 0) {
        gpio_init(led);
        gpio_set_dir(led, GPIO_OUT);
        while (1) {
            gpio_put(led, 1);
            Pico.Sleep(500);
            gpio_put(led, 0);
            Pico.Sleep(500);
        }
    }
}

// Register before main()
Picomimi_RegisterApp("blinky", my_app);
```

## Architecture

```
picomimi-v15/
├── include/
│   ├── api/            # Public API headers
│   ├── config/         # Configuration
│   ├── hal/            # Hardware abstraction
│   ├── kernel/         # Kernel internals
│   ├── ipc/            # Inter-process communication
│   ├── power/          # CPU governor
│   └── shell/          # Shell interface
├── src/
│   ├── kernel/         # Kernel implementation
│   ├── hal/            # HAL drivers
│   ├── ipc/            # IPC implementation
│   ├── power/          # Governor implementation
│   └── shell/          # Shell commands
├── lib/
│   ├── sd/             # SD card driver
│   └── fatfs/          # FatFS headers
└── examples/           # Example apps
```

## Status

This is an Alpha release. Working:
- [x] Kernel boot and task scheduling
- [x] Interactive shell
- [x] CPU governor
- [x] Memory management (bump allocator)
- [x] Basic HAL

Coming soon:
- [ ] Resource manager enforcement
- [ ] PMFS filesystem
- [ ] Core 1 task support
- [ ] OOM killer
- [ ] Display/Input driver interfaces

## License

MIT License - See original Picomimi project.

## Credits

Developed by Abinaash, ported from Arduino IDE to pure Pico-SDK.
