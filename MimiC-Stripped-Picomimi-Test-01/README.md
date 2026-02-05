# MimiC - Self-Hosted C Compiler & Runtime for RP2040/RP2350

```
╔═══════════════════════════════════════════════════════════════════════════╗
║   ███╗   ███╗██╗███╗   ███╗██╗ ██████╗                                   ║
║   ████╗ ████║██║████╗ ████║██║██╔════╝                                   ║
║   ██╔████╔██║██║██╔████╔██║██║██║                                        ║
║   ██║╚██╔╝██║██║██║╚██╔╝██║██║██║                                        ║
║   ██║ ╚═╝ ██║██║██║ ╚═╝ ██║██║╚██████╗                                   ║
║   ╚═╝     ╚═╝╚═╝╚═╝     ╚═╝╚═╝ ╚═════╝                                   ║
║                                                                           ║
║   Self-Hosted C Compiler for Microcontrollers                            ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

## Overview

MimiC is a **self-hosted C compiler** that runs directly on RP2040/RP2350 microcontrollers. Write C code, compile it on the device, and run it - no external toolchain required.

**Key Innovation:** Disk-buffered multi-pass compilation trades speed for memory, enabling full C98 support within the severe RAM constraints of microcontrollers.

## Architecture

### The Problem
- RP2040: 264KB SRAM
- RP2350: 520KB SRAM
- Traditional C compilers need megabytes of RAM

### The Solution
Use SD card as working memory. Each compilation pass:
1. Reads from disk
2. Processes with minimal RAM footprint
3. Writes results back to disk

```
┌─────────────────────────────────────────────────────────────┐
│                    COMPILATION PIPELINE                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  source.c  ──▶  Pass 1: LEXER     ──▶  source.tok  (2-4KB)  │
│                                                              │
│  source.tok ──▶  Pass 2: PARSER    ──▶  source.ast (8-16KB) │
│                                                              │
│  source.ast ──▶  Pass 3: SEMANTIC  ──▶  source.ir  (16-32KB)│
│                                                              │
│  source.ir  ──▶  Pass 4: CODEGEN   ──▶  source.o   (8-16KB) │
│                                                              │
│  source.o   ──▶  Pass 5: LINKER    ──▶  source.mimi         │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Key Principle: Dynamic Loading

Unlike ELF binaries with hardcoded addresses, `.mimi` binaries are **position-independent**:

- Compiler generates relocatable code
- Kernel dynamically allocates memory at load time
- Kernel patches all address references
- Programs can be loaded at any available memory location

## Components

### 1. MimiC Kernel (`src/kernel/`)
Stripped-down kernel based on Picomimi v14.3.1:
- **Memory Management**: Buddy allocator with separate kernel/user heaps
- **Task Management**: Cooperative multitasking with priority scheduling
- **Binary Loader**: Loads `.mimi` files, performs relocation
- **Syscall Dispatcher**: 50+ syscalls for hardware access

### 2. Custom FAT32 (`src/fs/`)
Minimal FAT32 implementation - no Arduino SD library bloat:
- Raw SPI communication
- Sector caching with lazy write-back
- Streaming I/O for compiler passes
- ~4KB RAM footprint

### 3. Compiler (`src/compiler/`)
Disk-buffered C compiler:
- **Lexer**: Tokenizes C source, outputs `.tok` files
- **Parser**: Builds AST from tokens, outputs `.ast` files  
- **Code Generator**: Generates ARM Thumb code
- **Linker**: Combines objects into `.mimi` binaries

### 4. SDK (`sdk/include/`)
pico-sdk compatible headers:
```c
// User code looks familiar:
#include <pico/stdlib.h>
#include <hardware/gpio.h>

int main() {
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    
    while (1) {
        gpio_put(25, 1);
        sleep_ms(500);
        gpio_put(25, 0);
        sleep_ms(500);
    }
}
```

Under the hood, `gpio_init()` → `mimi_gpio_init()` → syscall to kernel.

## .mimi Binary Format

64-byte header + sections + relocations + symbols:

```
┌────────────────────────────────────┐
│  HEADER (64 bytes)                 │
│  ├─ magic: "MIMI"                  │
│  ├─ version, arch, flags           │
│  ├─ entry_offset                   │
│  ├─ section sizes                  │
│  ├─ reloc_count, symbol_count      │
│  └─ stack/heap requests            │
├────────────────────────────────────┤
│  .text (executable code)           │
├────────────────────────────────────┤
│  .rodata (constants, strings)      │
├────────────────────────────────────┤
│  .data (initialized globals)       │
├────────────────────────────────────┤
│  RELOCATION TABLE                  │
│  (kernel patches at load time)     │
├────────────────────────────────────┤
│  SYMBOL TABLE (optional)           │
└────────────────────────────────────┘
```

## Building

### Prerequisites
- Pico SDK installed
- ARM GCC toolchain
- CMake 3.13+

### Build
```bash
export PICO_SDK_PATH=/path/to/pico-sdk

mkdir build && cd build
cmake .. -DPICO_BOARD=pico    # or pico2 for RP2350
make -j4
```

### Flash
```bash
# Copy UF2 file
cp mimic.uf2 /media/RPI-RP2/

# Or use picotool
picotool load -f mimic.uf2
```

## Usage

Connect via serial terminal (115200 baud):

```
╔═══════════════════════════════════════════════════════════════╗
║   MimiC - Self-Hosted C Compiler v1.0.0-alpha                 ║
║   Target: RP2040                                               ║
╚═══════════════════════════════════════════════════════════════╝

[INIT] SD card mounted successfully
Type 'help' for available commands

mimic> ls /mimic/src
  [DIR]  .
  [DIR]  ..
    245 hello.c

mimic> cat /mimic/src/hello.c
#include <pico/stdlib.h>

int main() {
    puts("Hello from MimiC!");
    return 0;
}

mimic> cc /mimic/src/hello.c

[CC] Pass 1: Lexer
[LEX] 42 tokens, 128 bytes strings
[CC] Pass 2: Parser
[PARSE] 15 AST nodes
[CC] Pass 4: Code Generation
[CODEGEN] 64 bytes code, 2 relocations
[CC] Pass 5: Linker
[LINK] Output: /mimic/src/hello.mimi

✓ Compilation successful
  Time: 847 ms
  Tokens: 42
  Code: 64 bytes

mimic> run /mimic/src/hello.mimi
Hello from MimiC!

mimic> 
```

## SD Card Layout

```
/mimic/
├── src/              # User source files
│   └── hello.c
├── bin/              # Compiled binaries
│   └── hello.mimi
├── tmp/              # Compilation workspace
│   ├── temp.tok
│   ├── temp.ast
│   └── temp.o
└── sdk/              # SDK headers
    └── include/
        ├── pico/
        │   └── stdlib.h
        ├── hardware/
        │   └── gpio.h
        └── mimic/
            └── syscall.h
```

## Syscall Interface

User programs access hardware through syscalls:

| Category | Syscalls |
|----------|----------|
| Process | exit, yield, sleep, time |
| Memory | malloc, free, realloc |
| File I/O | open, close, read, write, seek |
| Console | putchar, getchar, puts |
| GPIO | init, set_dir, put, get, set_pulls |
| PWM | init, set_wrap, set_level, enable |
| ADC | init, select, read |
| SPI | init, write, read, transfer |
| I2C | init, write, read |

## Memory Layout

### RP2040 (264KB)
```
┌─────────────────────────┐ 0x20000000
│  Kernel Code (XIP)      │
├─────────────────────────┤
│  Kernel Heap (50KB)     │
├─────────────────────────┤
│  User Heap (180KB)      │
│  ├─ Program .text       │
│  ├─ Program .data       │
│  ├─ Program heap        │
│  └─ Program stack       │
└─────────────────────────┘ 0x20042000
```

### RP2350 (520KB)
```
┌─────────────────────────┐ 0x20000000
│  Kernel Heap (80KB)     │
├─────────────────────────┤
│  User Heap (380KB)      │
│  └─ (same as above)     │
└─────────────────────────┘ 0x20082000
```

## Status

**Current:** Alpha - Core infrastructure complete

**Working:**
- [x] Kernel with memory management
- [x] Task loading and execution
- [x] Custom FAT32 filesystem
- [x] Lexer (tokenization)
- [x] Parser (AST generation)
- [x] ARM Thumb code generator (basic)
- [x] Linker (basic)
- [x] Syscall interface
- [x] SDK headers

**TODO:**
- [ ] Full semantic analysis pass
- [ ] Complete C98 standard support
- [ ] Optimization passes
- [ ] Preprocessor (#include, #define)
- [ ] Standard library (printf, etc.)
- [ ] Debugger integration

## Philosophy

> "Self-hosted compilation on a $4 microcontroller with full C98 support - not a toy, but a properly engineered system that makes embedded development accessible without external toolchains."

The disk-buffering approach is the key innovation. By trading compilation speed for memory efficiency, MimiC brings real C compilation to microcontrollers.

## License

MIT License

## Credits

- Kernel architecture derived from Picomimi v14.3.1
- Compiler design inspired by TCC (Tiny C Compiler)
- ARM Thumb encoding based on ARM Architecture Reference Manual
