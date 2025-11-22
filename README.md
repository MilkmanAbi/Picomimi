# Picomimi

**A tiny, hackable, and ultra-customisable MicroOS for the RP2040**

Picomimi turns the RP2040 into a miniature, interactable Micro Computer device. It's an Arduino-IDE-ready microkernel built for one purpose: sheer hackability. This is a system you can tear apart, customise, and rebuild to your exact specifications.

![Picomimi mascot](assets/Picomimi_Mascot.png)  
*Picomimi ready for hacking!*

---

## A Note on expectations and Polish...

Let's be clear: professional solutions like FreeRTOS and Zephyr are miles ahead in features and polish. Picomimi isn't trying to compete with them, nor will it ever be able to with its foundation nor its philosophy.

Instead, its usability lies in its accessibility. It's a MicroOS that exposes its internals—from the O(1) scheduler to the graceful OOM killer—through an interactive shell and APIs. Where others provide a finished product, Picomimi provides a box of high-performance parts and invites you to build something of your own.

It's a service-oriented platform that gives you the tools to build complex systems on a stable foundation.

---

## Key Architectural Features (V10 M2 Microkernel)

Picomimi's core kernel is designed for simplicity and stability, making it a viable base for complex projects:

### Minimalist Dual-Core
- Fully utilises the RP2040's two cores with high-performance Inter-Core Communication (IPC).  
- The kernel is lean; all non-essential hardware (like displays or complex buttons) is handled by separate application tasks.

### O(1) Bitmap Scheduler
- Supports true concurrent multitasking with a guaranteed low-latency task selection.  
- This is a high-performance scheduling concept common in RTOS environments, replicated to a certain extent, fully exposed for you to use.

### Memory Management
- Features custom kernel memory allocation (`kmalloc` and `kfree`) with task-specific accounting and a Graceful Out-of-Memory (OOM) Killer.  
- This system prevents crashes by recovering memory before system failure.

### Essential Persistent Storage
- The kernel treats the SD card as a fundamental resource for file system services and application data.  
- (Picomimi will function without an SD on a bare RP2040, but the addition of one is heavily encouraged to give you access to logging features.)

### Service Isolation
- The kernel ensures stability, while your applications handle the complex interactions.  
- This is a platform for Software Enablement, where you define the hardware and services.

### Kernel Panic Handler
- Built-in system protection to catch critical failures and provide diagnostic information, ensuring maximum uptime.

---

## Interactive Shell & Task Management

### Interactive Shell
This is your window into the kernel. Connect via serial using:

```bash
# Linux
picocom /dev/ttyACM0 -b 115200

# Windows
ttermpro.exe /C=x /BAUD=115200

# macOS
screen /dev/tty.usbmodemxxxxx 115200
````

Use the shell to inspect tasks (`ps`, `taskinfo`), check memory (`mem`, `memmap`), and view kernel-level stats (`schedstat`, `ipcstat`, `oomstat`) in real time.

### Task Lifecycle Control

* Create, suspend, resume, and terminate tasks interactively.
* This is OS-style task management on tiny hardware, fully at your command.

---

## Dependencies

### RP2040 / Raspberry Pi Pico Board Support

Add this URL to Arduino IDE under **File → Preferences → Additional Boards Manager URLs**:

[https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json](https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json)

Open **Tools → Board → Boards Manager**, search for Raspberry Pi RP2040, and install the package.

---

## Recommended Setup

* **Overclocking:** For smoother performance, overclock your RP2040 to 225 MHz, 240 MHz, 250 MHz, or 276 MHz. Best kept to 225MHz for stability across most boards.
* **Arduino IDE Optimization:** Set "Optimize Even More (-O3)" under the optimization settings.
* **Serial Terminal:** Use picocom or another serial terminal at 115200 baud to interact with the Picomimi shell.

---

## Getting Started

1. Install Arduino IDE and the RP2040 board support package.
2. Clone or download the Picomimi repository.
3. Open the main `Picomimi.ino` file in Arduino IDE.
4. Go to **Sketch → Add File...** and add any application sketch from the `app` folder.
5. Make sure the dependencies listed above are installed.
6. Upload the sketch to your RP2040.
7. Open a serial terminal and start hacking the Picomimi shell.

---

## Hardware Compatibility

* Picomimi is designed to be stable, efficient, and hackable.
* Encourages experimentation with dual-cores, advanced memory management, and hardware peripherals.
* Compatible with nearly all RP2040 boards. Missing hardware won't break the system—the kernel will simply ignore unconnected components.
* SD Card is heavily encouraged, although not mandated for functionality.

---

## Current Status

* **Architecturally viable and stable**
* Core kernel is stable, marking the successful end of the project's primary engineering goal with Milestone v10 M2; next expected milestone is v15 M2.
* Future work (V10 M2+) will focus on refining the developer experience, feature enablement, retaining API compatibility with v10 M2, and simplifying application integration.
* Picomimi v10 M2 is a milestone; thus, development on Picomimi has slowed down. Future versions will be made to support v10 M2 APIs and functions as closely as possible while adding features and improving core concepts, cross compatibility is now a focus until a new milestone for large ideology shifts, v15 M2 is reached.

---

## License

This project is licensed under the MIT License. See the LICENSE file for details.

---

## Contributing

Contributions are welcome! Feel free to fork, modify, and build upon Picomimi. If you use it in your projects or create something cool with it, a gentle credit back to the original project would be appreciated (but not required). Share the love! ♡
---

## Support

* For issues, questions, or discussions, please use the GitHub Issues page or check the repository for updates and community contributions.

---

## AI Usage Disclosure
AI Assistance & Development Philosophy

This project made extensive use of AI to accelerate development, streamline restructuring, and translate code into clean, readable, and maintainable components. While AI played a significant role in improving turnaround times and integration efficiency, the foundation of this project remains deeply rooted in conceptual and philosophical development.

A considerable—and often painstaking—amount of time was spent developing and refining the core ideas behind the system, drawing inspiration from classic UNIX philosophies such as simplicity, modularity, and clarity of purpose. AI tools were leveraged not as a replacement for thought, but as an extension of it—used to rapidly prototype, debug, iterate, and integrate complex features into the evolving framework.

Earlier versions of the project (Picomimi v0.2 through v3.1.2) were developed entirely without AI assistance, back when the project’s scope was smaller and turnaround times were less of a personal concern, and time was a non-issue. This project began as a hobbyist project, one to make myself something to learn and grow with, purely for entertainment and to satiate curiosity.

The result is a system built on both human insight and computational precision: a fusion of deliberate design and intelligent automation that emphasises robustness, maintainability, and conceptual integrity.

I used AI extensively to learn coding concepts, debug my own issues, and figure out practical ways to implement ideas. This has been an incredible learning opportunity. AI has helped me learn complex concepts, use them appropriately and even advised me on structuring and managing complexity. AI has helped me fix countless problems, including when I had to change god knows how many parts of my code or merge huge, fragmented segments scattered across many, many scattered files into one. Conceptually and implementation-wise, a lot of effort went into this project. During development, the code was always crude and messy; it was cleaned up by AI right before publication on GitHub.

---

## Why This project Stays Single-File (At least for the foreseeable future...)

Arduino IDE technically supports multiple `.ino` files, but in practice it’s a headache. Behind the scenes, the IDE concatenates all `.ino` files into a single `.cpp` before compiling, and the order is **alphabetical after the main file**. That means structs, typedefs, and globals are only visible to files that come later alphabetically. Splitting the kernel into multiple `.ino` files would force us to rename things like `a_types.ino`, `b_memory.ino`, `c_scheduler.ino` just to control compilation order — and any small change could silently break another file. Compiler errors reference the merged `.cpp`, not the original tab, making debugging slow and frustrating. With everything in one file, errors point exactly where they belong, Ctrl+F works, and bracket counting doesn’t turn into a scavenger hunt. If this were a Pico SDK + CMake project with proper includes and build tools, splitting into multiple files would make sense — but migrating a kernel like this isn’t realistic for a two-person team juggling school, other projects, limited screen space, and, occasionally, the need to just chill with plants or scroll through memes.

---

## Why We're Not Changing It (Not soon at least...)

This is a **hobby passion project**, not production software. We’re two people working on small monitors, trying to actually get something running without turning every minor tweak into a multi-day headache. Suggestions to modularize the kernel are well meaning, but gloss over how fragile Arduino’s multi-file `.ino` system is: splitting it would create real technical debt, slow down development, and make debugging tedious. **Our architecture keeps the kernel as one clean file, while apps are uploaded as separate `.ino` files** — this keeps the workspace manageable and avoids alphabetical dependency issues. The kernel is well-commented, structured with clear ASCII section headers (at least we’re working on clean ASCII headers…), and every function is a readable `void` with straightforward logic. Single-file isn’t a “shortcut” — it’s a **practical, maintainable choice** that lets us iterate quickly, preserve sanity, and still make a functional mini RP2040 kernel — all while leaving time to chill with plants or watch a few memes.
