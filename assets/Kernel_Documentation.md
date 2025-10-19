# Picomimi OS v9.5 - Full System Documentation

---

## 1. Introduction & Core Philosophy

**Picomimi OS** is a cooperative, priority-based multitasking operating system designed for the **Raspberry Pi Pico (RP2040)**. It aims to provide a stable, resource-managed environment for running multiple applications, including a command-line shell and GUI programs.

### Core Features

- **Cooperative Multitasking:** Tasks voluntarily yield control to the scheduler, simplifying task design but requiring careful management of long-running operations.  
- **Priority-Based Scheduling:** Ensures critical drivers (like input) and services remain responsive.  
- **Dynamic Memory Management:** A custom heap manager (`kmalloc`/`kfree`) tracks memory ownership, allowing automatic cleanup when a task terminates.  
- **Out-Of-Memory (OOM) Killer:** Protects the kernel by terminating non-critical applications when memory runs out.  
- **GUI Abstraction (UISocket):** Managed API allowing one application at a time to take exclusive control of the screen, handling focus and hardware abstraction.  
- **Dual Filesystems:**  
  - Volatile, RAM-based VFS for fast, temporary data.  
  - Persistent SD card FS for permanent storage.  
- **System Services:** Includes command-line shell, CPU/temperature monitors, and a system watchdog.  
- **Modular Architecture:** Components (drivers, services, apps) are independent tasks with defined life cycles (`init`, `tick`, `deinit`).  

---

## 2. Kernel Architecture

The kernel operates through `setup()` and `loop()`, managing initialisation and the central scheduling loop.

### 2.1 Main Loop (`loop()`)

The core OS loop performs:

1. **Scheduler Tick:** `scheduler_tick()` updates timers and wakes sleeping tasks.  
2. **Task Execution:** Runs `tick` function of `kernel.current_task` and tracks CPU usage.  
3. **Yield & Reschedule:** `task_yield()` selects the next highest-priority ready task.  
4. **Pacing:** Sleeps briefly to maintain `SCHEDULER_TICK_US` frequency.  

### 2.2 The Scheduler

- **scheduler_tick():** Updates task states, wakes waiting tasks, and respawns flagged tasks.  
- **task_yield():** Selects the highest-priority ready task, ensuring fairness among tasks of equal priority.  

---

## 3. Task Management API

Tasks are fundamental units of execution.  

### 3.1 Task Control Block (TCB)

```c
struct TCB {
    uint32_t id;
    TaskState state;
    uint8_t task_type;
    uint8_t oom_priority;
    uint8_t priority;
    
    void (*entry)(void*);
    void* arg;
    ModuleCallbacks* callbacks;
    
    uint32_t flags;
    uint64_t wake_time;
    uint32_t mem_used;
    uint32_t mem_peak;
    
    char name[TASK_NAME_LEN];
    uint32_t mem_limit;

    uint64_t start_time;
    uint64_t max_runtime;
    uint64_t last_respawn;
    uint32_t respawn_count;
    uint32_t cpu_time;
    uint32_t last_run;
    uint32_t page_faults;
    uint32_t context_switches;
    const char* description;
};
````

**Task States (`TaskState`):**

* `TASK_READY` – ready to run.
* `TASK_RUNNING` – currently running.
* `TASK_WAITING` – sleeping (`task_sleep()`).
* `TASK_SUSPENDED` – paused indefinitely.
* `TASK_TERMINATED` – finished or killed; respawned only if flagged.

### 3.2 Task Lifecycle & Callbacks

```c
struct ModuleCallbacks {
    void (*init)(uint32_t id);
    void (*tick)(void*);
    void (*deinit)();
};
```

### 3.3 Core Task Functions

* **task_create():** Registers a new task.
* **brutal_task_kill():** Safely terminates a task.
* **task_sleep():** Puts the task into `TASK_WAITING` for a specified time.

### 3.4 Best Practices

* Avoid blocking operations.
* Yield frequently using `task_sleep()`.
* Check task state regularly for external termination.

---

## 4. Memory Management API

### 4.1 Core Memory Functions

* **kmalloc(size, task_id):** Allocates memory tied to a task.
* **kfree(ptr):** Frees previously allocated memory.

### 4.2 Out-Of-Memory (OOM) Killer

* Triggered only after `kmalloc()` fails post-compaction.
* Targets only application tasks.
* Chooses victim by `oom_priority` and memory usage.
* Terminates victim using `brutal_task_kill()`.

---

## 5. GUI & UI System (UISocket API)

Allows a single application to gain exclusive control over the display.

```c
struct UISocket {
    bool (*request_focus)(uint32_t task_id);
    void (*release_focus)(uint32_t task_id);
    bool (*get_button_state)(UIButton button);
    void (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void (*draw_text)(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size, bool center);
    void (*clear_screen)();
};
```

---

## 6. Filesystem APIs

### 6.1 VFS (RAM FS)

* `vfs_create(name, type, owner_id)`
* `vfs_write(fd, data, size)`
* `vfs_read(fd, buffer, size)`
* `vfs_delete(fd)`

### 6.2 FS (SD Card)

* `fs_open(path, write_mode)`
* `fs_close(fd)`
* `fs_write_str(fd, data)`
* `fs_read_str(fd, buffer, size)`
* `fs_mkdir(path)`
* `fs_remove(path)`

---

## 7. Command-Line Interface (CLI)

### System Commands

* `help`, `ps`, `listapps`, `listmods`, `top`, `mem`, `memmap`, `compact`, `dmesg`, `uptime`, `temp`, `clear`, `reboot`, `shutdown`, `arch`, `oom`

### Task Management

* `kill <id>`, `root`, `root kill <id>`, `suspend <id>`, `resume <id>`

### VFS Commands

* `vfscreate`, `vfsls`, `vfsstat`, `vfsmkfile <name> <type>`, `vfsrm <id>`, `vfscat <id>`, `vfsdedicate`

### FS Commands

* `ls [path]`, `stat`, `mkdir <path>`, `rm <path>`, `cat <path>`, `write <path> <text>`, `logcat`

### Applications

* `snake`, `calc`, `clock`, `sysmon`, `memhog`, `cpuburn`, `stress`

---

## 8. Developer's Guide: Building a GUI Application

### 8.1 Focus Model

* Only one GUI app or terminal is visible at a time.
* Focused app controls the display and input.
* Must constantly check focus status.

### 8.2 Application Structure

* **State Struct:** Holds app data.
* **Callbacks:** `init`, `tick`, `deinit`.
* **Callback Struct:** `ModuleCallbacks` instance.
* **Spawn Function:** Calls `task_create()` to launch.

### 8.3 Init Function

```c
void my_app_init(uint32_t id) {
    my_app = (MyApp*)kmalloc(sizeof(MyApp), id);
    if (!my_app) { kout.println("[MY_APP] Memory allocation failed!"); return; }
    memset(my_app, 0, sizeof(MyApp));
    k_register_gui_app(&my_app->ui);
    my_app->player_x = 50;
    my_app->player_y = 50;
    my_app->has_focus = false;
    klog(0, "MY_APP: Initialized");
}
```

### 8.4 Tick Function

```c
void my_app_tick(void* arg) {
    if (!my_app) { brutal_task_kill(kernel.current_task); return; }

    // Focus acquisition
    if (!my_app->has_focus) {
        if (!my_app->ui.request_focus(kernel.current_task)) {
            task_sleep(100);
            return;
        }
        my_app->has_focus = true;
        my_app->ui.clear_screen();
    }

    // Focus verification
    if (kernel.gui_focus_task_id != kernel.current_task) {
        my_app->has_focus = false;
        task_sleep(200);
        return;
    }

    // Input handling
    if (my_app->ui.get_button_state(UI_BTN_TOP)) my_app->player_y -= 1;

    // Game logic
    if (my_app->player_y < 0) my_app->player_y = 0;

    // Drawing
    my_app->ui.clear_screen();
    my_app->ui.fill_rect(my_app->player_x, my_app->player_y, 10, 10, 0xFFFF);

    // Exit condition
    if (my_app->ui.get_button_state(UI_BTN_START)) {
        brutal_task_kill(kernel.current_task);
        return;
    }

    // Yield
    task_sleep(33);
}
```

### 8.5 Deinit Function

```c
void my_app_deinit() {
    if (my_app) {
        if (kernel.gui_focus_task_id == kernel.current_task)
            my_app->ui.release_focus(kernel.current_task);
        my_app = NULL;
    }
    klog(0, "MY_APP: Deinitialized");
}
```

### 8.6 Spawn Function

```c
ModuleCallbacks my_app_callbacks = {
    .init = my_app_init,
    .tick = my_app_tick,
    .deinit = my_app_deinit
};

void my_app_spawn() {
    uint32_t tid = task_create(
        "my_app",
        NULL,
        NULL,
        6,
        TASK_TYPE_APPLICATION,
        TASK_FLAG_ONESHOT,
        0,
        OOM_PRIORITY_NORMAL,
        16 * 1024,
        &my_app_callbacks,
        "A sample GUI application."
    );

    if (tid > 0) kout.println("My App started");
}
```

```
### 8.7 Additional Developer Best Practices

- **Memory Management:** Always use `kmalloc` with the task ID provided in `init()`. Do not manually `kfree` the state; the kernel handles cleanup automatically.  
- **Focus Checking:** Never draw or handle input without verifying that the task has GUI focus. This prevents conflicts and graphical glitches.  
- **Yielding:** Always end your `tick()` with `task_sleep()` to allow other tasks (especially drivers and system services) to run.  
- **Termination Safety:** Always respect external termination requests. Check your task state if performing loops or long operations.  
- **Single Instance Enforcement:** Use `TASK_FLAG_ONESHOT` in `task_create()` to prevent multiple instances of the same app.  

---

### 8.8 Example: Button Handling Template

```c
void handle_input(MyApp* app) {
    if (!app->has_focus) return; // Only process input if focused

    if (app->ui.get_button_state(UI_BTN_TOP)) {
        app->player_y -= 1;
    }
    if (app->ui.get_button_state(UI_BTN_BOTTOM)) {
        app->player_y += 1;
    }
    if (app->ui.get_button_state(UI_BTN_LEFT)) {
        app->player_x -= 1;
    }
    if (app->ui.get_button_state(UI_BTN_RIGHT)) {
        app->player_x += 1;
    }
}
````

---

### 8.9 Example: Drawing Template

```c
void draw_frame(MyApp* app) {
    if (!app->has_focus) return; // Only draw if focused

    app->ui.clear_screen();
    app->ui.fill_rect(app->player_x, app->player_y, 10, 10, 0xFFFF); // Draw player
    app->ui.draw_text(5, 5, "Score: 100", 0xFFFF, 1, false); // Example HUD
}
```

---

### 8.10 Summary: GUI Application Lifecycle

1. **Spawn Function:** Called by shell or other tasks to create the application task.
2. **Init Callback:** Allocate memory, initialize state, register GUI interface.
3. **Tick Callback:** Handle focus, input, logic, and rendering; yield periodically.
4. **Deinit Callback:** Release focus and clean up any remaining resources (kernel frees memory).
5. **Termination:** Either via user action or external `brutal_task_kill()`.

Following this lifecycle ensures safe, responsive, and cooperative GUI applications on **Picomimi OS v9.5**.

---

**If you read this far, like Amon, you are a femboi. Yep. You're a femboi.**  
*If you're a woman who read this, you too are a femboi now. No escape.* 🥳✨
