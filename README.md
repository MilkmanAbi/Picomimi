# **Picomimi MicroOS**

> 🐱 **Modular Development Workflow (v13+):**  
> Picomimi is now **fully modular**! The kernel is split into 32 focused modules for easier development, version control, and collaboration. The **MEOW Toolchain** provides working tools to split, edit, and reassemble the codebase seamlessly.
>   
> **Benefits:**  
> - Edit individual modules without navigating massive files  
> - Better git diffs and merge conflict resolution  
> - Clean separation of kernel components  
> - **Tooling** with MEOW (MicroOS Engineering Orchestration Workbench)
> - **Same simple Arduino IDE workflow** — reassemble with one command
>   
> The development process is now **modular and maintainable** while deployment remains **monolithic and simple**. ฅ(•ㅅ•❀)ฅ

---

Picomimi is a MicroOS for RP2040 and RP2350 microcontrollers.  
It is designed to be **hackable, learnable, and practical**: a minimal unified dual-core microkernel with task scheduling, inter-process communication (IPC), memory management, filesystem support, mini RTOS primitives, and an interactive shell — everything needed for a small but functional operating system.

The kernel is **fully open and editable**. You can modify scheduler behavior, tweak IPC, adjust memory policies, extend the filesystem, or focus entirely on building applications with exposed APIs. Picomimi provides **maximum control and learning potential** while remaining **fast, readable, and buildable using only Arduino IDE** — no CMake, no complex toolchains, no opaque scripts.

---
![Picomimi mascot](assets/Picomimi_Mascot.png)  
*Picomimi ready for hacking! ฅ(•ㅅ•❀)ฅ*

---

## **Project Philosophy**

Picomimi is built around **transparency and hackability**:

* **Kernel is yours:** tweak or rewrite the scheduler, memory manager, IPC system, filesystem, or system calls freely.  
* **Applications first:** easily build and run tasks without boilerplate.  
* **Learning through experimentation:** inspect kernel state, extend services, and adjust behavior as needed.  
* **Minimal but capable:** small enough to fully understand, but powerful enough to serve as a real development platform.  
* **Professional tooling:** MEOW toolchain makes modular development effortless.
* **Simple deployment:** Arduino IDE only, with direct upload and fast compilation.  

Picomimi is a **development platform as much as it is a MicroOS**, designed to let you focus on experimentation, embedded application development, and learning by hacking.

---

## **Project Goals**

1. Provide a minimal but functional MicroOS for RP2040/RP2350.  
2. Expose kernel internals for learning and modification.  
3. Enable multitasking across dual cores with priority-aware scheduling.  
4. Provide professional modular development tools (MEOW toolchain).
5. Simplify development with a zero-configuration Arduino IDE workflow.  
6. Enable full hackability: kernel, IPC, scheduler, filesystem, and system calls are open for modification.  
7. Serve as a foundation for embedded applications, letting developers focus on application logic instead of boilerplate.
8. **Reach v15 with stabilized architecture, comprehensive documentation, and mature tooling.**

---

## **Architecture Overview**

Picomimi is a **unified dual-core microkernel**:

* **O(1) priority scheduler** across both cores.
* **Priority-aware IPC** for deterministic communication between tasks.  
* **Per-task memory accounting** with Out-of-Memory (OOM) handling.
* **PMFS filesystem** (v13+) with transactional journaling, write caching, dual system banks (A/B OTA), tmpfs RAM disk, and file locking.
* **Mini RTOS primitives**: mutexes, semaphores, event flags with priority inheritance.
* **Root/privileged mode** for critical kernel operations.  
* **SD card support** for persistent storage, logging, and firmware updates.
* **Interactive serial shell** for monitoring and control.  

GUI, display handling, timers, and peripheral management run as user-space tasks, keeping the kernel **lean, readable, and fully editable**.

---

## **Version History**

### **v13.0 Foxxo-Base** — Current Stable (Modular Foundation)

* **PMFS filesystem integrated** — transactional journaling, dual system banks, tmpfs, write caching, file locking
* **Instant OOM killing** with microsecond precision and immediate memory reclamation
* **Modular codebase** — 32 focused modules with MEOW toolchain support
* ACE system removed for simplicity and focus
* Immediate block coalescing on memory free
* Cleaner, faster startup

### **v12 MACH 1** — Experimental Alpha

* App Check Environment (ACE) pre-execution sandbox
* Memory and CPU abuse detection
* Throttling mechanisms for stability
* Quality-of-Service oriented design

### **v11 Artemis 1** — Experimental / Unsupported

* Aggressive memory enforcement
* High-risk experimental features
* Research-only, unstable API

### **v11 Manifest v4** — Mini RTOS Feature Release

* Adds mutexes, semaphores, events
* Implements priority inheritance for deterministic scheduling
* GUI focus management
* Privilege separation via Root Mode

### **v10 Manifest v2** — Initial Stable Foundation

* First major milestone. Fully hackable.  
* Dual-core O(1) scheduler
* Priority-aware IPC
* Root mode for privileged operations
* Graceful OOM Killer with optional callbacks
* Kernel Panic Handler
* No memory protection; fully open and multipurpose

---

## **MEOW Toolchain** 🐱

**MEOW (MicroOS Engineering Orchestration Workbench)** is Picomimi's professional development toolchain for modular kernel development. Available starting with v13.

### **Four Integrated Tools:**

#### **MRRP** — Monolithic Repartition & Refactor Program
Splits the monolithic kernel into focused modules. Config-driven extraction ensures future-proof scalability as Picomimi grows.

#### **MIAU** — Monolithic INO Aggregator Utility  
Reassembles modules back into a single `.ino` file for Arduino IDE compilation. Maintains proper structure automatically.

#### **NYAA** — Normalize Your Architecture Automatically  
Applies surgical code edits via JSON manifests. Perfect for systematic refactoring with documented reasoning.

#### **MROW** — Mend & Review Our Weirdness  
Verifies code structure: checks brace balance, detects duplicates, finds issues before compilation.

**MEOW makes modular development effortless while preserving Arduino IDE simplicity.**

See `docs/MEOW_README.md` for full documentation.

---

## **Task and Memory Model**

* **Tasks:** create, suspend, resume, terminate via shell or API.  
* **Memory:** `kmalloc` / `kfree` with per-task accounting.  
* **OOM Handling:** graceful recovery (v13: instant killing with microsecond precision).
* **IPC:** message passing with optional priority handling.  
* **Mini RTOS Primitives:** mutexes, semaphores, event flags with priority inheritance.

All kernel features are **fully exposed for inspection and modification**.

---

## **PMFS Filesystem** (v13+)

Picomimi v13 introduces **PMFS (Picomimi Filesystem)**, designed for reliable embedded storage:

* **Transactional journaling** — crash-safe operations with automatic recovery
* **Dual system banks (A/B)** — safe firmware updates with rollback capability
* **tmpfs RAM disk** — fast temporary storage
* **Write caching** — improved performance
* **File locking** — concurrent access protection
* **Logging system** — system and user logs with rotation

PMFS enables applications requiring reliable persistent storage with crash safety and OTA update capabilities.

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

### **Command Categories:**

**Task Management:** `ps`, `taskinfo`, `kill`  
**Memory:** `mem`, `memmap`, `oomstat`  
**Scheduler & IPC:** `schedstat`, `ipcstat`  
**Filesystem (v13+):** `ls`, `cat`, `mkdir`, `rm`, `touch`, `write`, `stats`, `logtail`  
**System:** `help`, `reboot`, `root`

Users can launch tasks, inspect kernel state, manage files, and debug applications interactively.

---

## **Hardware Support**

* **RP2040** and **RP2350** boards (tested on Raspberry Pi Pico, Pico W, Pico 2)
* **SD card** for filesystem, logging, and firmware updates (v13+)
  * Services gracefully degrade if SD card is absent
  * System notifies when SD-dependent features unavailable
* **Missing peripherals do not affect kernel stability** — only dependent features are inactive

---

## **Building**

### **Standard Build (Monolithic .ino)**

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add RP2040 board support:
   ```
   https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   ```
3. Open the Picomimi `.ino` file in Arduino IDE
4. Add applications from the `app` folder if needed
5. Compile and upload

**No CMake, no external dependencies — pure Arduino IDE simplicity.** ฅ(•ㅅ•❀)ฅ

---

### **Modular Development (v13+)**

For developers working on kernel internals:

**1. Split kernel into modules:**
```bash
python3 MEOW.py
> 2  # MRRP mode
> load mrrp_config.json
> split Picomimi.ino modules/
```

**2. Edit individual modules** in `modules/` directory

**3. Verify changes:**
```bash
> 4  # MROW mode
> verify modules/TargetModule.txt
```

**4. Reassemble kernel:**
```bash
> 1  # MIAU mode
> load miau_config.json
> assemble modules/ Picomimi_NEW.ino
```

**5. Compile in Arduino IDE** as usual

See full MEOW documentation in `docs/` folder.

---

## **Customization**

* Kernel, scheduler, IPC, OOM, memory policies, and Root Mode are fully editable.
* Filesystem behavior (v13+) can be customized or replaced.
* Applications can be developed independently using exposed APIs.
* The system is intended to be modified, extended, and experimented on without restriction.

Picomimi is **both a MicroOS and a development platform**, letting you remove boilerplate, focus on applications, and hack the kernel itself.

---

## **Intended Use Cases**

* Learning microkernel design
* Building custom embedded devices
* Prototyping multi-task microcontroller systems
* Experimental research on stability and memory policies
* Hackable, rapid application development on RP2040/RP2350 embedded devices
* Embedded systems requiring reliable filesystem and OTA updates (v13+)

---

## **Development and Philosophy**

Picomimi is a product of iterative, practical development. It was designed for **clarity, accessibility, and hackability**, not for production-grade security or compliance. Its kernel is intentionally open, letting you explore, extend, or rewrite internal mechanisms. The system encourages **learning through experimentation**, giving developers tools to handle multitasking, IPC, and memory without unnecessary complexity.

The project was initially developed entirely by hand, but AI tools were later used to **refactor, streamline, and clean code** for readability and maintainability. AI acted as a **development assistant**, not a replacement for design thought. Picomimi's philosophy remains rooted in **modularity, simplicity, and experimentation**.

Starting with v13, the project embraced **professional modular development** with the MEOW toolchain, making kernel development more maintainable while preserving the project's core values of transparency and hackability.

---

## **Contributing**

Picomimi welcomes contributions! Whether you're:
* Adding new features
* Improving documentation
* Fixing bugs
* Creating example applications
* Enhancing the MEOW toolchain

Feel free to open issues or submit pull requests. The project values **clarity, maintainability, and keeping things hackable**.

---

## **License**

MIT License. See `LICENSE` file.

---

**Made with love ฅ(•ㅅ•❀)ฅ**
