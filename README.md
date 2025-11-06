# Picomimi

> A tiny, educational and productive Dual-Core Microkernel for the RP2040

**Arduino-IDE-ready and easy to use**, Picomimi is built for casual tinkering, fun experiments, and bare-metal chaos—bringing high-grade stability to your Pico projects while letting you push the microcontroller to its limits. Simple and cute (＾_＾).

![Picomimi mascot](assets/Picomimi_Mascot.png)  
*Picomimi ready for fun experiments!*

---

## What Makes Picomimi Different?

Picomimi is more than just an Arduino `loop()`—it introduces a **robust, service-oriented platform** that guarantees CPU time and resources for your applications. The kernel's job is enablement; hardware control is now implemented by your application code, allowing you to build whatever complex system you need on top of a single, stable foundation.

---

## Key Architectural Features (V10 M2 Microkernel)

Picomimi's core kernel is designed for efficiency and stability, making it a powerful learning tool and a reliable base for complex projects:

### **Minimalist Dual-Core**
Fully utilizes the RP2040's two cores with high-performance Inter-Core Communication (IPC). The kernel is lean; all non-essential hardware (like displays or complex buttons) is handled by separate application tasks.

### **O(1) Bitmap Scheduler**
Supports true concurrent multitasking with a guaranteed low-latency task selection. Learn advanced, high-performance scheduling concepts commonly seen in professional RTOS environments.

### **Intelligent Memory Management**
Features custom kernel memory allocation (`kmalloc` and `kfree`) with task-specific accounting and a Graceful Out-of-Memory (OOM) Killer. This system prevents crashes by intelligently recovering memory before system failure.

### **Essential Persistent Storage**
The kernel treats the SD card as a fundamental resource for file system services and application data. *(Seriously, if you ain't have an SD card, what are you even doing, bruh? It'll still work without it, but come on…)*

### **Service Isolation**
The kernel guarantees stability, while your applications handle the complex interactions. This is a platform for Software Enablement, where you define the hardware and services.

### **Kernel Panic Handler**
Built-in system protection to catch critical failures and provide diagnostic information, ensuring maximum uptime.

---

## Interactive Shell & Task Management

### **Interactive Shell**
Connect via serial using:

```bash
# Linux
picocom /dev/ttyACM0 -b 115200

# Windows
ttermpro.exe /C=3 /BAUD=115200

# macOS
screen /dev/tty.usbmodemxxxxx 115200
```

Use the shell to inspect tasks, check memory usage, and interact with the system in real time, including advanced commands like `schedstat` and `oomstat`.

### **Task Lifecycle Control**
Create, suspend, resume, and terminate tasks interactively. See the basics of OS-style task management on tiny hardware.

---

## Dependencies

To compile and upload Picomimi using the Arduino IDE, make sure you have the following installed:

### **RP2040 / Raspberry Pi Pico Board Support**

1. Go to **File → Preferences → Additional Boards Manager URLs** and add:
   ```
   https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   ```

2. Open **Tools → Board → Boards Manager**, search for **Raspberry Pi RP2040** and install the package.

### **Required Libraries**
Install via **Sketch → Include Library → Manage Libraries**:

- **Adafruit GFX Library** (required if using the display driver application)
- **Adafruit ILI9341** (required if using the display driver application)

> **Note:** The RP2040 SDK headers are included automatically with the board package—you don't need to install them manually.

---

## Recommended Setup

### **Overclocking**
For smoother performance, overclock your RP2040 to **225 MHz**, 240 MHz, 250 MHz, or 276 MHz. Best kept to **225MHz** for stability across most boards.

In the Arduino IDE, set **"Optimize Even More (-O3)"** under the optimization settings.

### **Serial Terminal**
Use `picocom` or another serial terminal at **115200 baud** to interact with the Picomimi shell.

---

## Getting Started

1. Install **Arduino IDE** and the **RP2040 board support package**.
2. Clone or download the Picomimi repository.
3. Open the main **Picomimi.ino** file in Arduino IDE.
4. Go to **Sketch → Add File...** and add any application sketch from the `app` folder.
5. Make sure the dependencies listed above are installed.
6. Upload the sketch to your RP2040.
7. Open a serial terminal and start interacting with the Picomimi shell.

---

## Hardware Compatibility

Picomimi is designed to be **stable, efficient, and fun**. It encourages experimentation with dual-cores, advanced memory management, and hardware peripherals.

This project is compatible with nearly all RP2040 boards and provides terminal commands via serial. Missing hardware won't break the system—the Picomimi kernel will simply ignore any components that aren't connected. *(Seriously, though, just add an SD card…)*

---

## Current Status

**Architecturally viable and Stable**

The core kernel is finished and stable, marking the successful end of the project's primary engineering goal. All future work (V10 M2+) will focus on refining the developer experience and simplifying application integration.

---

## License

This project is licensed under the **MIT License** - see the LICENSE file for details.

## Contributing

Contributions are welcome! Feel free to fork, modify, and build upon Picomimi. If you use it in your projects or create something cool with it, a gentle credit back to the original project would be appreciated (but not required). Share the love! ♡

## Support

For issues, questions, or discussions, please use the GitHub Issues page or check the repository for updates and community contributions.

## AI Usage Disclosure
AI Assistance & Development Philosophy

This project made extensive use of AI to accelerate development, streamline restructuring, and translate code into clean, readable, and maintainable components. While AI played a significant role in improving turnaround times and integration efficiency, the foundation of this project remains deeply rooted in conceptual and philosophical development.

A considerable—and often painstaking—amount of time was spent developing and refining the core ideas behind the system, drawing inspiration from classic UNIX philosophies such as simplicity, modularity, and clarity of purpose. AI tools were leveraged not as a replacement for thought, but as an extension of it—used to rapidly prototype, debug, iterate, and integrate complex features into the evolving framework.

Earlier versions of the project (Picomimi v0.2 through v3.1.2) were developed entirely without AI assistance, back when the project’s scope was smaller and turnaround times were less of a personal concern and time was a non issue. This project began as a hobbyist project, one to make myself something to learn and grow with, purely for entertainment and to statiate curiousity.

The result is a system built on both human insight and computational precision: a fusion of deliberate design and intelligent automation that emphasizes robustness, maintainability, and conceptual integrity.
