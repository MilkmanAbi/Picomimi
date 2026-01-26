# Picomimi Project — Modularisation Overview

Picomimi MicroOS has been modularized to make development, testing, and maintenance significantly easier. Previously, the kernel, app logic, and device management were all intertwined in a single massive sketch — over **5000 lines of code**. Navigating, debugging, or adding new features in such a monolithic structure was extremely difficult, time-consuming, and error-prone.

With the modular architecture, Picomimi is now split into self-contained modules, each responsible for a specific area of functionality. This approach transforms the codebase from a monstrous single file into a **organized, navigable system**, making it easier to find what you need, understand each subsystem, and modify it safely.

## Why Modularization Matters

The modular design provides several key advantages:

* **Ease of navigation:** Developers can quickly locate relevant code without scrolling through thousands of lines.
* **Isolation of functionality:** Each module has a clear purpose and defined interfaces, reducing accidental breakages.
* **Faster development cycles:** Work on one module independently without affecting the rest of the system.
* **Safe experimentation:** New features or apps can hook into the kernel without destabilizing the core.
* **Seamless integration with the toolchain:** Modules work with MIAU.py, MRRP.py, NYAA.py, and MRROW.py for assembly, splitting, auto-fixing, and verification.
* **Legacy support:** Historical code is preserved in separate folders, allowing you to reference or rollback as needed.

## Core Modules

The primary modules include:

* **Kernel Core:** Scheduling, memory management, RTOS primitives, and low-level operations.
* **PMFS Filesystem Module:** Transactional journaling, write caching, tmpfs RAM disk, dual system banks (A/B OTA), and file locking.
* **Device Drivers:** Abstract hardware interfaces for RP2040 peripherals, SD cards, display modules, input devices, and more.
* **Application Hooks:** Interfaces for external applications to interact safely with the kernel.
* **Utilities & Toolchain Integration:** Modules that interface with Picomimi’s toolchain for assembling, splitting, auto-fixing, and verification.

## Development Philosophy

The modular structure is designed to **make the codebase human-friendly**. By clearly separating concerns:

* You can quickly jump to the module you need.
* Debugging is localized and less intimidating.
* Legacy features and experimental additions coexist safely.
* Complex features like PMFS, RTOS primitives, or headless operation can be developed and tested independently.

In short, modularization is less about fancy architecture and more about **keeping a massive codebase navigable, understandable, and maintainable**. Without it, a 7.5k-line sketch would be nearly impossible to work with efficiently.

## Conclusion

The modular design of Picomimi MicroOS is the foundation for a developer-friendly microOS that balances safety, experimentation, and scalability. Each module acts as a “window” into a specific part of the system, making the entire codebase **easier to navigate and modify**, while still supporting advanced features, experimentation, and clean integration with the Picomimi toolchain.

> Made with determination ฅ(•ㅅ•❀)ฅ and a little bit of love ˗ˋˏ ♡ ˎˊ˗

---
