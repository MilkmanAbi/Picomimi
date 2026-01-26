# **Picomimi MicroOS**

---

> **⚠️ ARCHITECTURAL OVERHAUL IN PROGRESS**
> **Picomimi is graduating.**
> The era of the 12,000-line monolithic sketch is ending. We are currently migrating to a **professional `pico-sdk` architecture** with proper modularisation (`src/`, `include/`, `drivers/`).
> While the core is moving to native C/C++ for performance and structure, Picomimi will eventually return to the Arduino ecosystem as a **fully packaged, standalone library**—removing dependencies on Arduino core logic while remaining accessible to everyone. Proper HALs and Drivers are in the works! ฅ(•ㅅ•❀)ฅ
> Picomimi and AxisOS are heavily tied together; they influence each other heavily. AxisOS and Picomimi are used interchangeably in project READMEs for now and will be addressed soon.

---

> **Picomimi Project:**
> Picomimi is **not a professional RTOS**. It is an **embedded distribution (Heavily inspired by UNIX concepts)** for RP2040/RP2350 MCUs — a persistent, hackable system where you can run multiple apps, inspect kernel state, and experiment with hardware, all in one unified platform.

---

**Picomimi is an embedded MicroOS *distribution* for RP2040 and RP2350 microcontrollers.**

It does not aim for certification or minimal footprint. Instead, Picomimi is a **platform for embedded development** — a persistent, hackable system combining kernel, services, filesystem, shell, and application framework into one cohesive package.

The project is evolving from a single-file prototype into a **serious, modular system**. The goal is straightforward: **provide a stable, inspectable platform** where developers can build complex embedded systems without juggling boilerplate code.

---

![Picomimi mascot](assets/Picomimi_Logo.png)
*Picomimi: Evolving for the future ฅ(•ㅅ•❀)ฅ*

---

## **Project Philosophy**

Picomimi is built around **transparency, modularity, and serious ambition**:

* **Kernel is Modular:** No longer a monolith. The kernel is being split into `src/`, `include/`, and `drivers/`.
* **Hardware Abstraction:** Proper HALs serve as the bridge between kernel logic and hardware registers.
* **Inspectable Systems:** Monitor kernel state, extend services, and adjust behavior in real-time.
* **Native Performance:** Built on `pico-sdk` for maximum efficiency and control.
* **Future-Proof:** Designed to eventually become a portable library for the Arduino IDE.

Picomimi is **growing up**, transitioning from a hackable sketch to a robust embedded foundation.

---

## **What Picomimi Is (and Isn’t)**

Picomimi **is not** a professional RTOS seeking certifications or hard guarantees.

Picomimi **is**:

* A **full embedded distribution**, not just a kernel
* A **persistent system** with storage, logs, updates, and recovery
* A **modular platform** with decoupled drivers and logic
* A serious engineering effort to build a maintainable OS from scratch

It emphasizes **developer visibility, stability, and structure** over strict minimalism.

---

## **Project Goals (The Refactor)**

1. **De-Monolith:** Split the 12k line codebase into distinct, maintainable C++ modules.
2. **Native Foundation:** Port low-level hardware access to raw `pico-sdk` calls.
3. **Hardware Abstraction:** Implement a cleaner Driver/HAL model.
4. **Library Packaging:** Eventually package the Core as a standalone library for Arduino users.
5. **Collaboration:** Enable a proper contribution workflow with standard build tools.

---

## **Architecture Overview**

Picomimi is a **dual-core microkernel at the heart of a complete embedded system distribution**:

* **O(1) Priority Scheduler:** Preemptive multitasking across both cores.
* **Proper HAL:** A distinct Hardware Abstraction Layer separating kernel logic from MCU registers.
* **Priority-Aware IPC:** Deterministic inter-task communication.
* **PMFS Filesystem (v13+):** Journaling, write caching, dual OTA banks, tmpfs RAM disk.
* **Memory Management:** Per-task accounting with OOM handling and `kmalloc`/`kfree`.
* **User Space:** Peripheral handling, timers, and display tasks run as user-space processes.

---

## **Version History**

### **v15.0 (In Development) — The Great Refactor**

* Migration to `pico-sdk`
* Directory restructuring (`src`, `include`, `drivers`)
* Removal of Arduino Core dependencies
* Implementation of proper Hardware Abstraction Layers

### **v14.0 Quiet-Otter** — Legacy Monolith

* Improved RAM usage (29% dynamic RAM on RP2350)
* CPU power governing added

### **v13.0 Foxxo-Base** — Stable Foundation

* PMFS filesystem integrated
* Initial modular concepts

---

## **Task and Memory Model**

* **Tasks:** Behave like lightweight processes.
* **Lifecycle:** Create, suspend, resume, and terminate tasks via shell or API.
* **Memory:** Full heap tracking. If a task crashes or is killed, its resources are reclaimed.
* **IPC:** Message passing, signals, and shared memory are first-class citizens.

---

## **PMFS Filesystem (v13+)**

A custom filesystem designed for flash memory:

* **Transactional Journaling:** Protects against power loss corruption.
* **Dual Banks (A/B):** Safe firmware updates (rollback on failure).
* **Features:** `tmpfs` (RAM disk), write caching, file locking.

---

## **Shell and Interaction**

Connect via USB serial:

```bash
picocom /dev/ttyACM0 -b 115200

```

The Interactive Shell allows **inspection, control, and debugging** of the kernel in real-time.

---

## **Building**

**Current Status: Migration Mode**

The project is transitioning to a CMake-based workflow using the Raspberry Pi Pico SDK.

```bash
mkdir build && cd build
cmake ..
make

```

*Note: Arduino IDE support is temporarily paused while the core is refactored into a library.*

---

## **1.1 The One-Sentence Answer**

Picomimi is a **complete embedded distribution** for RP2040/RP2350 microcontrollers — an entire operating environment with a kernel, scheduler, memory manager, filesystem, shell, and SDK, now refactored into a professional modular architecture.

---

## **1.2 What Makes It Different?**

Most "operating systems" for microcontrollers are one of two things:

1. **Minimal RTOSes** - Like FreeRTOS (Kernel only).
2. **Arduino Sketches** - Superloop, single-threaded.

Picomimi is neither. It's a **complete embedded distribution**:

| Feature | FreeRTOS | Arduino | **Picomimi** |
| --- | --- | --- | --- |
| **Structure** | Kernel Only | Superloop | **Full OS Distro** |
| **Filesystem** | 3rd Party | Library | **Native PMFS (Journaled)** |
| **Shell** | 3rd Party | None | **Interactive / Native** |
| **Process Model** | Tasks | Single Thread | **Tasks w/ Resource Ownership** |
| **Development** | C/C++ | C++ Sketch | **C/C++ SDK / Library** |
| **Focus** | Efficiency | Simplicity | **Inspectability & Features** |

---

**Made with love and serious ambition ฅ(•ㅅ•❀)ฅ**

---
