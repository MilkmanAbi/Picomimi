# Picomimi-AxisOS Complete Documentation

## The Definitive Guide to Understanding, Learning, and Mastering Picomimi

**Version:** 14.3.1 "Quiet Otter" - Resource-Owning Kernel Edition  
**Target Hardware:** Raspberry Pi Pico (RP2040) and Pico 2 (RP2350)  
**Lines of Code:** ~12,000  
**Author's Note:** This documentation is written to take you from "Hello World" to understanding every line of a 12,000-line embedded operating system.

---

# Table of Contents

1. [What is Picomimi?](#part-1-what-is-picomimi)
2. [C++ From Zero](#part-2-c-from-zero)
3. [Embedded Systems Fundamentals](#part-3-embedded-systems-fundamentals)
4. [The Picomimi Philosophy](#part-4-the-picomimi-philosophy)
5. [Architecture Overview](#part-5-architecture-overview)
6. [The Kernel Core](#part-6-the-kernel-core)
7. [Memory Management](#part-7-memory-management)
8. [The Scheduler](#part-8-the-scheduler)
9. [CPU Frequency Governor](#part-9-cpu-frequency-governor)
10. [Resource Management](#part-10-resource-management)
11. [Inter-Process Communication](#part-11-inter-process-communication)
12. [The Filesystem (PMFS)](#part-12-the-filesystem-pmfs)
13. [The Shell](#part-13-the-shell)
14. [The SDK](#part-14-the-sdk)
15. [USB Serial Subsystem](#part-15-usb-serial-subsystem)
16. [Dual-Core Support](#part-16-dual-core-support)
17. [GUI Engine](#part-17-gui-engine)
18. [Code Patterns and Idioms](#part-18-code-patterns-and-idioms)
19. [The Evolution of Picomimi](#part-19-the-evolution-of-picomimi)
20. [Extending Picomimi](#part-20-extending-picomimi)

---

# Part 1: What is Picomimi?

## 1.1 The One-Sentence Answer

Picomimi-AxisOS is a **complete embedded distribution** for RP2040/RP2350 microcontrollers - not just an RTOS, but an entire operating environment with a kernel, scheduler, memory manager, filesystem, shell, SDK, and GUI engine, all in a single 12,000-line Arduino sketch.

## 1.2 What Makes It Different?

Most "operating systems" for microcontrollers are one of two things:

1. **Minimal RTOSes** - Like FreeRTOS. They give you task scheduling and maybe some synchronization primitives. That's it. You build everything else yourself.

2. **Arduino Sketches** - Single-threaded, no resource management, no protection, no abstraction.

Picomimi is neither. It's a **complete embedded distribution**:

| Feature | FreeRTOS | Arduino | Picomimi |
|---------|----------|---------|----------|
| Task Scheduling | ✓ | ✗ | ✓ |
| Memory Management | Basic | None | Full (kmalloc/kfree, compaction, OOM killer) |
| Filesystem | ✗ | SD library | PMFS (journaled, wear-leveling, tmpfs) |
| Shell | ✗ | ✗ | Full interactive shell |
| Power Management | ✗ | ✗ | 5-level CPU governor with thermal throttling |
| Resource Tracking | ✗ | ✗ | Hardware ownership, auto-cleanup |
| GUI | ✗ | ✗ | Display engine with focus management |
| IPC | Queues only | ✗ | Messages, signals, shared memory |
| Dual-Core | Manual | ✗ | Automatic load balancing |

## 1.3 The Name

- **Pico** - Raspberry Pi Pico, the target hardware
- **mimi** - Japanese for "ear" (耳), because I like cats and I named it after Nekomimi. T^T
- **AxisOS** - The underlying operating system architecture

## 1.4 The Journey

Picomimi started as a simple cooperative scheduler - maybe 500 lines. "What if I could run multiple things at once on a Pico?"

Then it grew:
- "What if tasks could talk to each other?" → IPC system
- "What if I could type commands?" → Shell
- "What if I could save files?" → Filesystem
- "What if the chip could save power?" → CPU governor
- "What if tasks couldn't hog resources?" → OOM killer
- "What if dying tasks cleaned up properly?" → Resource-owning kernel

11,000 lines later, here we are.

---

# Part 2: C++ From Zero

Before you can understand Picomimi, you need to understand C++. This section takes you from absolute zero to understanding the patterns used throughout the codebase.

## 2.1 Hello World

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
```

Let's break this down:

- `#include <iostream>` - This is a **preprocessor directive**. Before your code compiles, the preprocessor literally copy-pastes the contents of the `iostream` file into your code. This file contains definitions for input/output operations.

- `int main()` - This is a **function**. Every C++ program must have a `main` function - it's where execution begins. The `int` means it returns an integer (a whole number).

- `std::cout` - This is the **standard output stream**. Think of it as "the screen."

- `<<` - This is the **insertion operator**. It "inserts" data into the stream.

- `std::endl` - This is "end line" - it prints a newline and flushes the buffer.

- `return 0` - This tells the operating system "I finished successfully." Non-zero means error.

## 2.2 Variables and Types

```cpp
int age = 25;           // Integer (whole number)
float price = 19.99;    // Floating point (decimal)
double pi = 3.14159265; // Double precision float
char letter = 'A';      // Single character
bool alive = true;      // Boolean (true/false)
```

### Why Different Types?

Memory. An `int` typically uses 4 bytes. A `char` uses 1 byte. On a microcontroller with 264KB of RAM, every byte matters.

### Fixed-Width Types (Used Extensively in Picomimi)

```cpp
#include <stdint.h>

uint8_t  small = 255;        // Unsigned 8-bit (0 to 255)
int8_t   tiny = -128;        // Signed 8-bit (-128 to 127)
uint16_t medium = 65535;     // Unsigned 16-bit
int16_t  med_signed = -1000; // Signed 16-bit
uint32_t large = 4294967295; // Unsigned 32-bit
int32_t  big = -2000000000;  // Signed 32-bit
uint64_t huge = 0xFFFFFFFFFFFFFFFF; // Unsigned 64-bit
```

**Why use these?** Portability and precision. When you say `uint32_t`, you KNOW it's exactly 32 bits on every platform. A plain `int` might be 16 bits on one system and 32 on another.

In Picomimi, you'll see `uint32_t` everywhere because we need exact control over memory layout.

## 2.3 Pointers - The Most Important Concept

A pointer is a variable that holds a **memory address**.

```cpp
int value = 42;
int* ptr = &value;  // ptr holds the ADDRESS of value

// Now:
// value == 42
// ptr   == 0x20001234 (some memory address)
// *ptr  == 42 (dereferencing - getting the value AT that address)
```

### The Operators

- `&` (address-of) - Gets the memory address of a variable
- `*` (dereference) - Gets the value at a memory address
- `*` (in declaration) - Declares a pointer type

```cpp
int x = 10;
int* p = &x;    // p points to x
*p = 20;        // Changes x to 20 through the pointer
```

### Why Pointers Matter

1. **Efficiency** - Passing a pointer (4-8 bytes) is cheaper than copying a large struct
2. **Modification** - Functions can modify caller's variables
3. **Dynamic Memory** - Allocating memory at runtime
4. **Data Structures** - Linked lists, trees, etc.

### nullptr - The Null Pointer

```cpp
int* ptr = nullptr;  // ptr points to nothing

if (ptr == nullptr) {
    // ptr is not pointing to valid memory
}

if (ptr) {
    // This is false because nullptr converts to false
}

if (!ptr) {
    // This is true
}
```

`nullptr` is C++'s way of saying "this pointer doesn't point to anything valid." In older C code, you'll see `NULL` or `0` used instead.

**CRITICAL:** Dereferencing nullptr causes a crash:
```cpp
int* ptr = nullptr;
int x = *ptr;  // CRASH! Segmentation fault / hard fault
```

In Picomimi, you'll see null checks everywhere:
```cpp
if (!task) return;  // If task is null, bail out
if (ptr == nullptr) return false;
```

## 2.4 Arrays

```cpp
int numbers[5] = {1, 2, 3, 4, 5};

numbers[0];  // First element (1)
numbers[4];  // Last element (5)
numbers[5];  // UNDEFINED BEHAVIOR! Out of bounds
```

Arrays in C/C++ are just contiguous memory. The name of an array IS a pointer to its first element:

```cpp
int arr[3] = {10, 20, 30};
int* ptr = arr;  // Valid! arr decays to pointer

ptr[0];  // 10
ptr[1];  // 20
*(ptr + 2);  // 30 (pointer arithmetic)
```

## 2.5 Structs - Grouping Data

```cpp
struct Person {
    char name[32];
    int age;
    float height;
};

Person bob;
bob.age = 30;
strcpy(bob.name, "Bob");
bob.height = 5.9;

Person* ptr = &bob;
ptr->age = 31;  // Arrow operator for pointer-to-struct
```

### The Arrow Operator (->)

When you have a **pointer to a struct**, you use `->` to access members:

```cpp
Person* p = &bob;
p->age;      // Same as (*p).age
p->height;   // Same as (*p).height
```

In Picomimi, almost everything is a struct:

```cpp
struct TCB {  // Task Control Block
    uint32_t id;
    char name[MAX_TASK_NAME];
    TaskState state;
    void (*entry)();
    // ... many more fields
};

TCB* task = &kernel.tasks[0];
task->state = TASK_READY;
```

## 2.6 Enums - Named Constants

```cpp
enum TaskState {
    TASK_READY,      // 0
    TASK_RUNNING,    // 1
    TASK_BLOCKED,    // 2
    TASK_TERMINATED, // 3
    TASK_ZOMBIE      // 4
};

TaskState state = TASK_READY;

if (state == TASK_RUNNING) {
    // ...
}
```

Enums give meaningful names to numbers. Much better than:
```cpp
if (state == 1) {  // What does 1 mean?!
```

## 2.7 Functions

```cpp
// Declaration (prototype)
int add(int a, int b);

// Definition (implementation)
int add(int a, int b) {
    return a + b;
}

// Usage
int result = add(3, 4);  // result = 7
```

### Pass by Value vs Pass by Reference

```cpp
// Pass by value - copies the value
void increment_copy(int x) {
    x++;  // Only modifies the local copy
}

// Pass by pointer - can modify original
void increment_ptr(int* x) {
    (*x)++;  // Modifies the original
}

// Pass by reference (C++ only) - cleaner syntax
void increment_ref(int& x) {
    x++;  // Modifies the original
}

int num = 5;
increment_copy(num);  // num is still 5
increment_ptr(&num);  // num is now 6
increment_ref(num);   // num is now 7
```

### Function Pointers

Functions live in memory too. You can have pointers to them:

```cpp
void say_hello() {
    printf("Hello!\n");
}

void say_goodbye() {
    printf("Goodbye!\n");
}

// Declare a function pointer
void (*func_ptr)();

func_ptr = say_hello;
func_ptr();  // Prints "Hello!"

func_ptr = say_goodbye;
func_ptr();  // Prints "Goodbye!"
```

In Picomimi, every task has a function pointer as its entry point:

```cpp
struct TCB {
    void (*entry)();  // Pointer to the task's main function
};

task->entry();  // Run the task
```

## 2.8 Dynamic Memory Allocation

```cpp
// C-style (used in Picomimi for embedded compatibility)
int* p = (int*)malloc(sizeof(int));
*p = 42;
free(p);

// Picomimi's kernel allocator
void* ptr = kmalloc(1024);  // Allocate 1024 bytes
kfree(ptr);  // Free it
```

### Memory Leaks

If you allocate memory and never free it, that's a **memory leak**:

```cpp
void leaky_function() {
    int* p = (int*)malloc(4000);
    // Oops! Forgot to free(p)
    // Those 4000 bytes are now lost forever
}
```

On a desktop with 16GB RAM, leaks are bad but survivable. On a microcontroller with 264KB, leaks will kill you fast.

## 2.9 The static Keyword

`static` has multiple meanings depending on context:

### Static Local Variables
```cpp
void counter() {
    static int count = 0;  // Initialized once, persists between calls
    count++;
    printf("%d\n", count);
}

counter();  // Prints 1
counter();  // Prints 2
counter();  // Prints 3
```

### Static Global Variables
```cpp
static int secret = 42;  // Only visible in this file
```

## 2.10 The const Keyword

`const` means "this cannot be changed":

```cpp
const int MAX_SIZE = 100;
MAX_SIZE = 200;  // ERROR! Can't modify const

const char* str = "Hello";
str = "World";  // OK - pointer can change
str[0] = 'X';   // ERROR - data is const
```

## 2.11 The volatile Keyword

`volatile` tells the compiler: "This variable can change at any time, don't optimize it away."

```cpp
volatile bool flag = false;

// In main code:
while (!flag) {
    // Wait for flag
}

// In interrupt:
flag = true;
```

Without `volatile`, the compiler might optimize the while loop to an infinite loop because it doesn't see `flag` changing.

## 2.12 Preprocessor Directives

The preprocessor runs BEFORE compilation:

```cpp
// Include files
#include <stdio.h>      // System header
#include "myfile.h"     // Local header

// Define constants
#define MAX_TASKS 16
#define PI 3.14159

// Define macros (function-like)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Conditional compilation
#if RP2040_OR_RP2350 == 0
    #define CHIP_NAME "RP2040"
#else
    #define CHIP_NAME "RP2350"
#endif
```

## 2.13 Bitwise Operations

Working with individual bits is essential in embedded systems:

```cpp
uint8_t flags = 0b00000000;

// Set bit 3
flags |= (1 << 3);  // flags = 0b00001000

// Clear bit 3
flags &= ~(1 << 3);  // flags = 0b00000000

// Toggle bit 3
flags ^= (1 << 3);  // Flips bit 3

// Check if bit 3 is set
if (flags & (1 << 3)) {
    // Bit 3 is set
}
```

### Common Patterns in Picomimi

```cpp
// Task flags
#define TASK_FLAG_CRITICAL    (1 << 0)
#define TASK_FLAG_REALTIME    (1 << 1)
#define TASK_FLAG_GUI_APP     (1 << 2)

task->flags |= TASK_FLAG_CRITICAL;  // Set critical flag
task->flags &= ~TASK_FLAG_REALTIME; // Clear realtime flag

if (task->flags & TASK_FLAG_GUI_APP) {
    // This is a GUI app
}
```

---

# Part 3: Embedded Systems Fundamentals

## 3.1 What is a Microcontroller?

A microcontroller (MCU) is a tiny computer on a single chip:
- **CPU** - Executes instructions
- **RAM** - Volatile memory (lost on power off)
- **Flash** - Non-volatile memory (persists)
- **Peripherals** - GPIO, SPI, I2C, UART, ADC, PWM, etc.

The RP2040 (Raspberry Pi Pico):
- Dual ARM Cortex-M0+ cores at up to 133MHz (overclock to 300MHz+)
- 264KB SRAM
- 2MB Flash (external)
- 30 GPIO pins
- 2 SPI, 2 I2C, 2 UART
- 16 PWM channels
- 8 PIO state machines

The RP2350 (Raspberry Pi Pico 2):
- Dual ARM Cortex-M33 cores at up to 150MHz (overclock to 300MHz+)
- 520KB SRAM
- 4MB Flash
- 48 GPIO pins
- More peripherals

## 3.2 Memory Map

On an MCU, everything is memory-mapped:

```
0x00000000 - 0x00FFFFFF: Flash (code lives here)
0x20000000 - 0x2003FFFF: SRAM (RAM)
0x40000000 - 0x4FFFFFFF: Peripherals (GPIO, SPI, etc.)
```

To control a GPIO pin, you write to a specific memory address:
```cpp
volatile uint32_t* GPIO_OUT = (volatile uint32_t*)0x40014010;
*GPIO_OUT |= (1 << 25);  // Set GPIO 25 high
```

## 3.3 GPIO (General Purpose Input/Output)

Pins that can be inputs or outputs:

```cpp
// Arduino style
pinMode(25, OUTPUT);
digitalWrite(25, HIGH);
digitalWrite(25, LOW);

int value = digitalRead(15);  // Read input

// In Picomimi (with resource claiming):
int led = Pico.ClaimGPIO(25);
if (led >= 0) {
    pinMode(led, OUTPUT);
    digitalWrite(led, HIGH);
}
```

## 3.4 Interrupts

An interrupt is a hardware signal that pauses normal execution to handle an urgent event:

```cpp
void gpio_callback(uint gpio, uint32_t events) {
    // This runs when GPIO changes!
    // Keep it SHORT - you're interrupting everything else
}

gpio_set_irq_enabled_with_callback(15, GPIO_IRQ_EDGE_FALL, true, gpio_callback);
```

**Rules for interrupt handlers:**
1. Keep them SHORT
2. Don't allocate memory
3. Don't use blocking operations
4. Set a flag and handle in main loop if complex work needed

## 3.5 Clock and Timing

MCUs have internal clocks:

```cpp
// Get current time in microseconds
uint64_t now = time_us_64();

// Delay
delay(1000);              // 1 second (Arduino)
sleep_ms(1000);           // 1 second (Pico SDK)
busy_wait_us(100);        // 100 microseconds (busy wait)
```

In Picomimi:
```cpp
uint64_t get_time_us() { return time_us_64(); }
uint32_t get_time_ms() { return (uint32_t)(time_us_64() / 1000); }
```

## 3.6 Flash vs RAM

| Flash | RAM |
|-------|-----|
| Persistent | Volatile |
| Slow to write | Fast to write |
| Limited write cycles (~10,000-100,000) | Unlimited |
| Stores code and constants | Stores variables |

## 3.7 Watchdog Timer

A safety mechanism that resets the MCU if software hangs:

```cpp
watchdog_enable(8000, 1);  // 8 second timeout

while (true) {
    // Main loop
    watchdog_update();  // Pet the watchdog
}

// If you don't call watchdog_update() for 8 seconds, MCU resets
```

---

# Part 4: The Picomimi Philosophy

## 4.1 "Complete Embedded Distribution" vs "Minimal RTOS"

Most embedded operating systems follow the UNIX philosophy: "Do one thing well." FreeRTOS does scheduling. That's it.

Picomimi follows a different philosophy: **"Provide everything an embedded application needs, out of the box."**

| Minimal RTOS | Picomimi |
|--------------|----------|
| "Here's a scheduler, figure out the rest" | "Here's a complete system" |
| Memory management? Use heap or roll your own | kmalloc/kfree with compaction and OOM killer |
| Filesystem? Add a library | PMFS built-in with journaling |
| Shell? Write it yourself | Full interactive shell |
| Power management? Not our problem | 5-level CPU governor |

## 4.2 Zero-Overhead Abstraction

Picomimi provides abstraction without runtime cost:

```cpp
// Resource claiming - one-time cost
int led = Pico.ClaimGPIO(25);  // ~1 microsecond

// Hardware access - ZERO overhead
digitalWrite(led, HIGH);  // Direct hardware, no kernel involvement
```

The kernel tracks WHO owns WHAT, but doesn't intercept every operation. This gives you:
- Full-speed GPIO toggling (microsecond precision)
- Clean automatic cleanup when tasks die
- Resource conflict detection

## 4.3 Cooperative by Design

Picomimi uses **cooperative multitasking**, not preemptive:

**Preemptive (FreeRTOS style):**
- Kernel can interrupt tasks at any time
- Requires careful locking
- Context switches have overhead
- Complex to reason about

**Cooperative (Picomimi style):**
- Tasks voluntarily yield
- No surprise interruptions
- Simpler mental model
- Tasks must be well-behaved

```cpp
void my_task() {
    while (true) {
        // Do some work
        do_stuff();
        
        // Let other tasks run
        Pico.Yield();
    }
}
```

## 4.4 Immediate Memory Management

Traditional OS: When you free memory, it goes back to a pool. Maybe gets reclaimed later.

Picomimi: When you free memory, it's IMMEDIATELY available. No garbage collection delays, no background threads.

```cpp
void* ptr = kmalloc(1000);
// ... use it ...
kfree(ptr);
// Memory is IMMEDIATELY available for next allocation
```

## 4.5 The 30% Rule

Picomimi reserves ~30% of RAM for kernel data structures:
- Task control blocks
- Memory allocation tables
- IPC queues
- Filesystem cache
- Resource descriptors

The remaining ~70% is the kernel heap for applications.

On RP2040 (264KB): ~80KB kernel, ~180KB heap
On RP2350 (520KB): ~150KB kernel, ~350KB heap

## 4.6 Graceful Degradation

When things go wrong, Picomimi tries to recover gracefully:

1. **Out of Memory:**
   - Try memory compaction
   - Ask apps to free memory (OOM handlers)
   - Kill the worst offender
   - Only panic if nothing works

2. **Task Misbehavior:**
   - CPU abuser? Lower its priority
   - Resource hoarder? Higher OOM kill score
   - Infinite loop? Cooperative watchdog detection

3. **Hardware Failure:**
   - SD card removed? Switch to tmpfs
   - USB disconnected? Keep running

## 4.7 Single-File Architecture

The entire OS is ONE `.ino` file. Why?

1. **Simplicity** - No build system complexity
2. **Portability** - Works in Arduino IDE
3. **Transparency** - Everything is visible
4. **Teachability** - Students can read it all

Yes, 12,000 lines in one file is unconventional. But for an educational embedded OS, it works.

---

# Part 5: Architecture Overview

## 5.1 The Big Picture

```
┌─────────────────────────────────────────────────────────────────┐
│                        USER SPACE                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │ App 1    │  │ App 2    │  │ App 3    │  │ Driver   │        │
│  │ (GUI)    │  │ (Sensor) │  │ (Logger) │  │ (SPI)    │        │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘        │
│       │             │             │             │               │
│       └─────────────┴──────┬──────┴─────────────┘               │
│                            │                                     │
│                    ┌───────▼───────┐                            │
│                    │   Pico SDK    │                            │
│                    │  (API Layer)  │                            │
│                    └───────┬───────┘                            │
├────────────────────────────┼────────────────────────────────────┤
│                     KERNEL SPACE                                 │
│                            │                                     │
│  ┌─────────────────────────▼─────────────────────────────────┐  │
│  │                    KERNEL CORE                             │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐         │  │
│  │  │Scheduler│ │ Memory  │ │   IPC   │ │   FS    │         │  │
│  │  │         │ │ Manager │ │         │ │ (PMFS)  │         │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘         │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐         │  │
│  │  │Governor │ │Resource │ │  Shell  │ │   GUI   │         │  │
│  │  │         │ │ Manager │ │         │ │ Engine  │         │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘         │  │
│  └───────────────────────────────────────────────────────────┘  │
│                            │                                     │
├────────────────────────────┼────────────────────────────────────┤
│                     HARDWARE                                     │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐  │
│  │  CPU    │ │  RAM    │ │  GPIO   │ │  SPI    │ │  USB    │  │
│  │Core 0/1 │ │264/520KB│ │ 30/48   │ │  I2C    │ │  UART   │  │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## 5.2 Kernel State Structure

Everything about the kernel lives in ONE global structure:

```cpp
struct KernelState {
    // === Core State ===
    bool running;
    bool initialized;
    uint32_t current_task;
    uint32_t task_count;
    uint64_t boot_time_ms;
    
    // === Tasks ===
    TCB tasks[MAX_TASKS];
    
    // === Memory ===
    uint8_t* heap_start;
    uint8_t* heap_end;
    size_t heap_size;
    MemBlock* mem_blocks;
    
    // === Scheduler ===
    uint32_t scheduler_ticks;
    uint32_t context_switches;
    
    // === Governor ===
    GovernorState governor;
    
    // === Resources ===
    ResourceDescriptor gpio_resources[48];
    ResourceDescriptor spi_resources[2];
    // ... more resources ...
    
    // === IPC ===
    MessageQueue msg_queues[MAX_MSG_QUEUES];
    
    // === USB ===
    bool usb_connected;
    uint32_t usb_bytes_tx;
    uint32_t usb_bytes_rx;
    
    // ... and much more ...
};

KernelState kernel;  // THE kernel - one global instance
```

## 5.3 File Layout (Top to Bottom)

```
Line 1-250:     Configuration flags, includes, forward declarations
Line 250-500:   Constants and macros
Line 500-1500:  Data structures (TCB, MemBlock, etc.)
Line 1500-2000: KernelState structure
Line 2000-3000: Forward declarations for all functions
Line 3000-4000: USB Serial subsystem
Line 4000-5500: Resource Manager (v14.3.1)
Line 5500-6500: CPU Governor
Line 6500-7500: Memory Manager + OOM Killer
Line 7500-8500: Scheduler + Task Management
Line 8500-9000: IPC System
Line 9000-9500: Filesystem (PMFS)
Line 9500-10500: Shell + Commands
Line 10500-11000: SDK API (PicomimiAPI class)
Line 11000-11500: GUI Engine
Line 11500-12000: setup() and loop()
```

---

# Part 6: The Kernel Core

## 6.1 Boot Sequence

When the Pico powers on:

```cpp
void setup() {
    // 1. Initialize hardware
    Serial.begin(115200);
    
    // 2. Initialize kernel structures
    memset(&kernel, 0, sizeof(kernel));
    kernel.boot_time_ms = millis();
    
    // 3. Initialize memory manager
    mem_init();
    
    // 4. Initialize CPU governor
    governor_init();
    
    // 5. Initialize resource manager
    res_init();
    
    // 6. Initialize filesystem
    pmfs_init();
    
    // 7. Create idle task (Task 0)
    create_task("idle", idle_task, TASK_PRIO_IDLE);
    
    // 8. Create shell task
    create_task("shell", shell_task, TASK_PRIO_NORMAL);
    
    // 9. Mark kernel as running
    kernel.running = true;
    kernel.initialized = true;
    
    // 10. Print boot banner
    print_boot_banner();
    
    // 11. Start Core 1 (if enabled)
    core1_init();
}
```

## 6.2 The Main Loop

```cpp
void loop() {
    // Safety check
    if (!kernel.running || kernel.task_count == 0) {
        kernel_panic("Kernel loop fault");
    }
    
    // Run scheduler tick
    scheduler_tick();
    
    // Run governor tick
    governor_tick();
    
    // Run resource manager tick
    res_tick();
    
    // USB maintenance
    usb_poll();
    usb_recovery_check();
    
    // Get current task
    TCB* task = &kernel.tasks[kernel.current_task];
    
    // Skip terminated tasks
    if (task->state == TASK_TERMINATED) {
        task_yield();
        return;
    }
    
    // Execute the task
    if (task->entry) {
        task->entry();  // Call the task's function
    }
}
```

## 6.3 Task Control Block (TCB)

Every task has a TCB that stores its state:

```cpp
struct TCB {
    // Identity
    uint32_t id;
    char name[MAX_TASK_NAME];
    TaskType task_type;        // KERNEL, APPLICATION, DRIVER, SERVICE
    
    // State
    TaskState state;           // READY, RUNNING, BLOCKED, TERMINATED, ZOMBIE
    uint32_t flags;            // CRITICAL, REALTIME, GUI_APP, etc.
    
    // Execution
    void (*entry)();           // Main function pointer
    TaskCallbacks* callbacks;  // Optional tick/cleanup callbacks
    
    // Scheduling
    uint8_t priority;          // 0-255, lower = higher priority
    uint8_t base_priority;     // Original priority
    uint32_t time_slice;       // Ticks before yield
    uint64_t cpu_time_us;      // Total CPU time used
    uint64_t last_run;         // Last execution timestamp
    
    // Memory
    uint32_t heap_used;        // Bytes allocated
    uint32_t heap_peak;        // Peak allocation
    uint32_t alloc_count;      // Number of allocations
    
    // OOM
    int8_t oom_priority;       // Kill priority (-10 to +10)
    uint32_t alloc_velocity;   // Allocations per second
    bool is_cpu_abuser;        // Flagged for excessive CPU
    
    // Statistics
    uint32_t yields;
    uint32_t blocks;
    uint32_t wakeups;
};
```

## 6.4 Task States

```
          create_task()
               │
               ▼
        ┌─────────────┐
        │    READY    │◄─────────────────┐
        └──────┬──────┘                  │
               │ scheduler picks        │ yield() or
               ▼                        │ time slice
        ┌─────────────┐                 │
        │   RUNNING   │─────────────────┘
        └──────┬──────┘
               │
      ┌────────┼────────┐
      │        │        │
      ▼        ▼        ▼
┌─────────┐ ┌─────┐ ┌──────────┐
│ BLOCKED │ │EXIT │ │  ZOMBIE  │
└────┬────┘ └──┬──┘ └────┬─────┘
     │         │         │
     └────►READY        │
             (wake)     │
                        ▼
                  ┌────────────┐
                  │ TERMINATED │
                  └────────────┘
```

## 6.5 Task Types

```cpp
enum TaskType {
    TASK_TYPE_KERNEL,       // Core kernel tasks (idle, shell)
    TASK_TYPE_APPLICATION,  // User applications
    TASK_TYPE_DRIVER,       // Hardware drivers
    TASK_TYPE_SERVICE       // Background services
};
```

Only `TASK_TYPE_APPLICATION` can be killed by the OOM killer. Kernel tasks are protected.

## 6.6 Creating a Task

```cpp
uint32_t create_task(const char* name, void (*entry)(), uint8_t priority) {
    if (kernel.task_count >= MAX_TASKS) {
        return INVALID_TASK_ID;
    }
    
    uint32_t id = kernel.task_count;
    TCB* task = &kernel.tasks[id];
    
    // Initialize TCB
    memset(task, 0, sizeof(TCB));
    task->id = id;
    strncpy(task->name, name, MAX_TASK_NAME - 1);
    task->entry = entry;
    task->state = TASK_READY;
    task->priority = priority;
    task->base_priority = priority;
    task->task_type = TASK_TYPE_APPLICATION;
    task->time_slice = DEFAULT_TIME_SLICE;
    task->oom_priority = 0;
    
    kernel.task_count++;
    
    klog(0, "Task created");
    return id;
}
```

## 6.7 Killing a Task

```cpp
void brutal_task_kill(uint32_t id) {
    TCB* task = &kernel.tasks[id];
    
    // Don't kill kernel tasks
    if (task->task_type == TASK_TYPE_KERNEL) {
        kernel_panic("KERNEL TASK KILLED");
    }
    
    // Release GUI focus
    if (kernel.gui_focus_task_id == id) {
        k_release_gui_focus(id);
    }
    
    // Release all hardware resources (v14.3.1)
    res_cleanup_task(id);
    
    // Mark as zombie (memory not freed yet)
    task->state = TASK_ZOMBIE;
    
    // Free task's memory
    // ... memory cleanup ...
    
    // Mark as terminated
    task->state = TASK_TERMINATED;
}
```

## 6.8 Kernel Panic

When something unrecoverable happens:

```cpp
void kernel_panic(const char* message) {
    // Disable interrupts
    __disable_irq();
    
    // Print panic message
    Serial.println("\n\n!!! KERNEL PANIC !!!");
    Serial.println(message);
    
    // Flash LED rapidly forever
    while (true) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        busy_wait_ms(100);
    }
}
```

---

# Part 7: Memory Management

## 7.1 The Heap

Picomimi manages a contiguous block of RAM called the heap:

```cpp
void mem_init() {
    // Calculate heap location (after static data)
    kernel.heap_start = calculate_heap_start();
    kernel.heap_end = calculate_heap_end();
    kernel.heap_size = kernel.heap_end - kernel.heap_start;
    
    // Create initial free block
    MemBlock* first = (MemBlock*)kernel.heap_start;
    first->size = kernel.heap_size - sizeof(MemBlock);
    first->used = false;
    first->magic = MEM_MAGIC;
    first->owner_task = 0;
    first->next = nullptr;
    first->prev = nullptr;
    
    kernel.mem_blocks = first;
}
```

## 7.2 Memory Block Structure

```cpp
struct MemBlock {
    uint32_t magic;         // 0xDEADBEEF - corruption detection
    size_t size;            // Size of data area
    bool used;              // Is this block allocated?
    uint32_t owner_task;    // Which task owns this
    MemBlock* next;         // Next block in list
    MemBlock* prev;         // Previous block in list
    uint32_t alloc_time;    // When was this allocated
    char tag[8];            // Debug tag
};
```

## 7.3 kmalloc - Kernel Memory Allocation

```cpp
void* kmalloc(size_t size) {
    if (size == 0) return nullptr;
    
    // Align to 4 bytes
    size = (size + 3) & ~3;
    
    // Find a free block (first-fit)
    MemBlock* block = kernel.mem_blocks;
    while (block) {
        if (!block->used && block->size >= size) {
            // Found a suitable block!
            
            // Split if too big
            if (block->size > size + sizeof(MemBlock) + MIN_BLOCK_SIZE) {
                split_block(block, size);
            }
            
            // Mark as used
            block->used = true;
            block->owner_task = kernel.current_task;
            block->alloc_time = get_time_ms();
            
            // Update task's memory tracking
            TCB* task = &kernel.tasks[kernel.current_task];
            task->heap_used += block->size;
            task->alloc_count++;
            
            // Return pointer to data area (after header)
            return (void*)(block + 1);
        }
        block = block->next;
    }
    
    // No suitable block - try compaction
    mem_compact();
    
    // If still no memory, trigger OOM killer
    if (oom_killer(size)) {
        return kmalloc(size);  // Retry after killing
    }
    
    return nullptr;  // Truly out of memory
}
```

## 7.4 kfree - Kernel Memory Free

```cpp
void kfree(void* ptr) {
    if (!ptr) return;
    
    // Get block header
    MemBlock* block = ((MemBlock*)ptr) - 1;
    
    // Validate magic
    if (block->magic != MEM_MAGIC) {
        klog(3, "kfree: Invalid block or corruption!");
        return;
    }
    
    // Update task tracking
    if (block->owner_task < kernel.task_count) {
        TCB* task = &kernel.tasks[block->owner_task];
        task->heap_used -= block->size;
        task->alloc_count--;
    }
    
    // Mark as free
    block->used = false;
    
    // Coalesce with neighbors
    coalesce_blocks(block);
}
```

## 7.5 Block Coalescing

When you free a block, merge it with adjacent free blocks:

```
Before free:
[USED 100B] [FREE 50B] [USED 200B] [FREE 80B] [FREE 100B]

After freeing the 200B block and coalescing:
[USED 100B] [FREE 50B] [FREE 380B]  <- 200+80+100 merged!
```

## 7.6 Memory Compaction

Defragment the heap by moving blocks:

```
Before:
[USED 100] [FREE 50] [USED 200] [FREE 80] [USED 150]

After compaction:
[USED 100] [USED 200] [USED 150] [FREE 130]  <- All free space at end
```

## 7.7 OOM (Out of Memory) Killer

When memory is exhausted, kill a task to reclaim memory:

```cpp
bool oom_killer(size_t bytes_needed) {
    klog(3, "OOM: Out of memory!");
    
    // Try compaction first
    mem_compact();
    if (/* enough memory now */) return true;
    
    // Select victim (highest score = most likely to die)
    OOMVictim victim = oom_select_victim(bytes_needed);
    
    // Try graceful cleanup first
    if (victim.has_handler) {
        request_oom_cleanup(&victim);
    }
    
    // If graceful fails, kill brutally
    brutal_task_kill(victim.task_id);
    
    return true;
}
```

## 7.8 OOM Scoring

How do we choose which task to kill?

```cpp
int32_t oom_calculate_victim_score(TCB* task, uint32_t mem_used) {
    int32_t score = 0;
    
    // More memory = higher score (more likely to die)
    score += (mem_used / 1024);
    
    // OOM priority (user can set this)
    score += (task->oom_priority * 100);
    
    // Idle tasks are good targets
    uint64_t idle_time = get_time_ms() - task->last_run;
    if (idle_time > 5000) score += 200;
    
    // CPU abusers are good targets
    if (task->is_cpu_abuser) score += 150;
    
    // Resource hoarders are good targets (v14.3.1)
    uint32_t res_count = res_count_owned_by_task(task->id);
    score += (res_count * 30);
    
    // NEVER kill kernel tasks
    if (task->task_type == TASK_TYPE_KERNEL) score = -10000;
    if (task->flags & TASK_FLAG_CRITICAL) score = -10000;
    
    return score;
}
```

---

# Part 8: The Scheduler

## 8.1 Cooperative Scheduling

In Picomimi, tasks VOLUNTARILY give up the CPU:

```cpp
void my_task() {
    while (true) {
        do_work();
        Pico.Yield();  // "I'm done for now, let others run"
    }
}
```

## 8.2 The Scheduler Tick

Every iteration of loop(), the scheduler does bookkeeping:

```cpp
void scheduler_tick() {
    uint32_t now = get_time_ms();
    
    // Update current task's CPU time
    TCB* current = &kernel.tasks[kernel.current_task];
    current->cpu_time_us += (now - current->last_run) * 1000;
    current->last_run = now;
    
    // Check time slice
    kernel.scheduler_ticks++;
    if (kernel.scheduler_ticks >= current->time_slice) {
        kernel.scheduler_ticks = 0;
        kernel.preemption_pending = true;
    }
    
    // Update blocked tasks (wake sleeping tasks)
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        if (task->state == TASK_BLOCKED && task->sleep_until > 0) {
            if (now >= task->sleep_until) {
                task->state = TASK_READY;
                task->sleep_until = 0;
            }
        }
    }
}
```

## 8.3 Task Yielding

```cpp
void task_yield() {
    TCB* current = &kernel.tasks[kernel.current_task];
    current->yields++;
    current->state = TASK_READY;
    
    // Find next runnable task
    uint32_t next = select_next_task();
    
    if (next != kernel.current_task) {
        kernel.context_switches++;
        kernel.current_task = next;
    }
    
    // Update new task state
    TCB* next_task = &kernel.tasks[next];
    next_task->state = TASK_RUNNING;
    next_task->last_run = get_time_ms();
}
```

## 8.4 Task Selection Algorithm

Simple priority-based round-robin:

```cpp
uint32_t select_next_task() {
    uint32_t best_id = 0;  // Default to idle task
    uint8_t best_priority = 255;  // Lower = better
    
    // Start from task after current (round-robin)
    uint32_t start = (kernel.current_task + 1) % kernel.task_count;
    uint32_t i = start;
    
    do {
        TCB* task = &kernel.tasks[i];
        
        if (task->state == TASK_READY && task->priority < best_priority) {
            best_id = i;
            best_priority = task->priority;
        }
        
        i = (i + 1) % kernel.task_count;
    } while (i != start);
    
    return best_id;
}
```

## 8.5 Priority Levels

```cpp
#define TASK_PRIO_REALTIME  0    // Highest - time critical
#define TASK_PRIO_HIGH      32
#define TASK_PRIO_NORMAL    64   // Default
#define TASK_PRIO_LOW       128
#define TASK_PRIO_IDLE      255  // Lowest - only runs when nothing else can
```

## 8.6 The Idle Task

Task 0 is always the idle task - runs when nothing else can:

```cpp
void idle_task() {
    // Optionally enter low power mode
    if (kernel.governor.current_profile == CPU_PROFILE_ULTRA_LOW) {
        __wfi();  // Wait For Interrupt - CPU sleeps
    }
}
```

---

# Part 9: CPU Frequency Governor

## 9.1 Why Dynamic Frequency?

Running at full speed all the time:
- Wastes power (bad for battery)
- Generates heat
- Is unnecessary when idle

The governor automatically adjusts CPU frequency based on workload.

## 9.2 Frequency Profiles

```cpp
enum CPUProfile {
    CPU_PROFILE_ULTRA_LOW,   // 50MHz - deep sleep
    CPU_PROFILE_POWERSAVE,   // 125MHz - light work
    CPU_PROFILE_BALANCED,    // 200MHz - normal work  
    CPU_PROFILE_PERFORMANCE, // 250MHz - heavy work
    CPU_PROFILE_TURBO        // 300MHz+ - maximum
};
```

## 9.3 Governor State

```cpp
struct GovernorState {
    CPUProfile current_profile;
    CPUProfile requested_profile;
    uint32_t current_freq_mhz;
    
    // Workload tracking
    uint32_t cpu_usage_percent;
    uint32_t idle_ticks;
    uint32_t busy_ticks;
    
    // Thermal management
    float current_temp;
    bool thermal_throttled;
    
    // Turbo
    bool turbo_enabled;
    uint32_t turbo_task_id;
    
    // USB protection
    bool usb_blocking_lowpower;
};
```

## 9.4 The Governor Tick

```cpp
void governor_tick() {
    // Update CPU usage statistics
    update_cpu_stats();
    
    // Check temperature
    float temp = read_cpu_temperature();
    kernel.governor.current_temp = temp;
    
    // Thermal throttling
    if (temp > CORE_TEMP_LIMIT) {
        if (!kernel.governor.thermal_throttled) {
            klog(1, "Governor: Thermal throttling!");
        }
        governor_apply_profile(CPU_PROFILE_POWERSAVE);
        return;
    }
    
    // USB protection - never go below BALANCED when Serial active
    if (kernel.usb_blocking_lowpower) {
        if (kernel.governor.current_profile < CPU_PROFILE_BALANCED) {
            governor_apply_profile(CPU_PROFILE_BALANCED);
        }
        return;
    }
    
    // Auto-scaling based on CPU usage
    if (kernel.governor.cpu_usage_percent > 80) {
        governor_scale_up();
    } else if (kernel.governor.cpu_usage_percent < 20) {
        governor_scale_down();
    }
}
```

## 9.5 Instant Turbo

Tasks can request turbo mode for demanding operations:

```cpp
void my_heavy_computation() {
    Pico.RequestTurbo();  // Boost to max frequency
    
    // Do heavy work...
    for (int i = 0; i < 1000000; i++) {
        compute_stuff();
    }
    
    Pico.ReleaseTurbo();  // Back to normal scaling
}
```

---

# Part 10: Resource Management (v14.3.1)

## 10.1 The Problem

Traditional embedded code:
```cpp
void my_task() {
    pinMode(25, OUTPUT);
    digitalWrite(25, HIGH);
    // ... task runs forever or crashes ...
}
// If task is killed, GPIO 25 is left in undefined state!
```

Problems:
- Two tasks might fight over the same pin
- Dead tasks leave hardware in undefined state
- No audit trail of who's using what

## 10.2 The Solution: Resource-Owning Kernel

```cpp
void my_task() {
    int led = Pico.ClaimGPIO(25);  // Register ownership
    if (led < 0) {
        // Someone else owns it!
        return;
    }
    
    pinMode(led, OUTPUT);      // Direct hardware (zero overhead)
    digitalWrite(led, HIGH);   // Full speed!
    
    // If task is killed, kernel automatically:
    // 1. Disables interrupts on pin 25
    // 2. Sets pin to input (high-Z)
    // 3. Disables pull resistors
    // 4. Releases ownership
}
```

## 10.3 Zero-Overhead Design

The kernel does NOT intercept every GPIO operation:

```
CLAIMING (one-time, ~1µs):
┌─────────────────────────────────────────────┐
│ Task calls Pico.ClaimGPIO(25)               │
│   → Kernel records: "Task 3 owns GPIO 25"   │
│   → Returns pin number (or -1 if conflict)  │
└─────────────────────────────────────────────┘

OPERATIONS (zero overhead):
┌─────────────────────────────────────────────┐
│ Task calls digitalWrite(25, HIGH)           │
│   → Goes directly to hardware               │
│   → Kernel is NOT involved                  │
│   → Full speed, no function call overhead   │
└─────────────────────────────────────────────┘

CLEANUP (automatic on task death):
┌─────────────────────────────────────────────┐
│ Task is killed (OOM, crash, exit)           │
│   → Kernel sees task 3 owns GPIO 25         │
│   → Kernel resets GPIO 25 to safe state     │
│   → Kernel releases ownership               │
└─────────────────────────────────────────────┘
```

## 10.4 Resource Descriptor

```cpp
struct ResourceDescriptor {
    uint32_t owner_task_id;    // Who owns this
    uint32_t claim_time_ms;    // When it was claimed
    uint32_t last_access_ms;   // Last time updated
    
    ResHandle handle;          // Unique handle
    ResourceType type;         // GPIO, SPI, I2C, etc.
    uint8_t id;                // Pin number, channel, etc.
    
    ResourceState state;       // FREE, CLAIMED, KERNEL_RESERVED
    ResourceMode mode;         // EXCLUSIVE, SHARED_READ, etc.
    
    bool configured;           // Has it been initialized?
};
```

## 10.5 Resource Types

```cpp
enum ResourceType {
    RES_TYPE_GPIO,    // GPIO pins (30 on RP2040, 48 on RP2350)
    RES_TYPE_SPI,     // SPI buses (2)
    RES_TYPE_I2C,     // I2C buses x devices
    RES_TYPE_ADC,     // ADC channels (4-8)
    RES_TYPE_PWM,     // PWM channels (16)
    RES_TYPE_PIO,     // PIO state machines (8-12)
    RES_TYPE_UART,    // UART ports (2)
    RES_TYPE_DMA,     // DMA channels (12-16)
    RES_TYPE_TIMER    // Hardware timers (4)
};
```

## 10.6 GPIO Safe Reset

When cleaning up a GPIO:

```cpp
static void res_gpio_safe_reset(uint8_t pin) {
    // 1. Disable interrupts first
    gpio_set_irq_enabled(pin, 
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE |
        GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH, 
        false);
    
    // 2. Set to input (high-Z = safest)
    gpio_set_dir(pin, GPIO_IN);
    
    // 3. Disable pulls
    gpio_disable_pulls(pin);
    
    // 4. Reset to default function
    gpio_set_function(pin, GPIO_FUNC_SIO);
    
    // 5. Clear output latch
    gpio_put(pin, 0);
}
```

## 10.7 Kernel-Reserved Resources

Some resources are reserved for the kernel:

```cpp
static void res_mark_kernel_reserved() {
    // SD card SPI pins - cannot be claimed by apps
    kernel.gpio_resources[SD_CS].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_MOSI].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_MISO].state = RES_STATE_KERNEL_RESERVED;
    kernel.gpio_resources[SD_SCK].state = RES_STATE_KERNEL_RESERVED;
    
    // Button pin - kernel only
    kernel.gpio_resources[BTN_ONOFF].state = RES_STATE_KERNEL_RESERVED;
}
```

## 10.8 Resource API

```cpp
// Claim resources (returns resource ID or -1)
int led = Pico.ClaimGPIO(25);
int adc = Pico.ClaimADC(0);
int spi = Pico.ClaimSPI(0, cs_pin);
int pwm = Pico.ClaimPWM(slice, channel);

// Release resources
Pico.ReleaseGPIO(25);
Pico.ReleaseADC(0);

// Query
bool available = Pico.IsGPIOAvailable(25);
bool mine = Pico.OwnsGPIO(25);
uint32_t count = Pico.GetMyResourceCount();
```

---

# Part 11: Inter-Process Communication (IPC)

## 11.1 Why IPC?

Tasks need to communicate:
- "Sensor reading is ready"
- "Button was pressed"
- "Please process this data"

## 11.2 Message Queues

```cpp
// Create a queue
uint32_t queue_id = Pico.CreateMessageQueue(10);  // 10 message capacity

// Send a message
Message msg;
msg.type = MSG_SENSOR_DATA;
msg.data.value = 42;
Pico.SendMessage(queue_id, &msg);

// Receive a message (in another task)
Message received;
if (Pico.ReceiveMessage(queue_id, &received, 1000)) {  // 1s timeout
    process(received.data.value);
}
```

## 11.3 Message Structure

```cpp
struct Message {
    uint32_t type;           // Message type (user-defined)
    uint32_t sender_task;    // Who sent it
    uint64_t timestamp;      // When it was sent
    
    union {
        int32_t value;       // Simple value
        void* ptr;           // Pointer to data
        uint8_t bytes[16];   // Small data
    } data;
};
```

## 11.4 Signals

Simple notifications between tasks:

```cpp
// In task A:
Pico.WaitForSignal(SIG_DATA_READY);  // Block until signaled

// In task B:
Pico.SendSignal(task_a_id, SIG_DATA_READY);  // Wake up task A
```

---

# Part 12: The Filesystem (PMFS)

## 12.1 What is PMFS?

**Picomimi Micro Filesystem** - A simple, robust filesystem for SD cards and RAM.

Features:
- Journaling (crash recovery)
- Wear leveling
- Multiple mount points
- tmpfs (RAM disk)

## 12.2 Architecture

```
/                       <- Root (SD card)
├── system/             <- System files
│   ├── config.txt
│   └── log/
│       └── kernel.log
├── apps/               <- Application data
│   └── myapp/
│       └── data.bin
└── tmp/                <- tmpfs (RAM disk)
    └── cache.dat
```

## 12.3 File Operations

```cpp
// Open a file
FSFile file = Pico.FileOpen("/apps/myapp/data.txt", FILE_WRITE);

// Write to file
Pico.FileWrite(file, buffer, size);

// Read from file
int bytes_read = Pico.FileRead(file, buffer, sizeof(buffer));

// Close file
Pico.FileClose(file);

// Other operations
Pico.FileExists("/path/to/file");
Pico.FileDelete("/path/to/file");
Pico.CreateDirectory("/path/to/dir");
```

---

# Part 13: The Shell

## 13.1 Overview

Picomimi includes an interactive shell over USB Serial:

```
Picomimi v14.3.1 Shell
Type 'help' for commands

pico> help
=== Picomimi-AxisOS v14.3.1 Resource-Owning Kernel ===

--- System ---
 help       - Show this help
 ps         - List all tasks
 mem        - Memory stats
 uptime     - System uptime
 ...

pico> ps
=== Task List ===
ID  Name        State    Pri  Mem    CPU%
0   idle        READY    255  0KB    45.2%
1   shell       RUNNING  64   2KB    12.1%
2   sensor      READY    32   4KB    8.3%
```

## 13.2 Shell Commands

| Command | Description |
|---------|-------------|
| `help` | Show command list |
| `ps` | List tasks |
| `taskinfo <id>` | Task details |
| `top` | System monitor |
| `mem` | Memory stats |
| `memmap` | Memory block map |
| `compact` | Compact memory |
| `kill <id>` | Kill task |
| `ls [path]` | List files |
| `cat <file>` | Display file |
| `gov` | Governor status |
| `gov turbo` | Enable turbo |
| `res` | Resource status |
| `resmap` | GPIO map |
| `usbstat` | USB status |
| `reboot` | Restart |

---

# Part 14: The SDK (PicomimiAPI)

## 14.1 Overview

The `PicomimiAPI` class provides the application interface:

```cpp
class PicomimiAPI {
public:
    // Task management
    static void Yield();
    static void Sleep(uint32_t ms);
    static void Exit();
    static uint32_t GetTaskID();
    
    // Memory
    static void* Alloc(size_t size);
    static void Free(void* ptr);
    
    // Resources (v14.3.1)
    static int8_t ClaimGPIO(uint8_t pin);
    static void ReleaseGPIO(uint8_t pin);
    
    // IPC
    static bool SendMessage(uint32_t queue, Message* msg);
    static bool ReceiveMessage(uint32_t queue, Message* msg, uint32_t timeout);
    
    // Filesystem
    static FSFile FileOpen(const char* path, FileMode mode);
    static void FileClose(FSFile file);
    
    // System
    static void RequestTurbo();
    static void ReleaseTurbo();
    static uint32_t GetUptime();
    static float GetCPUTemp();
};

// Global instance
extern PicomimiAPI Pico;
```

## 14.2 Writing an Application

```cpp
void my_app_entry() {
    // Claim resources
    int led = Pico.ClaimGPIO(25);
    if (led < 0) {
        Pico.Log("Failed to claim LED!");
        return;
    }
    
    pinMode(led, OUTPUT);
    
    while (true) {
        digitalWrite(led, HIGH);
        Pico.Sleep(500);
        digitalWrite(led, LOW);
        Pico.Sleep(500);
        
        // Check for exit signal
        if (Pico.ShouldExit()) break;
    }
    
    // Cleanup (optional - kernel does this anyway)
    Pico.ReleaseGPIO(led);
}

// Register with SDK
APP_REGISTER("blink", my_app_entry);
```

---

# Part 15: USB Serial Subsystem

## 15.1 The Challenge

USB Serial on RP2040/RP2350 is tricky:
- Low power modes can break USB
- Long inactivity can cause disconnection
- Clock changes affect USB timing

## 15.2 Activity Tracking

```cpp
void usb_poll() {
    bool connected = (Serial);
    
    // Track connection state changes
    if (connected && !kernel.usb_was_connected) {
        klog(0, "USB: Connected");
        kernel.usb_blocking_lowpower = true;
    }
    
    kernel.usb_was_connected = connected;
    
    // Release low-power block after inactivity
    if (get_time_ms() - kernel.usb_last_activity_ms > USB_TIMEOUT_MS) {
        kernel.usb_blocking_lowpower = false;
    }
}
```

## 15.3 Governor Integration

The v14.1.1 fix: NEVER drop below BALANCED when USB Serial is active.

```cpp
void governor_tick() {
    if (kernel.usb_blocking_lowpower) {
        if (kernel.governor.current_profile < CPU_PROFILE_BALANCED) {
            governor_apply_profile(CPU_PROFILE_BALANCED);
        }
    }
}
```

---

# Part 16: Dual-Core Support

## 16.1 Overview

RP2040/RP2350 have TWO CPU cores. Picomimi can use both:

- **Core 0**: Main kernel, shell, most tasks
- **Core 1**: Overflow tasks, real-time tasks

## 16.2 Core 1 Initialization

```cpp
void core1_init() {
    multicore_launch_core1(core1_entry);
}

void core1_entry() {
    while (true) {
        core1_scheduler_tick();
        
        if (kernel.core1.current_task < kernel.core1.task_count) {
            TCB* task = &kernel.core1.tasks[kernel.core1.current_task];
            if (task->entry) {
                task->entry();
            }
        }
    }
}
```

## 16.3 Cross-Core Locking

When both cores access shared data:

```cpp
mutex_t my_mutex;
mutex_init(&my_mutex);

mutex_enter_blocking(&my_mutex);
// ... access shared data ...
mutex_exit(&my_mutex);
```

---

# Part 17: GUI Engine

## 17.1 Overview

Picomimi includes a simple GUI engine for displays like the GC9A01A round LCD.

Features:
- Focus management (only one app receives input)
- Input routing
- Basic widgets

## 17.2 GUI Focus

```cpp
void gui_app_entry() {
    if (!Pico.RequestGUIFocus()) {
        return;  // Another app has focus
    }
    
    draw_menu();
    
    while (true) {
        if (Pico.ButtonPressed()) {
            handle_button();
        }
        Pico.Yield();
    }
    
    Pico.ReleaseGUIFocus();
}
```

---

# Part 18: Code Patterns and Idioms

## 18.1 The likely() and unlikely() Macros

Hint to compiler about branch probability:

```cpp
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

if (likely(ptr != nullptr)) {
    use_ptr(ptr);  // Expected case
}

if (unlikely(error_occurred)) {
    handle_error();  // Rare case
}
```

## 18.2 Early Return Pattern

```cpp
void process_task(TCB* task) {
    // Guard clauses - bail out early if invalid
    if (!task) return;
    if (task->state == TASK_TERMINATED) return;
    if (task->state == TASK_ZOMBIE) return;
    
    // Now we know task is valid
    // ... actual processing ...
}
```

## 18.3 Magic Numbers for Validation

```cpp
#define MEM_MAGIC 0xDEADBEEF

struct MemBlock {
    uint32_t magic;  // Must be MEM_MAGIC
    // ...
};

void validate_block(MemBlock* block) {
    if (block->magic != MEM_MAGIC) {
        kernel_panic("Memory corruption detected!");
    }
}
```

## 18.4 Ring Buffer Pattern

```cpp
#define LOG_BUFFER_SIZE 32

struct KernelLog {
    LogEntry entries[LOG_BUFFER_SIZE];
    uint32_t head;  // Next write position
    uint32_t count;
};

void klog(int level, const char* message) {
    LogEntry* entry = &kernel.log.entries[kernel.log.head];
    entry->timestamp = get_time_ms();
    entry->level = level;
    strncpy(entry->message, message, sizeof(entry->message));
    
    kernel.log.head = (kernel.log.head + 1) % LOG_BUFFER_SIZE;
    if (kernel.log.count < LOG_BUFFER_SIZE) {
        kernel.log.count++;
    }
}
```

## 18.5 Callback Registration Pattern

```cpp
typedef void (*oom_callback_t)(uint32_t bytes_requested);
oom_callback_t oom_handlers[MAX_TASKS] = {nullptr};

void k_register_oom_handler(uint32_t task_id, oom_callback_t handler) {
    if (task_id < MAX_TASKS) {
        oom_handlers[task_id] = handler;
    }
}
```

---

# Part 19: The Evolution of Picomimi

## 19.1 Version History

### v1-v5: The Beginning
- Simple cooperative scheduler
- Basic task switching
- No memory management

### v6-v9: Growing Up
- kmalloc/kfree implemented
- Shell added
- Basic filesystem

### v10-v12: Struggles
- Multiple failed architectures
- Sandbox system (ACE) - removed
- Memory corruption bugs

### v13: Stabilization
- Clean architecture
- Working OOM killer
- Reliable scheduling

### v14.1: Power Management
- CPU frequency governor
- Thermal throttling
- USB Serial stability fix

### v14.1.1: USB Fix
- Fixed Serial lockup after inactivity
- Governor clamping for USB
- WFI blocking when Serial active

### v14.3.1: Resource-Owning Kernel
- Hardware resource tracking
- Automatic cleanup on task death
- Zero-overhead design
- OOM considers resource hoarding

## 19.2 Lessons Learned

1. **Simple is better** - Complex systems fail in complex ways
2. **Test with real workloads** - Synthetic tests miss real bugs
3. **Memory is precious** - Every byte counts on MCU
4. **USB is fragile** - Respect the USB timing requirements
5. **Cooperative works** - Preemptive isn't always necessary
6. **Documentation matters** - Future-you will thank present-you

---

# Part 20: Extending Picomimi

## 20.1 Adding a New Shell Command

```cpp
// In shell_execute():
else if (strcmp(cmd, "mycommand") == 0) {
    cmd_mycommand();
}

// Implementation:
void cmd_mycommand() {
    kout.println("=== My Command ===");
    kout.println("Hello from my command!");
}

// Update help:
kout.println(" mycommand  - My new command");
```

## 20.2 Adding a New Resource Type

1. Add to `ResourceType` enum
2. Add descriptor array to `KernelState`
3. Initialize in `res_init()`
4. Add claim/release functions
5. Add cleanup in `res_cleanup_task()`
6. Add SDK API methods

## 20.3 Creating a Complete Application

```cpp
// Temperature Logger Example

#define LOG_INTERVAL_MS 1000
#define MAX_READINGS 100

struct TempReading {
    uint32_t timestamp;
    float temperature;
};

TempReading readings[MAX_READINGS];
uint32_t reading_count = 0;

void temp_logger_entry() {
    kout.println("[TempLogger] Starting...");
    
    int adc = Pico.ClaimADC(4);  // Internal temp sensor
    if (adc < 0) {
        kout.println("[TempLogger] Failed to claim ADC!");
        return;
    }
    
    while (true) {
        float temp = Pico.GetCPUTemp();
        
        // Store reading
        if (reading_count < MAX_READINGS) {
            readings[reading_count].timestamp = Pico.GetUptime();
            readings[reading_count].temperature = temp;
            reading_count++;
        }
        
        Pico.Sleep(LOG_INTERVAL_MS);
    }
    
    Pico.ReleaseADC(adc);
}

// OOM handler
void temp_logger_oom_handler(uint32_t bytes_needed) {
    reading_count = 0;  // Clear readings to free conceptual memory
    Pico.OOMCleanupDone(sizeof(readings));
}

void register_temp_logger() {
    uint32_t id = create_task("temp_logger", temp_logger_entry, TASK_PRIO_LOW);
    k_register_oom_handler(id, temp_logger_oom_handler);
}
```

---

# Appendix A: Quick Reference

## Data Types
```cpp
uint8_t   // 0 to 255
int8_t    // -128 to 127
uint16_t  // 0 to 65535
int16_t   // -32768 to 32767
uint32_t  // 0 to 4294967295
int32_t   // -2147483648 to 2147483647
bool      // true or false
```

## Common Functions
```cpp
memset(ptr, value, size);      // Fill memory
memcpy(dest, src, size);       // Copy memory
strlen(str);                    // String length
strcmp(a, b);                   // Compare strings
snprintf(buf, size, fmt, ...); // Safe formatted print
```

## Picomimi SDK Quick Reference
```cpp
// Task
Pico.Yield();
Pico.Sleep(ms);
Pico.Exit();

// Memory
void* ptr = Pico.Alloc(size);
Pico.Free(ptr);

// Resources
int pin = Pico.ClaimGPIO(25);
Pico.ReleaseGPIO(25);

// System
uint32_t ms = Pico.GetUptime();
float temp = Pico.GetCPUTemp();
```

---

# Appendix B: Glossary

| Term | Definition |
|------|------------|
| **TCB** | Task Control Block - struct containing all task info |
| **kmalloc** | Kernel memory allocation function |
| **kfree** | Kernel memory free function |
| **OOM** | Out of Memory |
| **IPC** | Inter-Process Communication |
| **PMFS** | Picomimi Micro Filesystem |
| **GPIO** | General Purpose Input/Output |
| **WFI** | Wait For Interrupt - CPU sleep instruction |
| **DMA** | Direct Memory Access |
| **PIO** | Programmable I/O (RP2040 feature) |
| **Governor** | CPU frequency management system |
| **Turbo** | Maximum performance mode |
| **Yield** | Voluntarily give up CPU |
| **Handle** | Opaque reference to a resource |
| **Coalescing** | Merging adjacent free memory blocks |
| **Compaction** | Moving blocks to eliminate fragmentation |

---

# Appendix C: Hardware Memory Map

## RP2040
```
0x00000000 - 0x003FFFFF: Flash XIP (16MB max)
0x20000000 - 0x20041FFF: SRAM (264KB)
0x40000000 - 0x4FFFFFFF: APB Peripherals
0x50000000 - 0x50FFFFFF: AHB Peripherals
```

## RP2350
```
0x00000000 - 0x007FFFFF: Flash XIP (up to 32MB)
0x20000000 - 0x20081FFF: SRAM (520KB)
0x40000000 - 0x4FFFFFFF: APB Peripherals
```

---

# The End

This documentation represents approximately 12,000 lines of code, months of development, and countless debugging sessions condensed into one guide.

Picomimi-AxisOS v14.3.1 "Quiet Otter" is frozen here. When you return to this project, you'll have everything you need to understand, modify, and extend it.

Good luck, and happy coding!

---

*Documentation generated for Picomimi-AxisOS v14.3.1*  
*"Not just an RTOS - a complete embedded distribution"*
