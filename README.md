# **Picomimi MicroOS**
---

> **⚠️ NOTE:** Picomimi is taking a **long pause** for now — it’s not abandoned! Development is happening behind the scenes: new modularisation techniques are in the works to split the 12,000-line Arduino sketch into a sensible structure with `src/`, `includes/`, and `main/`, paving the way for Picomimi to eventually become a **full microOS library** for Arduino IDE. Picomimi is going full standalone soon, removing dependencies on Arduino IDE specific libraries soon!! Proper structure upcoming!! Proper drivers and HALs in the works! ฅ(•ㅅ•❀)ฅ

---
> **Picomimi Project:**
> Picomimi is **not a professional RTOS**. It’s an **embedded distribution (Heavily inspired by UNIX concepts) and a full development platform** for RP2040/RP2350 MCUs — a persistent, hackable system where you can run multiple apps, inspect kernel state, and experiment with hardware, all in one unified platform.

---

**Picomimi is an embedded MicroOS *distribution* for RP2040 and RP2350 microcontrollers.**

It is **not a professional RTOS** and does not aim for certification, minimal footprint, or hard real-time guarantees. Instead, Picomimi is a **platform for embedded development** — a persistent, hackable system combining kernel, services, filesystem, shell, tooling, and application framework into one cohesive package.

The goal is straightforward: **provide a stable, inspectable platform** where developers can build, run, and iterate on apps for low-power MCUs — from small robots to smart devices — without juggling multiple codebases or complex toolchains.

---

![Picomimi mascot](assets/Picomimi_Logo.png)
*Picomimi ready for development ฅ(•ㅅ•❀)ฅ*

---

## **Project Philosophy**

Picomimi is built around **transparency, modularity, and experimentation**:

* **Kernel is editable:** tweak scheduling, memory management, IPC, or filesystem freely
* **Applications are first-class:** develop long-running tasks without boilerplate
* **Inspectable systems:** monitor kernel state, extend services, and adjust behavior
* **Lightweight but capable:** small enough to understand, powerful enough to build real embedded systems
* **Modular tooling:** MEOW enables kernel-scale refactoring and version control
* **Fast iteration:** Arduino IDE only — no CMake, no complex scripts

Picomimi is **a MicroOS and platform for embedded development**, designed to let you explore, experiment, and build on a single unified system.

---

## **What Picomimi Is (and Isn’t)**

Picomimi **is not** a professional RTOS seeking certifications or hard guarantees.

Picomimi **is**:

* A **full embedded distribution**, not just a kernel
* A **persistent system** with storage, logs, updates, and recovery
* A **hackable platform** where kernel, services, and apps evolve together
* A learning and experimentation environment for embedded development

It emphasizes **developer visibility, stability, and consistency** over strict minimalism or formal certification.

---

## **Project Goals**

1. Provide a cohesive embedded distribution for RP2040/RP2350 MCUs
2. Enable apps to run on a single unified platform
3. Expose kernel internals for learning and experimentation
4. Support dual-core multitasking with priority-aware scheduling
5. Offer modular development tools via MEOW
6. Preserve Arduino IDE simplicity
7. Enable full hackability of kernel, services, and apps
8. Progress toward v17 with stabilized architecture, documentation, and tooling

---

## **Architecture Overview**

Picomimi is a **dual-core microkernel at the heart of a complete embedded system distribution**:

* **O(1) priority scheduler** across both cores
* **Priority-aware IPC** for deterministic inter-task communication
* **Per-task memory accounting** with OOM handling
* **PMFS filesystem (v13+)** — journaling, write caching, dual OTA banks, tmpfs RAM disk, file locking
* **Mini RTOS primitives** — mutexes, semaphores, event flags
* **Root / privileged mode** for critical operations
* **SD card support** for persistence, logging, and updates
* **Interactive shell** for monitoring and control

Peripheral handling, timers, and display tasks run as **user-space processes**, keeping the kernel lean, readable, and inspectable.

---

## **Version History**

### **v14.0 Quiet-Otter** — Pre-release

* Improved RAM usage
* 29% dynamic RAM on RP2350, 58% on RP2040
* CPU power governing added
* kmalloc and kfree improvements

### **v13.0 Foxxo-Base** — Stable Modular Foundation

* PMFS filesystem integrated
* Fully modular kernel (32 modules)
* MEOW toolchain introduced
* Simplified startup and memory handling

### **v12 MACH 1** — Experimental Alpha

* App Check Environment (ACE)
* Memory and CPU abuse detection

### **v11 Artemis 1** — Experimental / Unsupported

* Aggressive memory enforcement

### **v10 Manifest v2** — Initial Stable Base

* Dual-core scheduler, IPC, OOM handling

---

## **MEOW Toolchain** 🐱

* **MRRP** — split monolithic kernel into modules
* **MIAU** — reassemble modules into one `.ino`
* **NYAA** — structured refactors via JSON manifests
* **MROW** — validate structure and sanity

MEOW enables modular development **without leaving the Arduino IDE**.

---

## **Task and Memory Model**

* Tasks behave like **lightweight processes**
* Create, suspend, resume, and terminate tasks via shell or API
* Memory allocation via `kmalloc` / `kfree` with per-task accounting
* Deterministic OOM recovery
* Priority-aware IPC and mini RTOS primitives available to apps

---

## **PMFS Filesystem (v13+)**

* Transactional journaling with crash recovery
* Dual system banks (A/B) for safe firmware updates
* tmpfs RAM disk, write caching, file locking
* Log rotation and persistent storage

---

## **Shell and Interaction**

Connect via USB serial or Arduino IDE:

```bash
picocom /dev/ttyACM0 -b 115200
```

Interactive shell allows **inspection, control, and debugging**.

---

## **Hardware Support**

* RP2040 and RP2350 MCUs
* Optional SD card (features degrade gracefully)
* Missing peripherals do not compromise kernel stability

---

## **Building**

**Arduino IDE only:**

1. Install Arduino IDE
2. Add RP2040 board support
3. Open Picomimi `.ino`
4. Compile and upload

No CMake. No external toolchains.

---

## **Customization**

* Kernel internals fully open
* Services replaceable
* Apps first-class citizens
* Behavior inspectable and adjustable

---

## **Intended Use Cases**

* Learning microkernel and system design
* Building stateful embedded systems
* Rapid embedded prototyping
* Hackable experimentation platform
* App development on low-power MCUs

---

## **License**

MIT License

---

## 1.1 The One-Sentence Answer

Picomimi-AxisOS is a **complete embedded distribution** for RP2040/RP2350 microcontrollers - not just an RTOS, but an entire operating environment with a kernel, scheduler, memory manager, filesystem, shell, SDK, and GUI engine, all in a single 12,000-line Arduino sketch.

---

## 1.2 What Makes It Different?

Most "operating systems" for microcontrollers are one of two things:

1. **Minimal RTOSes** - Like FreeRTOS. They give you task scheduling and maybe some synchronization primitives. That's it. You build everything else yourself.

2. **Arduino Sketches** - Single-threaded, no resource management, no protection, no abstraction.

Picomimi is neither. It's a **complete embedded distribution**:

| Feature           | FreeRTOS    | Arduino    | Picomimi                                     |
| ----------------- | ----------- | ---------- | -------------------------------------------- |
| Task Scheduling   | ✓           | ✗          | ✓                                            |
| Memory Management | Basic       | None       | Full (kmalloc/kfree, compaction, OOM killer) |
| Filesystem        | ✗           | SD library | PMFS (journaled, wear-leveling, tmpfs)       |
| Shell             | ✗           | ✗          | Full interactive shell                       |
| Power Management  | ✗           | ✗          | 5-level CPU governor with thermal throttling |
| Resource Tracking | ✗           | ✗          | Hardware ownership, auto-cleanup             |
| GUI               | ✗           | ✗          | Display engine with focus management         |
| IPC               | Queues only | ✗          | Messages, signals, shared memory             |
| Dual-Core         | Manual      | ✗          | Automatic load balancing                     |

Note: Picomimi is not proffessional, not ceritifed, not competitive or in any way comparable to the proffessional project that is FreeRTOS. FreeRTOS is used in this comparision with only the intents of showing ideological differences.

---

**Made with love ฅ(•ㅅ•❀)ฅ**

---
