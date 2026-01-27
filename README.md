# **Picomimi MicroOS**

---

> **⚠️ ARCHITECTURAL OVERHAUL IN PROGRESS**
> **Picomimi is graduating.**
> The 12,000-line monolithic sketch era is ending. Picomimi is migrating to a professional **pico-sdk** architecture with proper modularisation (`src/`, `include/`, `drivers/`, `apps/`).
> The core is now native C/C++, improving performance, maintainability, and scalability. Eventually, Picomimi will return to the Arduino ecosystem as a fully packaged, standalone library, removing dependencies on Arduino core logic while remaining accessible to all developers.
> *Proper HALs and drivers are in active development. ฅ(•ㅅ•❀)ฅ*

> **Picomimi v14.3.1 is being ported over to the new workflow and Pico-SDK, for clarily, it will be called v15.0. Picomimi versions 15.x.x will build upon the 14.3.1 feature set. Alpha ports are non functional, work in progress.**

> **Note:** *Picomimi and AxisOS are tightly connected; they influence each other heavily. The distinction will be clarified in future updates.*

---

## **Project Overview**

Picomimi is a **complete embedded MicroOS distribution** for RP2040 and RP2350 MCUs.

Unlike minimal RTOSes or single-threaded Arduino sketches, Picomimi is a full platform for embedded development:

* **Persistent System:** Kernel, services, filesystem, shell, SDK, and apps in one package.
* **Inspectable Internals:** Monitor and tweak tasks, memory, IPC, and peripherals in real-time.
* **Hackable & Modular:** Kernel, drivers, and apps evolve together.
* **Dual-Core Aware:** Preemptive multitasking on RP2040/RP2350.
* **Collaborative-Ready:** Structured codebase supports maintainable contributions.

The goal: provide a stable, extensible platform for learning, prototyping, or building complex embedded systems without juggling boilerplate code.

---

![Picomimi mascot](assets/Picomimi_Logo.png)
*Picomimi: Evolving for embedded development ฅ(•ㅅ•❀)ฅ*

---

## **Philosophy**

Picomimi is built around **transparency, modularity, and serious ambition**:

1. **Modular Kernel:** Split into `src/`, `include/`, `drivers/`, and `apps/`.
2. **Hardware Abstraction:** HALs decouple kernel logic from MCU registers.
3. **Native Performance:** Built on `pico-sdk` for efficiency and low-level control.
4. **Inspectable Systems:** Tasks, memory, IPC, and peripherals are fully visible.
5. **Future-Proof:** Will become a portable, Arduino-compatible library without losing structure.

Picomimi is maturing from a hackable sketch to a **robust embedded foundation**.

---

## **What Picomimi Is (and Isn’t)**

**Not:**

* A certified RTOS
* A minimal library
* A single-developer monolith

**Is:**

* A full embedded distribution with kernel, services, filesystem, shell, SDK, and apps
* A persistent system with storage, logs, updates, and recovery
* A modular platform with decoupled drivers and HALs
* A serious engineering effort to build a maintainable, professional microOS

---

## **Project Goals (The Refactor)**

1. **De-Monolith:** Split the 12k line codebase into maintainable modules.
2. **Native Foundation:** Port all low-level MCU access to raw `pico-sdk` calls.
3. **Hardware Abstraction:** Implement clean, modular HALs for all supported peripherals.
4. **Library Packaging:** Package core as a standalone Arduino library while maintaining Pico-SDK support.
5. **Collaboration:** Enable contributions, CI workflows, and maintainable development.

---

## **Architecture Overview**

Picomimi is a **dual-core microkernel at the heart of a complete embedded system distribution**:

* **O(1) Priority Scheduler:** Preemptive multitasking across both cores.
* **Hardware Abstraction Layer:** Separates kernel logic from MCU hardware.
* **Priority-Aware IPC:** Deterministic inter-task communication.
* **PMFS Filesystem (v13+):** Journaling, write caching, dual OTA banks, tmpfs RAM disk.
* **Memory Management:** Per-task accounting with OOM handling using `kmalloc`/`kfree`.
* **User Space:** Peripheral handling, timers, and display tasks run as user-space processes.

---

## **Version History**

### **v15.0 (In Development) — The Great Refactor**

* Migration to `pico-sdk`
* Directory restructuring (`src`, `include`, `drivers`, `apps`)
* Removal of Arduino Core dependencies
* Implementation of proper HALs and drivers

### **v14.0 Quiet-Otter — Legacy Monolith**

* Improved RAM usage (29% dynamic RAM on RP2350)
* CPU power governing added

### **v13.0 Foxxo-Base — Stable Foundation**

* PMFS filesystem integrated
* Initial modular concepts introduced

---

## **Task and Memory Model**

* **Tasks:** Lightweight processes with isolated resources.
* **Lifecycle:** Create, suspend, resume, and terminate via shell or API.
* **Memory Management:** Full heap tracking; resources reclaimed on task exit or crash.
* **IPC:** First-class message passing, signals, and shared memory.

---

## **PMFS Filesystem (v13+)**

A custom filesystem designed for embedded flash memory:

* **Transactional Journaling:** Protects against corruption on power loss.
* **Dual Banks (A/B):** Safe firmware updates with rollback support.
* **Features:** `tmpfs` RAM disk, write caching, file locking.

---

## **Shell and Interaction**

Connect via USB serial:

```bash
picocom /dev/ttyACM0 -b 115200

```

The **Interactive Shell** allows inspection, control, and debugging of the kernel and tasks in real-time.

---

## **Building**

**Current Status: Migration to Pico-SDK**

```bash
mkdir build && cd build
cmake ..
make

```

*Note: Arduino IDE support is temporarily paused; the core will be packaged as a library later.*

---

## **One-Sentence Summary**

Picomimi is a **complete embedded microOS distribution** for RP2040/RP2350 — a full OS environment with kernel, scheduler, memory manager, filesystem, shell, and SDK, now refactored into a professional, modular architecture.

---

## **What Makes Picomimi Different**

Most microcontroller “OSes” are either:

1. **Minimal RTOSes** (like FreeRTOS) — kernel only
2. **Arduino sketches** — single-threaded loops

Picomimi is neither. It's a **full, inspectable embedded distribution**:

| Feature | FreeRTOS | Arduino | **Picomimi** |
| --- | --- | --- | --- |
| **Structure** | Kernel Only | Superloop | **Full OS Distribution** |
| **Filesystem** | 3rd Party | Library | **Native PMFS (Journaled)** |
| **Shell** | 3rd Party | None | **Interactive / Native** |
| **Process Model** | Tasks | Single Thread | **Tasks w/ Resource Ownership** |
| **Development** | C/C++ | C++ Sketch | **C/C++ SDK / Library** |
| **Focus** | Efficiency | Simplicity | **Inspectability & Features** |

---

**Made with love, ambition, and serious engineering ฅ(•ㅅ•❀)ฅ**
