# **Picomimi MicroOS**

> 🐱 **New Development Workflow (v12 Arte 2+):**  
> Picomimi is transitioning to a **modular development structure**. The project will be split into individual module files for easier editing, version control, and collaboration. **MIAU 🐱 (Monolithic Ino Aggregation Utility)** will assemble these modules into a single `.ino` file for compilation.  
>   
> **Benefits:**  
> - Edit individual modules without navigating massive files  
> - Better git diffs and merge conflict resolution  
> - Cleaner separation of kernel components  
> - **Same simple Arduino IDE workflow** — MIAU handles assembly automatically  
>   
> Developers can work on isolated modules (scheduler, IPC, memory manager, etc.), then run MIAU to build the final monolithic `.ino`. The development process becomes **modular** while deployment remains **monolithic and simple**.

---

Picomimi is a microOS for RP2040 and RP2350 microcontrollers.  
It is designed to be **hackable, learnable, and practical**: a minimal unified dual-core microkernel with task scheduling, inter-process communication (IPC), memory management, system calls, and an interactive shell — everything needed for a small but functional operating system.

The kernel is **fully open and editable**. You can modify scheduler behavior, tweak IPC, adjust memory policies, or focus entirely on building applications with exposed APIs. Picomimi provides **maximum control and learning potential** while remaining **fast, readable, and buildable using only Arduino IDE** — no CMake, no complex toolchains, no opaque scripts.

---
![Picomimi mascot](assets/Picomimi_Mascot.png)  
*Picomimi ready for hacking!*

___

## **Project Philosophy**

Picomimi is built around **transparency and hackability**:

* **Kernel is yours:** tweak or rewrite the scheduler, memory manager, IPC system, or system calls freely.  
* **Applications first:** easily build and run tasks without boilerplate.  
* **Learning through experimentation:** inspect kernel state, extend services, and adjust behavior as needed.  
* **Minimal but capable:** small enough to fully understand, but powerful enough to serve as a real development platform.  
* **Simple toolchain:** Arduino IDE only, with direct upload and fast compilation.  

Picomimi is a **development platform as much as it is a microOS**, designed to let you focus on experimentation, embedded application development, and learning by hacking.

---

## **Project Goals**

1. Provide a minimal but functional microOS for RP2040/RP2350.  
2. Expose kernel internals for learning and modification.  
3. Enable multitasking across dual cores with priority-aware scheduling.  
4. Simplify development with a zero-configuration Arduino IDE workflow.  
5. Enable full hackability: kernel, IPC, scheduler, and system calls are open for modification.  
6. Serve as a foundation for embedded applications, letting developers focus on application logic instead of boilerplate.
7. **Get the project to v15, stabilise it, lock down philosophies and make true proper documentation, split the kernel and create a builder script for it**

---

## **Architecture Overview**

Picomimi is a **unified dual-core microkernel**:

* **O(1) priority scheduler** across both cores.  
* **Priority-aware IPC** for deterministic communication between tasks.  
* **Per-task memory accounting** with Out-of-Memory (OOM) handling.  
* **Root/privileged mode** for critical kernel operations.  
* **Optional SD card support** for persistent storage and logging.  
* **Interactive serial shell** for monitoring and control.  

GUI, display handling, timers, and peripheral management run as user-space tasks, keeping the kernel **lean, readable, and fully editable**.

---

## **Version Overview**

### **v10 Manifest v2 (v10 M2)** — Stable / Foundational

* First major milestone. Fully hackable.  
* Dual-core O(1) scheduler.  
* Priority-aware IPC.  
* Root mode for privileged operations.  
* Graceful OOM Killer with optional callbacks.  
* Kernel Panic Handler.  
* No memory protection; fully open and multipurpose.  

### **v11 Manifest v4 (v11 M4)** — RTOS Primitives Feature Release

* Adds mutexes, semaphores, events.  
* Implements priority inheritance for deterministic scheduling.  
* GUI focus management.  
* Privilege separation via Root Mode.  

### **v11 Artemis 1 (v11 A1)** — Experimental / Unsupported

* Aggressive memory enforcement: tasks exceeding limits are killed or blocked.  
* Aggressive OOM Killer targeting high-velocity allocators.  
* API inconsistent and unstable.  
* Research-only, high-risk experimental features.  

### **v12 MACH 1 (v12 MACH1)** — Experimental Alpha

* App Check Environment (ACE) pre-execution sandbox.  
* Memory and CPU abuse detection.  
* Throttling mechanisms instead of termination-only OOM.  
* Quality-of-Service (QoS) oriented stability.  
* Pivot toward a self-protective, hyper-stable kernel.  

---

## **Task and Memory Model**

* **Tasks:** create, suspend, resume, terminate via shell or API.  
* **Memory:** `kmalloc` / `kfree` with per-task accounting.  
* **OOM Handling:** graceful recovery (or aggressive termination in experimental versions).  
* **IPC:** message passing with optional priority handling.  

All kernel features are **fully exposed for inspection and modification**.

---

## **Shell and Interaction**

Connect via USB serial:

**Linux:**

```bash
picocom /dev/ttyACM0 -b 115200
```

**macOS:**

```bash
screen /dev/tty.usbmodem* 115200
```

**Windows:** Arduino Serial Monitor

Shell commands allow:

* Task inspection (`ps`, `taskinfo`)
* Memory inspection (`mem`, `memmap`)
* Scheduler and IPC stats (`schedstat`, `ipcstat`)
* OOM and kernel statistics (`oomstat`)

Users can launch tasks, inspect kernel state, and debug applications interactively.

---

## **Hardware Support**

* RP2040 and RP2350 boards.
* Optional SD card for logging and filesystem. Services that depend on the SD card (e.g., file logging) will **remain inactive if no SD card is present**, and the system will notify the user that the functionality is unavailable.
* Missing peripherals **do not affect kernel stability**; only features depending on them are inactive.

---

## **Building**

### **Standard Build (Monolithic .ino)**

1. Install Arduino IDE.
2. Add RP2040 board support:

```
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

3. Open `Picomimi.ino` in Arduino IDE.
4. Add applications from the `app` folder if needed.
5. Compile and upload.

**No CMake, no external dependencies — sheer Arduino IDE simplicity.**

### **Modular Development Build (v13+)**

For developers working on kernel internals:

1. Edit individual module files in the `inc/` folder.
2. Run MIAU to assemble modules:

```bash
python3 MIAU.py
MIAU~> /path/to/picomimi/project
```

3. MIAU generates the final `.ino` in the `build/` folder.
4. Open the generated `.ino` in Arduino IDE and upload.

**MIAU keeps development modular while preserving Arduino IDE simplicity.**

---

## **Customisation**

* Kernel, scheduler, IPC, OOM, memory policies, and Root Mode are fully editable.
* Applications can be developed independently using exposed APIs.
* The system is intended to be modified, extended, and experimented on without restriction.

Picomimi is **both a microOS and a development platform**, letting you remove boilerplate, focus on applications, and hack the kernel itself.

---

## **Intended Use Cases**

* Learning microkernel design.
* Building custom embedded devices.
* Prototyping multi-task microcontroller systems.
* Experimental research on stability and memory policies.
* Hackable, rapid application development on RP2040/RP2350 embedded devices.

---

## **Development and Philosophy**

Picomimi is a product of iterative, practical development. It was designed for **clarity, accessibility, and hackability**, not for production-grade security or compliance. Its kernel is intentionally open, letting you explore, extend, or rewrite internal mechanisms. The system encourages **learning through experimentation**, giving developers tools to handle multitasking, IPC, and memory without unnecessary complexity.

The project was initially developed entirely by hand, but AI tools were later used to **refactor, streamline, and clean code** for readability and maintainability. AI acted as a **development assistant**, not a replacement for design thought. Picomimi's philosophy remains rooted in **modularity, simplicity, and experimentation**.

---

## **License**

MIT License. See `LICENSE` file.
