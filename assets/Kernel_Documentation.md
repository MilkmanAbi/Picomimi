# Picomimi Kernel v9.5 - Developer Documentation

## Overview

Picomimi is a priority-based preemptive multitasking kernel for the RP2040 microcontroller. It manages task scheduling, memory allocation, file systems (both RAM-based VFS and SD card), and a GUI abstraction layer for interactive applications.

**Version:** 9.5 (Task Termination Fix)  
**Hardware:** RP2040 (Raspberry Pi Pico)  
**Heap:** 180 KB  
**Max Tasks:** 32  

---

## Architecture

### Task System

The kernel supports five task types, each with different permissions and lifecycle behaviors:

#### Task Types
- **KERNEL** (0x01): Core system tasks (e.g., idle). Protected, cannot be killed, respawn on failure.
- **DRIVER** (0x02): Hardware abstraction (display, input). Protected, auto-respawn.
- **SERVICE** (0x04): System services (shell, VFS, FS). Protected, auto-respawn.
- **MODULE** (0x08): Extensions and utilities (watchdog, counter). Can be killed by OOM.
- **APPLICATION** (0x10): User programs (games, apps). Only type subject to OOM killing.

#### Task States
```
TASK_READY       → Can run (eligible for scheduler)
TASK_RUNNING     → Currently executing
TASK_WAITING     → Blocked (sleeping or waiting for event)
TASK_SUSPENDED   → Paused by user (applications only)
TASK_TERMINATED  → Dead (pending cleanup)
TASK_ZOMBIE      → Marked for cleanup
```

#### Task Flags
- `TASK_FLAG_PROTECTED` (0x01): Cannot be killed by normal kill command
- `TASK_FLAG_CRITICAL` (0x02): Protected from OOM killer
- `TASK_FLAG_RESPAWN` (0x04): Automatically restart on termination
- `TASK_FLAG_ONESHOT` (0x08): Only one instance allowed per 2-second window
- `TASK_FLAG_PERSISTENT` (0x10): Not destroyed on shutdown

### Scheduling

**Algorithm:** Priority-based round-robin with fairness  
**Quantum:** 1000 microseconds (1 ms)

The scheduler selects the highest-priority READY/RUNNING task, with round-robin among same-priority tasks. Lower priority numbers run first; higher numbers have higher priority.

```c
// Example priority levels
KERNEL idle:     0 (always runnable, lowest priority)
Services:        1-2
User apps:       5-7
Display driver:  8 (high priority)
Input driver:    9 (highest priority)
```

---

## Task Management API

### Entry Points: Callbacks vs Direct Entry

**⚠️ CRITICAL:** This is confusing by design. Understand both patterns:

#### Pattern 1: Direct Entry (Simple Tasks)
Use when your task is self-contained and doesn't need initialization/cleanup.

```c
void my_simple_task(void* arg) {
    // Runs once per scheduler tick, returns quickly
    do_some_work();
    task_sleep(100);  // Always end with sleep
}

task_create("simple", my_simple_task, NULL, 5,
            TASK_TYPE_APPLICATION, 0, 0,
            OOM_PRIORITY_NORMAL, 4096, 
            NULL,  // ← No callbacks
            "Simple app");
```

**Pros:** Straightforward, minimal overhead  
**Cons:** No init/deinit hooks, all logic in one function

#### Pattern 2: Callbacks (GUI Apps, Complex Tasks)
Use when you need per-task state, initialization, or cleanup.

```c
struct MyAppState {
    int value;
    bool initialized;
};

MyAppState* app_state = NULL;

void my_app_init(uint32_t id) {
    // Called ONCE at task creation, in creator's context
    // Receives NEW task's ID (not creator's!)
    app_state = (MyAppState*)kmalloc(sizeof(MyAppState), id);
    if (!app_state) {
        kout.println("Init failed!");
        return;  // Task will terminate
    }
    app_state->initialized = true;
}

void my_app_tick(void* arg) {
    // Called repeatedly by scheduler, in task's own context
    if (!app_state || !app_state->initialized) {
        task_sleep(100);
        return;
    }
    
    do_work();
    task_sleep(50);
}

void my_app_deinit() {
    // Called ONCE on termination, in task's context
    if (app_state) {
        kfree(app_state);
        app_state = NULL;
    }
}

ModuleCallbacks my_app_callbacks = {
    .init = my_app_init,
    .tick = my_app_tick,
    .deinit = my_app_deinit
};

task_create("myapp", NULL, NULL, 6,  // ← entry is NULL
            TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 30000,
            OOM_PRIORITY_NORMAL, 10 * 1024,
            &my_app_callbacks,  // ← Callbacks provided
            "Complex app");
```

**Pros:** Clean state management, proper initialization/cleanup  
**Cons:** Slightly more complex, but essential for GUI/persistent state

#### ⚠️ Init Callback Gotcha (v9.4 Bug)

```c
void my_init(uint32_t id) {
    // 'id' is the NEW task's ID, NOT the current task!
    // Use 'id' for memory allocation:
    void* ptr = kmalloc(1024, id);  // ✓ Correct: allocate for new task
    
    // NOT this:
    // void* ptr = kmalloc(1024, kernel.current_task);  // ✗ Wrong!
    // The current_task is still the shell/creator at init time
}
```

**Why:** Init runs in the creator's context, so `kernel.current_task` still points to the creator. The `id` parameter is the real owner for memory tracking.

### Creating Tasks

```c
uint32_t task_create(
    const char* name,
    void (*entry)(void*),
    void* arg,
    uint8_t priority,
    uint8_t task_type,
    uint32_t flags,
    uint64_t max_runtime_ms,
    uint8_t oom_priority,
    uint32_t mem_limit,
    ModuleCallbacks* callbacks,
    const char* description
);
```

**Parameters:**
- `name`: Task identifier (max 24 chars)
- `entry`: Main function (NULL if using callbacks; scheduler calls callbacks->tick instead)
- `arg`: Argument passed to entry/tick
- `priority`: 0-255 (higher = higher priority)
- `task_type`: One of TASK_TYPE_* constants
- `flags`: Bitfield of TASK_FLAG_*
- `max_runtime_ms`: Kill task if it runs longer (0 = no limit)
- `oom_priority`: OOM_PRIORITY_NEVER/CRITICAL/HIGH/NORMAL/LOW (applications only)
- `mem_limit`: Max heap this task can allocate (0 = no limit); kmalloc returns NULL if exceeded
- `callbacks`: Pointer to init/tick/deinit handlers (NULL for basic direct-entry tasks)
- `description`: Human-readable purpose

**Returns:** Task ID (0 reserved for idle) or 0 on failure

### Task Control

```c
void task_sleep(uint32_t ms);              // Sleep for ms milliseconds
void task_yield();                          // Yield to scheduler
void brutal_task_kill(uint32_t id);         // Kill task and free resources
```

### Task Lifecycle with Callbacks

```c
struct ModuleCallbacks {
    void (*init)(uint32_t id);   // Called once at task creation (with task ID)
    void (*tick)(void*);         // Called repeatedly by scheduler
    void (*deinit)();            // Called on termination
};
```

**Important:** The `init` callback runs in the context of the creating task (usually shell) but receives the NEW task's ID. Use this ID for memory allocation and GUI focus requests.

---

## Memory Management

### Allocation

```c
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);
```

**Features:**
- First-fit allocation with automatic block splitting
- Aligned to 4-byte boundaries
- Per-task memory tracking (used, peak, limit)
- Automatic memory compaction on failed allocation
- OOM killer as last resort

### Memory Queries

```c
size_t get_free_memory();           // Free heap in bytes
size_t get_used_memory();           // Used heap in bytes
size_t get_task_memory(uint32_t id);// Memory owned by task
```

### Memory Compaction

```c
void mem_compact();                 // Merge adjacent free blocks
void calculate_fragmentation();     // Update fragmentation stats
```

---

## Out-of-Memory (OOM) Killer

**Policy:** Only APPLICATIONS can be killed by OOM. System tasks are protected.

**OOM Priority Levels:**
- Level 0 (NEVER): Never kill this task
- Level 1 (CRITICAL): Kill as last resort only
- Level 2 (HIGH): High likelihood of being killed
- Level 3 (NORMAL): Standard priority (default)
- Level 4 (LOW): Kill this first

**Selection:** OOM killer selects highest OOM priority task with largest memory footprint.

```c
void oom_killer();                  // Manual trigger (automatic on malloc failure)
```

---

## Error Handling Patterns

### Memory Allocation Failures

```c
// What happens: kmalloc returns NULL if mem_limit exceeded
void* ptr = kmalloc(1024, kernel.current_task);

if (!ptr) {
    // Option 1: Graceful degradation (recommended)
    kout.println("Memory low, using fallback");
    use_smaller_buffer();  // Use pre-allocated or stack buffer
    
    // Option 2: Exit gracefully
    klog(1, "Task: Memory allocation failed");
    task_sleep(10000);  // Let scheduler clean us up
    
    // Option 3: Nuclear (kills entire task)
    // brutal_task_kill(kernel.current_task);  // Last resort only!
}
```

**Key point:** Allocation failure is NOT an OOM killer trigger. Your task just gets NULL. The OOM killer only runs when malloc can't find ANY free space after compaction.

### GUI Focus Failures

```c
if (!my_app_data->ui.request_focus(kernel.current_task)) {
    // Focus denied: another app has it
    
    // Option 1: Wait and retry
    task_sleep(500);
    // Next tick, try again
    
    // Option 2: Accept no GUI and do text output
    kout.println("Running in text mode (no GUI available)");
    // Fall back to serial output, continue working
    
    // Option 3: Exit (only if GUI is critical)
    brutal_task_kill(kernel.current_task);
}
```

**Pattern:** Don't immediately kill. GUI focus conflicts are normal when users cycle between apps.

### File Operations

```c
int fd = fs_open("/data.txt", false);  // false = read mode

if (fd < 0) {
    kout.println("File not found");
    // File operations are optional; decide how critical this is
    
    if (CRITICAL_FILE) {
        // Abort task
        task_sleep(10000);
    } else {
        // Continue without the file
        kout.println("Continuing with defaults");
    }
} else {
    char buf[256];
    fs_read_str(fd, buf, sizeof(buf));
    fs_close(fd);
}
```

---

## GUI System (UISocket API)

### Overview

The GUI system enforces a **single-focus model**: only one application can have exclusive display control at a time. GUI apps register via the UISocket API and must properly acquire/release focus.

### GUI State Machine

The GUI system has three states, controlled by `current_ui_mode`:

```
TERMINAL
  ↓ (BTN_ONOFF press)
KEYBOARD
  ↓ (BTN_ONOFF press, or return from app)
APPLICATION (GUI app has focus)
  ↓ (BTN_ONOFF press)
TERMINAL
```

#### State Responsibilities

**UI_TERMINAL:** Display task renders text history. Input task polls buttons for scrolling.

**UI_KEYBOARD:** Display task shows virtual keyboard. Input task handles BTN_A/B/START/SELECT for text entry.

**UI_APPLICATION:** Display task does nothing. Input task does nothing (except check BTN_ONOFF). The focused app's task handles all drawing and input polling via UISocket.

#### Focus Management

```c
// Only ONE task can have focus at a time
int32_t kernel.gui_focus_task_id;  // Task ID with display control, -1 = none
```

When an app requests focus:
1. If another app has focus, kernel kills the old app
2. Sets `gui_focus_task_id` to new app's ID
3. Switches `current_ui_mode` to UI_APPLICATION
4. Display task stops rendering terminal

**Important:** The app must check `kernel.gui_focus_task_id == kernel.current_task` every tick. If false, the user cycled away (or you were killed). Release resources and return immediately.

### Race Conditions & Button Mashing

```c
// Race condition 1: User toggles BTN_ONOFF mid-draw
void my_app_task(void* arg) {
    // We have focus at tick start
    if (kernel.gui_focus_task_id != kernel.current_task) {
        // Lost focus! Maybe user pressed BTN_ONOFF
        has_focus = false;
        task_sleep(200);
        return;  // Don't draw
    }
    
    // Now safe to draw (focus is ours for this entire tick)
    my_app_data->ui.fill_rect(0, 0, 320, 240, ILI9341_BLACK);
}

// Kernel guarantee: if gui_focus_task_id == your_id at tick start,
// you keep focus for the ENTIRE tick duration. No surprise kills mid-tick.
```

```c
// Race condition 2: Another app steals focus while you're initializing
void my_app_init(uint32_t id) {
    // Init runs, nothing can steal focus yet (we're not scheduled)
    my_app_data = (MyAppData*)kmalloc(..., id);
}

void my_app_task(void* arg) {
    // First tick: request focus
    if (!my_app_data->has_focus) {
        if (!my_app_data->ui.request_focus(kernel.current_task)) {
            // Someone already has focus
            task_sleep(200);  // Back off and retry
            return;
        }
        my_app_data->has_focus = true;
    }
    
    // Subsequent ticks: just verify we still have it
    if (kernel.gui_focus_task_id != kernel.current_task) {
        has_focus = false;
        task_sleep(200);
        return;
    }
    
    // Safe to draw
}

// Best practice: Request focus once, then just verify each tick.
// If you lose it, gracefully yield.
```

### UISocket Structure

```c
struct UISocket {
    bool (*request_focus)(uint32_t task_id);
    void (*release_focus)(uint32_t task_id);
    bool (*get_button_state)(UIButton button);
    void (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void (*draw_text)(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size, bool center);
    void (*clear_screen)();
};

enum UIButton {
    UI_BTN_TOP = 0,
    UI_BTN_RIGHT = 1,
    UI_BTN_BOTTOM = 2,
    UI_BTN_LEFT = 3,
};
```

### Registering a GUI Application

**Step 1:** Add init callback to populate UISocket

```c
void my_app_init(uint32_t id) {
    my_app_data = (MyAppData*)kmalloc(sizeof(MyAppData), id);
    if (!my_app_data) return;
    
    // Register and get the API socket
    k_register_gui_app(&my_app_data->ui);
    
    klog(0, "MYAPP: Initialized");
}
```

**Step 2:** Request focus in tick function

```c
void my_app_task(void* arg) {
    if (!my_app_data) {
        brutal_task_kill(kernel.current_task);
        task_sleep(10000);
        return;
    }
    
    // Acquire focus (killing any other GUI app)
    if (!my_app_data->has_focus) {
        if (!my_app_data->ui.request_focus(kernel.current_task)) {
            kout.println("[MYAPP] Could not get focus!");
            brutal_task_kill(kernel.current_task);
            task_sleep(10000);
            return;
        }
        my_app_data->has_focus = true;
        my_app_data->ui.clear_screen();
    }
    
    // Check we still have focus
    if (kernel.gui_focus_task_id != kernel.current_task) {
        my_app_data->has_focus = false;
        task_sleep(200);
        return;
    }
    
    // Now safe to draw
    my_app_data->ui.fill_rect(0, 0, 320, 240, ILI9341_BLACK);
    my_app_data->ui.draw_text(160, 120, "Hello", ILI9341_WHITE, 2, true);
    
    // Poll input
    if (my_app_data->ui.get_button_state(UI_BTN_TOP)) {
        // Handle button press
    }
    
    task_sleep(50);
}
```

**Step 3:** Release focus and cleanup in deinit

```c
void my_app_deinit() {
    if (my_app_data) {
        if (kernel.gui_focus_task_id == kernel.current_task) {
            my_app_data->ui.release_focus(kernel.current_task);
        }
        kfree(my_app_data);
        my_app_data = NULL;
    }
}
```

### GUI API Reference

```c
// Request exclusive display control
bool request_focus(uint32_t task_id);

// Release display control
void release_focus(uint32_t task_id);

// Read button state (true = pressed)
bool get_button_state(UIButton button);

// Fill rectangle with color
void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// Draw text (center = center by width/height)
void draw_text(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size, bool center);

// Clear screen to black
void clear_screen();
```

### Color Constants
- `ILI9341_BLACK` (0x0000)
- `ILI9341_WHITE` (0xFFFF)
- `ILI9341_RED` (0xF800)
- `ILI9341_GREEN` (0x07E0)
- `ILI9341_BLUE` (0x001F)
- `ILI9341_YELLOW` (0xFFE0)
- `ILI9341_CYAN` (0x07FF)

### Display Properties
- Resolution: 320×240 pixels
- Rotation: 3 (landscape)
- Character cell: 6×8 pixels (1 text size)

---

## Porting Arduino Sketches to Picomimi

### What Stays the Same

Most Arduino APIs work unchanged:
- `digitalWrite()`, `digitalRead()`, `pinMode()`
- `Serial.print()`, `Serial.println()`
- `millis()`, `micros()`, `delay()`, `delayMicroseconds()`
- `analogRead()`, `analogWrite()`
- SPI, Wire, etc. (though shared resources can conflict)

### What Changes

#### 1. No `setup()` / `loop()` Structure

**Before (Arduino):**
```c
void setup() {
    Serial.begin(115200);
    pinMode(13, OUTPUT);
}

void loop() {
    digitalWrite(13, HIGH);
    delay(500);
    digitalWrite(13, LOW);
    delay(500);
}
```

**After (Picomimi):**
```c
void my_blink_task(void* arg) {
    // This IS your loop, called repeatedly by scheduler
    digitalWrite(13, HIGH);
    task_sleep(500);  // Use task_sleep, not delay!
    
    digitalWrite(13, LOW);
    task_sleep(500);
    
    // Return, scheduler calls us again next time
}

void my_blink_init(uint32_t id) {
    // One-time setup goes here (like setup())
    Serial.begin(115200);
    pinMode(13, OUTPUT);
}

ModuleCallbacks blink_callbacks = {
    .init = my_blink_init,
    .tick = my_blink_task,
    .deinit = NULL
};

// In shell command handler or startup:
task_create("blink", NULL, NULL, 5,
            TASK_TYPE_APPLICATION, 0, 0,
            OOM_PRIORITY_NORMAL, 2 * 1024,
            &blink_callbacks,
            "Blinky");
```

**Key difference:** Your loop returns after each operation. The scheduler calls it again later. Never spin-wait; always call `task_sleep()` or `task_yield()`.

#### 2. Use `task_sleep()` Instead of `delay()`

```c
// ✗ Wrong: Blocks entire kernel for 1 second
delay(1000);

// ✓ Correct: Just puts this task to sleep
task_sleep(1000);
```

Why: `delay()` blocks interrupts on many boards and prevents other tasks from running. `task_sleep()` cooperates with the scheduler.

#### 3. Per-Task State via Callbacks

**Before (Arduino):**
```c
int counter = 0;

void setup() {}

void loop() {
    counter++;
    Serial.println(counter);
    delay(1000);
}
```

**After (Picomimi):**
```c
struct CounterState {
    int value;
};

CounterState* counter_state = NULL;

void counter_init(uint32_t id) {
    counter_state = (CounterState*)kmalloc(sizeof(CounterState), id);
    if (!counter_state) return;
    counter_state->value = 0;
}

void counter_task(void* arg) {
    if (!counter_state) {
        task_sleep(100);
        return;
    }
    
    counter_state->value++;
    kout.print("Count: ");
    kout.println(counter_state->value);
    
    task_sleep(1000);
}

void counter_deinit() {
    if (counter_state) {
        kfree(counter_state);
        counter_state = NULL;
    }
}
```

This pattern lets multiple instances of the "same" app run independently (each with their own state).

#### 4. Memory Constraints

```c
// Before (Arduino): Global array, fixed at compile time
char buffer[1024];

// After (Picomimi): Allocate at runtime, constrained by mem_limit
void my_task_init(uint32_t id) {
    my_state->buffer = (char*)kmalloc(1024, id);
    if (!my_state->buffer) {
        kout.println("Buffer alloc failed");
    }
}
```

Check your task's `mem_limit` when designing. Default apps use 4-50 KB.

#### 5. Avoid Global State

**Before (Arduino - usually OK):**
```c
int global_counter = 0;

void loop() {
    global_counter++;
}
```

**After (Picomimi - problematic):**
```c
// If you run this app twice, both instances share global_counter!
// Instead, use per-task state:

struct AppState {
    int counter;
};
// Each task gets its own AppState via kmalloc
```

### Minimal Porting Checklist

- [ ] Convert `setup()` → init callback
- [ ] Convert `loop()` → tick callback, add `task_sleep()` at end
- [ ] Replace `delay()` with `task_sleep()`
- [ ] Move global state to struct, allocate in init
- [ ] For GUI: use `UISocket` API instead of direct display calls
- [ ] Handle `kmalloc()` failures gracefully
- [ ] Test with `mem_limit` set low to catch allocation issues
- [ ] Always return from tick; never infinite loop

### Example: Porting a Simple Sensor Reader

**Arduino version:**
```c
void setup() {
    Serial.begin(115200);
}

void loop() {
    int reading = analogRead(A0);
    Serial.print("Sensor: ");
    Serial.println(reading);
    delay(1000);
}
```

**Picomimi version:**
```c
void sensor_task(void* arg) {
    int reading = analogRead(A0);
    kout.print("Sensor: ");
    kout.println(reading);
    task_sleep(1000);
}

task_create("sensor", sensor_task, NULL, 5,
            TASK_TYPE_APPLICATION, 0, 0,
            OOM_PRIORITY_NORMAL, 2 * 1024,
            NULL,  // No callbacks needed
            "Sensor reader");
```

---

## Hardware: SPI Bus Contention & UI Modes

### SPI Sharing: Display vs SD Card

Both the ILI9341 display and SD card use SPI on the RP2040. They share `SCK` (GPIO 18) and `MOSI` (GPIO 19), but have separate `CS` pins:

```
SCK  (GPIO 18) – shared – both devices
MOSI (GPIO 19) – shared – both devices
MISO (GPIO 16) – SD card only (LCD doesn't use MISO)
CS   (GPIO 5)  – SD card only
CS   (GPIO 21) – Display only
```

#### How the Kernel Handles This

The kernel does NOT automatically arbitrate SPI access. **You must not call SPI functions simultaneously from different tasks.**

**Safe patterns:**
- Display task and FS task run in sequence (different tasks), so kernel's scheduler prevents overlap
- Both use `save_and_disable_interrupts()` / `restore_interrupts()` around SPI operations
- Arduino's SD library internally handles CS, so no direct conflict

**Unsafe pattern:**
```c
// ✗ Don't do this:
void my_task(void* arg) {
    fs_write_str(fd, "data");  // SPI access
    // ...
    tft.fillRect(...);         // SPI access (if display task also runs, race!)
}
```

**Safe pattern:**
```c
// ✓ Correct: Either use FS, or use display, but not both in same tick:
void my_task(void* arg) {
    // Option A: Just use display
    if (kernel.gui_focus_task_id == kernel.current_task) {
        my_ui->fill_rect(0, 0, 100, 100, ILI9341_BLUE);
    }
    
    // Option B: Just use FS
    if (my_need_file_data) {
        fs_read_str(fd, buffer, 256);
    }
    
    // Not both in same tick
    task_sleep(50);
}
```

**Current behavior:** If you violate this, data corruption or hang is likely. Future versions may add an SPI mutex, but for now, discipline is required.

### Terminal/UI Mode Switching

Three UI modes govern what gets displayed and which inputs are processed:

#### UI_TERMINAL (Default)
```
Display:     Text history (terminal output)
Input:       BTN_TOP/BOTTOM for scrolling, BTN_ONOFF to switch modes
Processing:  Shell task reads Serial, Display task renders history
```

When active:
- `display_task()` renders the scrollback history
- `input_task()` polls buttons for scroll up/down
- `shell_task()` reads from Serial and executes commands
- **Display is shared:** Any `kout.println()` adds to history

#### UI_KEYBOARD
```
Display:     Virtual keyboard + input line
Input:       BTN_A/B/START/SELECT for text entry, BTN_ONOFF to switch modes
Processing:  Display task renders keyboard, Input task handles key presses
```

When active:
- `display_task()` draws the virtual keyboard and input buffer
- `input_task()` converts button presses to characters
- **No shell command execution** until user presses ENTER
- Pressing BTN_SELECT + button sequence selects characters

#### UI_APPLICATION
```
Display:     App's exclusive control (blank except app drawing)
Input:       App polls buttons via UISocket API, BTN_ONOFF exits app
Processing:  App's task controls display, Input task watches BTN_ONOFF only
```

When active:
- `display_task()` does nothing (screen is app's to control)
- `input_task()` does nothing except check BTN_ONOFF
- `kernel.gui_focus_task_id` points to app's task ID
- **App is solely responsible** for all drawing; no interference from kernel

#### Mode Transitions

```
User presses BTN_ONOFF:

TERMINAL ──→ KEYBOARD ──→ APPLICATION ──→ TERMINAL (cycle)

If no app running:
TERMINAL ──→ KEYBOARD ──→ TERMINAL (skip APPLICATION)
```

#### What Your App Sees

```c
void my_app_task(void* arg) {
    // You only run when current_ui_mode == UI_APPLICATION
    // and kernel.gui_focus_task_id == kernel.current_task
    
    // If user presses BTN_ONOFF:
    // 1. Input task detects it
    // 2. Kernel calls k_release_gui_focus(your_id)
    // 3. Your deinit is called
    // 4. Next scheduler run: you're TERMINATED, skipped
    // 5. Terminal mode resumes
}
```

**Important:** If you lose focus, you're being killed. Don't try to keep running or draw. Your deinit cleanup WILL run, even if you're mid-drawing.

---

## File Systems

### Virtual File System (VFS) - RAM-Based

VFS stores files in flash memory with fragmented block allocation.

#### Commands
```
vfscreate           Create and mount VFS
vfsls               List all files
vfsstat             Show statistics
vfsmkfile <n> <t>   Create file (name, type)
vfsrm <id>          Delete file
vfscat <id>         Read and display file
vfsdedicate         Save all files to SD card
```

#### VFS API
```c
void vfs_init();                            // Initialize VFS
void vfs_format();                          // Format filesystem
bool vfs_mount();                           // Mount filesystem
void vfs_unmount();                         // Unmount filesystem

int vfs_create(const char* name, uint8_t type, uint32_t owner_id);
int vfs_write(int fd, const void* data, uint32_t size);
int vfs_read(int fd, void* buffer, uint32_t size);
void vfs_delete(int fd);

void vfs_list();                            // Show contents
void vfs_stats();                           // Show statistics
```

#### File Types
- FILE_TYPE_TEXT (0x01)
- FILE_TYPE_LOG (0x02)
- FILE_TYPE_DATA (0x03)
- FILE_TYPE_CONFIG (0x04)

#### Constraints
- Max files: 16
- Max filename: 16 chars
- Max file size: 16 KB
- Total storage: 128 KB
- Max blocks per file: 64 (fragmented)

### SD Card File System (FS)

Provides standard SD card access via Arduino SD library.

#### Commands
```
ls [path]           List directory
stat                Show statistics
mkdir <path>        Create directory
rm <path>           Delete file
cat <path>          Read file contents
write <path> <text> Write text to file
logcat              View system error log
```

#### FS API
```c
void fs_init();                             // Initialize SD card
bool fs_mount();                            // Mount filesystem
void fs_unmount();                          // Unmount filesystem

bool fs_exists(const char* path);
bool fs_mkdir(const char* path);
bool fs_remove(const char* path);
void fs_list(const char* path);
void fs_stats();

int fs_open(const char* path, bool write_mode);
void fs_close(int fd);
int fs_write_str(int fd, const char* data);
int fs_read_str(int fd, char* buffer, size_t size);
void fs_cat(const char* path);

void fs_log_init();                         // Initialize error log
void fs_log_write(const char* message);     // Append to log
```

#### Constraints
- Max open files: 8
- Max filename: 32 chars
- Max card size: 4 GB
- Requires 400 kHz SPI for initialization

---

## Logging System

### Basic Logging

```c
void klog(uint8_t level, const char* msg);

// Levels:
// 0 = INFO
// 1 = WARN
// 2 = ERROR
// 3 = CRITICAL
```

### Log Queries

```c
// Kernel log buffer (last 40 entries)
LogEntry kernel.log[MAX_LOG_ENTRIES];
uint32_t kernel.log_count;
uint32_t kernel.log_head;

// dmesg command shows system log
```

### Error Log (SD Card)

If FS is mounted, level 2+ logs are persisted to `/LogRecord` on SD card with automatic numbering.

---

## Command Shell

### Built-in Commands

**System:**
- `help` - Show command list
- `ps` - List all tasks
- `top` - System resources
- `uptime` - System runtime
- `temp` - CPU temperature
- `dmesg` - System log
- `clear` - Clear screen
- `reboot` - Restart system
- `shutdown` - Safe shutdown

**Task Management:**
- `listapps` - List applications only
- `listmods` - List modules only
- `kill <id>` - Kill application
- `suspend <id>` - Pause application
- `resume <id>` - Resume application
- `root` - Toggle root mode
- `root kill <id>` - Force kill any task

**Memory:**
- `mem` - Memory stats
- `memmap` - Memory block details
- `compact` - Compact memory

**Architecture:**
- `arch` - System architecture overview
- `oom` - OOM killer info

**Applications:**
- `snake` - Snake game (GUI)
- `calc` - Calculator
- `clock` - Digital clock
- `sysmon` - System monitor
- `memhog` - Memory stress test
- `cpuburn` - CPU stress test
- `stress` - Full system stress

---

## Quick Start: Getting Started

### Setup & Compilation

**Hardware Required:**
- RP2040 board (Raspberry Pi Pico or compatible)
- ILI9341 SPI display (320×240)
- SD card module (SPI)
- 9 push buttons or tactile switches
- Wiring per pin definitions (below)

**Software Setup:**

1. **Install Arduino IDE** (1.8.13+)

2. **Install RP2040 Board Support:**
   - File → Preferences → Additional Board Manager URLs
   - Add: `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
   - Tools → Board Manager → Search "pico" → Install "Raspberry Pi Pico/RP2040"

3. **Install Libraries:**
   - Sketch → Include Library → Manage Libraries
   - Search and install:
     - `Adafruit GFX Library` (display graphics)
     - `Adafruit ILI9341` (display driver)
     - `SD` (SD card, included with RP2040 board package)

4. **Select Board:**
   - Tools → Board: "Raspberry Pi Pico"
   - Tools → Flash Size: "2MB (Sketch: 1MB, SPIFFS: None)"
   - Tools → USB Stack: "Dual Serial"
   - Tools → CPU Speed: "125 MHz"

5. **Copy Picomimi.ino** and compile:
   - Sketch → Upload (or Ctrl+U)
   - Monitor serial output at 115200 baud

### First App: Hello World

```c
void hello_init(uint32_t id) {
    kout.println("\n[HELLO] App started!");
}

void hello_task(void* arg) {
    static uint32_t count = 0;
    kout.print("Tick: ");
    kout.println(count++);
    task_sleep(1000);
}

void hello_deinit() {
    kout.println("[HELLO] App ended");
}

ModuleCallbacks hello_callbacks = {
    .init = hello_init,
    .tick = hello_task,
    .deinit = hello_deinit
};

void hello_spawn() {
    uint32_t tid = task_create("hello", NULL, NULL, 5,
                               TASK_TYPE_APPLICATION,
                               TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL,
                               2 * 1024,
                               &hello_callbacks,
                               "Hello world app");
    if (tid > 0) kout.println("Hello app spawned");
}
```

Add to `shell_execute()` command handler:
```c
else if (strcmp(cmd, "hello") == 0) {
    extern void hello_spawn();
    hello_spawn();
}
```

Run on device: Type `hello` in terminal. See ticks printed every second.

---

## Memory Budget Recommendations

### Per-Task Type

| Type | Typical | Min | Max |
|------|---------|-----|-----|
| Idle (Kernel) | 256 B | 256 B | 1 KB |
| Display Driver | 4 KB | 2 KB | 8 KB |
| Input Driver | 2 KB | 1 KB | 4 KB |
| Shell Service | 4 KB | 2 KB | 8 KB |
| VFS Service | 2 KB | 1 KB | 4 KB |
| FS Service | 4 KB | 2 KB | 8 KB |
| Watchdog Module | 2 KB | 1 KB | 4 KB |
| **Simple App** | 4 KB | 2 KB | 16 KB |
| **GUI App (Snake)** | 10 KB | 4 KB | 32 KB |
| **Stress Test** | 50 KB | 20 KB | 100 KB |

### Total Heap Budget

Total heap: **180 KB**

**Recommended allocation:**
- Kernel tasks (idle, drivers, services): ~30 KB (reserved)
- Modules: ~10 KB
- **Applications: ~140 KB available** (can run multiple small apps or one large one)

If you create an app with `mem_limit = 50 * 1024` (50 KB), you're using 28% of application heap. Running 2-3 such apps simultaneously leaves buffer for others.

### What Counts Toward Limit

- All `kmalloc()` calls in your task
- State structs allocated in init
- Buffers for file I/O, display manipulation, etc.
- **Does NOT count:** Stack variables (they're on each task's minimal stack)

### Checking Your App's Memory Use

```c
// In shell:
ps               // Shows mem_used and mem_peak per task
mem              // Shows fragmentation, largest free block

// In your app:
TCB* me = &kernel.tasks[kernel.current_task];
kout.print("My memory: ");
kout.print(me->mem_used / 1024);
kout.println(" KB");
```

If `mem_used` approaches `mem_limit`, kmalloc will start returning NULL.

---

## Memory Limit Behavior

### What Happens When Task Hits mem_limit

```c
void my_task_init(uint32_t id) {
    my_state = (State*)kmalloc(sizeof(State), id);
    if (!my_state) {
        // This task was created with mem_limit=2KB
        // and init tried to allocate more
        kout.println("Init OOM!");
        return;  // Task becomes zombie, never runs
    }
}
```

**Behavior:**

1. **During init:** If init's allocs fail, `my_state` is NULL. The task is created but never successfully initialized. Its first tick sees NULL and should abort gracefully.

2. **During tick:** If malloc fails:
   ```c
   void* ptr = kmalloc(1024, kernel.current_task);
   if (!ptr) {
       // Task has hit its mem_limit
       // OOM killer is NOT triggered for this
       // You must handle the failure
       kout.println("Can't allocate more!");
   }
   ```

3. **After mem_limit exceeded:**
   ```c
   // malloc returns NULL
   // Task keeps running but can't allocate
   // Task is NOT killed (unlike system-wide OOM)
   // Task is NOT forcibly freed
   // Task must gracefully degrade
   ```

**Key point:** `mem_limit` is a **soft limit**. It prevents one app from starving others, but the app must cooperate. If the app ignores NULL returns and crashes trying to use NULL pointers, that's the app's bug.

### System-Wide OOM (Different From mem_limit)

System-wide OOM occurs when the entire 180 KB heap is exhausted:

```c
// If NO free blocks exist anywhere in heap:
// 1. Kernel calls mem_compact()
// 2. If compaction doesn't free 4KB+, kernel calls oom_killer()
// 3. oom_killer() kills highest-priority APPLICATION
// 4. Task's memory is freed
// 5. malloc retries
```

**oom_killer only kills APPLICATIONS** (not drivers, services, kernel).

---

## Common Patterns & Anti-Patterns

### ✓ Pattern: Defensive Allocation

```c
void my_task(void* arg) {
    char* buffer = (char*)kmalloc(512, kernel.current_task);
    if (!buffer) {
        kout.println("No memory, skipping operation");
        task_sleep(1000);
        return;  // Graceful degrade
    }
    
    // Use buffer
    kfree(buffer);
    task_sleep(100);
}
```

### ✗ Anti-Pattern: Assuming Success

```c
void my_task(void* arg) {
    char* buffer = (char*)kmalloc(512, kernel.current_task);
    strcpy(buffer, "hello");  // ✗ buffer might be NULL!
    
    // Crashes if malloc failed
}
```

### ✓ Pattern: GUI Focus Check Every Tick

```c
void my_gui_app(void* arg) {
    if (kernel.gui_focus_task_id != kernel.current_task) {
        has_focus = false;
        task_sleep(100);
        return;  // Don't draw if we don't have focus
    }
    
    // Safe to draw
    my_ui->fill_rect(0, 0, 100, 100, ILI9341_GREEN);
    task_sleep(50);
}
```

### ✗ Anti-Pattern: Assuming Focus Is Stable

```c
void my_gui_app(void* arg) {
    // Assume if we got focus once, we have it forever
    my_ui->fill_rect(0, 0, 100, 100, ILI9341_GREEN);  // ✗ Might lose focus!
    
    // If user pressed BTN_ONOFF, we're being killed mid-draw
    // Causes memory corruption
}
```

### ✓ Pattern: Tick Returns Quickly

```c
void my_task(void* arg) {
    do_small_piece_of_work();  // < 10ms of work
    task_sleep(50);  // Yield back to scheduler
}
```

### ✗ Anti-Pattern: Blocking in Tick

```c
void my_task(void* arg) {
    for (int i = 0; i < 1000000; i++) {
        // Spin loop, burns CPU, starves other tasks
        calculate_stuff();
    }
    task_sleep(50);
}
```

### ✓ Pattern: Respect max_runtime

When creating stress tests or long-running tasks:

```c
task_create("compute", compute_task, NULL, 3,
            TASK_TYPE_APPLICATION, 0,
            60000,  // ← Kill after 60 seconds
            OOM_PRIORITY_LOW,
            20 * 1024,
            NULL,
            "Computation");
```

If task runs longer, kernel forcibly kills it.

---

## Decision Trees

### When to Use Callbacks vs Direct Entry?

```
Does your app need:
  - Per-task state (local variables that persist)?
  - Initialization/cleanup hooks?
  - GUI focus management?
    → YES to any? Use callbacks
    → NO to all?  Direct entry is OK (but callbacks still work)
```

### When to Use VFS vs FS?

```
VFS (128 KB RAM):
  - Fast (no SD latency)
  - Data lost on reboot
  - Small files (max 16 KB each)
  - Use for: Temp data, caches, logs

FS (SD Card):
  - Persistent (survives reboot)
  - Slower (SPI latency ~10ms per operation)
  - Large storage (up to 4 GB)
  - Use for: Config files, user data, long-term logs
```

### When to Use task_sleep() vs task_yield()?

```c
task_yield();      // I'm done this tick, pick another task
                   // Immediate, no wait
                   // Use when: task has nothing to do right now

task_sleep(100);   // Sleep for 100ms, then wake up
                   // Use when: task is idle and will wake at specific time
                   // More efficient than busy-yielding
```

### When to Set mem_limit?

```
No mem_limit (0):
  - Task can use all available heap
  - Risk: One bad app crashes system
  - Use for: Trusted system tasks

mem_limit = 10KB:
  - Task limited to 10 KB of heap
  - Safer for untrusted apps
  - Use for: User-loaded applications

mem_limit = 50KB:
  - Large apps (games, complex tools)
  - Still protects rest of system
  - Use for: GUI apps that might need graphics buffers
```

---

## Troubleshooting

### "App starts but won't draw"

```
Checklist:
1. Is kernel.gui_focus_task_id == kernel.current_task?
   → If NO: You don't have focus (call request_focus() again)
   → If YES: Continue to step 2

2. Did you call ui.clear_screen() after getting focus?
   → If NO: Old terminal text still visible, try drawing anyway
   → If YES: Continue to step 3

3. Are you drawing to 0,0 at 320x240?
   → If NO: Drawing off-screen
   → If YES: Continue to step 4

4. Is LCD_LED (GPIO 22) high?
   → If LOW: Display is powered off
   → Check setup() sets digitalWrite(LCD_LED, HIGH)
```

### "SD card detected on boot but not after reset"

This is a known issue (noted in code comments). Workaround: Power cycle (unplug USB) instead of pressing reset button.

### "Memory keeps fragmenting"

Run `compact` command to merge free blocks. If fragmentation stays high (>70%), your app is allocating/freeing many small blocks. Consider:
- Pre-allocate buffers once in init
- Reuse buffers instead of allocate/free each tick
- Run `mem_compact()` during idle time in your app

### "Task won't die when I press BTN_ONOFF"

If your GUI app ignores focus loss and keeps drawing:
- It's interfering with terminal rendering
- Likely cause: No check of `kernel.gui_focus_task_id` in tick
- Fix: Add the check at START of tick function

### "Shell is frozen after running an app"

App didn't call `task_sleep()` and starved shell. Check your app's tick function:
- Does every code path end with `task_sleep(N)`?
- No infinite loops or long busy-waits?
- If you have complex loops, add `task_yield()` inside them

### "Multiple button presses detected as one"

Button debouncing is set to 150ms. Press slower or increase DEBOUNCE_DELAY in code if you need different timing.

---

## Performance Characteristics

### Context Switch Overhead

- **Time:** ~500 µs per switch
- **Happens:** After every task's tick completes
- **32 tasks running:** ~16 ms per full round-robin

### malloc/kmalloc Time

- **First-fit search:** O(n) where n = number of blocks (typically <50)
- **Average:** ~50 µs for allocation
- **Compaction trigger:** After 10+ failed allocations
- **Compaction time:** Depends on fragmentation; worst case ~5 ms

### Display Rendering

- **Full screen refresh:** ~33 ms (at 1 pixel per microsecond)
- **Text output:** ~100 µs per character
- **Drawing functions are slow:** minimize fullscreen clears

### Task Sleep Accuracy

- **Resolution:** 1 ms (1000 µs scheduler tick)
- **Actual wake time:** Within ±1 ms of requested
- **Example:** `task_sleep(100)` wakes between 99-101 ms

---

## Pin Definitions

```
Display (ILI9341):
  LCD_CS      = GPIO 21
  LCD_RESET   = GPIO 20
  LCD_DC      = GPIO 17
  LCD_MOSI    = GPIO 19
  LCD_SCK     = GPIO 18
  LCD_LED     = GPIO 22

SD Card:
  SD_CS       = GPIO 5
  SD_MOSI     = GPIO 19
  SD_MISO     = GPIO 16
  SD_SCK      = GPIO 18

Buttons:
  BTN_TOP     = GPIO 0
  BTN_RIGHT   = GPIO 1
  BTN_BOTTOM  = GPIO 2
  BTN_LEFT    = GPIO 3
  BTN_SELECT  = GPIO 4
  BTN_START   = GPIO 6
  BTN_A       = GPIO 7
  BTN_B       = GPIO 8
  BTN_ONOFF   = GPIO 9 (UI cycle)
```

---

## Version History

**v9.5:** Task Termination Fix
- GUI apps now properly transition to TASK_TERMINATED
- GUI focus correctly revoked on app exit
- File handles closed during task cleanup
- Init callbacks receive new task ID

**v9.0:** GUI System
- UISocket API for exclusive display control
- Single-focus application model

---

## Constraints & Limits

| Item | Limit |
|------|-------|
| Max Tasks | 32 |
| Heap Size | 180 KB |
| Max Memory Blocks | 256 |
| VFS Max Files | 16 |
| VFS File Size | 16 KB |
| VFS Storage | 128 KB |
| FS Max Open Files | 8 |
| FS Max Filename | 32 chars |
| FS Max Card Size | 4 GB |
| Log Entries | 40 |
| Scrollback Lines | 120 |
| Terminal Columns | 53 |
| Terminal Rows | 30 |
| Task Name Length | 24 chars |
| Max Log Entry Size | 56 chars |
| Scheduler Quantum | 1 ms |
| VFS Blocks Per File | 64 (fragmented) |
| FS Max Simultaneous Files | 8 |

---

## Architecture Overview

```
KERNEL (180 KB Heap)
├── Scheduler (Priority-based, 1ms quantum)
├── Memory Manager (First-fit, auto-compaction)
├── OOM Killer (Apps only)
├── GUI System (Single-focus UISocket)
│
├── DRIVERS (Protected, auto-respawn)
│   ├── Display (ILI9341 SPI at 320×240)
│   └── Input (9-button polling)
│
├── SERVICES (Protected, auto-respawn)
│   ├── Shell (Serial command processing)
│   ├── CPU Monitor (Usage calculation)
│   ├── Temp Monitor (ADC temperature)
│   ├── VFS (128 KB RAM filesystem)
│   └── FS (SD card via SPI)
│
├── MODULES (Killable, respawning)
│   ├── Counter (example)
│   └── Watchdog (health monitoring)
│
└── APPLICATIONS (Killable, single-focus GUI)
    ├── Snake (GUI game, 10 KB)
    ├── Calc (Calculator)
    ├── Clock (Digital display)
    ├── Sysmon (System monitor)
    ├── Memhog (Stress test, 50 KB)
    ├── CPUburn (Stress test)
    └── Stress (Full system test)
```

---

## License & Credits

Picomimi v9.5 - RP2040 Multitasking Kernel  
Built for embedded systems education and experimentation.

Dependencies:
- Arduino IDE 1.8.13+
- Adafruit GFX Library
- Adafruit ILI9341 Driver
- Arduino SD Library

---

## Next Steps

1. **Set up hardware** per pin definitions
2. **Flash Picomimi.ino** to RP2040
3. **Open Serial Monitor** (115200 baud)
4. **Type `help`** to see command list
5. **Create your first app** using "Porting Arduino Sketches" guide
6. **Test with stress** command to verify stability
7. **Read kernel code** for deeper understanding (it's well-commented)

**If you read this far, like Amon, you are a femboi. Yep. You're a femboi.**  
*If you're a woman who read this, you too are a femboi now. No escape.* 🥳✨
